/*
 * toolbar.cpp — Shared toolbar drawing for standalone and plugin GUIs.
 *
 * Contains the common toolbar code: logo, transport (PLAY/STOP, REC),
 * BPM/Swing/Volume knobs, panel toggle buttons, theme popup, pattern
 * selector, and status display.
 *
 * Standalone-only: window controls (_ [] X) and window drag handler.
 * Plugin: omits window controls and drag.
 */

#include "imgui.h"
#include "imgui_impl_sdl2.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>

extern "C" {
#include "gui/toolbar.h"
#include "gui/gui.h"
#include "gui/knobs.h"
#include "gui/export_dialog.h"
#include "gui/pattern_presets.h"
#include "gui/theme.h"
#include "engine/export.h"
#include "engine/synth.h"
#include "engine/sq_midi.h"
#define LOG_TAG "toolbar"
#include "core/log.h"
}

#include <cstdio>
#include <cstring>
#include <cmath>

#ifdef _WIN32
#include <windows.h>
#endif

/* Pattern scroll offset — shared between standalone and plugin */
int g_pat_scroll = 0;

/* Autosave indicator timer (seconds remaining) — set by gui_trigger_autosave_indicator() */
static float g_autosave_timer = 0.0f;

extern "C" void gui_trigger_autosave_indicator(void)
{
    g_autosave_timer = 2.0f; /* show for 2 seconds */
}

/* ─── Helper: draw a soft glow behind a rect ──────────────────────────────── */

static void DrawGlow(ImVec2 min, ImVec2 max, ImVec4 color, float expand = 4.0f,
                     int alpha = 50)
{
    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImU32 glow = IM_COL32(
        (int)(color.x * 255), (int)(color.y * 255), (int)(color.z * 255), alpha / 2);
    dl->AddRectFilled(
        ImVec2(min.x - expand * 2, min.y - expand * 2),
        ImVec2(max.x + expand * 2, max.y + expand * 2),
        glow, 8.0f);
    ImU32 inner = IM_COL32(
        (int)(color.x * 255), (int)(color.y * 255), (int)(color.z * 255), alpha);
    dl->AddRectFilled(
        ImVec2(min.x - expand, min.y - expand),
        ImVec2(max.x + expand, max.y + expand),
        inner, 6.0f);
}

/* ─── Helper: colored button with optional glow ───────────────────────────── */

static bool ColoredButton(const char *label, bool active,
                          ImVec4 active_color = ImVec4(0.39f, 0.71f, 1.00f, 1.00f),
                          ImVec2 size = ImVec2(0, 0))
{
    if (active) {
        /* Draw glow behind active buttons */
        ImVec2 pos = ImGui::GetCursorScreenPos();
        ImVec2 sz = size;
        if (sz.x <= 0) sz.x = ImGui::CalcTextSize(label).x + 16;
        if (sz.y <= 0) sz.y = ImGui::GetFrameHeight();
        DrawGlow(pos, ImVec2(pos.x + sz.x, pos.y + sz.y), active_color, 3.0f, 40);

        ImGui::PushStyleColor(ImGuiCol_Button, active_color);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(active_color.x * 1.1f, active_color.y * 1.1f, active_color.z * 1.1f, 1.0f));
    }
    bool clicked = ImGui::Button(label, size);
    if (active)
        ImGui::PopStyleColor(2);
    return clicked;
}

/* ─── Initialize a new pattern by copying track layout from pattern 0 ─────── */

