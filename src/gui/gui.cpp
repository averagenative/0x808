/*
 * gui.cpp — SDL2 + OpenGL + Dear ImGui GUI initialization and main frame loop.
 *
 * Immediate-mode GUI: every frame we call ImGui functions that both draw
 * widgets AND return interaction results. No persistent widget state.
 *
 * Frame loop:
 * 1. Poll SDL events and feed to ImGui
 * 2. ImGui::NewFrame()
 * 3. Draw all panels (toolbar, drum grid, synth editor, etc.)
 * 4. ImGui::Render() + OpenGL render
 * 5. Swap buffers
 */

#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_opengl3.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>
#include <SDL2/SDL_syswm.h>

extern "C" {
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
#define LOG_TAG "gui"
#include "core/log.h"
}

#include <cstdio>
#include <cstring>
#include <cmath>

#ifdef _WIN32
#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>
#endif

/* ─── Custom borderless window resize/maximize via Win32 ─────────────────── */

#define RESIZE_BORDER 8

#ifdef _WIN32
/* Subclass the Win32 HWND to handle WM_NCHITTEST + WM_NCCALCSIZE for
 * borderless window resize/maximize. SDL2's HitTest is broken for resize
 * on Windows (SDL issue #8586). The fix is to:
 *   1. Add WS_THICKFRAME so Windows provides native resize behavior
 *   2. Handle WM_NCCALCSIZE returning 0 to hide the frame (keep borderless look)
 *   3. Handle WM_NCHITTEST via DefWindowProc for edge detection
 *   4. Handle WM_GETMINMAXINFO to constrain maximize to work area
 * Ref: rossy/borderless-window, alek-tron.com/borderless */
static WNDPROC g_orig_wndproc = NULL;

static LRESULT CALLBACK borderless_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    /* WM_NCCALCSIZE: Remove non-client area to keep borderless look.
     * Without this, WS_THICKFRAME causes a visible border/white line. */
    if (msg == WM_NCCALCSIZE && wp == TRUE) {
        if (IsZoomed(hwnd)) {
            /* When maximized, constrain to work area so we don't cover the taskbar */
            NCCALCSIZE_PARAMS *params = (NCCALCSIZE_PARAMS *)lp;
            HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
            MONITORINFO mi;
            mi.cbSize = sizeof(mi);
            if (GetMonitorInfo(mon, &mi))
                params->rgrc[0] = mi.rcWork;
        }
        return 0; /* 0 = no non-client area = borderless */
    }

    /* WM_NCHITTEST: Let DefWindowProc detect the WS_THICKFRAME edges first,
     * then overlay our own border detection for the resize zones. */
    if (msg == WM_NCHITTEST) {
        LRESULT hit = DefWindowProcW(hwnd, msg, wp, lp);
        if (hit == HTCLIENT) {
            RECT rc;
            GetClientRect(hwnd, &rc);
            POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            ScreenToClient(hwnd, &pt);

            int w = rc.right, h = rc.bottom;
            bool top    = pt.y < RESIZE_BORDER;
            bool bottom = pt.y >= h - RESIZE_BORDER;
            bool left   = pt.x < RESIZE_BORDER;
            bool right  = pt.x >= w - RESIZE_BORDER;

            if (top && left)     return HTTOPLEFT;
            if (top && right)    return HTTOPRIGHT;
            if (bottom && left)  return HTBOTTOMLEFT;
            if (bottom && right) return HTBOTTOMRIGHT;
            if (top)             return HTTOP;
            if (bottom)          return HTBOTTOM;
            if (left)            return HTLEFT;
            if (right)           return HTRIGHT;
        } else {
            /* DefWindowProc already detected a frame edge — use it */
            return hit;
        }
    }

    /* WM_GETMINMAXINFO: Constrain maximize to monitor work area (excludes taskbar) */
    if (msg == WM_GETMINMAXINFO) {
        MINMAXINFO *mmi = (MINMAXINFO *)lp;
        HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi;
        mi.cbSize = sizeof(mi);
        if (GetMonitorInfo(mon, &mi)) {
            mmi->ptMaxPosition.x = mi.rcWork.left - mi.rcMonitor.left;
            mmi->ptMaxPosition.y = mi.rcWork.top  - mi.rcMonitor.top;
            mmi->ptMaxSize.x     = mi.rcWork.right  - mi.rcWork.left;
            mmi->ptMaxSize.y     = mi.rcWork.bottom - mi.rcWork.top;
        }
        return 0; /* must return 0, not fall through */
    }

    return CallWindowProcW(g_orig_wndproc, hwnd, msg, wp, lp);
}

