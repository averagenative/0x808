/*
 * plugin_gui.cpp — Embedded GUI for the DAW plugin (Dear ImGui version).
 *
 * Creates an SDL2+OpenGL+ImGui rendering surface inside the host DAW's
 * native window. Reuses all the same sub-component drawing code as the
 * standalone GUI.
 *
 * Threading model:
 *   - Host creates/destroys GUI on its UI thread
 *   - A render thread runs at ~60fps, polling SDL events and drawing
 *   - Audio runs on the host's audio thread via cplug_process()
 */

/* Load GL3 function pointers (Windows needs runtime loading via SDL) */
#define GL3_LOADER_IMPLEMENTATION
#include "gl3_loader.h"

#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_opengl3.h"

extern "C" {
#include "plugin/plugin_gui.h"
#include "gui/gui.h"
#include "gui/drum_grid.h"
#include "gui/knobs.h"
#include "gui/synth_editor.h"
#include "gui/sample_browser.h"
#include "gui/export_dialog.h"
#include "gui/piano_roll.h"
#include "gui/arrangement.h"
#include "gui/mixer_view.h"
#include "gui/undo.h"
#include "gui/pattern_presets.h"
#include "gui/virtual_keyboard.h"
#include "gui/theme.h"
#include "formats/project.h"
#include "engine/export.h"
#define LOG_TAG "plugin_gui"
#include "core/log.h"
}

#include <cstdio>
#include <cstring>
#include <cmath>
#include <cstdlib>
#include <atomic>

#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>

#ifdef _WIN32
#include <windows.h>
#endif

#define STEPS_PER_BEAT 4
#define RENDER_FPS 60

/* globals defined in gui_globals.cpp (linked via libsq_gui) */
static int g_pat_scroll = 0;

/* Initialize a new pattern by copying track layout from pattern 0 (no step data) */
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

/* ─── Plugin GUI instance ────────────────────────────────────────────────── */

struct sq_plugin_gui {
    sq_engine_t      *engine;
    SDL_Window       *window;
    SDL_GLContext      gl_ctx;
    ImGuiContext      *imgui_ctx;

    uint32_t width;
    uint32_t height;
    float    scale_factor;

    /* Render thread */
    SDL_Thread *render_thread;
    std::atomic<int> running;

    /* UI state */
    bool show_browser;
    bool show_mixer;
    bool show_piano_roll;
    bool show_keyboard;
    char save_status[128];
    uint32_t status_timer;

    /* Wall-clock playhead tracking */
    Uint64 play_start_ticks;
    bool   was_playing;
};

/* ─── Helper: colored toggle button ──────────────────────────────────────── */

static bool ColoredButton(const char *label, bool active,
                           ImVec4 active_color = ImVec4(0.24f, 0.47f, 0.71f, 1.0f),
                           ImVec2 size = ImVec2(0, 0))
{
    if (active) {
        ImGui::PushStyleColor(ImGuiCol_Button, active_color);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
            ImVec4(active_color.x * 1.2f, active_color.y * 1.2f,
                   active_color.z * 1.2f, 1.0f));
    }
    bool clicked = ImGui::Button(label, size);
    if (active) ImGui::PopStyleColor(2);
    return clicked;
}

/* ─── Plugin file logger (Windows: file, Linux: stderr) ─────────────────── */

#ifdef _WIN32
extern "C" const char *sq_plugin_log_path(void);

static void sq_gui_log(const char *level, const char *tag, const char *fmt, ...)
{
    FILE *f = fopen(sq_plugin_log_path(), "a");
    if (!f) return;

    SYSTEMTIME st;
    GetLocalTime(&st);
    fprintf(f, "%02d:%02d:%02d.%03d %s %s: ",
            st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
            level, tag);

    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);

    fprintf(f, "\n");
    fflush(f);
    fclose(f);
}

#define GLOG_DEBUG(fmt, ...) sq_gui_log("DEBUG", "plugin_gui", fmt, ##__VA_ARGS__)
#define GLOG_INFO(fmt, ...)  sq_gui_log("INFO ", "plugin_gui", fmt, ##__VA_ARGS__)
#define GLOG_WARN(fmt, ...)  sq_gui_log("WARN ", "plugin_gui", fmt, ##__VA_ARGS__)
#define GLOG_ERROR(fmt, ...) sq_gui_log("ERROR", "plugin_gui", fmt, ##__VA_ARGS__)
#else
#define GLOG_DEBUG(fmt, ...) LOG_DEBUG(fmt, ##__VA_ARGS__)
#define GLOG_INFO(fmt, ...)  LOG_INFO(fmt, ##__VA_ARGS__)
#define GLOG_WARN(fmt, ...)  LOG_WARN(fmt, ##__VA_ARGS__)
#define GLOG_ERROR(fmt, ...) LOG_ERROR(fmt, ##__VA_ARGS__)
#endif