static void init_new_pattern(sq_engine_t *engine, int idx)
{
    sq_pattern_t *np = &engine->patterns[idx];
    sq_pattern_t *src = &engine->patterns[0];
    memset(np, 0, sizeof(*np));
    snprintf(np->name, SQ_PATTERN_NAME_LEN, "Pattern %d", idx + 1);
    np->num_tracks = src->num_tracks;
    for (uint32_t t = 0; t < np->num_tracks; t++) {
        np->tracks[t].type = src->tracks[t].type;
        np->tracks[t].length = src->tracks[t].length;
        np->tracks[t].volume = src->tracks[t].volume;
        np->tracks[t].pan = src->tracks[t].pan;
        np->tracks[t].sample_index = src->tracks[t].sample_index;
        np->tracks[t].synth_preset = src->tracks[t].synth_preset;
        np->tracks[t].sf2_preset = src->tracks[t].sf2_preset;
        np->tracks[t].color_index = src->tracks[t].color_index;
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * toolbar_draw — shared toolbar rendering
 * ═══════════════════════════════════════════════════════════════════════════ */

extern "C" void toolbar_draw(const sq_toolbar_params_t *p)
{
    sq_engine_t *engine = p->engine;
    float toolbar_h = p->toolbar_h;
    bool is_plugin = p->is_plugin;

    /* Sizes scale with toolbar height */
    float btn_h = is_plugin ? 28.0f : 35.0f;
    float knob_sz = is_plugin ? 28.0f : 35.0f;
    float knob_label_off = is_plugin ? 7.0f : 10.0f;

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2((float)g_win_width, toolbar_h));
    ImGui::Begin("Toolbar", nullptr,
                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar |
                 ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBringToFrontOnFocus);

    /* ── Glowing 0x808 logo with box and pulse ────────────────────── */
    {
        const char *logo = "0x808";
        float scale = 2.0f;
        float font_sz = ImGui::GetFontSize() * scale;
        ImFont *font = ImGui::GetFont();

        ImVec2 text_sz = font->CalcTextSizeA(font_sz, FLT_MAX, 0.0f, logo);
        ImVec2 wpos = ImGui::GetWindowPos();
        float pad_x = 10.0f, pad_y = 5.0f;
        float box_w = text_sz.x + pad_x * 2;
        float box_h = text_sz.y + pad_y * 2;
        float bx = wpos.x + 10;
        float by = wpos.y + (is_plugin ? 35.0f - box_h : toolbar_h - box_h) * 0.5f;
        float lx = bx + pad_x;
        float ly = by + pad_y;

        ImVec4 gc = ImGui::GetStyleColorVec4(ImGuiCol_SliderGrab);
        int cr = (int)(gc.x * 255), cg = (int)(gc.y * 255), cb = (int)(gc.z * 255);

        float t = (float)SDL_GetTicks() / 1000.0f;
        float pulse = 0.4f + 0.6f * sinf(t * 2.094f);

        ImDrawList *dl = ImGui::GetForegroundDrawList();
        float rnd = 4.0f;
        int outer_a = (int)(11 + 11 * pulse);
        dl->AddRect(ImVec2(bx - 2, by - 2), ImVec2(bx + box_w + 2, by + box_h + 2),
                    IM_COL32(cr, cg, cb, outer_a), rnd + 1, 0, 1.0f);
        int border_a = (int)(53 + 33 * pulse);
        dl->AddRect(ImVec2(bx, by), ImVec2(bx + box_w, by + box_h),
                    IM_COL32(cr, cg, cb, border_a), rnd, 0, 1.5f);
        int text_a = 93 + (int)(54 * pulse);
        dl->AddText(font, font_sz, ImVec2(lx, ly),
                    IM_COL32(cr, cg, cb, text_a), logo);

        ImGui::Dummy(ImVec2(box_w + 14, is_plugin ? 28.0f : 35.0f));
        ImGui::SameLine();
    }

    /* ── Transport: PLAY/STOP ──────────────────────────────────────── */
    bool was_playing = engine->transport.playing;
    {
        if (!is_plugin) {
            /* Standalone: glow behind play button */
            ImVec2 play_pos = ImGui::GetCursorScreenPos();
            if (was_playing) {
                DrawGlow(play_pos, ImVec2(play_pos.x + 60, play_pos.y + btn_h),
                         ImVec4(0.2f, 0.8f, 0.3f, 1.0f), 4.0f, 50);
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.55f, 0.20f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.65f, 0.25f, 1.0f));
            }
        }
    }
    if (ImGui::Button(was_playing ? "STOP" : "PLAY", ImVec2(60, btn_h))) {
        bool new_state = !engine->transport.playing;
        engine->transport.playing = new_state;
        engine->transport.current_beat = 0.0;
        engine->transport.sample_position = 0;
        engine->transport.current_step = 0;
        g_visual_step = 0;
        if (new_state) {
            engine->transport.step0_pending = true;
            if (p->play_start_ticks)
                *p->play_start_ticks = SDL_GetPerformanceCounter();
        } else {
            /* Kill all voices immediately on stop */
            for (int v = 0; v < SQ_MAX_VOICES; v++)
                engine->voices[v].active = false;
            for (int v = 0; v < SQ_MAX_SYNTH_VOICES; v++)
                engine->synth_voices[v].active = false;
        }
    }
    if (!is_plugin && was_playing)
        ImGui::PopStyleColor(2);

    ImGui::SameLine();

    /* ── REC button ────────────────────────────────────────────────── */
    {
        sq_recorder_t *rec = &engine->recorder;
        bool is_rec = (rec->state == SQ_REC_ACTIVE);

        /* Build label with elapsed time when recording */
        char rec_label[64] = "REC";
        if (is_rec) {
            uint64_t secs = rec->sample_rate > 0
                ? rec->frames_written / rec->sample_rate : 0;
            uint64_t file_bytes = rec->frames_written * 2 * (rec->bit_depth / 8);
            if (secs >= 3600)
                snprintf(rec_label, sizeof(rec_label), "REC %u:%02u:%02u %.1fMB",
                         (unsigned)(secs / 3600), (unsigned)((secs % 3600) / 60),
                         (unsigned)(secs % 60),
                         (double)file_bytes / (1024.0 * 1024.0));
            else
                snprintf(rec_label, sizeof(rec_label), "REC %u:%02u %.1fMB",
                         (unsigned)(secs / 60), (unsigned)(secs % 60),
                         (double)file_bytes / (1024.0 * 1024.0));
        }

        float rec_btn_w = is_rec ? 150.0f : 55.0f; /* fixed width to avoid click issues */

        if (!is_plugin && is_rec) {
            ImVec2 rec_pos = ImGui::GetCursorScreenPos();
            DrawGlow(rec_pos, ImVec2(rec_pos.x + rec_btn_w, rec_pos.y + btn_h),
                     ImVec4(0.9f, 0.15f, 0.15f, 1.0f), 4.0f, rec_btn_w);
        }
        if (is_rec)
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.78f, 0.12f, 0.12f, 1.0f));

        /* Use stable ID so clicks always register (label changes every frame) */
        char rec_btn_id[80];
        snprintf(rec_btn_id, sizeof(rec_btn_id), "%s###rec_btn", rec_label);
        if (ImGui::Button(rec_btn_id, ImVec2(rec_btn_w, btn_h))) {
            if (is_rec) {
                /* Stop recording */
                uint64_t frames = rec->frames_written;
                sq_recorder_stop(rec);
                if (frames > 0) {
                    snprintf(p->save_status, (size_t)p->save_status_size,
                             "Saved: %.120s", rec->filepath);
                    *p->status_timer = 300;
                }
            } else {
                /* Start recording with auto-incremented filename */
                sq_rec_config_t *cfg = p->rec_config;
                if (cfg) {
                    char rec_path[512];
                    int num = sq_recorder_next_filename(
                        cfg->output_dir, cfg->prefix,
                        rec->next_number, rec_path, sizeof(rec_path));
                    rec->next_number = num;
                    if (sq_recorder_start(rec, rec_path,
                                          engine->sample_rate, cfg->bit_depth) == 0) {
                        snprintf(p->save_status, (size_t)p->save_status_size,
                                 "REC -> %.120s", rec_path);
                        *p->status_timer = 0;
                    } else {
                        snprintf(p->save_status, (size_t)p->save_status_size,
                                 "REC failed — check output dir");
                        *p->status_timer = 300;
                    }
                }
            }
        }
        if (is_rec) ImGui::PopStyleColor();

        /* Handle error state (disk full) */
        if (rec->state == SQ_REC_ERROR) {
            snprintf(p->save_status, (size_t)p->save_status_size,
                     "Recording stopped: disk full or I/O error");
            *p->status_timer = 300;
            rec->state = SQ_REC_IDLE;
        }

        /* Disk space warning */
        if (rec->disk_low && is_rec) {
            snprintf(p->save_status, (size_t)p->save_status_size,
                     "WARNING: Low disk space (<500 MB)");
            *p->status_timer = 0;
        }
    }

    /* Separator after transport (standalone only — plugin has tighter spacing) */
    if (!is_plugin) {
        ImGui::SameLine(0, 4);
        { float sy = ImGui::GetCursorScreenPos().y; float sx = ImGui::GetCursorScreenPos().x;
          ImGui::GetWindowDrawList()->AddLine(ImVec2(sx, sy), ImVec2(sx, sy + btn_h), IM_COL32(80, 80, 85, 200), 1.0f);
          ImGui::Dummy(ImVec2(1, btn_h)); }
        ImGui::SameLine(0, 6);
    }

    /* ── BPM knob ──────────────────────────────────────────────────── */
    ImGui::SameLine();
    {
        ImGui::Text("BPM"); ImGui::SameLine();
        float bpm_f = (float)engine->transport.bpm;
        ImGui::PushID("bpm_knob");
        ImVec2 kpos = ImGui::GetCursorScreenPos();
        if (knob_core_ext("##bpm", &bpm_f, 40.0f, 300.0f, 120.0f, 1.0f, kpos.x, kpos.y, knob_sz, knob_sz))
            engine->transport.bpm = (double)bpm_f;
        ImGui::SameLine();
        char bpm_txt[16];
        snprintf(bpm_txt, sizeof(bpm_txt), "%.0f", bpm_f);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + knob_label_off);
        ImGui::Text("%s", bpm_txt);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - knob_label_off);
        ImGui::PopID();
    }

    /* ── Tap Tempo button ─────────────────────────────────────────── */
    ImGui::SameLine();
    {
        static uint64_t s_tap_times[4] = {0};
        static int s_tap_count = 0;

        if (ImGui::Button("TAP", ImVec2(36, btn_h))) {
            uint64_t now_us = (uint64_t)(
                (double)SDL_GetPerformanceCounter() /
                (double)SDL_GetPerformanceFrequency() * 1000000.0);

            /* Reset if gap > 2 seconds */
            if (s_tap_count > 0 &&
                (now_us - s_tap_times[(s_tap_count - 1) % 4]) > 2000000)
                s_tap_count = 0;

            s_tap_times[s_tap_count % 4] = now_us;
            s_tap_count++;

            if (s_tap_count >= 2) {
                int n = s_tap_count > 4 ? 4 : s_tap_count;
                uint64_t first = s_tap_times[(s_tap_count - n) % 4];
                uint64_t last  = s_tap_times[(s_tap_count - 1) % 4];
                double avg_sec = (double)(last - first) / (double)(n - 1) / 1000000.0;
                if (avg_sec > 0.0) {
                    double bpm = 60.0 / avg_sec;
                    if (bpm < 20.0)  bpm = 20.0;
                    if (bpm > 300.0) bpm = 300.0;
                    engine->transport.bpm = bpm;
                }
            }
        }
        if (ImGui::IsItemHovered()) SQ_TOOLTIP("Tap Tempo (Ctrl+T)");
    }

    /* ── MIDI Learn indicator ─────────────────────────────────────── */
    {
        extern struct sq_midi *gui_get_midi(void);
        struct sq_midi *midi = gui_get_midi();
        if (midi && sq_midi_learn_active(midi) != SQ_PARAM_NONE) {
            ImGui::SameLine();
            float t = (float)ImGui::GetTime();
            float pulse = 0.5f + 0.5f * sinf(t * 6.0f);
            ImGui::PushStyleColor(ImGuiCol_Text,
                ImVec4(1.0f, 0.85f, 0.15f, pulse));
            ImGui::Text("LEARN");
            ImGui::PopStyleColor();
        }
    }

    if (!is_plugin) {
        ImGui::SameLine(0, 4);
        { float sy = ImGui::GetCursorScreenPos().y; float sx = ImGui::GetCursorScreenPos().x;
          ImGui::GetWindowDrawList()->AddLine(ImVec2(sx, sy), ImVec2(sx, sy + btn_h), IM_COL32(80, 80, 85, 200), 1.0f);
          ImGui::Dummy(ImVec2(1, btn_h)); }
        ImGui::SameLine(0, 6);
    }

    /* ── Swing knob ────────────────────────────────────────────────── */
    ImGui::SameLine();
    {
        ImGui::Text("Sw"); ImGui::SameLine();
        float swing_pct = engine->transport.swing * 100.0f;
        ImGui::PushID("sw_knob");
        ImVec2 kpos = ImGui::GetCursorScreenPos();
        if (knob_core_ext("##sw", &swing_pct, 0.0f, 100.0f, 0.0f, 0.5f, kpos.x, kpos.y, knob_sz, knob_sz))
            engine->transport.swing = swing_pct / 100.0f;
        ImGui::SameLine();
        char sw_txt[16];
        snprintf(sw_txt, sizeof(sw_txt), "%.0f%%", swing_pct);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + knob_label_off);
        ImGui::Text("%s", sw_txt);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - knob_label_off);
        ImGui::PopID();
    }
    if (!is_plugin) {
        ImGui::SameLine(0, 4);
        { float sy = ImGui::GetCursorScreenPos().y; float sx = ImGui::GetCursorScreenPos().x;
          ImGui::GetWindowDrawList()->AddLine(ImVec2(sx, sy), ImVec2(sx, sy + btn_h), IM_COL32(80, 80, 85, 200), 1.0f);
          ImGui::Dummy(ImVec2(1, btn_h)); }
        ImGui::SameLine(0, 6);
    }

    /* ── Volume knob ───────────────────────────────────────────────── */
    ImGui::SameLine();
    {
        ImGui::Text("Vol"); ImGui::SameLine();
        float vol_pct = engine->master_volume * 100.0f;
        ImGui::PushID("vol_knob");
        ImVec2 kpos = ImGui::GetCursorScreenPos();
        if (knob_core_ext("##vol", &vol_pct, 0.0f, 100.0f, 80.0f, 0.5f, kpos.x, kpos.y, knob_sz, knob_sz))
            engine->master_volume = vol_pct / 100.0f;
        ImGui::SameLine();
        char vol_txt[16];
        snprintf(vol_txt, sizeof(vol_txt), "%.0f%%", vol_pct);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + knob_label_off);
        ImGui::Text("%s", vol_txt);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - knob_label_off);
        ImGui::PopID();
    }
    if (!is_plugin) {
        ImGui::SameLine(0, 4);
        { float sy = ImGui::GetCursorScreenPos().y; float sx = ImGui::GetCursorScreenPos().x;
          ImGui::GetWindowDrawList()->AddLine(ImVec2(sx, sy), ImVec2(sx, sy + btn_h), IM_COL32(80, 80, 85, 200), 1.0f);
          ImGui::Dummy(ImVec2(1, btn_h)); }
        ImGui::SameLine(0, 6);
    }

    /* ── Panel toggle buttons ──────────────────────────────────────── */
    ImGui::SameLine(0, 8);
    const float tbtn_w = is_plugin ? 45.0f : 70.0f;
    const float tbtn_sm = 45.0f; /* compact buttons for all builds */
    const ImVec2 btn_sz(tbtn_w, btn_h);
    const ImVec2 btn_sm(tbtn_sm, btn_h);

    float export_x = ImGui::GetCursorPosX();

    if (ImGui::Button(export_dialog_visible() ? "EXP*" : "EXP",
                       ImVec2(45.0f, btn_h))) {
        if (export_dialog_visible())
            export_dialog_hide();
        else
            export_dialog_show();
    }
    if (ImGui::IsItemHovered()) SQ_TOOLTIP("Export (WAV/MP3/FLAC)");
    ImGui::SameLine();

    {
        int *vis = pattern_presets_visible_ptr();
        const char *pre_label = *vis ? "PRE*" : "PRE";
        if (ColoredButton(pre_label, *vis != 0, ImVec4(0.71f, 0.55f, 0.24f, 1.0f),
                          ImVec2(45.0f, btn_h)))
            *vis = !*vis;
    }
    if (ImGui::IsItemHovered()) SQ_TOOLTIP("Pattern Presets");
    ImGui::SameLine();

    /* Panel toggles differ: plugin has PNO + KEY, standalone has just PIANO */
    if (is_plugin) {
        if (ColoredButton(*p->show_piano_roll ? "PNO*" : "PNO",
                          *p->show_piano_roll, ImVec4(0.24f, 0.47f, 0.71f, 1.0f), btn_sm)) {
            *p->show_piano_roll = !*p->show_piano_roll;
            if (*p->show_piano_roll) {
                /* Auto-select first synth track if none selected */
                bool has_synth = false;
                int pi = engine->transport.current_pattern;
                if (g_selected_track >= 0 && pi >= 0 &&
                    (uint32_t)pi < engine->num_patterns) {
                    sq_pattern_t *pat = &engine->patterns[pi];
                    if ((uint32_t)g_selected_track < pat->num_tracks &&
                        pat->tracks[g_selected_track].type == TRACK_SYNTH)
                        has_synth = true;
                }
                if (!has_synth && pi >= 0 && (uint32_t)pi < engine->num_patterns) {
                    sq_pattern_t *pat = &engine->patterns[pi];
                    for (uint32_t t = 0; t < pat->num_tracks; t++) {
                        if (pat->tracks[t].type == TRACK_SYNTH) {
                            g_selected_track = (int)t;
                            has_synth = true;
                            break;
                        }
                    }
                }
                if (!has_synth) {
                    snprintf(p->save_status, (size_t)p->save_status_size,
                             "No synth tracks — add one first");
                    *p->status_timer = 120;
                }
            }
        }
        if (ImGui::IsItemHovered()) SQ_TOOLTIP("Piano Roll");
        ImGui::SameLine();

        if (ColoredButton(*p->show_keyboard ? "KEY*" : "KEY",
                          *p->show_keyboard, ImVec4(0.71f, 0.51f, 0.24f, 1.0f), btn_sm))
            *p->show_keyboard = !*p->show_keyboard;
        if (ImGui::IsItemHovered()) SQ_TOOLTIP("Virtual Keyboard");
        ImGui::SameLine();
    } else {
        /* Standalone: PIANO button toggles keyboard */
        if (ColoredButton(*p->show_keyboard ? "PNO*" : "PNO",
                          *p->show_keyboard, ImVec4(0.24f, 0.47f, 0.71f, 1.0f), btn_sz))
            *p->show_keyboard = !*p->show_keyboard;
        if (ImGui::IsItemHovered()) SQ_TOOLTIP("Piano / Virtual Keyboard");
        ImGui::SameLine();
    }

    /* Play mode toggle */
    {
        const char *mode_labels[] = {"PAT", is_plugin ? "SNG" : "SONG", is_plugin ? "PRF" : "PERF"};
        ImVec4 mode_colors[] = {
            ImVec4(0.22f, 0.22f, 0.24f, 1.0f),
            ImVec4(0.24f, 0.39f, 0.71f, 1.0f),
            ImVec4(0.71f, 0.24f, 0.39f, 1.0f),
        };
        if (ColoredButton(mode_labels[engine->transport.mode],
                          engine->transport.mode != MODE_PATTERN,
                          mode_colors[engine->transport.mode], btn_sm))
            engine->transport.mode = (sq_play_mode_t)((engine->transport.mode + 1) % 3);
    }
    if (ImGui::IsItemHovered()) SQ_TOOLTIP("Pattern / Song / Perform mode");
    ImGui::SameLine();

    if (ColoredButton(*p->show_mixer ? "MIX/FX*" : "MIX/FX",
                      *p->show_mixer, ImVec4(0.51f, 0.31f, 0.63f, 1.0f),
                      ImVec2(is_plugin ? 55.0f : 75.0f, btn_h)))
        *p->show_mixer = !*p->show_mixer;
    if (ImGui::IsItemHovered()) SQ_TOOLTIP("Mixer / Effects");
    ImGui::SameLine();

    if (ColoredButton(*p->show_browser ? "BRW*" : "BRW",
                      *p->show_browser, ImVec4(0.24f, 0.51f, 0.24f, 1.0f),
                      ImVec2(is_plugin ? 45.0f : 70.0f, btn_h)))
        *p->show_browser = !*p->show_browser;
    if (ImGui::IsItemHovered()) SQ_TOOLTIP("Sample Browser");
    ImGui::SameLine();

    /* THEME button with popup selector */
    {
        if (ImGui::Button("THM", btn_sm))
            ImGui::OpenPopup("ThemePopup");
        if (ImGui::IsItemHovered()) SQ_TOOLTIP("Theme");
        if (ImGui::BeginPopup("ThemePopup")) {
            ImGui::Text("Select Theme:");
            ImGui::Separator();
            for (int t = 0; t < THEME_COUNT; t++) {
                bool selected = (theme_current() == (sq_theme_t)t);
                if (ImGui::Selectable(theme_name((sq_theme_t)t), selected))
                    theme_apply((sq_theme_t)t);
            }
            if (!is_plugin && theme_num_user_themes() > 0) {
                ImGui::Separator();
                ImGui::TextDisabled("User Themes:");
                for (int u = 0; u < theme_num_user_themes(); u++) {
                    if (ImGui::Selectable(theme_user_name(u), false))
                        theme_apply_user(u);
                }
            }
            ImGui::EndPopup();
        }
    }

    /* Help button with keyboard/mouse controls popup */
    ImGui::SameLine();
    {
        if (ImGui::Button("?", ImVec2(btn_h, btn_h)))
            ImGui::OpenPopup("HelpPopup");
        if (ImGui::BeginPopup("HelpPopup")) {
            ImGui::Text("Keyboard Shortcuts");
            ImGui::Separator();
            ImGui::Text("Space          Play / Stop");
            ImGui::Text("1-9            Select pattern");
            ImGui::Text("Ctrl+Z         Undo");
            ImGui::Text("Ctrl+Shift+Z   Redo");
            ImGui::Text("Ctrl+T         Cycle themes");
            ImGui::Text("Ctrl+S         Save project");
            ImGui::Text("Ctrl+O         Open project");
            ImGui::Text("Escape         Quit");

            ImGui::Text("Ctrl+C         Copy pattern");
            ImGui::Text("Ctrl+V         Paste pattern");
            ImGui::Text("+/=            New pattern");

            ImGui::Spacing();
            ImGui::Text("QWERTY Piano (when KEYS panel open)");
            ImGui::Separator();
            ImGui::Text("Z/X/C/V/B/N/M          Lower octave white keys");
            ImGui::Text("Q/W/E/R/T/Y/U/I/O/P    Upper octave white keys");

            ImGui::Spacing();
            ImGui::Text("Mouse Controls");
            ImGui::Separator();
            ImGui::Text("Left-click         Toggle step / Place note");
            ImGui::Text("Left-drag          Paint steps / Extend note length");
            ImGui::Text("Right-click        Velocity/pitch editor (drum grid)");
            ImGui::Text("Right-drag         Erase notes (piano roll)");
            ImGui::Text("Scroll wheel       Scroll / Cycle dropdown values");

            ImGui::Spacing();
            ImGui::Text("Knobs");
            ImGui::Separator();
            ImGui::Text("Drag up/down       Adjust value");
            ImGui::Text("Shift + drag       Fine adjustment (10x precision)");
            ImGui::Text("Double-click       Reset to default");
            ImGui::EndPopup();
        }
    }

    /* ── Window controls: [autosave] [gear] _ [] X (standalone only) ─ */
    if (!is_plugin && p->window) {
        ImGui::SameLine();
        ImVec2 wc_sz(35.0f, btn_h);
        float gear_w = btn_h; /* square button */
        float autosave_w = 0.0f;

        /* Pulsing "auto-saving..." indicator */
        if (g_autosave_timer > 0.0f) {
            autosave_w = 80.0f;
            g_autosave_timer -= ImGui::GetIO().DeltaTime;
        }

        float controls_w = autosave_w + gear_w + 4 + wc_sz.x * 3 + 2 * 2 + 8;
        ImGui::SameLine(ImGui::GetWindowWidth() - controls_w);

        if (g_autosave_timer > 0.0f) {
            float t = (float)ImGui::GetTime();
            float pulse = 0.5f + 0.5f * sinf(t * 4.0f);
            ImGui::PushStyleColor(ImGuiCol_Text,
                ImVec4(0.4f, 0.85f, 0.4f, pulse));
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4);
            ImGui::Text("saving...");
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 4);
            ImGui::PopStyleColor();
            ImGui::SameLine();
        }

        /* Settings gear icon */
        if (p->show_settings) {
            bool active = *p->show_settings;
            if (active) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.35f, 0.35f, 0.40f, 1.0f));
            } else {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.20f, 0.22f, 1.0f));
            }
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.40f, 0.40f, 0.45f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.15f, 0.15f, 0.17f, 1.0f));

            ImVec2 gear_pos = ImGui::GetCursorScreenPos();
            if (ImGui::Button("##gear", ImVec2(gear_w, btn_h)))
                *p->show_settings = !*p->show_settings;

            /* Draw gear icon on top of the button */
            ImDrawList *dl = ImGui::GetWindowDrawList();
            float cx = gear_pos.x + gear_w * 0.5f;
            float cy = gear_pos.y + btn_h * 0.5f;
            float r_outer = btn_h * 0.3f;
            float r_inner = r_outer * 0.55f;
            ImU32 gear_col = active ? IM_COL32(200, 200, 210, 255)
                                    : IM_COL32(160, 160, 170, 255);

            /* Gear teeth */
            int teeth = 8;
            for (int i = 0; i < teeth; i++) {
                float a = (float)i / (float)teeth * 6.2832f;
                float tooth_w = 0.35f; /* angular half-width */
                float r_tip = r_outer + 2.5f;
                ImVec2 p1(cx + cosf(a - tooth_w) * r_outer, cy + sinf(a - tooth_w) * r_outer);
                ImVec2 p2(cx + cosf(a - tooth_w * 0.6f) * r_tip, cy + sinf(a - tooth_w * 0.6f) * r_tip);
                ImVec2 p3(cx + cosf(a + tooth_w * 0.6f) * r_tip, cy + sinf(a + tooth_w * 0.6f) * r_tip);
                ImVec2 p4(cx + cosf(a + tooth_w) * r_outer, cy + sinf(a + tooth_w) * r_outer);
                dl->AddQuadFilled(p1, p2, p3, p4, gear_col);
            }

            /* Outer circle (body) */
            dl->AddCircleFilled(ImVec2(cx, cy), r_outer, gear_col, 24);
            /* Inner hole */
            ImU32 bg_col = active ? IM_COL32(89, 89, 102, 255)
                                  : IM_COL32(51, 51, 56, 255);
            dl->AddCircleFilled(ImVec2(cx, cy), r_inner, bg_col, 16);

            ImGui::PopStyleColor(3);
            ImGui::SameLine(0, 4);
        }

        /* Minimize */
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.20f, 0.22f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.35f, 0.35f, 0.38f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.15f, 0.15f, 0.17f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
        if (ImGui::Button("_##wmin", wc_sz)) {
#ifdef _WIN32
            SDL_SysWMinfo wmInfo;
            SDL_VERSION(&wmInfo.version);
            if (SDL_GetWindowWMInfo(p->window, &wmInfo))
                ShowWindow(wmInfo.info.win.window, SW_MINIMIZE);
#else
            SDL_MinimizeWindow(p->window);
#endif
        }
        ImGui::PopStyleColor(4);
        ImGui::SameLine(0, 2);

        /* Maximize / Restore */
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.20f, 0.22f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.35f, 0.35f, 0.38f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.15f, 0.15f, 0.17f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
        {
            bool maximized = false;
#ifdef _WIN32
            SDL_SysWMinfo wmInfo;
            SDL_VERSION(&wmInfo.version);
            if (SDL_GetWindowWMInfo(p->window, &wmInfo))
                maximized = IsZoomed(wmInfo.info.win.window);
#else
            maximized = (SDL_GetWindowFlags(p->window) & SDL_WINDOW_MAXIMIZED) != 0;
#endif
            if (ImGui::Button(maximized ? "[]##wmax" : "[ ]##wmax", wc_sz)) {
#ifdef _WIN32
                SDL_SysWMinfo wmInfo2;
                SDL_VERSION(&wmInfo2.version);
                if (SDL_GetWindowWMInfo(p->window, &wmInfo2))
                    ShowWindow(wmInfo2.info.win.window, maximized ? SW_RESTORE : SW_MAXIMIZE);
#else
                if (maximized) SDL_RestoreWindow(p->window);
                else SDL_MaximizeWindow(p->window);
#endif
            }
        }
        ImGui::PopStyleColor(4);
        ImGui::SameLine(0, 2);

        /* Close — red */
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.80f, 0.10f, 0.10f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.95f, 0.20f, 0.20f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.00f, 0.30f, 0.30f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
        if (ImGui::Button("X##wclose", wc_sz)) {
            LOG_INFO("Close button pressed");
            if (p->quit) *p->quit = 1;
        }
        ImGui::PopStyleColor(4);
    }

    /* ── Row 2: Pattern selector + status ──────────────────────────── */
    {
        ImVec4 active_col(0.24f, 0.63f, 0.39f, 1.0f);
        ImVec4 inactive_col(0.18f, 0.18f, 0.20f, 1.0f);
        float pat_btn_w = is_plugin ? 26.0f : 28.0f;
        float pat_btn_h = 22.0f;

        int max_visible = 9;
        int total = (int)engine->num_patterns;
        if (g_pat_scroll > total - max_visible) g_pat_scroll = total - max_visible;
        if (g_pat_scroll < 0) g_pat_scroll = 0;

        if (!is_plugin) {
            /* Standalone: align pattern row under the EXPORT button */
            ImGui::SetCursorPosX(export_x);
        } else {
            ImGui::Text("Pat:");
            ImGui::SameLine(0, 4);
        }

        /* Separator before pattern selector (plugin only) */
        if (is_plugin) {
            /* Plugin already has "Pat:" label */
        }

        /* Left scroll button */
        float scroll_btn_w = is_plugin ? 18.0f : 20.0f;
        if (g_pat_scroll > 0) {
            if (ImGui::Button("<##patL", ImVec2(scroll_btn_w, pat_btn_h)))
                g_pat_scroll--;
            ImGui::SameLine(0, 2);
        }

        int end = g_pat_scroll + max_visible;
        if (end > total) end = total;
        for (int pp = g_pat_scroll; pp < end; pp++) {
            char plbl[8];
            snprintf(plbl, sizeof(plbl), "%d##p%d", pp + 1, pp);
            bool is_active = (pp == engine->transport.current_pattern);
            if (is_active)
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
            if (ColoredButton(plbl, is_active, is_active ? active_col : inactive_col,
                              ImVec2(pat_btn_w, pat_btn_h))) {
                engine->transport.current_pattern = pp;
                /* Auto-select first synth track in new pattern */
                sq_pattern_t *pat = &engine->patterns[pp];
                g_selected_track = -1;
                for (uint32_t st = 0; st < pat->num_tracks; st++) {
                    if (pat->tracks[st].type == TRACK_SYNTH) {
                        g_selected_track = (int)st;
                        break;
                    }
                }
            }
            if (is_active)
                ImGui::PopStyleColor();
            ImGui::SameLine(0, 2);
        }

        /* MIDI Learn button (only when a MIDI device is connected) */
        {
            extern struct sq_midi *gui_get_midi(void);
            struct sq_midi *lmidi = gui_get_midi();
            if (lmidi && sq_midi_get_open_port(lmidi) >= 0) {
                bool learning = (sq_midi_learn_active(lmidi) != SQ_PARAM_NONE);
                if (learning) {
                    float lt = (float)ImGui::GetTime();
                    float lp = 0.5f + 0.5f * sinf(lt * 6.0f);
                    ImGui::PushStyleColor(ImGuiCol_Button,
                        ImVec4(0.6f * lp, 0.55f * lp, 0.1f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                        ImVec4(0.7f, 0.65f, 0.15f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                        ImVec4(0.5f, 0.45f, 0.1f, 1.0f));
                }
                if (ImGui::Button(learning ? "LRN*" : "LRN",
                                  ImVec2(35.0f, pat_btn_h))) {
                    if (learning)
                        sq_midi_learn_cancel(lmidi);
                    else
                        sq_midi_learn_start(lmidi, SQ_PARAM_FILTER_CUTOFF);
                }
                if (learning)
                    ImGui::PopStyleColor(3);
                if (ImGui::IsItemHovered())
                    SQ_TOOLTIP(learning
                        ? "Click to cancel. Or twist a knob."
                        : "MIDI Learn: binds next CC to filter cutoff.\nRight-click synth knobs for other params.");
                ImGui::SameLine(0, 2);
            }
        }

        /* Right scroll button */
        if (end < total) {
            if (ImGui::Button(">##patR", ImVec2(scroll_btn_w, pat_btn_h)))
                g_pat_scroll++;
            ImGui::SameLine(0, 2);
        }

        /* "+" button to add pattern */
        if (engine->num_patterns < 30) {
            if (ImGui::Button("+##addpat", ImVec2(pat_btn_w, pat_btn_h))) {
                int ni = (int)engine->num_patterns;
                engine->num_patterns++;
                init_new_pattern(engine, ni);
                engine->transport.current_pattern = ni;
                if (ni >= g_pat_scroll + max_visible)
                    g_pat_scroll = ni - max_visible + 1;
                /* Auto-select first synth track in new pattern */
                {
                    sq_pattern_t *pat = &engine->patterns[ni];
                    g_selected_track = -1;
                    for (uint32_t st = 0; st < pat->num_tracks; st++) {
                        if (pat->tracks[st].type == TRACK_SYNTH) {
                            g_selected_track = (int)st;
                            break;
                        }
                    }
                }
            }
            ImGui::SameLine(0, 2);
        }

        /* "-" button to delete last pattern */
        if (engine->num_patterns > 1) {
            if (ImGui::Button("-##delpat", ImVec2(pat_btn_w, pat_btn_h))) {
                engine->num_patterns--;
                if ((uint32_t)engine->transport.current_pattern >= engine->num_patterns)
                    engine->transport.current_pattern = (int)engine->num_patterns - 1;
                if (g_pat_scroll > 0 && g_pat_scroll + max_visible > (int)engine->num_patterns)
                    g_pat_scroll--;
            }
            if (ImGui::IsItemHovered())
                SQ_TOOLTIP("Delete last pattern");
            ImGui::SameLine(0, 2);
        }

        /* Status message */
        ImGui::SameLine(0, is_plugin ? 10.0f : 20.0f);
        if (engine->recorder.state == SQ_REC_ACTIVE) {
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%s", p->save_status);
        } else if (*p->status_timer > 0) {
            ImGui::TextColored(ImVec4(0.39f, 1.0f, 0.39f, 1.0f), "%s", p->save_status);
            (*p->status_timer)--;
        }
    }

    /* ── Window drag handler (standalone only) ─────────────────────── */
    if (!is_plugin && p->window) {
        ImGuiIO &drag_io = ImGui::GetIO();
        static bool dragging_window = false;
        static int drag_start_wx, drag_start_wy;
        static int drag_start_mx, drag_start_my;

        bool in_toolbar = (drag_io.MousePos.y < toolbar_h);
        bool over_widget = ImGui::IsAnyItemHovered() || ImGui::IsAnyItemActive();

        /* Double-click empty toolbar: maximize/restore */
        if (drag_io.MouseDoubleClicked[0] && in_toolbar && !over_widget) {
#ifdef _WIN32
            SDL_SysWMinfo wmInfo;
            SDL_VERSION(&wmInfo.version);
            if (SDL_GetWindowWMInfo(p->window, &wmInfo)) {
                bool is_max = IsZoomed(wmInfo.info.win.window);
                ShowWindow(wmInfo.info.win.window, is_max ? SW_RESTORE : SW_MAXIMIZE);
            }
#else
            bool is_max = (SDL_GetWindowFlags(p->window) & SDL_WINDOW_MAXIMIZED) != 0;
            if (is_max) SDL_RestoreWindow(p->window); else SDL_MaximizeWindow(p->window);
#endif
        }

        if (drag_io.MouseClicked[0] && in_toolbar && !over_widget) {
            dragging_window = true;
            SDL_GetWindowPosition(p->window, &drag_start_wx, &drag_start_wy);
            SDL_GetGlobalMouseState(&drag_start_mx, &drag_start_my);
        }
        if (dragging_window) {
            if (drag_io.MouseDown[0]) {
                int mx, my;
                SDL_GetGlobalMouseState(&mx, &my);
                int dx = mx - drag_start_mx;
                int dy = my - drag_start_my;
                SDL_SetWindowPosition(p->window, drag_start_wx + dx, drag_start_wy + dy);
            } else {
                dragging_window = false;
            }
        }
    }

    ImGui::End();
}