static void install_borderless_wndproc(SDL_Window *window)
{
    SDL_SysWMinfo wmInfo;
    SDL_VERSION(&wmInfo.version);
    if (SDL_GetWindowWMInfo(window, &wmInfo)) {
        HWND hwnd = wmInfo.info.win.window;
        g_orig_wndproc = (WNDPROC)SetWindowLongPtrW(hwnd, GWLP_WNDPROC,
                                                      (LONG_PTR)borderless_wndproc);
        /* Add WS_THICKFRAME for native resize, WS_CAPTION for window snapping,
         * keep borderless look via WM_NCCALCSIZE returning 0 */
        LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
        style |= WS_THICKFRAME | WS_CAPTION | WS_SYSMENU |
                 WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
        SetWindowLongPtrW(hwnd, GWL_STYLE, style);
        /* Force Windows to recalculate the frame */
        SetWindowPos(hwnd, NULL, 0, 0, 0, 0,
                     SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);
    }
}
#endif

/* ─── Native file dialogs ─────────────────────────────────────────────────── */

/* Returns true if user picked a file, writes path into buf (bufsize).
   is_save=true for Save dialog, false for Open dialog. */
static bool native_file_dialog(char *buf, size_t bufsize, bool is_save,
                                const char *default_path)
{
#ifdef _WIN32
    OPENFILENAMEA ofn;
    char file[512] = "";
    if (default_path && default_path[0])
        snprintf(file, sizeof(file), "%s", default_path);

    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL;
    ofn.lpstrFilter = "0x808 Projects (*.sqproj)\0*.sqproj\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = sizeof(file);
    ofn.lpstrDefExt = "sqproj";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;

    BOOL ok;
    if (is_save) {
        ofn.lpstrTitle = "Save Project";
        ok = GetSaveFileNameA(&ofn);
    } else {
        ofn.lpstrTitle = "Open Project";
        ofn.Flags |= OFN_FILEMUSTEXIST;
        ok = GetOpenFileNameA(&ofn);
    }
    if (ok) {
        snprintf(buf, bufsize, "%s", file);
        return true;
    }
    return false;
#else
    /* On Linux, just use the default path (no native dialog without GTK) */
    if (default_path && default_path[0])
        snprintf(buf, bufsize, "%s", default_path);
    return true;
#endif
}

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

/* ─── Module state ────────────────────────────────────────────────────────── */

static SDL_Window    *g_window = NULL;
static SDL_GLContext  g_gl_ctx = NULL;
static sq_app_t       g_app;
static char g_project_path[512] = "";
static bool g_project_path_init = false;

/* ─── Public API ──────────────────────────────────────────────────────────── */

int gui_init(int width, int height, const char *title)
{
    g_win_width  = width;
    g_win_height = height;
    sq_app_init(&g_app);

    LOG_INFO("gui_init: starting (w=%d h=%d)", width, height);

    LOG_INFO("gui_init: SDL_Init(VIDEO|AUDIO)...");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
        LOG_ERROR("SDL_Init failed: %s", SDL_GetError());
        return -1;
    }
    LOG_INFO("gui_init: SDL_Init OK");

    /* Request OpenGL 3.3 Core Profile */
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    LOG_INFO("gui_init: SDL_CreateWindow (borderless)...");
    g_window = SDL_CreateWindow(
        title,
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        width, height,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_BORDERLESS |
        SDL_WINDOW_ALLOW_HIGHDPI
    );
    if (!g_window) {
        LOG_ERROR("SDL_CreateWindow failed: %s", SDL_GetError());
        SDL_Quit();
        return -1;
    }
    SDL_SetWindowMinimumSize(g_window, 800, 500);
#ifdef _WIN32
    install_borderless_wndproc(g_window);
