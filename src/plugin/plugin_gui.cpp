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
#include "gui/toolbar.h"
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
#include "app/sq_app.h"
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

#define RENDER_FPS 60

/* ─── SDL keycode → SQ_KEY translation ────────────────────────────────────── */

static int sdl_to_sq_key(int sdlk)
{
    switch (sdlk) {
    case SDLK_SPACE:     return SQ_KEY_SPACE;
    case SDLK_ESCAPE:    return SQ_KEY_ESCAPE;
    case SDLK_1:         return SQ_KEY_1;
    case SDLK_2:         return SQ_KEY_2;
    case SDLK_3:         return SQ_KEY_3;
    case SDLK_4:         return SQ_KEY_4;
    case SDLK_5:         return SQ_KEY_5;
    case SDLK_6:         return SQ_KEY_6;
    case SDLK_7:         return SQ_KEY_7;
    case SDLK_8:         return SQ_KEY_8;
    case SDLK_9:         return SQ_KEY_9;
    case SDLK_c:         return SQ_KEY_C;
    case SDLK_o:         return SQ_KEY_O;
    case SDLK_s:         return SQ_KEY_S;
    case SDLK_t:         return SQ_KEY_T;
    case SDLK_v:         return SQ_KEY_V;
    case SDLK_z:         return SQ_KEY_Z;
    case SDLK_EQUALS:    return SQ_KEY_EQUALS;
    default:             return SQ_KEY_NONE;
    }
}

static int sdl_to_sq_mod(int sdl_mod)
{
    int mod = 0;
    if (sdl_mod & KMOD_CTRL)  mod |= SQ_MOD_CTRL;
    if (sdl_mod & KMOD_SHIFT) mod |= SQ_MOD_SHIFT;
    if (sdl_mod & KMOD_ALT)   mod |= SQ_MOD_ALT;
    return mod;
}

/* ─── Win32 mouse event latching ─────────────────────────────────────────
 * GetAsyncKeyState polling at 60fps can miss fast clicks (press+release
 * between frames). We subclass the SDL window's WndProc to capture
 * WM_LBUTTONDOWN/UP messages so every click is guaranteed to be seen by
 * ImGui for at least one frame. */
#ifdef _WIN32
static WNDPROC s_orig_wndproc = nullptr;
static bool s_mouse_pressed[3]  = {false, false, false}; /* currently held */
static bool s_mouse_clicked[3]  = {false, false, false}; /* down since last frame */

