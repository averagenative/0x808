/*
 * settings_panel.cpp — Settings panel (Dear ImGui).
 *
 * Two sections:
 *   1. Audio Device — SDL2 device enumeration, selection, and apply
 *   2. Recording — output dir, filename prefix, bit depth
 */

#include "imgui.h"
#include <SDL2/SDL.h>
#include <math.h>

extern "C" {
#include "gui/settings_panel.h"
#include "gui/gui.h"
#include "engine/sequencer.h"
#include "engine/kits.h"
#include "engine/synth.h"
#include "gui/undo.h"
#include "app/session.h"
#include "formats/project.h"
#define LOG_TAG "settings"
#include "core/log.h"
}

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#endif

#include <string.h>

/* ─── Audio device cache ─────────────────────────────────────────────────── */

#define MAX_DEVICES 32

static struct {
    char names[MAX_DEVICES][128];
    int  count;
    bool refreshed;
} s_devices;

static void refresh_device_list(void)
{
    int n = SDL_GetNumAudioDevices(0); /* 0 = playback */
    if (n > MAX_DEVICES) n = MAX_DEVICES;
    s_devices.count = n;
    for (int i = 0; i < n; i++) {
        const char *name = SDL_GetAudioDeviceName(i, 0);
        if (name)
            snprintf(s_devices.names[i], sizeof(s_devices.names[i]), "%s", name);
        else
            snprintf(s_devices.names[i], sizeof(s_devices.names[i]), "Device %d", i);
    }
    s_devices.refreshed = true;
}

/* ─── Folder picker (Windows native) ─────────────────────────────────────── */

#ifdef _WIN32
static bool pick_folder(char *buf, size_t /* bufsize */)
{
    BROWSEINFOA bi;
    memset(&bi, 0, sizeof(bi));
    bi.lpszTitle = "Select Recording Output Folder";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderA(&bi);
    if (pidl) {
        SHGetPathFromIDListA(pidl, buf);
        CoTaskMemFree(pidl);
        return true;
    }
    return false;
}
#endif

/* ─── Draw ────────────────────────────────────────────────────────────────── */

