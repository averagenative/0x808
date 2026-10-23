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

/* Linux embeds through native X11 + GLX instead of SDL. Hosts hand us an
 * X11 window ID, while the system SDL2 can be sdl2-compat over SDL3, which
 * prefers Wayland, never lands inside the host window, and hung REAPER's UI
 * thread. X11 headers come last because they define macros like None/Bool. */
#if defined(__linux__)
#define SQ_PLUGIN_GUI_X11 1
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/XKBlib.h>
#include <X11/keysym.h>
#include <GL/glx.h>
#include <pthread.h>
#include <time.h>
#include <cfloat>
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

#ifdef SQ_PLUGIN_GUI_X11
    /* Every X11/GLX/ImGui call happens on x11_thread, on its own Display
     * connection; the host UI thread only starts and joins it. */
    Window    x11_parent;
    pthread_t x11_thread;
    bool      x11_thread_started;
    std::atomic<uint64_t> x11_req_size; /* (w << 32) | h from host, 0 = none */
#endif

    /* App state (shared logic) */
    sq_app_t app;

    /* Plugin clock setting, owned by the plugin (true = follow the host) */
    bool *host_sync;

    /* Held while a frame reads or edits the engine, so the plugin can
     * rebuild the engine (state load) without the GUI touching it */
    SDL_mutex *engine_lock;
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

/* ─── Key dispatch (QWERTY piano + app shortcuts) ───────────────────────────
 * Takes SDL keycodes/modifiers; the X11 path translates into these. */

static void plugin_gui_handle_key(sq_plugin_gui_t *gui, SDL_Keycode sym,
                                  Uint16 kmod, bool down)
{
    sq_engine_t *engine = gui->engine;
    bool is_ctrl = (kmod & KMOD_CTRL) != 0;

    /* QWERTY piano — always active when keyboard panel is shown */
    if (!is_ctrl && gui->app.panels[SQ_PANEL_KEYBOARD] && !(kmod & KMOD_ALT)) {
        int kb_preset = sq_app_get_keyboard_preset(&gui->app, engine);
        if (kb_preset < 0) kb_preset = 0;
        virtual_keyboard_key_event(engine, kb_preset, sym, down);
    }

    /* Key dispatch via sq_app */
    if (!down) return;

    int sq_key = sdl_to_sq_key(sym);
    int sq_mod = sdl_to_sq_mod(kmod);

    /* HOST clock: the host's transport owns play/stop and tempo */
    if (*gui->host_sync &&
        (sq_key == SQ_KEY_SPACE || (is_ctrl && sq_key == SQ_KEY_T))) {
        sq_app_set_status(&gui->app, "Clock is HOST: use the host's transport", 120);
        return;
    }

    bool imgui_wants_kb = ImGui::GetIO().WantCaptureKeyboard;
    bool is_transport = (sq_key == SQ_KEY_SPACE);
    bool is_pattern_key = (!is_ctrl && sq_key >= SQ_KEY_1 && sq_key <= SQ_KEY_9);

    if (!(is_transport || is_pattern_key || is_ctrl || !imgui_wants_kb))
        return;

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
        sq_app_set_status(&gui->app, "Use DAW to save plugin state", 180);
        break;
    case SQ_ACTION_LOAD:
        sq_app_set_status(&gui->app, "Use DAW to load plugin state", 180);
        break;
    case SQ_ACTION_TOGGLE_THEME:
        theme_toggle();
        break;
    case SQ_ACTION_TAP_TEMPO: {
        uint64_t now_us = (uint64_t)(
            (double)SDL_GetPerformanceCounter() /
            (double)SDL_GetPerformanceFrequency() * 1000000.0);
        double bpm = sq_app_tap_tempo(&gui->app, now_us);
        if (bpm > 0.0) {
            engine->transport.bpm = bpm;
            char msg[32];
            snprintf(msg, sizeof(msg), "Tap: %.0f BPM", bpm);
            sq_app_set_status(&gui->app, msg, 90);
        }
        break;
    }
    case SQ_ACTION_NONE:
    default:
        break;
    }
}

/* ─── Draw one frame ─────────────────────────────────────────────────────── */

static void plugin_gui_draw_ui(sq_plugin_gui_t *gui);
static void plugin_gui_render_gl(int w, int h);