static LRESULT CALLBACK PluginMouseWndProc(HWND hwnd, UINT msg,
                                            WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_LBUTTONDOWN: s_mouse_pressed[0] = true;  s_mouse_clicked[0] = true; break;
    case WM_LBUTTONUP:   s_mouse_pressed[0] = false; break;
    case WM_RBUTTONDOWN: s_mouse_pressed[1] = true;  s_mouse_clicked[1] = true; break;
    case WM_RBUTTONUP:   s_mouse_pressed[1] = false; break;
    case WM_MBUTTONDOWN: s_mouse_pressed[2] = true;  s_mouse_clicked[2] = true; break;
    case WM_MBUTTONUP:   s_mouse_pressed[2] = false; break;
    default: break;
    }
    return CallWindowProcW(s_orig_wndproc, hwnd, msg, wp, lp);
}
#endif

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

    /* App state (shared logic) */
    sq_app_t app;
};

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

    /* Sync globals from app state */
    g_visual_step = gui->app.visual_step;
    g_selected_track = gui->app.selected_track;

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

        {
            bool is_ctrl = (evt.key.keysym.mod & KMOD_CTRL) != 0;

            /* QWERTY piano — always active when keyboard panel is shown */
            if (!is_ctrl && gui->app.panels[SQ_PANEL_KEYBOARD] &&
                !(evt.key.keysym.mod & KMOD_ALT) &&
                (evt.type == SDL_KEYDOWN || evt.type == SDL_KEYUP))
            {
                int kb_preset = sq_app_get_keyboard_preset(&gui->app, engine);
                if (kb_preset < 0) kb_preset = 0;
                virtual_keyboard_key_event(engine, kb_preset,
                                           evt.key.keysym.sym,
                                           evt.type == SDL_KEYDOWN);
            }

            /* Key dispatch via sq_app */
            if (evt.type == SDL_KEYDOWN) {
                int sq_key = sdl_to_sq_key(evt.key.keysym.sym);
                int sq_mod = sdl_to_sq_mod(evt.key.keysym.mod);

                bool imgui_wants_kb = ImGui::GetIO().WantCaptureKeyboard;
                bool is_transport = (sq_key == SQ_KEY_SPACE);
                bool is_pattern_key = (!is_ctrl && sq_key >= SQ_KEY_1 && sq_key <= SQ_KEY_9);

                if (is_transport || is_pattern_key || is_ctrl || !imgui_wants_kb) {
                    sq_app_action_t action = sq_app_handle_key(&gui->app, engine,
                                                                sq_key, sq_mod, true);

                    /* If space started playback, record the start ticks */
                    if (sq_key == SQ_KEY_SPACE && engine->transport.playing)
                        gui->app.play_start_ticks = SDL_GetPerformanceCounter();

                    switch (action) {
                    case SQ_ACTION_QUIT:
                        /* Plugin ignores quit — no escape to close */
                        break;
                    case SQ_ACTION_SAVE:
                        sq_app_set_status(&gui->app,
                                          "Use DAW to save plugin state", 180);
                        break;
                    case SQ_ACTION_LOAD:
                        sq_app_set_status(&gui->app,
                                          "Use DAW to load plugin state", 180);
                        break;
                    case SQ_ACTION_TOGGLE_THEME:
                        theme_toggle();
                        break;
                    case SQ_ACTION_NONE:
                    default:
                        break;
                    }
                }
            }
        }
    }

    /* Visual playhead from wall-clock time */
    sq_app_update_playhead(&gui->app, engine,
                           SDL_GetPerformanceCounter(),
                           SDL_GetPerformanceFrequency());
    g_visual_step = gui->app.visual_step;

    /* Start ImGui frame */
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();

    /* Override mouse state with Win32 WndProc-latched events BEFORE NewFrame().
     * SDL misreports mouse position/clicks when reparented into a DAW host.
     * The WndProc hook (PluginMouseWndProc) captures every WM_LBUTTONDOWN/UP
     * so we never miss a fast click between frames.
     * Must be after ImplSDL2_NewFrame but before ImGui::NewFrame. */
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

            /* Report button as down if currently held OR if a click arrived
             * since last frame (latched by WndProc). This guarantees ImGui
             * sees at least one frame of MouseDown=true for every click. */
            for (int b = 0; b < 3; b++) {
                io.MouseDown[b] = s_mouse_pressed[b] || s_mouse_clicked[b];
                s_mouse_clicked[b] = false; /* consume the latch */
            }
        }
    }
