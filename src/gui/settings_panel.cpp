/*
 * settings_panel.cpp — Settings panel (Dear ImGui).
 *
 * Two sections:
 *   1. Audio Device — SDL2 device enumeration, selection, and apply
 *   2. Recording — output dir, filename prefix, bit depth
 */

#include "imgui.h"
#include <SDL2/SDL.h>

extern "C" {
#include "gui/settings_panel.h"
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
        static bool midi_refreshed = false;
        if (!midi_refreshed) {
            midi_port_count = sq_midi_get_port_count(midi);
            midi_refreshed = true;
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

    ImGui::End();
}