/* Visual playhead: wall-clock time on the plugin's own clock. With the HOST
 * clock the sequencer can start mid-pattern or jump, so show its step. */
static void plugin_gui_update_playhead(sq_plugin_gui_t *gui)
{
    sq_engine_t *engine = gui->engine;
    sq_app_update_playhead(&gui->app, engine,
                           SDL_GetPerformanceCounter(),
                           SDL_GetPerformanceFrequency());
    if (*gui->host_sync && engine->transport.playing)
        gui->app.visual_step = engine->transport.current_step;
    g_visual_step = gui->app.visual_step;
}

static bool s_last_playing = false;

static void plugin_gui_draw_frame(sq_plugin_gui_t *gui)
{
    sq_engine_t *engine = gui->engine;
    if (!engine || !gui->window || !gui->running.load()) return;

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

    /* Poll SDL events.
     * On Windows, SDL_PollEvent pumps the Win32 message queue which can
     * deadlock during shutdown (host UI thread waiting on us, we waiting
     * on host). Check the running flag first so we bail out immediately
     * when detach has been called. */
    if (!gui->running.load()) return;

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

        if (evt.type == SDL_KEYDOWN || evt.type == SDL_KEYUP)
            plugin_gui_handle_key(gui, evt.key.keysym.sym, evt.key.keysym.mod,
                                  evt.type == SDL_KEYDOWN);
    }

    plugin_gui_update_playhead(gui);

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

    plugin_gui_draw_ui(gui);

    int w, h;
    SDL_GetWindowSize(gui->window, &w, &h);
    plugin_gui_render_gl(w, h);
    SDL_GL_SwapWindow(gui->window);

    /* Sync app state back from globals (components may have changed them) */
    gui->app.selected_track = g_selected_track;
}

/* Toolbar, panels and dialogs for one ImGui frame. Shared by the SDL and X11
 * paths, which handle input before it and GL presentation after it. */
static void plugin_gui_draw_ui(sq_plugin_gui_t *gui)
{
    sq_engine_t *engine = gui->engine;

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
        tp.host_sync = gui->host_sync;
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
}