#endif
    LOG_INFO("gui_init: SDL_CreateWindow OK (borderless, min size 800x500)");

    LOG_INFO("gui_init: SDL_GL_CreateContext...");
    g_gl_ctx = SDL_GL_CreateContext(g_window);
    if (!g_gl_ctx) {
        LOG_ERROR("SDL_GL_CreateContext failed: %s", SDL_GetError());
        SDL_DestroyWindow(g_window);
        SDL_Quit();
        return -1;
    }
    LOG_INFO("gui_init: SDL_GL_CreateContext OK");

    SDL_GL_SetSwapInterval(1);

    /* Initialize Dear ImGui */
    LOG_INFO("gui_init: ImGui init...");
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    /* Disable imgui.ini file — we manage layout ourselves */
    io.IniFilename = NULL;

    ImGui_ImplSDL2_InitForOpenGL(g_window, g_gl_ctx);
    ImGui_ImplOpenGL3_Init("#version 330 core");

    /* Load embedded DejaVu Sans Mono Bold — open source console font */
    {
        #include "font_dejavu_mono_bold.h"
        ImFontConfig fc;
        fc.FontDataOwnedByAtlas = false; /* data is static, don't free */
        io.Fonts->AddFontFromMemoryTTF(
            DejaVuSansMono_Bold_ttf, DejaVuSansMono_Bold_ttf_len,
            15.0f, &fc);
        LOG_INFO("Loaded embedded font: DejaVu Sans Mono Bold (15px)");
    }

    LOG_INFO("gui_init: ImGui init OK");

    /* Apply hacker theme (90s green-on-black aesthetic) */
    theme_apply(THEME_LIGHT);

    LOG_INFO("GUI initialized: %dx%d, OpenGL 3.3, Dear ImGui", width, height);
    return 0;
}