/* ─── Draw one frame ─────────────────────────────────────────────────────── */

static bool s_last_playing = false;

static void plugin_gui_draw_frame(sq_plugin_gui_t *gui)
{
    sq_engine_t *engine = gui->engine;
    if (!engine) return;

    /* Log transport state changes (debug play button issue) */
    if (engine->transport.playing != s_last_playing) {
        GLOG_INFO("Transport changed: playing=%d (was %d) — NOT from GUI button",
                  engine->transport.playing, s_last_playing);
        s_last_playing = engine->transport.playing;
    }

    /* Sync SDL window size with host parent on Windows — every frame,
     * so maximize/restore/resize are always tracked */
#ifdef _WIN32
    {
        SDL_SysWMinfo wminfo;
        SDL_VERSION(&wminfo.version);
        if (SDL_GetWindowWMInfo(gui->window, &wminfo)) {
            HWND sdl_hwnd = wminfo.info.win.window;
            HWND parent = GetParent(sdl_hwnd);
            if (parent) {
                RECT rc;
                GetClientRect(parent, &rc);
                int pw = rc.right - rc.left;
                int ph = rc.bottom - rc.top;
                if (pw > 0 && ph > 0) {
                    gui->width = (uint32_t)pw;
                    gui->height = (uint32_t)ph;
                    /* Always force SDL window to fill parent */
                    SDL_SetWindowSize(gui->window, pw, ph);
                    SetWindowPos(sdl_hwnd, NULL, 0, 0, pw, ph,
                                 SWP_NOZORDER);
                }
            }
        }
    }
#endif
    SDL_GetWindowSize(gui->window, &g_win_width, &g_win_height);

    /* Poll SDL events */
    SDL_Event evt;
    while (SDL_PollEvent(&evt)) {
#ifdef _WIN32
        /* Skip SDL mouse button/motion events — we inject mouse state via Win32 API.
         * Letting both through causes double-click detection (toggle on+off).
         * Keep SDL_MOUSEWHEEL for scrolling. */
        if (evt.type == SDL_MOUSEBUTTONDOWN || evt.type == SDL_MOUSEBUTTONUP ||
            evt.type == SDL_MOUSEMOTION)
            continue;
#endif
        ImGui_ImplSDL2_ProcessEvent(&evt);

        if (evt.type == SDL_KEYDOWN && !ImGui::GetIO().WantCaptureKeyboard) {
            switch (evt.key.keysym.sym) {
            case SDLK_SPACE:
                engine->transport.playing = !engine->transport.playing;
                engine->transport.current_beat = 0.0;
                engine->transport.sample_position = 0;
                engine->transport.current_step = 0;
                g_visual_step = 0;
                if (engine->transport.playing)
                    gui->play_start_ticks = SDL_GetPerformanceCounter();
                break;
            case SDLK_s:
                if (evt.key.keysym.mod & KMOD_CTRL) {
                    snprintf(gui->save_status, sizeof(gui->save_status),
                             "Use DAW to save plugin state");
                    gui->status_timer = 180;
                }
                break;
            case SDLK_1: case SDLK_2: case SDLK_3:
            case SDLK_4: case SDLK_5: case SDLK_6:
            case SDLK_7: case SDLK_8: case SDLK_9:
                if (!(evt.key.keysym.mod & KMOD_CTRL)) {
                    int pat = evt.key.keysym.sym - SDLK_1;
                    if ((uint32_t)pat < engine->num_patterns) {
                        engine->transport.current_pattern = pat;
                        snprintf(gui->save_status, sizeof(gui->save_status),
                                 "Pattern %d", pat + 1);
                        gui->status_timer = 90;
                    }
                }
                break;
            case SDLK_z:
                if (evt.key.keysym.mod & KMOD_CTRL) {
                    if (evt.key.keysym.mod & KMOD_SHIFT) {
                        if (undo_redo(engine)) {
                            snprintf(gui->save_status, sizeof(gui->save_status), "Redo");
                            gui->status_timer = 90;
                        }
                    } else {
                        if (undo_undo(engine)) {
                            snprintf(gui->save_status, sizeof(gui->save_status), "Undo");
                            gui->status_timer = 90;
                        }
                    }
                }
                break;
            default:
                break;
            }
        }
    }

    /* Visual playhead from wall-clock time */
    if (engine->transport.playing) {
        Uint64 now = SDL_GetPerformanceCounter();
        double elapsed = (double)(now - gui->play_start_ticks)
                       / (double)SDL_GetPerformanceFrequency();
        double beats = elapsed * (engine->transport.bpm / 60.0);
        int pat_len = 16;
        int pi = engine->transport.current_pattern;
        if (pi >= 0 && (uint32_t)pi < engine->num_patterns &&
            engine->patterns[pi].num_tracks > 0) {
            pat_len = (int)engine->patterns[pi].tracks[0].length;
        }
        g_visual_step = ((int)floor(beats * STEPS_PER_BEAT)) % pat_len;
    } else if (gui->was_playing && !engine->transport.playing) {
        g_visual_step = 0;
    }
    gui->was_playing = engine->transport.playing;

    /* Start ImGui frame */
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();

    /* Override mouse state with Win32 API BEFORE NewFrame() — SDL often
     * misreports mouse position/clicks when reparented into a DAW host window.
     * Must be after ImplSDL2_NewFrame (which sets defaults) but before
     * ImGui::NewFrame (which reads io.MouseDown to detect clicks). */
#ifdef _WIN32
    {
        SDL_SysWMinfo wminfo;
        SDL_VERSION(&wminfo.version);
        if (SDL_GetWindowWMInfo(gui->window, &wminfo)) {
            HWND sdl_hwnd = wminfo.info.win.window;
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(sdl_hwnd, &pt);

            ImGuiIO &io = ImGui::GetIO();
            io.MousePos = ImVec2((float)pt.x, (float)pt.y);
            io.MouseDown[0] = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
            io.MouseDown[1] = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
            io.MouseDown[2] = (GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0;
        }
    }
#endif

    ImGui::NewFrame();

    float toolbar_h = 70.0f; /* 2 rows: transport + panels/patterns */

    /* ── Determine if synth editor should be shown (needed for toolbar hints) ── */
    bool show_synth_editor = false;
    int synth_preset_idx = -1;
    int *synth_preset_ptr = nullptr;
    if (g_selected_track >= 0) {
        int pi = engine->transport.current_pattern;
        if (pi >= 0 && (uint32_t)pi < engine->num_patterns) {
            sq_pattern_t *pat = &engine->patterns[pi];
            if ((uint32_t)g_selected_track < pat->num_tracks &&
                pat->tracks[g_selected_track].type == TRACK_SYNTH) {
                show_synth_editor = true;
                synth_preset_ptr = &pat->tracks[g_selected_track].synth_preset;
                synth_preset_idx = *synth_preset_ptr;
            }
        }
    }

    /* ── Toolbar ─────────────────────────────────────────────── */
    {
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2((float)g_win_width, toolbar_h));
        ImGui::Begin("Toolbar", nullptr,
                     ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar |
                     ImGuiWindowFlags_NoBringToFrontOnFocus |
                     ImGuiWindowFlags_NoScrollbar);

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
            float by = wpos.y + (35.0f - box_h) * 0.5f; /* center on row 1 height */
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

            ImGui::Dummy(ImVec2(box_w + 14, 28));
            ImGui::SameLine();
        }

        /* Row 1: Transport — matches standalone toolbar */
        if (ImGui::Button(engine->transport.playing ? "STOP" : "PLAY", ImVec2(60, 28))) {
            bool new_state = !engine->transport.playing;
            GLOG_INFO("PLAY button clicked: playing %d -> %d", engine->transport.playing, new_state);
            engine->transport.playing = new_state;
            s_last_playing = new_state;
            engine->transport.current_beat = 0.0;
            engine->transport.sample_position = 0;
            engine->transport.current_step = 0;
            g_visual_step = 0;
            if (new_state)
                gui->play_start_ticks = SDL_GetPerformanceCounter();
        }

        ImGui::SameLine();
        {
            bool was_rec = engine->recording;
            if (was_rec)
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.78f, 0.12f, 0.12f, 1.0f));
            if (ImGui::Button(was_rec ? "REC *" : "REC", ImVec2(55, 28))) {
                if (engine->recording) {
                    engine->recording = false;
                    if (engine->rec_frames > 0) {
                        sq_export_result_t rec_result = {};
                        rec_result.data = engine->rec_buffer;
                        rec_result.num_frames = engine->rec_frames;
                        rec_result.sample_rate = engine->sample_rate;
                        sq_export_write_wav("recording.wav", &rec_result, 16);
                        snprintf(gui->save_status, sizeof(gui->save_status), "Saved: recording.wav");
                        gui->status_timer = 300;
                    }
                    engine->rec_frames = 0;
                } else {
                    sq_engine_start_recording(engine);
                    snprintf(gui->save_status, sizeof(gui->save_status), "REC -> recording.wav");
                    gui->status_timer = 0;
                }
            }
            if (was_rec) ImGui::PopStyleColor();
        }

        /* BPM knob */
        ImGui::SameLine();
        {
            ImGui::Text("BPM"); ImGui::SameLine();
            float bpm_f = (float)engine->transport.bpm;
            ImGui::PushID("bpm_knob");
            ImVec2 kpos = ImGui::GetCursorScreenPos();
            if (knob_core_ext("##bpm", &bpm_f, 40.0f, 300.0f, 120.0f, 1.0f, kpos.x, kpos.y, 28, 28))
                engine->transport.bpm = (double)bpm_f;
            ImGui::SameLine();
            char bpm_txt[16];
            snprintf(bpm_txt, sizeof(bpm_txt), "%.0f", bpm_f);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 7);
            ImGui::Text("%s", bpm_txt);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 7);
            ImGui::PopID();
        }

        /* Swing knob */
        ImGui::SameLine();
        {
            ImGui::Text("Sw"); ImGui::SameLine();
            float swing_pct = engine->transport.swing * 100.0f;
            ImGui::PushID("sw_knob");
            ImVec2 kpos = ImGui::GetCursorScreenPos();
            if (knob_core_ext("##sw", &swing_pct, 0.0f, 100.0f, 0.0f, 0.5f, kpos.x, kpos.y, 28, 28))
                engine->transport.swing = swing_pct / 100.0f;
            ImGui::SameLine();
            char sw_txt[16];
            snprintf(sw_txt, sizeof(sw_txt), "%.0f%%", swing_pct);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 7);
            ImGui::Text("%s", sw_txt);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 7);
            ImGui::PopID();
        }

        /* Volume knob */
        ImGui::SameLine();
        {
            ImGui::Text("Vol"); ImGui::SameLine();
            float vol_pct = engine->master_volume * 100.0f;
            ImGui::PushID("vol_knob");
            ImVec2 kpos = ImGui::GetCursorScreenPos();
            if (knob_core_ext("##vol", &vol_pct, 0.0f, 100.0f, 80.0f, 0.5f, kpos.x, kpos.y, 28, 28))
                engine->master_volume = vol_pct / 100.0f;
            ImGui::SameLine();
            char vol_txt[16];
            snprintf(vol_txt, sizeof(vol_txt), "%.0f%%", vol_pct);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 7);
            ImGui::Text("%s", vol_txt);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 7);
            ImGui::PopID();
        }

        /* End of row 1 — add EXPORT and PRESETS on the right side */
        ImGui::SameLine(0, 8);
        const float btn_h = 28.0f;
        const ImVec2 btn_sz(60.0f, btn_h);
        const ImVec2 btn_sm(45.0f, btn_h);

        if (ImGui::Button("EXPORT", btn_sz))
            export_dialog_show();
        ImGui::SameLine();
        {
            int *vis = pattern_presets_visible_ptr();
            if (ColoredButton(*vis ? "PRE*" : "PRE", *vis != 0, ImVec4(0.71f, 0.55f, 0.24f, 1.0f), btn_sm))
                *vis = !*vis;
        }

        /* ── Row 2: Panel toggles | THEME | Pattern selector | status ──────── */
        if (ColoredButton(gui->show_piano_roll ? "PNO*" : "PNO",
                          gui->show_piano_roll, ImVec4(0.24f, 0.47f, 0.71f, 1.0f), btn_sm)) {
            gui->show_piano_roll = !gui->show_piano_roll;
            if (gui->show_piano_roll && !show_synth_editor) {
                snprintf(gui->save_status, sizeof(gui->save_status),
                         "Select a synth track for Piano Roll");
                gui->status_timer = 120;
            }
        }
        ImGui::SameLine();

        if (ColoredButton(gui->show_keyboard ? "KEY*" : "KEY",
                          gui->show_keyboard, ImVec4(0.71f, 0.51f, 0.24f, 1.0f), btn_sm))
            gui->show_keyboard = !gui->show_keyboard;
        ImGui::SameLine();

        {
            const char *mode_labels[] = {"PAT", "SNG", "PRF"};
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

        if (ColoredButton(gui->show_mixer ? "FX*" : "FX",
                          gui->show_mixer, ImVec4(0.51f, 0.31f, 0.63f, 1.0f), btn_sm))
            gui->show_mixer = !gui->show_mixer;
        ImGui::SameLine();

        if (ColoredButton(gui->show_browser ? "BRW*" : "BRW",
                          gui->show_browser, ImVec4(0.24f, 0.51f, 0.24f, 1.0f), btn_sm))
            gui->show_browser = !gui->show_browser;
        ImGui::SameLine();

        {
            if (ImGui::Button("THM", btn_sm))
                ImGui::OpenPopup("ThemePopup");
            if (ImGui::BeginPopup("ThemePopup")) {
                ImGui::Text("Select Theme:");
                ImGui::Separator();
                for (int t = 0; t < THEME_COUNT; t++) {
                    bool selected = (theme_current() == (sq_theme_t)t);
                    if (ImGui::Selectable(theme_name((sq_theme_t)t), selected))
                        theme_apply((sq_theme_t)t);
                }
                ImGui::EndPopup();
            }
        }
        ImGui::SameLine(0, 6);

        /* Separator before pattern selector */
        { float sy = ImGui::GetCursorScreenPos().y; float sx = ImGui::GetCursorScreenPos().x;
          ImGui::GetWindowDrawList()->AddLine(ImVec2(sx, sy), ImVec2(sx, sy + 22),
              IM_COL32(80, 80, 85, 200), 1.0f);
          ImGui::Dummy(ImVec2(1, 22)); }
        ImGui::SameLine(0, 6);

        /* Pattern selector */
        {
            ImVec4 active_col(0.24f, 0.63f, 0.39f, 1.0f);
            ImVec4 inactive_col(0.18f, 0.18f, 0.20f, 1.0f);
            float pat_btn_w = 26.0f;
            float pat_btn_h = 22.0f;

            ImGui::Text("Pat:");
            ImGui::SameLine(0, 4);

            int max_visible = 9;
            int total = (int)engine->num_patterns;
            if (g_pat_scroll > total - max_visible) g_pat_scroll = total - max_visible;
            if (g_pat_scroll < 0) g_pat_scroll = 0;

            if (g_pat_scroll > 0) {
                if (ImGui::Button("<##patL", ImVec2(18, pat_btn_h)))
                    g_pat_scroll--;
                ImGui::SameLine(0, 2);
            }

            int end = g_pat_scroll + max_visible;
            if (end > total) end = total;
            for (int p = g_pat_scroll; p < end; p++) {
                char plbl[8];
                snprintf(plbl, sizeof(plbl), "%d##p%d", p + 1, p);
                bool is_active = (p == engine->transport.current_pattern);
                if (is_active)
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
                if (ColoredButton(plbl, is_active, is_active ? active_col : inactive_col,
                                  ImVec2(pat_btn_w, pat_btn_h))) {
                    engine->transport.current_pattern = p;
                    sq_pattern_t *pat = &engine->patterns[p];
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

            if (end < total) {
                if (ImGui::Button(">##patR", ImVec2(18, pat_btn_h)))
                    g_pat_scroll++;
                ImGui::SameLine(0, 2);
            }
            if (engine->num_patterns < 30) {
                if (ImGui::Button("+##addpat", ImVec2(pat_btn_w, pat_btn_h))) {
                    int ni = (int)engine->num_patterns;
                    engine->num_patterns++;
                    init_new_pattern(engine, ni);
                    engine->transport.current_pattern = ni;
                    if (ni >= g_pat_scroll + max_visible)
                        g_pat_scroll = ni - max_visible + 1;
                    sq_pattern_t *pat = &engine->patterns[ni];
                    g_selected_track = -1;
                    for (uint32_t st = 0; st < pat->num_tracks; st++) {
                        if (pat->tracks[st].type == TRACK_SYNTH) {
                            g_selected_track = (int)st;
                            break;
                        }
                    }
                }
                ImGui::SameLine(0, 2);
            }
        }

        ImGui::SameLine(0, 10);
        if (engine->recording) {
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%s", gui->save_status);
        } else if (gui->status_timer > 0) {
            ImGui::TextColored(ImVec4(0.39f, 1.0f, 0.39f, 1.0f), "%s", gui->save_status);
            gui->status_timer--;
        }

        ImGui::End();
    }

    /* ── Arrangement panel ──────────────────────────────────── */
    float arrange_h = 0.0f;
    if (engine->transport.mode != MODE_PATTERN) {
        arrange_h = 160.0f;
        arrangement_draw(engine, 0.0f, toolbar_h, (float)g_win_width, arrange_h);
    }

    /* ── Main layout ─────────────────────────────────────────── */
    float grid_y = toolbar_h + arrange_h;
    float kb_reserve = gui->show_keyboard ? 120.0f : 0.0f;
    float total_h = (float)g_win_height - grid_y - kb_reserve;
    float total_w = (float)g_win_width;

    float browser_w = 0.0f;
    float main_w = total_w;
    if (gui->show_browser) {
        browser_w = 350.0f;
        if (browser_w > total_w * 0.4f)
            browser_w = total_w * 0.4f;
        main_w = total_w - browser_w;
    }

    /* Full-height piano roll mode */
    if (gui->show_piano_roll && show_synth_editor && synth_preset_idx >= 0) {
        if (gui->show_mixer) {
            float pr_w = main_w * 0.60f;
            float se_w = main_w * 0.20f;
            float mx_w = main_w - pr_w - se_w;
            piano_roll_draw(engine, g_selected_track,
                            0.0f, grid_y, pr_w, total_h);
            synth_editor_draw(engine, synth_preset_ptr,
                              pr_w, grid_y, se_w, total_h);
            mixer_view_draw(engine,
                            pr_w + se_w, grid_y, mx_w, total_h);
        } else {
            float pr_w = main_w * 0.70f;
            float se_w = main_w - pr_w;
            piano_roll_draw(engine, g_selected_track,
                            0.0f, grid_y, pr_w, total_h);
            synth_editor_draw(engine, synth_preset_ptr,
                              pr_w, grid_y, se_w, total_h);
        }
    } else {
        /* Normal layout: drum grid + optional bottom panels */
        float grid_h, bottom_h;
        bool has_bottom = show_synth_editor || gui->show_mixer;
        if (has_bottom) {
            bottom_h = 280.0f;
            grid_h = total_h - bottom_h;
            if (grid_h < 150.0f) grid_h = 150.0f;
            bottom_h = total_h - grid_h;
        } else {
            grid_h = total_h;
            bottom_h = 0.0f;
        }

        drum_grid_draw(engine, 0.0f, grid_y, main_w, grid_h);

        if (has_bottom) {
            float bottom_y = grid_y + grid_h;
            if (show_synth_editor && synth_preset_idx >= 0 && gui->show_mixer) {
                float pr_w = main_w * 0.40f;
                float se_w = main_w * 0.30f;
                float mx_w = main_w - pr_w - se_w;
                piano_roll_draw(engine, g_selected_track,
                                0.0f, bottom_y, pr_w, bottom_h);
                synth_editor_draw(engine, synth_preset_ptr,
                                  pr_w, bottom_y, se_w, bottom_h);
                mixer_view_draw(engine,
                                pr_w + se_w, bottom_y, mx_w, bottom_h);
            } else if (show_synth_editor && synth_preset_idx >= 0) {
                float pr_w = main_w * 0.55f;
                float se_w = main_w - pr_w;
                piano_roll_draw(engine, g_selected_track,
                                0.0f, bottom_y, pr_w, bottom_h);
                synth_editor_draw(engine, synth_preset_ptr,
                                  pr_w, bottom_y, se_w, bottom_h);
            } else if (gui->show_mixer) {
                mixer_view_draw(engine,
                                0.0f, bottom_y, main_w, bottom_h);
            }
        }
    }

    /* ── Virtual keyboard ─────────────────────────────────────── */
    if (gui->show_keyboard) {
        float kb_h = 120.0f;
        int kb_preset = -1;
        if (synth_preset_idx >= 0) {
            kb_preset = synth_preset_idx;
        } else {
            int pi2 = engine->transport.current_pattern;
            if (pi2 >= 0 && (uint32_t)pi2 < engine->num_patterns) {
                for (uint32_t t = 0; t < engine->patterns[pi2].num_tracks; t++) {
                    if (engine->patterns[pi2].tracks[t].type == TRACK_SYNTH) {
                        kb_preset = engine->patterns[pi2].tracks[t].synth_preset;
                        break;
                    }
                }
            }
        }
        if (kb_preset < 0) kb_preset = 0;
        virtual_keyboard_draw(engine, kb_preset,
                              0.0f, (float)g_win_height - kb_h,
                              main_w, kb_h);
    }

    if (gui->show_browser) {
        sample_browser_draw(engine,
                            main_w, grid_y, browser_w, total_h);
        if (sample_browser_close_requested()) {
            gui->show_browser = false;
        }
    }

    /* Export dialog */
    if (export_dialog_visible())
        export_dialog_draw(engine);

    /* Pattern presets */
    pattern_presets_draw(engine);

    /* ── Render ──────────────────────────────────────────────── */
    ImGui::Render();
    int w, h;
    SDL_GetWindowSize(gui->window, &w, &h);
    glViewport(0, 0, w, h);
    float bg[4];
    theme_get_clear_color(bg);
    glClearColor(bg[0], bg[1], bg[2], bg[3]);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    SDL_GL_SwapWindow(gui->window);
}

/* ─── Render thread ──────────────────────────────────────────────────────── */

static int render_thread_func(void *userdata)
{
    sq_plugin_gui_t *gui = (sq_plugin_gui_t *)userdata;

    LOG_INFO("Plugin GUI render thread started (%d fps)", RENDER_FPS);

    SDL_GL_MakeCurrent(gui->window, gui->gl_ctx);

    while (gui->running.load()) {
        Uint32 start = SDL_GetTicks();

        plugin_gui_draw_frame(gui);

        Uint32 elapsed = SDL_GetTicks() - start;
        Uint32 target_ms = 1000 / RENDER_FPS;
        if (elapsed < target_ms)
            SDL_Delay(target_ms - elapsed);
    }

    SDL_GL_MakeCurrent(gui->window, NULL);

    LOG_INFO("Plugin GUI render thread exiting");
    return 0;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Public API (called from plugin.c CPLUG callbacks)
 * ═══════════════════════════════════════════════════════════════════════════ */

extern "C" sq_plugin_gui_t *plugin_gui_create(sq_engine_t *engine)
{
    sq_plugin_gui_t *gui = (sq_plugin_gui_t *)calloc(1, sizeof(sq_plugin_gui_t));
    if (!gui) return nullptr;

    gui->engine = engine;
    gui->width  = 1280;
    gui->height = 720;
    gui->scale_factor = 1.0f;
    gui->running.store(0);

    /* Default: select first synth track so synth editor is visible in bottom panel */
    gui->show_piano_roll = false;
    int pi = engine->transport.current_pattern;
    if (pi >= 0 && (uint32_t)pi < engine->num_patterns) {
        sq_pattern_t *pat = &engine->patterns[pi];
        for (uint32_t t = 0; t < pat->num_tracks; t++) {
            if (pat->tracks[t].type == TRACK_SYNTH) {
                g_selected_track = (int)t;
                break;
            }
        }
    }

    return gui;
}

extern "C" void plugin_gui_destroy(sq_plugin_gui_t *gui)
{
    if (!gui) return;
    plugin_gui_detach(gui);
    free(gui);
}

extern "C" int plugin_gui_attach(sq_plugin_gui_t *gui, void *native_handle)
{
    GLOG_INFO("attach called: gui=%p native_handle=%p", (void*)gui, native_handle);

    if (!gui || !native_handle) return -1;

    /* Initialize SDL2 video subsystem if not already done */
    if (!SDL_WasInit(SDL_INIT_VIDEO)) {
        GLOG_INFO("SDL_Init(SDL_INIT_VIDEO)...");
        if (SDL_Init(SDL_INIT_VIDEO) != 0) {
            GLOG_ERROR("SDL_Init(VIDEO) failed: %s", SDL_GetError());
            return -1;
        }
        GLOG_INFO("SDL_Init(VIDEO) OK");
    }

    /* Request OpenGL 3.3 Core Profile */
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 0);

    GLOG_INFO("Creating SDL GL window to embed in host HWND %p...", native_handle);

    gui->window = SDL_CreateWindow(
        "0x808",
        SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
        (int)gui->width, (int)gui->height,
        SDL_WINDOW_OPENGL | SDL_WINDOW_BORDERLESS | SDL_WINDOW_HIDDEN);
    if (!gui->window) {
        GLOG_ERROR("SDL_CreateWindow failed: %s", SDL_GetError());
        return -1;
    }
    GLOG_INFO("SDL_CreateWindow OK: window=%p", (void*)gui->window);

#ifdef _WIN32
    /* Reparent the SDL window into the host's HWND */
    {
        SDL_SysWMinfo wminfo;
        SDL_VERSION(&wminfo.version);
        if (!SDL_GetWindowWMInfo(gui->window, &wminfo)) {
            GLOG_ERROR("SDL_GetWindowWMInfo failed: %s", SDL_GetError());
            SDL_DestroyWindow(gui->window);
            gui->window = nullptr;
            return -1;
        }
        HWND sdl_hwnd = wminfo.info.win.window;
        HWND host_hwnd = (HWND)native_handle;

        SetParent(sdl_hwnd, host_hwnd);
        LONG style = GetWindowLong(sdl_hwnd, GWL_STYLE);
        style = (style & ~WS_POPUP) | WS_CHILD;
        SetWindowLong(sdl_hwnd, GWL_STYLE, style);
        SetWindowPos(sdl_hwnd, NULL, 0, 0,
                     (int)gui->width, (int)gui->height,
                     SWP_NOZORDER | SWP_FRAMECHANGED);
        ShowWindow(sdl_hwnd, SW_SHOW);
        GLOG_INFO("Reparented SDL HWND %p into host HWND %p",
                  (void*)sdl_hwnd, (void*)host_hwnd);
    }
#else
    (void)native_handle;
#endif

    SDL_ShowWindow(gui->window);

    /* Create OpenGL context */
    GLOG_INFO("Creating GL context...");
    gui->gl_ctx = SDL_GL_CreateContext(gui->window);
    if (!gui->gl_ctx) {
        GLOG_ERROR("SDL_GL_CreateContext failed: %s", SDL_GetError());
        SDL_DestroyWindow(gui->window);
        gui->window = nullptr;
        return -1;
    }
    GLOG_INFO("GL context created OK");

    SDL_GL_SetSwapInterval(1);

    /* Load GL3 extension functions */
    gl3_loader_init();
    GLOG_DEBUG("GL3 loader init done");

    /* Initialize Dear ImGui */
    IMGUI_CHECKVERSION();
    gui->imgui_ctx = ImGui::CreateContext();
    ImGui::SetCurrentContext(gui->imgui_ctx);
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui_ImplSDL2_InitForOpenGL(gui->window, gui->gl_ctx);
    ImGui_ImplOpenGL3_Init("#version 330");

    /* Apply theme */
    theme_apply(THEME_LIGHT);
    GLOG_INFO("ImGui init done: ctx=%p", (void*)gui->imgui_ctx);

    /* Release GL context from this thread so the render thread can use it */
    SDL_GL_MakeCurrent(gui->window, NULL);

    /* Start render thread */
    gui->running.store(1);
    gui->render_thread = SDL_CreateThread(render_thread_func, "sq_plugin_gui", gui);
    if (!gui->render_thread) {
        GLOG_ERROR("Failed to create render thread: %s", SDL_GetError());
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext(gui->imgui_ctx);
        gui->imgui_ctx = nullptr;
        SDL_GL_DeleteContext(gui->gl_ctx);
        SDL_DestroyWindow(gui->window);
        gui->gl_ctx = nullptr;
        gui->window = nullptr;
        gui->running.store(0);
        return -1;
    }

    LOG_INFO("Plugin GUI attached to host window, render thread started");
    return 0;
}

extern "C" void plugin_gui_detach(sq_plugin_gui_t *gui)
{
    if (!gui) return;

    /* Stop render thread */
    if (gui->running.load()) {
        gui->running.store(0);
        if (gui->render_thread) {
            SDL_WaitThread(gui->render_thread, NULL);
            gui->render_thread = nullptr;
        }
    }

    /* Clean up ImGui + GL */
    if (gui->imgui_ctx) {
        if (gui->window && gui->gl_ctx)
            SDL_GL_MakeCurrent(gui->window, gui->gl_ctx);
        ImGui::SetCurrentContext(gui->imgui_ctx);
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext(gui->imgui_ctx);
        gui->imgui_ctx = nullptr;
    }
    if (gui->gl_ctx) {
        SDL_GL_DeleteContext(gui->gl_ctx);
        gui->gl_ctx = nullptr;
    }
    if (gui->window) {
        SDL_DestroyWindow(gui->window);
        gui->window = nullptr;
    }

    LOG_INFO("Plugin GUI detached from host window");
}

extern "C" void plugin_gui_set_size(sq_plugin_gui_t *gui, uint32_t width, uint32_t height)
{
    if (!gui) return;
    gui->width = width;
    gui->height = height;
    g_win_width = (int)width;
    g_win_height = (int)height;

    if (gui->window)
        SDL_SetWindowSize(gui->window, (int)width, (int)height);
}

extern "C" void plugin_gui_get_size(sq_plugin_gui_t *gui, uint32_t *width, uint32_t *height)
{
    if (!gui) {
        *width = 1280;
        *height = 720;
        return;
    }
    *width = gui->width;
    *height = gui->height;
}

extern "C" void plugin_gui_set_scale(sq_plugin_gui_t *gui, float scale)
{
    if (gui) gui->scale_factor = scale;
}