static void plugin_gui_render_gl(int w, int h)
{
    glViewport(0, 0, w, h);
    float bg[4];
    theme_get_clear_color(bg);
    glClearColor(bg[0], bg[1], bg[2], bg[3]);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
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

        SDL_LockMutex(gui->engine_lock);
        plugin_gui_draw_frame(gui);
        SDL_UnlockMutex(gui->engine_lock);

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

#ifdef SQ_PLUGIN_GUI_X11

/* ═══════════════════════════════════════════════════════════════════════════
 * Linux: native X11 child window + GLX
 *
 * attach() only spawns x11_render_thread; that thread opens its own Display,
 * creates a child window inside the host's window, owns the GL and ImGui
 * contexts, and polls events without blocking. detach() clears `running` and
 * joins, so the host UI thread never waits on X, GL or the compositor.
 * ═══════════════════════════════════════════════════════════════════════════ */

/* ─── X11 error handling ─────────────────────────────────────────────────────
 * Xlib's error handler is process-wide and the default one exits. If the host
 * destroys its window just before we notice, our next draw raises BadWindow or
 * GLXBadDrawable, which must not take the DAW down. Errors on our own
 * connections are logged and dropped; the rest go to the previous handler. */

#define X11_MAX_DISPLAYS 16

static std::atomic<Display *> s_x11_displays[X11_MAX_DISPLAYS];
static XErrorHandler s_x11_prev_error_handler = nullptr;
static pthread_once_t s_x11_error_once = PTHREAD_ONCE_INIT;

static int x11_error_handler(Display *dpy, XErrorEvent *ev)
{
    for (int i = 0; i < X11_MAX_DISPLAYS; i++) {
        if (s_x11_displays[i].load() == dpy) {
            GLOG_WARN("X11 error on GUI connection: code=%d request=%d.%d resource=0x%lx",
                      ev->error_code, ev->request_code, ev->minor_code,
                      ev->resourceid);
            return 0;
        }
    }
    return s_x11_prev_error_handler ? s_x11_prev_error_handler(dpy, ev) : 0;
}

/* Runs on the host UI thread (from attach), never inside a GDK error trap */
static void x11_install_error_handler(void)
{
    s_x11_prev_error_handler = XSetErrorHandler(x11_error_handler);
}

static bool x11_track_display(Display *dpy, bool add)
{
    for (int i = 0; i < X11_MAX_DISPLAYS; i++) {
        Display *expected = add ? nullptr : dpy;
        if (s_x11_displays[i].compare_exchange_strong(expected, add ? dpy : nullptr))
            return true;
    }
    return false;
}

static bool x11_has_glx_ext(Display *dpy, const char *name)
{
    const char *exts = glXQueryExtensionsString(dpy, DefaultScreen(dpy));
    size_t len = strlen(name);
    for (const char *p = exts; p && (p = strstr(p, name)) != nullptr; p += len) {
        bool starts = (p == exts || p[-1] == ' ');
        bool ends = (p[len] == ' ' || p[len] == '\0');
        if (starts && ends) return true;
    }
    return false;
}

/* ─── Input translation ──────────────────────────────────────────────────── */

static ImGuiKey x11_imgui_key(KeySym ks)
{
    if (ks >= XK_a && ks <= XK_z)   return (ImGuiKey)(ImGuiKey_A + (ks - XK_a));
    if (ks >= XK_0 && ks <= XK_9)   return (ImGuiKey)(ImGuiKey_0 + (ks - XK_0));
    if (ks >= XK_F1 && ks <= XK_F12) return (ImGuiKey)(ImGuiKey_F1 + (ks - XK_F1));
    switch (ks) {
    case XK_Tab:
    case XK_ISO_Left_Tab: return ImGuiKey_Tab;
    case XK_Left:         return ImGuiKey_LeftArrow;
    case XK_Right:        return ImGuiKey_RightArrow;
    case XK_Up:           return ImGuiKey_UpArrow;
    case XK_Down:         return ImGuiKey_DownArrow;
    case XK_Page_Up:      return ImGuiKey_PageUp;
    case XK_Page_Down:    return ImGuiKey_PageDown;
    case XK_Home:         return ImGuiKey_Home;
    case XK_End:          return ImGuiKey_End;
    case XK_Insert:       return ImGuiKey_Insert;
    case XK_Delete:       return ImGuiKey_Delete;
    case XK_BackSpace:    return ImGuiKey_Backspace;
    case XK_space:        return ImGuiKey_Space;
    case XK_Return:       return ImGuiKey_Enter;
    case XK_KP_Enter:     return ImGuiKey_KeypadEnter;
    case XK_Escape:       return ImGuiKey_Escape;
    case XK_Control_L:    return ImGuiKey_LeftCtrl;
    case XK_Control_R:    return ImGuiKey_RightCtrl;
    case XK_Shift_L:      return ImGuiKey_LeftShift;
    case XK_Shift_R:      return ImGuiKey_RightShift;
    case XK_Alt_L:        return ImGuiKey_LeftAlt;
    case XK_Alt_R:        return ImGuiKey_RightAlt;
    case XK_Super_L:      return ImGuiKey_LeftSuper;
    case XK_Super_R:      return ImGuiKey_RightSuper;
    default:              return ImGuiKey_None;
    }
}

/* SDL keycodes for printable ASCII are the character itself, and so are the
 * X11 Latin-1 keysyms, so only non-printing keys need a table. */
static SDL_Keycode x11_sdl_keycode(KeySym ks)
{
    if (ks >= 0x20 && ks <= 0x7e) return (SDL_Keycode)ks;
    switch (ks) {
    case XK_Escape:    return SDLK_ESCAPE;
    case XK_Return:    return SDLK_RETURN;
    case XK_BackSpace: return SDLK_BACKSPACE;
    case XK_Tab:       return SDLK_TAB;
    case XK_Delete:    return SDLK_DELETE;
    default:           return SDLK_UNKNOWN;
    }
}

static void x11_key_event(sq_plugin_gui_t *gui, XKeyEvent *ev, bool down)
{
    ImGuiIO &io = ImGui::GetIO();
    KeySym ks = XLookupKeysym(ev, 0);

    /* ev->state holds the modifiers from before this event */
    bool ctrl  = (ev->state & ControlMask) != 0;
    bool shift = (ev->state & ShiftMask) != 0;
    bool alt   = (ev->state & Mod1Mask) != 0;
    bool meta  = (ev->state & Mod4Mask) != 0;
    if (ks == XK_Control_L || ks == XK_Control_R) ctrl = down;
    if (ks == XK_Shift_L || ks == XK_Shift_R)     shift = down;
    if (ks == XK_Alt_L || ks == XK_Alt_R)         alt = down;
    if (ks == XK_Super_L || ks == XK_Super_R)     meta = down;
    io.AddKeyEvent(ImGuiMod_Ctrl, ctrl);
    io.AddKeyEvent(ImGuiMod_Shift, shift);
    io.AddKeyEvent(ImGuiMod_Alt, alt);
    io.AddKeyEvent(ImGuiMod_Super, meta);

    ImGuiKey key = x11_imgui_key(ks);
    if (key != ImGuiKey_None)
        io.AddKeyEvent(key, down);

    if (down && !ctrl && !alt) {
        char buf[8];
        KeySym text_ks;
        int n = XLookupString(ev, buf, (int)sizeof(buf), &text_ks, nullptr);
        for (int i = 0; i < n; i++) {
            if (buf[i] >= 0x20 && buf[i] < 0x7f)
                io.AddInputCharacter((unsigned int)buf[i]);
        }
    }

    SDL_Keycode sym = x11_sdl_keycode(ks);
    if (sym != SDLK_UNKNOWN) {
        Uint16 kmod = (Uint16)((ctrl ? KMOD_LCTRL : 0) |
                               (shift ? KMOD_LSHIFT : 0) |
                               (alt ? KMOD_LALT : 0));
        plugin_gui_handle_key(gui, sym, kmod, down);
    }
}

/* Drains pending X events into ImGui without blocking.
 * Returns false once our window has been destroyed (host tore down its own). */
static bool x11_pump_events(sq_plugin_gui_t *gui, Display *dpy, Window win,
                            int *win_w, int *win_h)
{
    ImGuiIO &io = ImGui::GetIO();

    while (XPending(dpy) > 0) {
        XEvent ev;
        XNextEvent(dpy, &ev);

        switch (ev.type) {
        case ConfigureNotify:
            if (ev.xconfigure.window == gui->x11_parent) {
                /* Fill the host container, like the Win32 path */
                if (ev.xconfigure.width > 0 && ev.xconfigure.height > 0 &&
                    (ev.xconfigure.width != *win_w || ev.xconfigure.height != *win_h))
                    XResizeWindow(dpy, win, (unsigned)ev.xconfigure.width,
                                  (unsigned)ev.xconfigure.height);
            } else if (ev.xconfigure.window == win) {
                *win_w = ev.xconfigure.width;
                *win_h = ev.xconfigure.height;
            }
            break;

        case DestroyNotify:
            if (ev.xdestroywindow.window == win) return false;
            break;

        case MotionNotify:
            io.AddMousePosEvent((float)ev.xmotion.x, (float)ev.xmotion.y);
            break;

        case EnterNotify:
            io.AddMousePosEvent((float)ev.xcrossing.x, (float)ev.xcrossing.y);
            break;

        case LeaveNotify:
            io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
            break;

        case ButtonPress:
        case ButtonRelease: {
            bool down = (ev.type == ButtonPress);
            unsigned int b = ev.xbutton.button;

            /* Buttons 4-7 are wheel notches: up, down, left, right */
            if (b >= 4 && b <= 7) {
                if (down)
                    io.AddMouseWheelEvent(b == 6 ? 1.0f : b == 7 ? -1.0f : 0.0f,
                                          b == 4 ? 1.0f : b == 5 ? -1.0f : 0.0f);
                break;
            }

            int ib = (b == 1) ? 0 : (b == 3) ? 1 : (b == 2) ? 2 : -1;
            if (ib < 0) break;
            io.AddMousePosEvent((float)ev.xbutton.x, (float)ev.xbutton.y);
            io.AddMouseButtonEvent(ib, down);

            /* Take keyboard focus on click so shortcuts and the QWERTY
             * piano reach the plugin instead of the host */
            if (down)
                XSetInputFocus(dpy, win, RevertToParent, CurrentTime);
            break;
        }

        case KeyPress:
        case KeyRelease:
            x11_key_event(gui, &ev.xkey, ev.type == KeyPress);
            break;

        case FocusIn:
            io.AddFocusEvent(true);
            break;

        case FocusOut:
            io.AddFocusEvent(false);
            break;

        default:
            break;
        }
    }
    return true;
}

/* ─── Window + GL context ────────────────────────────────────────────────── */

typedef struct {
    Display   *dpy;
    Window     win;
    Colormap   cmap;
    GLXContext ctx;
    int        w, h;
} x11_surface_t;

static void x11_surface_close(x11_surface_t *xs, bool window_alive)
{
    if (!xs->dpy) return;
    if (xs->ctx) {
        glXMakeCurrent(xs->dpy, None, nullptr);
        glXDestroyContext(xs->dpy, xs->ctx);
    }
    if (xs->win && window_alive) XDestroyWindow(xs->dpy, xs->win);
    if (xs->cmap) XFreeColormap(xs->dpy, xs->cmap);

    /* Flush so any errors arrive while our handler still claims this display */
    XSync(xs->dpy, False);
    x11_track_display(xs->dpy, false);
    XCloseDisplay(xs->dpy);
    memset(xs, 0, sizeof(*xs));
}

static bool x11_surface_open(x11_surface_t *xs, Window parent,
                             uint32_t def_w, uint32_t def_h)
{
    memset(xs, 0, sizeof(*xs));

    Display *dpy = XOpenDisplay(nullptr);
    if (!dpy) {
        GLOG_ERROR("XOpenDisplay failed");
        return false;
    }
    if (!x11_track_display(dpy, true)) {
        GLOG_ERROR("Too many plugin GUIs open");
        XCloseDisplay(dpy);
        return false;
    }
    xs->dpy = dpy;

    /* Fill the host container; fall back to our default size */
    XWindowAttributes pa;
    if (!XGetWindowAttributes(dpy, parent, &pa)) {
        GLOG_ERROR("Host window 0x%lx is not valid", (unsigned long)parent);
        x11_surface_close(xs, false);
        return false;
    }
    xs->w = pa.width > 1 ? pa.width : (int)def_w;
    xs->h = pa.height > 1 ? pa.height : (int)def_h;

    static const int fb_attribs[] = {
        GLX_X_RENDERABLE,  True,
        GLX_DRAWABLE_TYPE, GLX_WINDOW_BIT,
        GLX_RENDER_TYPE,   GLX_RGBA_BIT,
        GLX_X_VISUAL_TYPE, GLX_TRUE_COLOR,
        GLX_RED_SIZE,      8,
        GLX_GREEN_SIZE,    8,
        GLX_BLUE_SIZE,     8,
        GLX_DOUBLEBUFFER,  True,
        None
    };
    int nfb = 0;
    GLXFBConfig *fbs = glXChooseFBConfig(dpy, DefaultScreen(dpy), fb_attribs, &nfb);
    if (!fbs || nfb == 0) {
        GLOG_ERROR("glXChooseFBConfig found no double-buffered RGB config");
        if (fbs) XFree(fbs);
        x11_surface_close(xs, false);
        return false;
    }

    /* Prefer a depth-24 visual: 32-bit ARGB visuals can composite as transparent */
    GLXFBConfig fb = fbs[0];
    for (int i = 0; i < nfb; i++) {
        XVisualInfo *v = glXGetVisualFromFBConfig(dpy, fbs[i]);
        if (!v) continue;
        bool depth24 = (v->depth == 24);
        XFree(v);
        if (depth24) {
            fb = fbs[i];
            break;
        }
    }
    XFree(fbs);

    XVisualInfo *vi = glXGetVisualFromFBConfig(dpy, fb);
    if (!vi) {
        GLOG_ERROR("glXGetVisualFromFBConfig failed");
        x11_surface_close(xs, false);
        return false;
    }

    xs->cmap = XCreateColormap(dpy, RootWindow(dpy, vi->screen), vi->visual, AllocNone);

    XSetWindowAttributes swa;
    memset(&swa, 0, sizeof(swa));
    swa.colormap = xs->cmap;
    swa.border_pixel = 0;
    swa.background_pixmap = None;
    swa.event_mask = ExposureMask | StructureNotifyMask | PointerMotionMask |
                     ButtonPressMask | ButtonReleaseMask |
                     KeyPressMask | KeyReleaseMask |
                     EnterWindowMask | LeaveWindowMask | FocusChangeMask;
    xs->win = XCreateWindow(dpy, parent, 0, 0, (unsigned)xs->w, (unsigned)xs->h, 0,
                            vi->depth, InputOutput, vi->visual,
                            CWColormap | CWBorderPixel | CWBackPixmap | CWEventMask,
                            &swa);
    XFree(vi);

    /* Track host container resizes */
    XSelectInput(dpy, parent, StructureNotifyMask);

    /* Key repeat sends repeated KeyPress only, not Release+Press pairs */
    XkbSetDetectableAutoRepeat(dpy, True, nullptr);

    PFNGLXCREATECONTEXTATTRIBSARBPROC create_ctx =
        (PFNGLXCREATECONTEXTATTRIBSARBPROC)glXGetProcAddressARB(
            (const GLubyte *)"glXCreateContextAttribsARB");
    if (create_ctx && x11_has_glx_ext(dpy, "GLX_ARB_create_context")) {
        static const int ctx_attribs[] = {
            GLX_CONTEXT_MAJOR_VERSION_ARB, 3,
            GLX_CONTEXT_MINOR_VERSION_ARB, 3,
            GLX_CONTEXT_PROFILE_MASK_ARB,  GLX_CONTEXT_CORE_PROFILE_BIT_ARB,
            None
        };
        xs->ctx = create_ctx(dpy, fb, nullptr, True, ctx_attribs);
        XSync(dpy, False);
    }
    if (!xs->ctx)
        xs->ctx = glXCreateNewContext(dpy, fb, GLX_RGBA_TYPE, nullptr, True);
    if (!xs->ctx || !glXMakeCurrent(dpy, xs->win, xs->ctx)) {
        GLOG_ERROR("GLX context creation failed");
        x11_surface_close(xs, true);
        return false;
    }

    /* No vsync: frame pacing is ours, and a blocked swap would stall detach */
    if (x11_has_glx_ext(dpy, "GLX_EXT_swap_control")) {
        PFNGLXSWAPINTERVALEXTPROC swap_ext = (PFNGLXSWAPINTERVALEXTPROC)
            glXGetProcAddressARB((const GLubyte *)"glXSwapIntervalEXT");
        if (swap_ext) swap_ext(dpy, xs->win, 0);
    } else if (x11_has_glx_ext(dpy, "GLX_MESA_swap_control")) {
        PFNGLXSWAPINTERVALMESAPROC swap_mesa = (PFNGLXSWAPINTERVALMESAPROC)
            glXGetProcAddressARB((const GLubyte *)"glXSwapIntervalMESA");
        if (swap_mesa) swap_mesa(0);
    }

    XMapWindow(dpy, xs->win);
    XSync(dpy, False);
    return true;
}

/* ─── Render thread ──────────────────────────────────────────────────────── */

/* ImGui's current context and the sq_gui globals (g_selected_track, ...) are
 * process-wide, so render threads of different plugin instances take turns:
 * anything touching ImGui or those globals runs under this lock. */
static pthread_mutex_t s_x11_frame_lock = PTHREAD_MUTEX_INITIALIZER;

static void x11_draw_frame(sq_plugin_gui_t *gui, int w, int h)
{
    g_win_width = w;
    g_win_height = h;
    g_visual_step = gui->app.visual_step;
    g_selected_track = gui->app.selected_track;

    plugin_gui_update_playhead(gui);

    ImGui_ImplOpenGL3_NewFrame();
    plugin_gui_draw_ui(gui);
    plugin_gui_render_gl(w, h);

    /* Sync app state back from globals (components may have changed them) */
    gui->app.selected_track = g_selected_track;
}

static void *x11_render_thread(void *userdata)
{
    sq_plugin_gui_t *gui = (sq_plugin_gui_t *)userdata;

    x11_surface_t xs;
    if (!x11_surface_open(&xs, gui->x11_parent, gui->width, gui->height)) {
        GLOG_ERROR("Plugin GUI unavailable: could not embed in host window");
        return nullptr;
    }
    GLOG_INFO("Embedded X11 window 0x%lx in host window 0x%lx (%dx%d)",
              (unsigned long)xs.win, (unsigned long)gui->x11_parent, xs.w, xs.h);

    pthread_mutex_lock(&s_x11_frame_lock);
    ImGuiContext *imgui = ImGui::CreateContext();
    ImGui::SetCurrentContext(imgui);
    gui->imgui_ctx = imgui;
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.BackendPlatformName = "0x808_x11";
    io.IniFilename = nullptr; /* don't drop imgui.ini in the host's working dir */
    ImGui_ImplOpenGL3_Init("#version 330");
    theme_apply(THEME_LIGHT);
    pthread_mutex_unlock(&s_x11_frame_lock);

    const long frame_ns = 1000000000L / RENDER_FPS;
    struct timespec prev;
    clock_gettime(CLOCK_MONOTONIC, &prev);
    bool window_alive = true;

    while (gui->running.load()) {
        struct timespec now;

        SDL_LockMutex(gui->engine_lock);
        pthread_mutex_lock(&s_x11_frame_lock);
        ImGui::SetCurrentContext(imgui);

        window_alive = x11_pump_events(gui, xs.dpy, xs.win, &xs.w, &xs.h);
        if (window_alive) {
            uint64_t req = gui->x11_req_size.exchange(0);
            if (req)
                XResizeWindow(xs.dpy, xs.win, (unsigned)(req >> 32),
                              (unsigned)(req & 0xffffffffu));

            clock_gettime(CLOCK_MONOTONIC, &now);
            float dt = (float)(now.tv_sec - prev.tv_sec) +
                       (float)(now.tv_nsec - prev.tv_nsec) * 1e-9f;
            prev = now;
            io.DeltaTime = dt > 0.0f ? dt : 1.0f / RENDER_FPS;
            io.DisplaySize = ImVec2((float)xs.w, (float)xs.h);
            io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
            gui->width = (uint32_t)xs.w;
            gui->height = (uint32_t)xs.h;

            x11_draw_frame(gui, xs.w, xs.h);
        }
        pthread_mutex_unlock(&s_x11_frame_lock);
        SDL_UnlockMutex(gui->engine_lock);

        if (!window_alive) {
            GLOG_WARN("Host destroyed the plugin window before detach");
            break;
        }

        glXSwapBuffers(xs.dpy, xs.win);

        struct timespec end;
        clock_gettime(CLOCK_MONOTONIC, &end);
        long spent = (end.tv_sec - now.tv_sec) * 1000000000L +
                     (end.tv_nsec - now.tv_nsec);
        if (spent < frame_ns) {
            struct timespec nap = { 0, frame_ns - spent };
            nanosleep(&nap, nullptr);
        }
    }

    pthread_mutex_lock(&s_x11_frame_lock);
    ImGui::SetCurrentContext(imgui);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui::DestroyContext(imgui);
    gui->imgui_ctx = nullptr;
    pthread_mutex_unlock(&s_x11_frame_lock);
    x11_surface_close(&xs, window_alive);

    GLOG_INFO("Plugin GUI render thread exiting");
    return nullptr;
}

static void x11_detach(sq_plugin_gui_t *gui);

static int x11_attach(sq_plugin_gui_t *gui, void *native_handle)
{
    /* Re-parent without an intervening detach */
    if (gui->x11_thread_started) x11_detach(gui);

    pthread_once(&s_x11_error_once, x11_install_error_handler);

    gui->x11_parent = (Window)(uintptr_t)native_handle;
    gui->x11_req_size.store(0);
    gui->running.store(1);
    if (pthread_create(&gui->x11_thread, nullptr, x11_render_thread, gui) != 0) {
        GLOG_ERROR("Failed to create render thread");
        gui->running.store(0);
        return -1;
    }
    gui->x11_thread_started = true;
    return 0;
}

static void x11_detach(sq_plugin_gui_t *gui)
{
    if (!gui->x11_thread_started) return;
    gui->running.store(0);
    pthread_join(gui->x11_thread, nullptr);
    gui->x11_thread_started = false;
    GLOG_INFO("Plugin GUI detached from host window");
}

#endif /* SQ_PLUGIN_GUI_X11 */

/* ═══════════════════════════════════════════════════════════════════════════
 * Public API (called from plugin.c CPLUG callbacks)
 * ═══════════════════════════════════════════════════════════════════════════ */

extern "C" sq_plugin_gui_t *plugin_gui_create(sq_engine_t *engine, bool *host_sync)
{
    sq_plugin_gui_t *gui = (sq_plugin_gui_t *)calloc(1, sizeof(sq_plugin_gui_t));
    if (!gui) return nullptr;

    gui->engine = engine;
    gui->host_sync = host_sync;
    gui->engine_lock = SDL_CreateMutex();
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

extern "C" void plugin_gui_lock(sq_plugin_gui_t *gui)
{
    if (gui) SDL_LockMutex(gui->engine_lock);
}

extern "C" void plugin_gui_unlock(sq_plugin_gui_t *gui)
{
    if (gui) SDL_UnlockMutex(gui->engine_lock);
}

extern "C" void plugin_gui_destroy(sq_plugin_gui_t *gui)
{
    if (!gui) return;
    plugin_gui_detach(gui);
    SDL_DestroyMutex(gui->engine_lock);
    free(gui);
}

extern "C" int plugin_gui_attach(sq_plugin_gui_t *gui, void *native_handle)
{
    GLOG_INFO("attach called: gui=%p native_handle=%p", (void*)gui, native_handle);

    if (!gui || !native_handle) return -1;

#ifdef SQ_PLUGIN_GUI_X11
    return x11_attach(gui, native_handle);
#endif

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

#ifdef SQ_PLUGIN_GUI_X11
    x11_detach(gui);
    return;
#endif

    /* Signal render thread to stop */
    if (gui->running.load()) {
        gui->running.store(0);

        if (gui->render_thread) {
#ifdef _WIN32
            /* On Windows, the render thread's SDL_PollEvent pumps Win32
             * messages via PeekMessage/DispatchMessage. If the host UI
             * thread (us) blocks on SDL_WaitThread, and the render thread
             * is blocked waiting for a message response from the host,
             * we get a deadlock. Solution:
             *
             * 1. Destroy the window FIRST — this invalidates the HWND and
             *    unblocks any pending message operations in the render thread.
             * 2. The render thread sees running==0 and window==NULL, exits.
             * 3. Then we can safely join.
             */

            /* Restore WndProc before destroying */
            if (s_orig_wndproc && gui->window) {
                SDL_SysWMinfo wminfo;
                SDL_VERSION(&wminfo.version);
                if (SDL_GetWindowWMInfo(gui->window, &wminfo)) {
                    SetWindowLongPtrW(wminfo.info.win.window,
                                      GWLP_WNDPROC, (LONG_PTR)s_orig_wndproc);
                }
                s_orig_wndproc = nullptr;
            }

            /* Destroy window to unblock the render thread */
            if (gui->window) {
                SDL_DestroyWindow(gui->window);
                gui->window = nullptr;
            }

            SDL_WaitThread(gui->render_thread, NULL);
            gui->render_thread = nullptr;

            /* GL context was destroyed with the window */
            gui->gl_ctx = nullptr;

            /* Clean up ImGui (context only, no GL calls) */
            if (gui->imgui_ctx) {
                ImGui::SetCurrentContext(gui->imgui_ctx);
                ImGui::DestroyContext(gui->imgui_ctx);
                gui->imgui_ctx = nullptr;
            }

            GLOG_INFO("Plugin GUI detached from host window (Windows path)");
            return;
#else
            SDL_WaitThread(gui->render_thread, NULL);
#endif
            gui->render_thread = nullptr;
        }
    }

    /* Clean up ImGui + GL (non-Windows path, or if render thread wasn't running) */
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
#ifdef SQ_PLUGIN_GUI_X11
    /* Applied by the render thread; globals are written there too */
    gui->x11_req_size.store(((uint64_t)width << 32) | height);
#else
    g_win_width = (int)width;
    g_win_height = (int)height;

    if (gui->window)
        SDL_SetWindowSize(gui->window, (int)width, (int)height);
#endif
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
