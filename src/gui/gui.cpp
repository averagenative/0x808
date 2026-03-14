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

#define STEPS_PER_BEAT 4

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

/* ─── Module state ────────────────────────────────────────────────────────── */

static SDL_Window    *g_window = NULL;
static SDL_GLContext  g_gl_ctx = NULL;
/* globals defined in gui_globals.cpp (linked via libsq_gui) */
static bool g_show_browser = false;
static bool g_show_mixer = false;
static bool g_show_piano_roll = false;
static bool g_show_keyboard  = false;
static char g_project_path[512] = "";
static bool g_project_path_init = false;
static char g_save_status[128] = "";
static uint32_t g_status_timer = 0;
static sq_pattern_t g_clipboard_pattern;
static bool g_clipboard_valid = false;
/* g_pat_scroll defined in gui_globals.cpp, declared extern in gui.h */

static Uint64 g_play_start_ticks = 0;

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
static bool   g_was_playing      = false;

/* ─── Public API ──────────────────────────────────────────────────────────── */

int gui_init(int width, int height, const char *title)
{
    g_win_width  = width;
    g_win_height = height;

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
    theme_apply(THEME_HACKER);

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

    /* 1. Poll SDL events and feed to ImGui */
    SDL_Event evt;
    while (SDL_PollEvent(&evt)) {
        ImGui_ImplSDL2_ProcessEvent(&evt);
        if (evt.type == SDL_QUIT)
            quit = 1;

        /* Keyboard shortcuts — transport keys (space, esc) and Ctrl combos
           always processed. QWERTY piano only when ImGui doesn't want keyboard. */
        bool is_ctrl = (evt.key.keysym.mod & KMOD_CTRL) != 0;
        bool imgui_wants_kb = ImGui::GetIO().WantCaptureKeyboard;

        /* Transport keys always work, even when ImGui has focus */
        if (evt.type == SDL_KEYDOWN && !imgui_wants_kb) {
            /* nothing — fall through to full handler below */
        }
        if (evt.type == SDL_KEYDOWN) {
            if (evt.key.keysym.sym == SDLK_SPACE) {
                engine->transport.playing = !engine->transport.playing;
                engine->transport.current_beat = 0.0;
                engine->transport.sample_position = 0;
                engine->transport.current_step = 0;
                g_visual_step = 0;
                if (engine->transport.playing)
                    g_play_start_ticks = SDL_GetPerformanceCounter();
                continue;
            }
            if (evt.key.keysym.sym == SDLK_ESCAPE) {
                quit = 1;
                continue;
            }
        }

        /* QWERTY piano — always active when keyboard panel is shown */
        if (!is_ctrl && g_show_keyboard &&
            !(evt.key.keysym.mod & KMOD_ALT) &&
            (evt.type == SDL_KEYDOWN || evt.type == SDL_KEYUP))
        {
            int kb_preset_key = -1;
            if (g_selected_track >= 0) {
                int kpi = engine->transport.current_pattern;
                if (kpi >= 0 && (uint32_t)kpi < engine->num_patterns) {
                    sq_pattern_t *kpat = &engine->patterns[kpi];
                    if ((uint32_t)g_selected_track < kpat->num_tracks &&
                        kpat->tracks[g_selected_track].type == TRACK_SYNTH)
                        kb_preset_key = kpat->tracks[g_selected_track].synth_preset;
                }
            }
            if (kb_preset_key < 0) {
                int kpi = engine->transport.current_pattern;
                if (kpi >= 0 && (uint32_t)kpi < engine->num_patterns) {
                    for (uint32_t tt = 0; tt < engine->patterns[kpi].num_tracks; tt++) {
                        if (engine->patterns[kpi].tracks[tt].type == TRACK_SYNTH) {
                            kb_preset_key = engine->patterns[kpi].tracks[tt].synth_preset;
                            break;
                        }
                    }
                }
            }
            if (kb_preset_key < 0) kb_preset_key = 0;
            if (virtual_keyboard_key_event(engine, kb_preset_key,
                                           evt.key.keysym.sym,
                                           evt.type == SDL_KEYDOWN))
                continue;
        }

        /* Pattern select 1-9 — always active */
        if (evt.type == SDL_KEYDOWN && !is_ctrl) {
            if (evt.key.keysym.sym >= SDLK_1 && evt.key.keysym.sym <= SDLK_9) {
                int pat = evt.key.keysym.sym - SDLK_1;
                if ((uint32_t)pat < engine->num_patterns) {
                    engine->transport.current_pattern = pat;
                    snprintf(g_save_status, sizeof(g_save_status), "Pattern %d", pat + 1);
                    g_status_timer = 90;
                }
                continue;
            }
        }

        if (is_ctrl || !imgui_wants_kb) {
            if (evt.type == SDL_KEYDOWN) {
                switch (evt.key.keysym.sym) {
                case SDLK_s:
                    if (evt.key.keysym.mod & KMOD_CTRL) {
                        char save_path[512];
                        if (native_file_dialog(save_path, sizeof(save_path), true, g_project_path)) {
                            if (project_save(engine, save_path) == 0) {
                                snprintf(g_project_path, sizeof(g_project_path), "%s", save_path);
                                snprintf(g_save_status, sizeof(g_save_status), "Saved: %.100s", save_path);
                            } else {
                                snprintf(g_save_status, sizeof(g_save_status), "Save FAILED!");
                            }
                        }
                        g_status_timer = 180;
                    }
                    break;
                case SDLK_o:
                    if (evt.key.keysym.mod & KMOD_CTRL) {
                        char load_path[512];
                        if (native_file_dialog(load_path, sizeof(load_path), false, g_project_path)) {
                            if (project_load(engine, load_path) == 0) {
                                snprintf(g_project_path, sizeof(g_project_path), "%s", load_path);
                                undo_clear();
                                theme_apply(theme_current());
                                snprintf(g_save_status, sizeof(g_save_status), "Loaded: %.100s", load_path);
                            } else {
                                snprintf(g_save_status, sizeof(g_save_status), "Load FAILED!");
                            }
                        }
                        g_status_timer = 180;
                    }
                    break;
                case SDLK_t:
                    if (evt.key.keysym.mod & KMOD_CTRL)
                        theme_toggle();
                    break;
                /* 1-9 pattern select handled above (always active) */
                case SDLK_c:
                    if (evt.key.keysym.mod & KMOD_CTRL) {
                        int pi = engine->transport.current_pattern;
                        if (pi >= 0 && (uint32_t)pi < engine->num_patterns) {
                            g_clipboard_pattern = engine->patterns[pi];
                            g_clipboard_valid = true;
                            snprintf(g_save_status, sizeof(g_save_status), "Copied pattern %d", pi + 1);
                            g_status_timer = 90;
                        }
                    }
                    break;
                case SDLK_v:
                    if ((evt.key.keysym.mod & KMOD_CTRL) && g_clipboard_valid) {
                        int pi = engine->transport.current_pattern;
                        if (pi >= 0 && (uint32_t)pi < engine->num_patterns) {
                            engine->patterns[pi] = g_clipboard_pattern;
                            snprintf(g_save_status, sizeof(g_save_status), "Pasted to pattern %d", pi + 1);
                            g_status_timer = 90;
                        }
                    }
                    break;
                case SDLK_z:
                    if (evt.key.keysym.mod & KMOD_CTRL) {
                        if (evt.key.keysym.mod & KMOD_SHIFT) {
                            if (undo_redo(engine)) {
                                snprintf(g_save_status, sizeof(g_save_status), "Redo");
                                g_status_timer = 90;
                            }
                        } else {
                            if (undo_undo(engine)) {
                                snprintf(g_save_status, sizeof(g_save_status), "Undo");
                                g_status_timer = 90;
                            }
                        }
                    }
                    break;
                case SDLK_EQUALS:
                    if (engine->num_patterns < SQ_MAX_PATTERNS) {
                        int ni = (int)engine->num_patterns;
                        engine->num_patterns++;
                        init_new_pattern(engine, ni);
                        engine->transport.current_pattern = ni;
                        snprintf(g_save_status, sizeof(g_save_status), "New pattern %d", ni + 1);
                        g_status_timer = 90;
                    }
                    break;
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
    if (engine->transport.playing) {
        Uint64 now = SDL_GetPerformanceCounter();
        double elapsed = (double)(now - g_play_start_ticks)
                       / (double)SDL_GetPerformanceFrequency();
        double beats = elapsed * (engine->transport.bpm / 60.0);
        int pat_len = 16;
        int pi = engine->transport.current_pattern;
        if (pi >= 0 && (uint32_t)pi < engine->num_patterns &&
            engine->patterns[pi].num_tracks > 0)
            pat_len = (int)engine->patterns[pi].tracks[0].length;
        g_visual_step = ((int)floor(beats * STEPS_PER_BEAT)) % pat_len;
    } else if (g_was_playing && !engine->transport.playing) {
        g_visual_step = 0;
    }
    g_was_playing = engine->transport.playing;

    /* ── Layout constants ─────────────────────────────────────────────────── */
    float toolbar_h = 80.0f;

    /* ── Toolbar (shared implementation) ──────────────────────────────────── */
    {
        Uint64 play_ticks = g_play_start_ticks;
        sq_toolbar_params_t tp = {};
        tp.engine = engine;
        tp.toolbar_h = toolbar_h;
        tp.is_plugin = false;
        tp.quit = &quit;
        tp.window = g_window;
        tp.show_browser = &g_show_browser;
        tp.show_mixer = &g_show_mixer;
        tp.show_piano_roll = &g_show_piano_roll;
        tp.show_keyboard = &g_show_keyboard;
        tp.save_status = g_save_status;
        tp.save_status_size = (int)sizeof(g_save_status);
        tp.status_timer = &g_status_timer;
        tp.play_start_ticks = &play_ticks;
        toolbar_draw(&tp);
        g_play_start_ticks = play_ticks;
    }

    /* ── Determine if synth editor should be shown ────────────────────────── */
    bool show_synth_editor = false;
    int synth_preset_idx = -1;
    int *synth_preset_ptr = NULL;
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

    /* ── Arrangement panel ────────────────────────────────────────────────── */
    float arrange_h = 0.0f;
    if (engine->transport.mode != MODE_PATTERN) {
        arrange_h = 160.0f;
        arrangement_draw(engine, 0.0f, toolbar_h, (float)g_win_width, arrange_h);
    }

    /* ── Main layout ──────────────────────────────────────────────────────── */
    float grid_y = toolbar_h + arrange_h;
    float kb_reserve = g_show_keyboard ? 120.0f : 0.0f;
    float total_h = (float)g_win_height - grid_y - kb_reserve;
    float total_w = (float)g_win_width;

    float browser_w = 0.0f;
    float main_w = total_w;
    if (g_show_browser) {
        browser_w = 350.0f;
        if (browser_w > total_w * 0.4f)
            browser_w = total_w * 0.4f;
        main_w = total_w - browser_w;
    }

    /* ── Full-height piano roll mode ──────────────────────────────────────── */
    if (g_show_piano_roll && show_synth_editor && synth_preset_idx >= 0) {
        if (g_show_mixer) {
            float pr_w = main_w * 0.60f;
            float se_w = main_w * 0.20f;
            float mx_w = main_w - pr_w - se_w;
            piano_roll_draw(engine, g_selected_track, 0.0f, grid_y, pr_w, total_h);
            synth_editor_draw(engine, synth_preset_ptr, pr_w, grid_y, se_w, total_h);
            mixer_view_draw(engine, pr_w + se_w, grid_y, mx_w, total_h);
        } else {
            float pr_w = main_w * 0.70f;
            float se_w = main_w - pr_w;
            piano_roll_draw(engine, g_selected_track, 0.0f, grid_y, pr_w, total_h);
            synth_editor_draw(engine, synth_preset_ptr, pr_w, grid_y, se_w, total_h);
        }
    } else {
        /* Normal layout */
        float grid_h, bottom_h;
        bool has_bottom = show_synth_editor || g_show_mixer;
        if (has_bottom) {
            bottom_h = 360.0f;
            if (g_show_keyboard && bottom_h > total_h - 150.0f)
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
            if (show_synth_editor && synth_preset_idx >= 0 && g_show_mixer) {
                float pr_w = main_w * 0.40f;
                float se_w = main_w * 0.30f;
                float mx_w = main_w - pr_w - se_w;
                piano_roll_draw(engine, g_selected_track, 0.0f, bottom_y, pr_w, bottom_h);
                synth_editor_draw(engine, synth_preset_ptr, pr_w, bottom_y, se_w, bottom_h);
                mixer_view_draw(engine, pr_w + se_w, bottom_y, mx_w, bottom_h);
            } else if (show_synth_editor && synth_preset_idx >= 0) {
                float pr_w = main_w * 0.55f;
                float se_w = main_w - pr_w;
                piano_roll_draw(engine, g_selected_track, 0.0f, bottom_y, pr_w, bottom_h);
                synth_editor_draw(engine, synth_preset_ptr, pr_w, bottom_y, se_w, bottom_h);
            } else if (g_show_mixer) {
                mixer_view_draw(engine, 0.0f, bottom_y, main_w, bottom_h);
            }
        }
    }

    if (g_show_browser) {
        sample_browser_draw(engine, main_w, grid_y, browser_w, total_h);
        if (sample_browser_close_requested())
            g_show_browser = false;
    }

    /* Virtual keyboard */
    if (g_show_keyboard) {
        float kb_h = 120.0f;
        int kb_preset = synth_preset_idx;
        if (kb_preset < 0) {
            int pi2 = engine->transport.current_pattern;
            if (pi2 >= 0 && (uint32_t)pi2 < engine->num_patterns) {
                for (uint32_t tt = 0; tt < engine->patterns[pi2].num_tracks; tt++) {
                    if (engine->patterns[pi2].tracks[tt].type == TRACK_SYNTH) {
                        kb_preset = engine->patterns[pi2].tracks[tt].synth_preset;
                        break;
                    }
                }
            }
        }
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