void settings_panel_draw(sq_engine_t *engine, sq_app_t *app,
                         sq_midi_t *midi,
                         float x, float y, float w, float h)
{
    if (!engine || !app) return;

    ImGui::SetNextWindowPos(ImVec2(x, y));
    ImGui::SetNextWindowSize(ImVec2(w, h));
    ImGui::Begin("Settings", nullptr,
                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);

    /* Refresh device list on first draw or when panel just opened */
    if (!s_devices.refreshed) {
        refresh_device_list();
    }

    ImGui::TextUnformatted("Settings");
    ImGui::Separator();

    /* ── Audio Device Section ─────────────────────────────────────────── */
    if (ImGui::CollapsingHeader("Audio Device", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Indent(8);

        /* Device dropdown */
        sq_audio_config_t *acfg = &app->audio_config;

        /* Build preview string */
        const char *preview = acfg->device_name[0] ? acfg->device_name : "Default";

        if (ImGui::BeginCombo("Output Device", preview)) {
            /* Default option */
            bool is_default = (acfg->device_index < 0);
            if (ImGui::Selectable("Default", is_default)) {
                acfg->device_name[0] = '\0';
                acfg->device_index = -1;
            }
            if (is_default) ImGui::SetItemDefaultFocus();

            /* Enumerated devices */
            for (int i = 0; i < s_devices.count; i++) {
                bool selected = (acfg->device_index == i);
                if (ImGui::Selectable(s_devices.names[i], selected)) {
                    snprintf(acfg->device_name, SQ_DEVICE_NAME_LEN, "%s",
                             s_devices.names[i]);
                    acfg->device_index = i;
                }
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        /* Refresh button */
        ImGui::SameLine();
        if (ImGui::SmallButton("Refresh")) {
            refresh_device_list();
        }

        /* Sample rate display */
        ImGui::Text("Sample Rate: %u Hz", engine->sample_rate);

        /* Apply button */
        if (ImGui::Button("Apply Audio Device")) {
            if (app->audio_restart_fn) {
                /* Stop recording if active */
                if (engine->recorder.state == SQ_REC_ACTIVE) {
                    sq_recorder_stop(&engine->recorder);
                    LOG_INFO("Recording stopped before audio device change");
                }
                /* Stop playback */
                engine->transport.playing = false;

                app->audio_restart_fn(app->audio_restart_userdata);
                LOG_INFO("Audio device changed to: %s",
                         acfg->device_name[0] ? acfg->device_name : "Default");
            }
        }

        ImGui::Unindent(8);
    }

    /* ── MIDI Input Section ───────────────────────────────────────────── */
    if (midi && ImGui::CollapsingHeader("MIDI Input", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Indent(8);

        static int midi_port_count = 0;
        static double last_midi_refresh = 0;
        double now = ImGui::GetTime();
        /* Auto-refresh MIDI ports every 3 seconds (hot-plug support) */
        if (now - last_midi_refresh > 3.0) {
            int old_count = midi_port_count;
            midi_port_count = sq_midi_get_port_count(midi);
            last_midi_refresh = now;

            /* Auto-reconnect: if port was lost and a matching device reappears */
            int open_now = sq_midi_get_open_port(midi);
            if (open_now < 0 && app->midi_device_name[0] && midi_port_count > 0) {
                for (int i = 0; i < midi_port_count; i++) {
                    const char *name = sq_midi_get_port_name(midi, i);
                    if (name && strstr(name, app->midi_device_name)) {
                        sq_midi_open_port(midi, i);
                        app->midi_port_index = i;
                        sq_app_set_status(app, "MIDI reconnected", 120);
                        break;
                    }
                }
            }
            (void)old_count;
        }

        int open_port = sq_midi_get_open_port(midi);
        const char *preview = (open_port >= 0)
            ? sq_midi_get_port_name(midi, open_port) : "None";

        if (ImGui::BeginCombo("MIDI Device", preview)) {
            /* None option */
            if (ImGui::Selectable("None", open_port < 0)) {
                sq_midi_close_port(midi);
                app->midi_port_index = -1;
                app->midi_device_name[0] = '\0';
            }
            if (open_port < 0) ImGui::SetItemDefaultFocus();

            for (int i = 0; i < midi_port_count; i++) {
                const char *name = sq_midi_get_port_name(midi, i);
                bool selected = (open_port == i);
                if (ImGui::Selectable(name, selected)) {
                    sq_midi_open_port(midi, i);
                    app->midi_port_index = i;
                    snprintf(app->midi_device_name, SQ_DEVICE_NAME_LEN, "%s", name);
                }
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        ImGui::SameLine();
        if (ImGui::SmallButton("Refresh##midi")) {
            midi_port_count = sq_midi_get_port_count(midi);
        }

        /* Status */
        if (open_port >= 0) {
            ImGui::TextColored(ImVec4(0.39f, 1.0f, 0.39f, 1.0f), "Connected");
        } else {
            ImGui::TextDisabled("No MIDI device");
        }

        /* MIDI Input Mode */
        ImGui::Spacing();
        const char *mode_labels[] = { "Synth (keyboard)", "Drum Pads (GM map)" };
        int mode = (int)sq_midi_get_input_mode(midi);
        if (ImGui::Combo("Input Mode", &mode, mode_labels, 2)) {
            sq_midi_set_input_mode(midi, (sq_midi_input_mode_t)mode);
        }

        /* MIDI Learn */
        ImGui::Spacing();
        {
            bool learning = (sq_midi_learn_active(midi) != SQ_PARAM_NONE);
            if (learning) {
                float t = (float)ImGui::GetTime();
                float pulse = 0.5f + 0.5f * (float)sin((double)t * 6.0);
                ImGui::PushStyleColor(ImGuiCol_Button,
                    ImVec4(0.6f * pulse, 0.55f * pulse, 0.1f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                    ImVec4(0.7f, 0.65f, 0.15f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                    ImVec4(0.5f, 0.45f, 0.1f, 1.0f));
            }
            if (ImGui::Button(learning ? "LEARN (active)" : "MIDI Learn")) {
                if (learning)
                    sq_midi_learn_cancel(midi);
                else
                    sq_midi_learn_start(midi, SQ_PARAM_FILTER_CUTOFF);
            }
            if (learning)
                ImGui::PopStyleColor(3);

            if (ImGui::IsItemHovered())
                SQ_TOOLTIP(learning
                    ? "Click to cancel MIDI learn.\nOr twist a knob on your controller to bind it."
                    : "Start MIDI learn for filter cutoff.\nOr right-click any synth knob to learn that parameter.");

            if (learning) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f),
                    "Twist a knob on your controller...");
            }
        }

        /* MIDI Output Port */
        ImGui::Spacing();
        {
            static int out_port_count = 0;
            static double last_out_refresh = 0;
            if (now - last_out_refresh > 3.0) {
                out_port_count = sq_midi_get_output_port_count(midi);
                last_out_refresh = now;
            }
            int out_port = sq_midi_get_open_output_port(midi);
            const char *out_preview = (out_port >= 0)
                ? sq_midi_get_output_port_name(midi, out_port) : "None";
            if (ImGui::BeginCombo("MIDI Out", out_preview)) {
                if (ImGui::Selectable("None", out_port < 0))
                    sq_midi_close_output_port(midi);
                for (int i = 0; i < out_port_count; i++) {
                    const char *name = sq_midi_get_output_port_name(midi, i);
                    if (ImGui::Selectable(name, out_port == i))
                        sq_midi_open_output_port(midi, i);
                }
                ImGui::EndCombo();
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Refresh##midiout")) {
                out_port_count = sq_midi_get_output_port_count(midi);
            }
        }

        ImGui::Unindent(8);
    }

    /* ── Groove Templates ────────────────────────────────────────────── */
    if (ImGui::CollapsingHeader("Groove")) {
        ImGui::Indent(8);
        static int selected_groove = 0;
        const char *groove_names[SQ_NUM_GROOVE_TEMPLATES];
        for (int i = 0; i < SQ_NUM_GROOVE_TEMPLATES; i++)
            groove_names[i] = sequencer_get_groove(i)->name;
        ImGui::Combo("Template", &selected_groove,
                     groove_names, SQ_NUM_GROOVE_TEMPLATES);
        if (ImGui::Button("Apply Groove")) {
            undo_push(engine);
            sequencer_apply_groove(engine, selected_groove);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(Ctrl+Z to undo)");
        ImGui::Unindent(8);
    }

    /* ── Recording Section ────────────────────────────────────────────── */
    if (ImGui::CollapsingHeader("Recording", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Indent(8);

        sq_rec_config_t *rcfg = &app->rec_config;

        /* Output directory */
        ImGui::Text("Output Directory:");
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 80);
        ImGui::InputText("##rec_dir", rcfg->output_dir, SQ_REC_DIR_LEN);

#ifdef _WIN32
        ImGui::SameLine();
        if (ImGui::Button("Browse...")) {
            char folder[SQ_REC_DIR_LEN];
            snprintf(folder, sizeof(folder), "%s", rcfg->output_dir);
            if (pick_folder(folder, sizeof(folder))) {
                snprintf(rcfg->output_dir, SQ_REC_DIR_LEN, "%s", folder);
            }
        }
#endif

        /* Filename prefix */
        ImGui::Text("Filename Prefix:");
        ImGui::SetNextItemWidth(200);
        ImGui::InputText("##rec_prefix", rcfg->prefix, SQ_REC_PREFIX_LEN);
        ImGui::SameLine();
        ImGui::TextDisabled("(e.g. %s_001.wav)", rcfg->prefix);

        /* Bit depth */
        ImGui::Text("Bit Depth:");
        const char *depths[] = {"16-bit", "24-bit", "32-bit float"};
        int depth_idx = 0;
        if (rcfg->bit_depth == 24) depth_idx = 1;
        else if (rcfg->bit_depth == 32) depth_idx = 2;

        ImGui::SetNextItemWidth(150);
        if (ImGui::Combo("##rec_depth", &depth_idx, depths, 3)) {
            switch (depth_idx) {
            case 0: rcfg->bit_depth = 16; break;
            case 1: rcfg->bit_depth = 24; break;
            case 2: rcfg->bit_depth = 32; break;
            }
        }

        ImGui::Unindent(8);
    }

    /* ── UI Preferences ──────────────────────────────────────────────── */
    if (ImGui::CollapsingHeader("UI Preferences")) {
        ImGui::Indent(8);
        if (ImGui::Checkbox("Show Tooltips", &app->show_tooltips))
            g_tooltips_enabled = app->show_tooltips ? 1 : 0;
        ImGui::Unindent(8);
    }

    /* ── Project ──────────────────────────────────────────────────────── */
    if (ImGui::CollapsingHeader("Project")) {
        ImGui::Indent(8);

        static char project_path[512] = "";
        ImGui::Text("Path:");
        ImGui::SameLine();
        ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);
        ImGui::InputText("##proj_path", project_path, sizeof(project_path));
        ImGui::PopItemWidth();

        if (ImGui::Button("Save Project")) {
            if (project_path[0] == '\0') {
                /* Show save dialog */
                gui_file_dialog(project_path, sizeof(project_path), 1, "project.sqproj");
            }
            if (project_path[0]) {
                if (project_save(engine, project_path) == 0) {
                    char msg[64];
                    snprintf(msg, sizeof(msg), "Saved: %s", project_path);
                    sq_app_set_status(app, msg, 180);
                } else {
                    sq_app_set_status(app, "Save FAILED!", 180);
                }
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Load Project")) {
            char load_path[512] = "";
            if (gui_file_dialog(load_path, sizeof(load_path), 0, project_path)) {
                if (project_load(engine, load_path) == 0) {
                    snprintf(project_path, sizeof(project_path), "%s", load_path);
                    char msg[64];
                    snprintf(msg, sizeof(msg), "Loaded: %s", load_path);
                    sq_app_set_status(app, msg, 180);
                } else {
                    sq_app_set_status(app, "Load FAILED!", 180);
                }
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Save As...")) {
            char save_path[512] = "";
            if (gui_file_dialog(save_path, sizeof(save_path), 1, project_path)) {
                snprintf(project_path, sizeof(project_path), "%s", save_path);
                if (project_save(engine, save_path) == 0) {
                    char msg[64];
                    snprintf(msg, sizeof(msg), "Saved: %s", save_path);
                    sq_app_set_status(app, msg, 180);
                } else {
                    sq_app_set_status(app, "Save As FAILED!", 180);
                }
            }
        }

        ImGui::Unindent(8);
    }

    /* ── Reset to Defaults ───────────────────────────────────────────── */
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    if (ImGui::Button("Reset to Defaults")) {
        /* Reset app state */
        sq_app_init(app);
        sq_app_init_rec_config(&app->rec_config);

        /* Reset engine CC map to factory defaults */
        memset(engine->cc_map.map, -1, sizeof(engine->cc_map.map));
        engine->cc_map.map[1]  = SQ_PARAM_FILTER_CUTOFF;
        engine->cc_map.map[7]  = SQ_PARAM_MASTER_VOLUME;
        engine->cc_map.map[70] = SQ_PARAM_FILTER_CUTOFF;
        engine->cc_map.map[71] = SQ_PARAM_FILTER_RESONANCE;
        engine->cc_map.map[72] = SQ_PARAM_AMP_RELEASE;
        engine->cc_map.map[73] = SQ_PARAM_AMP_ATTACK;
        engine->cc_map.map[74] = SQ_PARAM_FILTER_CUTOFF;
        engine->cc_map.map[75] = SQ_PARAM_AMP_DECAY;
        engine->cc_map.map[76] = SQ_PARAM_AMP_SUSTAIN;
        engine->cc_map.map[77] = SQ_PARAM_DELAY_WET;
        engine->cc_map.map[91] = SQ_PARAM_REVERB_WET;
        engine->cc_map.map[93] = SQ_PARAM_DELAY_WET;
        engine->cc_map.map[21] = SQ_PARAM_FILTER_CUTOFF;
        engine->cc_map.map[22] = SQ_PARAM_FILTER_RESONANCE;
        engine->cc_map.map[23] = SQ_PARAM_AMP_ATTACK;
        engine->cc_map.map[24] = SQ_PARAM_AMP_DECAY;
        engine->cc_map.map[25] = SQ_PARAM_AMP_SUSTAIN;
        engine->cc_map.map[26] = SQ_PARAM_AMP_RELEASE;
        engine->cc_map.map[27] = SQ_PARAM_REVERB_WET;
        engine->cc_map.map[28] = SQ_PARAM_DELAY_WET;

        /* Reset transport */
        engine->transport.bpm = 145.0;
        engine->transport.swing = 0.0f;
        engine->transport.current_pattern = 0;
        engine->master_volume = 1.0f;
        /* Kill all active voices immediately */
        for (int v = 0; v < SQ_MAX_VOICES; v++)
            engine->voices[v].active = false;
        for (int v = 0; v < SQ_MAX_SYNTH_VOICES; v++)
            engine->synth_voices[v].active = false;

        /* Reinitialize synth presets to factory defaults */
        synth_init_presets(engine);

        /* Fully rebuild patterns from scratch */
        engine->num_patterns = 5;
        for (uint32_t pi = 0; pi < engine->num_patterns; pi++) {
            sq_pattern_t *pat = &engine->patterns[pi];
            memset(pat, 0, sizeof(*pat));
            snprintf(pat->name, SQ_PATTERN_NAME_LEN, "Pattern %d", pi + 1);

            uint32_t ns = engine->num_samples;
            if (ns > 8) ns = 8;
            pat->num_tracks = ns + 2;
            for (uint32_t t = 0; t < ns; t++) {
                pat->tracks[t].type = TRACK_SAMPLER;
                pat->tracks[t].sample_index = (int)t;
                pat->tracks[t].length = 16;
                pat->tracks[t].volume = 0.8f;
            }
            uint32_t sb = ns, sp = ns + 1;
            if (sb < pat->num_tracks) {
                pat->tracks[sb].type = TRACK_SYNTH;
                pat->tracks[sb].synth_preset = 50;
                pat->tracks[sb].length = 16;
                pat->tracks[sb].volume = 0.85f;
            }
            if (sp < pat->num_tracks) {
                pat->tracks[sp].type = TRACK_SYNTH;
                pat->tracks[sp].synth_preset = 52;
                pat->tracks[sp].length = 16;
                pat->tracks[sp].volume = 0.35f;
            }
        }

        /* Trap 808 drum pattern on pattern 1 */
        {
            sq_pattern_t *p0 = &engine->patterns[0];
            static const uint8_t trap[6][16] = {
                {127,0,0,0,0,0,0,0,0,0,0,0,110,0,0,0},
                {0,0,0,0,0,0,0,0,127,0,0,0,0,0,0,0},
                {100,50,70,50,100,50,70,90,100,50,70,50,100,70,90,100},
                {0,0,0,0,0,0,0,0,110,0,0,0,0,0,0,0},
                {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,70},
                {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}
            };
            int applied = 0;
            for (uint32_t t = 0; t < p0->num_tracks && applied < 6; t++) {
                if (p0->tracks[t].type == TRACK_SAMPLER) {
                    for (int s = 0; s < 16; s++)
                        p0->tracks[t].steps[s].velocity = trap[applied][s];
                    applied++;
                }
            }
            uint32_t ns = engine->num_samples > 8 ? 8 : engine->num_samples;
            if (ns < p0->num_tracks) {
                p0->tracks[ns].steps[0].note = 24;  /* C1 */
                p0->tracks[ns].steps[0].velocity = 127;
                p0->tracks[ns].steps[0].length = 14.0f;
            }
            if (ns + 1 < p0->num_tracks) {
                p0->tracks[ns + 1].steps[0].note = 39;
                p0->tracks[ns + 1].steps[0].velocity = 80;
                p0->tracks[ns + 1].steps[0].length = 0.0f;
            }
        }

        /* Reload default 808 kit */
        sq_kit_load(engine, 0, engine->base_dir);

        /* Delete session + autosave files */
        remove(sq_session_path());
        remove(sq_session_autosave_path());
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(resets all settings to factory defaults)");

    ImGui::End();
}