#endif

    ImGui::NewFrame();

    float toolbar_h = 70.0f; /* 2 rows: transport + panels/patterns */

    /* ── Determine if synth editor should be shown (needed for layout below) ── */
    bool show_synth_editor = false;
    int synth_preset_idx = -1;
    int *synth_preset_ptr = nullptr;
    if (gui->app.selected_track >= 0) {
        int pi = engine->transport.current_pattern;
        if (pi >= 0 && (uint32_t)pi < engine->num_patterns) {
            sq_pattern_t *pat = &engine->patterns[pi];
            if ((uint32_t)gui->app.selected_track < pat->num_tracks &&
                pat->tracks[gui->app.selected_track].type == TRACK_SYNTH) {
                show_synth_editor = true;
                synth_preset_ptr = &pat->tracks[gui->app.selected_track].synth_preset;
                synth_preset_idx = *synth_preset_ptr;
            }
        }
    }

    /* ── Toolbar (shared implementation) ──────────────────────────────────── */
    {
        Uint64 play_ticks = gui->app.play_start_ticks;
        sq_toolbar_params_t tp = {};
        tp.engine = engine;
        tp.toolbar_h = toolbar_h;
        tp.is_plugin = true;
        tp.quit = nullptr;
        tp.window = nullptr;
        tp.show_browser    = &gui->app.panels[SQ_PANEL_BROWSER];
        tp.show_mixer      = &gui->app.panels[SQ_PANEL_MIXER];
        tp.show_piano_roll = &gui->app.panels[SQ_PANEL_PIANO_ROLL];
        tp.show_keyboard   = &gui->app.panels[SQ_PANEL_KEYBOARD];
        tp.save_status = gui->app.status_msg;
        tp.save_status_size = SQ_STATUS_LEN;
        tp.status_timer = &gui->app.status_timer;
        tp.play_start_ticks = &play_ticks;
        tp.rec_config = &gui->app.rec_config;
        toolbar_draw(&tp);
        gui->app.play_start_ticks = play_ticks;
    }

    /* ── Arrangement panel ──────────────────────────────────────── */
    float arrange_h = 0.0f;
    if (engine->transport.mode != MODE_PATTERN) {
        arrange_h = 160.0f;
        arrangement_draw(engine, 0.0f, toolbar_h, (float)g_win_width, arrange_h);
    }

    /* ── Main layout ─────────────────────────────────────────── */
    float grid_y = toolbar_h + arrange_h;
    float kb_reserve = gui->app.panels[SQ_PANEL_KEYBOARD] ? 120.0f : 0.0f;
    float total_h = (float)g_win_height - grid_y - kb_reserve;
    float total_w = (float)g_win_width;

    float browser_w = 0.0f;
    float main_w = total_w;
    if (gui->app.panels[SQ_PANEL_BROWSER]) {
        browser_w = 350.0f;
        if (browser_w > total_w * 0.4f)
            browser_w = total_w * 0.4f;
        main_w = total_w - browser_w;
    }

    /* Full-height piano roll mode */
    if (gui->app.panels[SQ_PANEL_PIANO_ROLL] && show_synth_editor && synth_preset_idx >= 0) {
        if (gui->app.panels[SQ_PANEL_MIXER]) {
            float pr_w = main_w * 0.60f;
            float se_w = main_w * 0.20f;
            float mx_w = main_w - pr_w - se_w;
            piano_roll_draw(engine, gui->app.selected_track,
                            0.0f, grid_y, pr_w, total_h);
            synth_editor_draw(engine, synth_preset_ptr,
                              pr_w, grid_y, se_w, total_h);
            mixer_view_draw(engine,
                            pr_w + se_w, grid_y, mx_w, total_h);
        } else {
            float pr_w = main_w * 0.70f;
            float se_w = main_w - pr_w;
            piano_roll_draw(engine, gui->app.selected_track,
                            0.0f, grid_y, pr_w, total_h);
            synth_editor_draw(engine, synth_preset_ptr,
                              pr_w, grid_y, se_w, total_h);
        }
    } else {
        /* Normal layout: drum grid + optional bottom panels */
        float grid_h, bottom_h;
        bool has_bottom = show_synth_editor || gui->app.panels[SQ_PANEL_MIXER];
        if (has_bottom) {
            bottom_h = 360.0f;
            if (gui->app.panels[SQ_PANEL_KEYBOARD] && bottom_h > total_h - 150.0f)
                bottom_h = total_h - 150.0f;
            if (bottom_h < 120.0f) bottom_h = 120.0f;
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
            if (show_synth_editor && synth_preset_idx >= 0 && gui->app.panels[SQ_PANEL_MIXER]) {
                float pr_w = main_w * 0.40f;
                float se_w = main_w * 0.30f;
                float mx_w = main_w - pr_w - se_w;
                piano_roll_draw(engine, gui->app.selected_track,
                                0.0f, bottom_y, pr_w, bottom_h);
                synth_editor_draw(engine, synth_preset_ptr,
                                  pr_w, bottom_y, se_w, bottom_h);
                mixer_view_draw(engine,
                                pr_w + se_w, bottom_y, mx_w, bottom_h);
            } else if (show_synth_editor && synth_preset_idx >= 0) {
                float pr_w = main_w * 0.55f;
                float se_w = main_w - pr_w;
                piano_roll_draw(engine, gui->app.selected_track,
                                0.0f, bottom_y, pr_w, bottom_h);
                synth_editor_draw(engine, synth_preset_ptr,
                                  pr_w, bottom_y, se_w, bottom_h);
            } else if (gui->app.panels[SQ_PANEL_MIXER]) {
                mixer_view_draw(engine,
                                0.0f, bottom_y, main_w, bottom_h);
            }
        }
    }

    /* ── Virtual keyboard ─────────────────────────────────────── */
    if (gui->app.panels[SQ_PANEL_KEYBOARD]) {
        float kb_h = 120.0f;
        int kb_preset = sq_app_get_keyboard_preset(&gui->app, engine);
        if (kb_preset < 0) kb_preset = 0;
        virtual_keyboard_draw(engine, kb_preset,
                              0.0f, (float)g_win_height - kb_h,
                              main_w, kb_h);
    }

    if (gui->app.panels[SQ_PANEL_BROWSER]) {
        sample_browser_draw(engine,
                            main_w, grid_y, browser_w, total_h);
        if (sample_browser_close_requested()) {
            gui->app.panels[SQ_PANEL_BROWSER] = false;
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

    /* Sync app state back from globals (components may have changed them) */
    gui->app.selected_track = g_selected_track;
}

/* ─── Render thread ──────────────────────────────────────────────────────── */

static int render_thread_func(void *userdata)
{
    sq_plugin_gui_t *gui = (sq_plugin_gui_t *)userdata;

    LOG_INFO("Plugin GUI render thread started (%d fps)", RENDER_FPS);

    SDL_GL_MakeCurrent(gui->window, gui->gl_ctx);

    while (gui->running.load()) {
        if (!gui->window) break;

        Uint32 start = SDL_GetTicks();

        plugin_gui_draw_frame(gui);

        Uint32 elapsed = SDL_GetTicks() - start;
        Uint32 target_ms = 1000 / RENDER_FPS;
        if (elapsed < target_ms)
            SDL_Delay(target_ms - elapsed);
    }

    if (gui->window && gui->gl_ctx)
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

    /* Initialize app state */
    sq_app_init(&gui->app);

    /* Default: select first synth track so synth editor is visible in bottom panel */
    int pi = engine->transport.current_pattern;
    if (pi >= 0 && (uint32_t)pi < engine->num_patterns) {
        sq_pattern_t *pat = &engine->patterns[pi];
        for (uint32_t t = 0; t < pat->num_tracks; t++) {
            if (pat->tracks[t].type == TRACK_SYNTH) {
                gui->app.selected_track = (int)t;
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

        /* Install WndProc hook to latch mouse button events so we never
         * miss a fast click between render frames */
        s_orig_wndproc = (WNDPROC)SetWindowLongPtrW(
            sdl_hwnd, GWLP_WNDPROC, (LONG_PTR)PluginMouseWndProc);
        GLOG_INFO("Installed mouse WndProc hook (orig=%p)", (void*)s_orig_wndproc);
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

    /* Disable vsync — we do our own frame timing with SDL_Delay.
     * Vsync can cause SDL_GL_SwapWindow to block indefinitely when the
     * host destroys the parent window during shutdown, hanging the DAW. */
    SDL_GL_SetSwapInterval(0);

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

    /* Signal render thread to stop */
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
#ifdef _WIN32
        /* Restore original WndProc before destroying window */
        if (s_orig_wndproc) {
            SDL_SysWMinfo wminfo;
            SDL_VERSION(&wminfo.version);
            if (SDL_GetWindowWMInfo(gui->window, &wminfo)) {
                SetWindowLongPtrW(wminfo.info.win.window,
                                  GWLP_WNDPROC, (LONG_PTR)s_orig_wndproc);
            }
            s_orig_wndproc = nullptr;
        }
#endif
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