int gui_frame(sq_engine_t *engine)
{
    int quit = 0;

    /* Set default project path from engine base_dir on first frame */
    if (!g_project_path_init) {
        g_project_path_init = true;
        if (engine->base_dir[0])
            snprintf(g_project_path, sizeof(g_project_path), "%sproject.sqproj", engine->base_dir);
        else
            snprintf(g_project_path, sizeof(g_project_path), "project.sqproj");
    }

    /* Auto-select first synth track on startup */
    if (g_app.selected_track < 0) {
        int pi = engine->transport.current_pattern;
        if (pi >= 0 && (uint32_t)pi < engine->num_patterns) {
            sq_pattern_t *pat = &engine->patterns[pi];
            for (uint32_t t = 0; t < pat->num_tracks; t++) {
                if (pat->tracks[t].type == TRACK_SYNTH) {
                    g_app.selected_track = (int)t;
                    break;
                }
            }
        }
    }

    /* Sync globals from app state */
    g_visual_step = g_app.visual_step;
    g_selected_track = g_app.selected_track;

    /* 1. Poll SDL events and feed to ImGui */
    SDL_Event evt;
    while (SDL_PollEvent(&evt)) {
        ImGui_ImplSDL2_ProcessEvent(&evt);
        if (evt.type == SDL_QUIT)
            quit = 1;

        bool is_ctrl = (evt.key.keysym.mod & KMOD_CTRL) != 0;

        /* QWERTY piano — always active when keyboard panel is shown */
        if (!is_ctrl && g_app.panels[SQ_PANEL_KEYBOARD] &&
            !(evt.key.keysym.mod & KMOD_ALT) &&
            (evt.type == SDL_KEYDOWN || evt.type == SDL_KEYUP))
        {
            int kb_preset = sq_app_get_keyboard_preset(&g_app, engine);
            if (kb_preset < 0) kb_preset = 0;
            if (virtual_keyboard_key_event(engine, kb_preset,
                                           evt.key.keysym.sym,
                                           evt.type == SDL_KEYDOWN))
                continue;
        }

        /* Key dispatch via sq_app */
        if (evt.type == SDL_KEYDOWN) {
            int sq_key = sdl_to_sq_key(evt.key.keysym.sym);
            int sq_mod = sdl_to_sq_mod(evt.key.keysym.mod);

            /* Transport + pattern keys always active; Ctrl combos always active;
             * other keys only when ImGui doesn't want keyboard */
            bool imgui_wants_kb = ImGui::GetIO().WantCaptureKeyboard;
            bool is_transport = (sq_key == SQ_KEY_SPACE || sq_key == SQ_KEY_ESCAPE);
            bool is_pattern_key = (!is_ctrl && sq_key >= SQ_KEY_1 && sq_key <= SQ_KEY_9);

            if (is_transport || is_pattern_key || is_ctrl || !imgui_wants_kb) {
                sq_app_action_t action = sq_app_handle_key(&g_app, engine,
                                                            sq_key, sq_mod, true);

                /* If space started playback, record the start ticks */
                if (sq_key == SQ_KEY_SPACE && engine->transport.playing)
                    g_app.play_start_ticks = SDL_GetPerformanceCounter();

                switch (action) {
                case SQ_ACTION_QUIT:
                    quit = 1;
                    break;
                case SQ_ACTION_SAVE: {
                    char save_path[512];
                    if (native_file_dialog(save_path, sizeof(save_path), true, g_project_path)) {
                        if (project_save(engine, save_path) == 0) {
                            snprintf(g_project_path, sizeof(g_project_path), "%s", save_path);
                            sq_app_set_status(&g_app, NULL, 180);
                            snprintf(g_app.status_msg, SQ_STATUS_LEN, "Saved: %.100s", save_path);
                        } else {
                            sq_app_set_status(&g_app, "Save FAILED!", 180);
                        }
                    }
                    break;
                }
                case SQ_ACTION_LOAD: {
                    char load_path[512];
                    if (native_file_dialog(load_path, sizeof(load_path), false, g_project_path)) {
                        if (project_load(engine, load_path) == 0) {
                            snprintf(g_project_path, sizeof(g_project_path), "%s", load_path);
                            undo_clear();
                            theme_apply(theme_current());
                            sq_app_set_status(&g_app, NULL, 180);
                            snprintf(g_app.status_msg, SQ_STATUS_LEN, "Loaded: %.100s", load_path);
                        } else {
                            sq_app_set_status(&g_app, "Load FAILED!", 180);
                        }
                    }
                    break;
                }
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

    /* Get current window size */
    SDL_GetWindowSize(g_window, &g_win_width, &g_win_height);

    /* Start ImGui frame */
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    /* ── Compute visual playhead ──────────────────────────────────────────── */
    sq_app_update_playhead(&g_app, engine,
                           SDL_GetPerformanceCounter(),
                           SDL_GetPerformanceFrequency());
    g_visual_step = g_app.visual_step;

    /* ── Layout constants ─────────────────────────────────────────────────── */
    float toolbar_h = 80.0f;

    /* ── Toolbar (shared implementation) ──────────────────────────────────── */
    {
        Uint64 play_ticks = g_app.play_start_ticks;
        sq_toolbar_params_t tp = {};
        tp.engine = engine;
        tp.toolbar_h = toolbar_h;
        tp.is_plugin = false;
        tp.quit = &quit;
        tp.window = g_window;
        tp.show_browser    = &g_app.panels[SQ_PANEL_BROWSER];
        tp.show_mixer      = &g_app.panels[SQ_PANEL_MIXER];
        tp.show_piano_roll = &g_app.panels[SQ_PANEL_PIANO_ROLL];
        tp.show_keyboard   = &g_app.panels[SQ_PANEL_KEYBOARD];
        tp.save_status = g_app.status_msg;
        tp.save_status_size = SQ_STATUS_LEN;
        tp.status_timer = &g_app.status_timer;
        tp.play_start_ticks = &play_ticks;
        toolbar_draw(&tp);
        g_app.play_start_ticks = play_ticks;
    }

    /* ── Determine if synth editor should be shown ────────────────────────── */
    bool show_synth_editor = false;
    int synth_preset_idx = -1;
    int *synth_preset_ptr = NULL;
    if (g_app.selected_track >= 0) {
        int pi = engine->transport.current_pattern;
        if (pi >= 0 && (uint32_t)pi < engine->num_patterns) {
            sq_pattern_t *pat = &engine->patterns[pi];
            if ((uint32_t)g_app.selected_track < pat->num_tracks &&
                pat->tracks[g_app.selected_track].type == TRACK_SYNTH) {
                show_synth_editor = true;
                synth_preset_ptr = &pat->tracks[g_app.selected_track].synth_preset;
                synth_preset_idx = *synth_preset_ptr;
            }
        }
    }

    /* ── Arrangement panel ────────────────────────────────────────────────── */
    float arrange_h = 0.0f;
    if (engine->transport.mode != MODE_PATTERN) {
        arrange_h = 160.0f;
        arrangement_draw(engine, 0.0f, toolbar_h, (float)g_win_width, arrange_h);
    }

    /* ── Main layout ──────────────────────────────────────────────────────── */
    float grid_y = toolbar_h + arrange_h;
    float kb_reserve = g_app.panels[SQ_PANEL_KEYBOARD] ? 120.0f : 0.0f;
    float total_h = (float)g_win_height - grid_y - kb_reserve;
    float total_w = (float)g_win_width;

    float browser_w = 0.0f;
    float main_w = total_w;
    if (g_app.panels[SQ_PANEL_BROWSER]) {
        browser_w = 350.0f;
        if (browser_w > total_w * 0.4f)
            browser_w = total_w * 0.4f;
        main_w = total_w - browser_w;
    }

    /* ── Full-height piano roll mode ──────────────────────────────────────── */
    if (g_app.panels[SQ_PANEL_PIANO_ROLL] && show_synth_editor && synth_preset_idx >= 0) {
        if (g_app.panels[SQ_PANEL_MIXER]) {
            float pr_w = main_w * 0.60f;
            float se_w = main_w * 0.20f;
            float mx_w = main_w - pr_w - se_w;
            piano_roll_draw(engine, g_app.selected_track, 0.0f, grid_y, pr_w, total_h);
            synth_editor_draw(engine, synth_preset_ptr, pr_w, grid_y, se_w, total_h);
            mixer_view_draw(engine, pr_w + se_w, grid_y, mx_w, total_h);
        } else {
            float pr_w = main_w * 0.70f;
            float se_w = main_w - pr_w;
            piano_roll_draw(engine, g_app.selected_track, 0.0f, grid_y, pr_w, total_h);
            synth_editor_draw(engine, synth_preset_ptr, pr_w, grid_y, se_w, total_h);
        }
    } else {
        /* Normal layout */
        float grid_h, bottom_h;
        bool has_bottom = show_synth_editor || g_app.panels[SQ_PANEL_MIXER];
        if (has_bottom) {
            bottom_h = 280.0f;
            if (g_app.panels[SQ_PANEL_KEYBOARD] && bottom_h > total_h - 150.0f)
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
            if (show_synth_editor && synth_preset_idx >= 0 && g_app.panels[SQ_PANEL_MIXER]) {
                float pr_w = main_w * 0.40f;
                float se_w = main_w * 0.30f;
                float mx_w = main_w - pr_w - se_w;
                piano_roll_draw(engine, g_app.selected_track, 0.0f, bottom_y, pr_w, bottom_h);
                synth_editor_draw(engine, synth_preset_ptr, pr_w, bottom_y, se_w, bottom_h);
                mixer_view_draw(engine, pr_w + se_w, bottom_y, mx_w, bottom_h);
            } else if (show_synth_editor && synth_preset_idx >= 0) {
                float pr_w = main_w * 0.55f;
                float se_w = main_w - pr_w;
                piano_roll_draw(engine, g_app.selected_track, 0.0f, bottom_y, pr_w, bottom_h);
                synth_editor_draw(engine, synth_preset_ptr, pr_w, bottom_y, se_w, bottom_h);
            } else if (g_app.panels[SQ_PANEL_MIXER]) {
                mixer_view_draw(engine, 0.0f, bottom_y, main_w, bottom_h);
            }
        }
    }

    if (g_app.panels[SQ_PANEL_BROWSER]) {
        sample_browser_draw(engine, main_w, grid_y, browser_w, total_h);
        if (sample_browser_close_requested())
            g_app.panels[SQ_PANEL_BROWSER] = false;
    }

    /* Virtual keyboard */
    if (g_app.panels[SQ_PANEL_KEYBOARD]) {
        float kb_h = 120.0f;
        int kb_preset = sq_app_get_keyboard_preset(&g_app, engine);
        if (kb_preset < 0) kb_preset = 0;
        virtual_keyboard_draw(engine, kb_preset,
                              0.0f, (float)g_win_height - kb_h, main_w, kb_h);
    }

    /* Floating dialogs */
    if (export_dialog_visible())
        export_dialog_draw(engine);
    pattern_presets_draw(engine);

    /* ── Window border + resize grip (borderless mode) ──────────────────── */
    {
        ImDrawList *fg = ImGui::GetForegroundDrawList();
        ImU32 border_col = IM_COL32(60, 60, 65, 200);
        fg->AddRect(ImVec2(0, 0),
                    ImVec2((float)g_win_width, (float)g_win_height),
                    border_col, 0.0f, 0, 2.0f);

        /* Resize grip triangle — bottom-right corner */
        float gs = 14.0f;
        float bx = (float)g_win_width, by = (float)g_win_height;
        ImU32 grip_col = IM_COL32(100, 100, 110, 180);
        fg->AddTriangleFilled(
            ImVec2(bx - gs, by), ImVec2(bx, by - gs), ImVec2(bx, by), grip_col);
    }

    /* ── Render ───────────────────────────────────────────────────────────── */
    ImGui::Render();
    float bg[4];
    theme_get_clear_color(bg);
    glViewport(0, 0, g_win_width, g_win_height);
    glClearColor(bg[0], bg[1], bg[2], bg[3]);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    SDL_GL_SwapWindow(g_window);

    /* Sync app state back from globals (components may have changed them) */
    g_app.selected_track = g_selected_track;

    return quit;
}

void gui_shutdown(void)
{
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    if (g_gl_ctx) SDL_GL_DeleteContext(g_gl_ctx);
    if (g_window) SDL_DestroyWindow(g_window);
    SDL_Quit();
    LOG_INFO("GUI shut down");
}
