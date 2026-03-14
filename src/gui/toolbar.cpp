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
        if (new_state && p->play_start_ticks)
            *p->play_start_ticks = SDL_GetPerformanceCounter();
    }
    if (!is_plugin && was_playing)
        ImGui::PopStyleColor(2);

    ImGui::SameLine();

    /* ── REC button ────────────────────────────────────────────────── */
    {
        bool was_rec = engine->recording;
        if (!is_plugin && was_rec) {
            ImVec2 rec_pos = ImGui::GetCursorScreenPos();
            DrawGlow(rec_pos, ImVec2(rec_pos.x + 55, rec_pos.y + btn_h),
                     ImVec4(0.9f, 0.15f, 0.15f, 1.0f), 4.0f, 55);
        }
        if (was_rec)
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.78f, 0.12f, 0.12f, 1.0f));
        if (ImGui::Button(was_rec ? "REC *" : "REC", ImVec2(55, btn_h))) {
            if (engine->recording) {
                engine->recording = false;
                if (engine->rec_frames > 0) {
                    char rec_path[600];
                    snprintf(rec_path, sizeof(rec_path), "%srecording.wav",
                             engine->base_dir[0] ? engine->base_dir : "");
                    sq_export_result_t rec_result = {};
                    rec_result.data = engine->rec_buffer;
                    rec_result.num_frames = engine->rec_frames;
                    rec_result.sample_rate = engine->sample_rate;
                    sq_export_write_wav(rec_path, &rec_result, 16);
                    LOG_INFO("Saved recording: %u frames -> %s", engine->rec_frames, rec_path);
                    snprintf(p->save_status, (size_t)p->save_status_size, "Saved: %.120s", rec_path);
                    *p->status_timer = 300;
                }
                engine->rec_frames = 0;
            } else {
                sq_engine_start_recording(engine);
                char rec_path[600];
                snprintf(rec_path, sizeof(rec_path), "%srecording.wav",
                         engine->base_dir[0] ? engine->base_dir : "");
                snprintf(p->save_status, (size_t)p->save_status_size, "REC -> %.120s", rec_path);
                *p->status_timer = 0;
            }
        }
        if (was_rec) ImGui::PopStyleColor();
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
    const float tbtn_sm = is_plugin ? 45.0f : 50.0f;
    const ImVec2 btn_sz(tbtn_w, btn_h);
    const ImVec2 btn_sm(tbtn_sm, btn_h);

    float export_x = ImGui::GetCursorPosX();

    if (ImGui::Button(export_dialog_visible() ? "EXPORT*" : "EXPORT",
                       ImVec2(is_plugin ? 60.0f : 70.0f, btn_h))) {
        if (export_dialog_visible())
            export_dialog_hide();
        else
            export_dialog_show();
    }
    ImGui::SameLine();

    {
        int *vis = pattern_presets_visible_ptr();
        const char *pre_label = is_plugin ? (*vis ? "PRE*" : "PRE")
                                          : (*vis ? "PRESETS*" : "PRESETS");
        if (ColoredButton(pre_label, *vis != 0, ImVec4(0.71f, 0.55f, 0.24f, 1.0f),
                          ImVec2(is_plugin ? 45.0f : 70.0f, btn_h)))
            *vis = !*vis;
    }
    ImGui::SameLine();

    /* Panel toggles differ: plugin has PNO + KEY, standalone has just PIANO */
    if (is_plugin) {
        if (ColoredButton(*p->show_piano_roll ? "PNO*" : "PNO",
                          *p->show_piano_roll, ImVec4(0.24f, 0.47f, 0.71f, 1.0f), btn_sm)) {
            *p->show_piano_roll = !*p->show_piano_roll;
            if (*p->show_piano_roll) {
                /* Check if a synth track is selected */
                bool has_synth = false;
                if (g_selected_track >= 0) {
                    int pi = engine->transport.current_pattern;
                    if (pi >= 0 && (uint32_t)pi < engine->num_patterns) {
                        sq_pattern_t *pat = &engine->patterns[pi];
                        if ((uint32_t)g_selected_track < pat->num_tracks &&
                            pat->tracks[g_selected_track].type == TRACK_SYNTH)
                            has_synth = true;
                    }
                }
                if (!has_synth) {
                    snprintf(p->save_status, (size_t)p->save_status_size,
                             "Select a synth track for Piano Roll");
                    *p->status_timer = 120;
                }
            }
        }
        ImGui::SameLine();

        if (ColoredButton(*p->show_keyboard ? "KEY*" : "KEY",
                          *p->show_keyboard, ImVec4(0.71f, 0.51f, 0.24f, 1.0f), btn_sm))
            *p->show_keyboard = !*p->show_keyboard;
        ImGui::SameLine();
    } else {
        /* Standalone: PIANO button toggles keyboard */
        if (ColoredButton(*p->show_keyboard ? "PIANO*" : "PIANO",
                          *p->show_keyboard, ImVec4(0.24f, 0.47f, 0.71f, 1.0f), btn_sz))
            *p->show_keyboard = !*p->show_keyboard;
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
    ImGui::SameLine();

    if (ColoredButton(*p->show_mixer ? "FX*" : "FX",
                      *p->show_mixer, ImVec4(0.51f, 0.31f, 0.63f, 1.0f), btn_sm))
        *p->show_mixer = !*p->show_mixer;
    ImGui::SameLine();

    if (ColoredButton(*p->show_browser ? (is_plugin ? "BRW*" : "BROWSE*")
                                       : (is_plugin ? "BRW" : "BROWSE"),
                      *p->show_browser, ImVec4(0.24f, 0.51f, 0.24f, 1.0f),
                      ImVec2(is_plugin ? 45.0f : 70.0f, btn_h)))
        *p->show_browser = !*p->show_browser;
    ImGui::SameLine();

    /* THEME button with popup selector */
    {
        if (ImGui::Button(is_plugin ? "THM" : "THEME", btn_sm))
            ImGui::OpenPopup("ThemePopup");
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

    /* ── Window controls: _ [] X (standalone only) ─────────────────── */
    if (!is_plugin && p->window) {
        ImGui::SameLine();
        ImVec2 wc_sz(35.0f, btn_h);
        float controls_w = wc_sz.x * 3 + 2 * 2 + 8;
        ImGui::SameLine(ImGui::GetWindowWidth() - controls_w);

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
                if (is_plugin) {
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
            }
            if (is_active)
                ImGui::PopStyleColor();
            ImGui::SameLine(0, 2);
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
                if (is_plugin) {
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

        /* Status message */
        ImGui::SameLine(0, is_plugin ? 10.0f : 20.0f);
        if (engine->recording) {
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
