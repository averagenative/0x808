/*
 * gui.c — SDL2 + OpenGL + Nuklear GUI initialization and main frame loop.
 *
 * How immediate-mode GUI works:
 * Unlike "retained mode" GUIs (Qt, GTK) where you create widget objects
 * that persist, Nuklear redraws EVERYTHING every frame. You call functions
 * like nk_button_label() and they both draw the button AND return whether
 * it was clicked. No widget state to manage — just code that runs each frame.
 *
 * Frame loop:
 * 1. Poll SDL events (mouse, keyboard) and feed them to Nuklear
 * 2. Call our drawing code (drum grid, knobs, etc.)
 * 3. Tell Nuklear to render to OpenGL
 * 4. Swap buffers
 */

/* Load GL3 function pointers (runtime loading on Windows, no-op on Linux/macOS) */
#define GL3_LOADER_IMPLEMENTATION
#include "gl3_loader.h"

/* Nuklear configuration — must come before including nuklear.h */
#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#define NK_IMPLEMENTATION
#include "nuklear.h"

/* SDL2 + OpenGL3 backend for Nuklear */
#define NK_SDL_GL3_IMPLEMENTATION
#include "nuklear_sdl_gl3.h"

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

#define LOG_TAG "gui"
#include "core/log.h"

#include <stdio.h>
#include <string.h>
#include <math.h>

/* Max vertex/element buffer sizes for Nuklear rendering */
#define MAX_VERTEX_BUFFER  (512 * 1024)
#define MAX_ELEMENT_BUFFER (128 * 1024)

#define STEPS_PER_BEAT 4

/* ─── Module state ────────────────────────────────────────────────────────── */

static SDL_Window    *g_window = NULL;
static SDL_GLContext  g_gl_ctx = NULL;
static struct nk_context *g_nk_ctx = NULL;
int g_win_width  = 1280;
int g_win_height = 720;
int g_visual_step = 0;
int g_selected_track = -1;
static bool g_show_browser = false;
static bool g_show_mixer = false;
static bool g_show_piano_roll = false;  /* full-height piano roll mode */
static bool g_show_keyboard  = false;  /* virtual piano keyboard */
static char g_project_path[512] = "project.sqproj";
static char g_save_status[128] = "";
static uint32_t g_status_timer = 0;
static sq_pattern_t g_clipboard_pattern;
static bool g_clipboard_valid = false;

/* Wall-clock playhead: tracks play start time so the visual step is
 * computed from real elapsed time, not the audio thread's position.
 * This avoids audio device latency affecting the visual display. */
static Uint64 g_play_start_ticks = 0;
static bool   g_was_playing      = false;

/* ─── Public API ──────────────────────────────────────────────────────────── */

int gui_init(int width, int height, const char *title)
{
    g_win_width  = width;
    g_win_height = height;

    LOG_INFO("gui_init: starting (w=%d h=%d)", width, height);

    /* Initialize SDL2 */
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

    /* Create window */
    LOG_INFO("gui_init: SDL_CreateWindow...");
    g_window = SDL_CreateWindow(
        title,
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        width, height,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI
    );
    if (!g_window) {
        LOG_ERROR("SDL_CreateWindow failed: %s", SDL_GetError());
        SDL_Quit();
        return -1;
    }
    SDL_SetWindowMinimumSize(g_window, 800, 500);
    LOG_INFO("gui_init: SDL_CreateWindow OK (min size 800x500)");

    /* Create OpenGL context */
    LOG_INFO("gui_init: SDL_GL_CreateContext...");
    g_gl_ctx = SDL_GL_CreateContext(g_window);
    if (!g_gl_ctx) {
        LOG_ERROR("SDL_GL_CreateContext failed: %s", SDL_GetError());
        SDL_DestroyWindow(g_window);
        SDL_Quit();
        return -1;
    }
    LOG_INFO("gui_init: SDL_GL_CreateContext OK");

    /* Load GL3 function pointers (needed on Windows where opengl32.dll is GL 1.1) */
    LOG_INFO("gui_init: gl3_loader_init...");
    gl3_loader_init();
    LOG_INFO("gui_init: gl3_loader_init OK");

    /* Enable VSync */
    SDL_GL_SetSwapInterval(1);

    /* Initialize Nuklear with SDL+GL3 backend */
    LOG_INFO("gui_init: nk_sdl_init...");
    g_nk_ctx = nk_sdl_init(g_window);
    LOG_INFO("gui_init: nk_sdl_init OK (ctx=%p)", (void*)g_nk_ctx);

    /* Load default font */
    {
        struct nk_font_atlas *atlas;
        nk_sdl_font_stash_begin(&atlas);
        /* Using default font — no custom font loading needed */
        nk_sdl_font_stash_end();
    }

    /* Set Nuklear style — apply default dark theme */
    theme_apply(g_nk_ctx, THEME_DARK);

    LOG_INFO("GUI initialized: %dx%d, OpenGL 3.3", width, height);
    return 0;
}

int gui_frame(sq_engine_t *engine)
{
    int quit = 0;

    /* 1. Poll SDL events and feed to Nuklear */
    SDL_Event evt;
    nk_input_begin(g_nk_ctx);
    while (SDL_PollEvent(&evt)) {
        if (evt.type == SDL_QUIT) {
            quit = 1;
        }
        /* Keyboard shortcuts */
        /* QWERTY piano: intercept keydown/keyup for musical typing */
        if (g_show_keyboard && !(evt.key.keysym.mod & (KMOD_CTRL | KMOD_ALT)) &&
            (evt.type == SDL_KEYDOWN || evt.type == SDL_KEYUP))
        {
            /* Find synth preset for keyboard */
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
                /* Fallback: first synth track */
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
            {
                continue; /* key consumed by piano — skip other handlers */
            }
        }

        if (evt.type == SDL_KEYDOWN) {
            switch (evt.key.keysym.sym) {
            case SDLK_SPACE:
                engine->transport.playing = !engine->transport.playing;
                /* Always reset to beginning */
                engine->transport.current_beat = 0.0;
                engine->transport.sample_position = 0;
                engine->transport.current_step = 0;
                g_visual_step = 0;
                if (engine->transport.playing) {
                    g_play_start_ticks = SDL_GetPerformanceCounter();
                }
                LOG_DEBUG("SPACE: %s",
                         engine->transport.playing ? "PLAY" : "STOP");
                break;
            case SDLK_ESCAPE:
                quit = 1;
                break;
            case SDLK_s:
                if (evt.key.keysym.mod & KMOD_CTRL) {
                    if (project_save(engine, g_project_path) == 0) {
                        snprintf(g_save_status, sizeof(g_save_status),
                                 "Saved: %.100s", g_project_path);
                    } else {
                        snprintf(g_save_status, sizeof(g_save_status),
                                 "Save FAILED!");
                    }
                    g_status_timer = 180; /* ~3 seconds at 60fps */
                }
                break;
            case SDLK_o:
                if (evt.key.keysym.mod & KMOD_CTRL) {
                    if (project_load(engine, g_project_path) == 0) {
                        undo_clear();
                        theme_apply(g_nk_ctx, theme_current());
                        snprintf(g_save_status, sizeof(g_save_status),
                                 "Loaded: %.100s", g_project_path);
                    } else {
                        snprintf(g_save_status, sizeof(g_save_status),
                                 "Load FAILED!");
                    }
                    g_status_timer = 180;
                }
                break;
            case SDLK_t:
                if (evt.key.keysym.mod & KMOD_CTRL) {
                    theme_toggle();
                    theme_apply(g_nk_ctx, theme_current());
                }
                break;
            /* 1-9: select pattern */
            case SDLK_1: case SDLK_2: case SDLK_3:
            case SDLK_4: case SDLK_5: case SDLK_6:
            case SDLK_7: case SDLK_8: case SDLK_9:
                if (!(evt.key.keysym.mod & KMOD_CTRL)) {
                    int pat = evt.key.keysym.sym - SDLK_1;
                    if ((uint32_t)pat < engine->num_patterns) {
                        engine->transport.current_pattern = pat;
                        snprintf(g_save_status, sizeof(g_save_status),
                                 "Pattern %d", pat + 1);
                        g_status_timer = 90;
                        LOG_DEBUG("Pattern select: %d", pat + 1);
                    }
                }
                break;
            /* Ctrl+C: copy current pattern */
            case SDLK_c:
                if (evt.key.keysym.mod & KMOD_CTRL) {
                    int pi = engine->transport.current_pattern;
                    if (pi >= 0 && (uint32_t)pi < engine->num_patterns) {
                        g_clipboard_pattern = engine->patterns[pi];
                        g_clipboard_valid = true;
                        snprintf(g_save_status, sizeof(g_save_status),
                                 "Copied pattern %d", pi + 1);
                        g_status_timer = 90;
                    }
                }
                break;
            /* Ctrl+V: paste pattern */
            case SDLK_v:
                if ((evt.key.keysym.mod & KMOD_CTRL) && g_clipboard_valid) {
                    int pi = engine->transport.current_pattern;
                    if (pi >= 0 && (uint32_t)pi < engine->num_patterns) {
                        engine->patterns[pi] = g_clipboard_pattern;
                        snprintf(g_save_status, sizeof(g_save_status),
                                 "Pasted to pattern %d", pi + 1);
                        g_status_timer = 90;
                    }
                }
                break;
            /* Ctrl+Z: undo, Ctrl+Shift+Z: redo */
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
            /* +/= : add new pattern */
            case SDLK_EQUALS:
                if (engine->num_patterns < SQ_MAX_PATTERNS) {
                    int ni = (int)engine->num_patterns;
                    engine->num_patterns++;
                    sq_pattern_t *np = &engine->patterns[ni];
                    memset(np, 0, sizeof(*np));
                    snprintf(np->name, SQ_PATTERN_NAME_LEN,
                             "Pattern %d", ni + 1);
                    np->num_tracks = 4;
                    for (uint32_t t = 0; t < np->num_tracks; t++) {
                        np->tracks[t].length = 16;
                        np->tracks[t].volume = 0.8f;
                        np->tracks[t].sample_index = -1;
                        np->tracks[t].synth_preset = -1;
                    }
                    engine->transport.current_pattern = ni;
                    snprintf(g_save_status, sizeof(g_save_status),
                             "New pattern %d", ni + 1);
                    g_status_timer = 90;
                }
                break;
            default:
                break;
            }
        }
        nk_sdl_handle_event(&evt);
    }
    nk_input_end(g_nk_ctx);

    /* Get current window size for layout */
    {
        int prev_w = g_win_width, prev_h = g_win_height;
        SDL_GetWindowSize(g_window, &g_win_width, &g_win_height);
        /* On resize, clear all Nuklear windows so they get recreated
         * with correct bounds (avoids stale clip rects). */
        if (g_win_width != prev_w || g_win_height != prev_h)
            nk_clear(g_nk_ctx);
    }

    /* 2. Draw the GUI */

    /* Toolbar is rendered as a second pass overlay (see end of function).
     * Input for toolbar buttons is handled via manual hit-testing here. */
    float toolbar_h = 60.0f;

    /* ── Manual toolbar hit-testing (same approach as plugin_gui.c) ───── */
    {
        int mx, my;
        Uint32 buttons = SDL_GetMouseState(&mx, &my);
        bool mouse_down = (buttons & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0;
        static bool tb_mouse_was_down = false;
        bool just_clicked = mouse_down && !tb_mouse_was_down;

        /* Slider drag state: -1=none, 0=BPM, 1=swing, 2=volume */
        static int dragging_slider = -1;

        /* Handle active slider drag (works even if mouse leaves toolbar) */
        if (mouse_down && dragging_slider >= 0) {
            float col_w = (float)g_win_width / 15.0f;
            float slider_col = 0;
            switch (dragging_slider) {
            case 0: slider_col = 3; break; /* BPM slider column */
            case 1: slider_col = 5; break; /* Swing slider column */
            case 2: slider_col = 7; break; /* Volume slider column */
            }
            float slider_x = col_w * slider_col;
            float t = ((float)mx - slider_x) / col_w;
            if (t < 0.0f) t = 0.0f;
            if (t > 1.0f) t = 1.0f;
            switch (dragging_slider) {
            case 0: engine->transport.bpm = 40.0 + t * 260.0; break;
            case 1: engine->transport.swing = t; break;
            case 2: engine->master_volume = t; break;
            }
        }
        if (!mouse_down) dragging_slider = -1;

        if (just_clicked && my < (int)toolbar_h && my >= 0 &&
            mx >= 0 && mx < g_win_width)
        {
            /* 15 columns, single row toolbar */
            float col_w = (float)g_win_width / 15.0f;
            int col = (int)((float)mx / col_w);

            switch (col) {
            case 0: /* PLAY */
                engine->transport.playing = !engine->transport.playing;
                engine->transport.current_beat = 0.0;
                engine->transport.sample_position = 0;
                engine->transport.current_step = 0;
                g_visual_step = 0;
                if (engine->transport.playing)
                    g_play_start_ticks = SDL_GetPerformanceCounter();
                LOG_DEBUG("BUTTON: %s", engine->transport.playing ? "PLAY" : "STOP");
                break;
            case 1: /* REC */
                if (engine->recording) {
                    engine->recording = false;
                    if (engine->rec_frames > 0) {
                        sq_export_result_t rec_result = {0};
                        rec_result.data = engine->rec_buffer;
                        rec_result.num_frames = engine->rec_frames;
                        rec_result.sample_rate = engine->sample_rate;
                        sq_export_write_wav("recording.wav", &rec_result, 16);
                        LOG_INFO("Saved recording: %u frames to recording.wav", engine->rec_frames);
                        snprintf(g_save_status, sizeof(g_save_status), "%s", "Recording saved: recording.wav");
                        g_status_timer = 180;
                    }
                    engine->rec_frames = 0;
                } else {
                    engine->rec_frames = 0;
                    engine->recording = true;
                    LOG_INFO("Recording started");
                    snprintf(g_save_status, sizeof(g_save_status), "%s", "Recording...");
                    g_status_timer = 90;
                }
                break;
            /* Slider columns: click starts drag */
            case 3: dragging_slider = 0; break; /* BPM slider */
            case 5: dragging_slider = 1; break; /* Swing slider */
            case 7: dragging_slider = 2; break; /* Volume slider */
            case 8: /* EXPORT */
                export_dialog_show();
                break;
            case 9: /* PRESETS */
                { int *vis = pattern_presets_visible_ptr(); *vis = !*vis; }
                break;
            case 10: /* PIANO (virtual keyboard) */
                g_show_keyboard = !g_show_keyboard;
                break;
            case 11: /* PAT/SONG/PERF */
                engine->transport.mode = (sq_play_mode_t)((engine->transport.mode + 1) % 3);
                break;
            case 12: /* FX */
                g_show_mixer = !g_show_mixer;
                break;
            case 13: /* BROWSE */
                g_show_browser = !g_show_browser;
                break;
            case 14: /* EXIT */
                LOG_INFO("EXIT button pressed");
                quit = 1;
                break;
            }
        }

        tb_mouse_was_down = mouse_down;
    }

    /* ── Compute visual playhead from wall-clock time ────────────────────── */
    if (engine->transport.playing) {
        Uint64 now = SDL_GetPerformanceCounter();
        double elapsed = (double)(now - g_play_start_ticks)
                       / (double)SDL_GetPerformanceFrequency();
        double beats = elapsed * (engine->transport.bpm / 60.0);
        int pat_len = 16;
        int pi = engine->transport.current_pattern;
        if (pi >= 0 && (uint32_t)pi < engine->num_patterns &&
            engine->patterns[pi].num_tracks > 0) {
            pat_len = (int)engine->patterns[pi].tracks[0].length;
        }
        g_visual_step = ((int)floor(beats * STEPS_PER_BEAT)) % pat_len;
    } else if (g_was_playing && !engine->transport.playing) {
        g_visual_step = 0;
    }
    g_was_playing = engine->transport.playing;

    /* ── Determine if synth editor should be shown ─────────────────────── */
    bool show_synth_editor = false;
    int synth_preset_idx = -1;
    int *synth_preset_ptr = NULL; /* pointer to track's synth_preset for live editing */
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

    /* ── Arrangement panel (when in song/perform mode) ──────────────── */
    float arrange_h = 0.0f;
    if (engine->transport.mode != MODE_PATTERN) {
        arrange_h = 160.0f;
        arrangement_draw(g_nk_ctx, engine,
                         0.0f, 60.0f, (float)g_win_width, arrange_h);
    }

    /* ── Layout: drum grid + optional synth editor + optional browser ── */
    float grid_y = 60.0f + arrange_h;
    float kb_reserve = g_show_keyboard ? 120.0f : 0.0f;
    float total_h = (float)g_win_height - grid_y - kb_reserve;
    float total_w = (float)g_win_width;

    /* Horizontal split: browser takes right side when visible */
    float browser_w = 0.0f;
    float main_w = total_w;
    if (g_show_browser) {
        browser_w = 350.0f;
        if (browser_w > total_w * 0.4f)
            browser_w = total_w * 0.4f;
        main_w = total_w - browser_w;
    }

    /* ── Full-height piano roll mode ─────────────────────────────── */
    if (g_show_piano_roll && show_synth_editor && synth_preset_idx >= 0) {
        /* Piano roll gets the main area (like Ableton clip view) */
        float pr_h = total_h;
        float se_w = 0;

        if (g_show_mixer) {
            /* Piano roll (60%) + synth editor (20%) + mixer (20%) */
            float pr_w = main_w * 0.60f;
            se_w = main_w * 0.20f;
            float mx_w = main_w - pr_w - se_w;
            piano_roll_draw(g_nk_ctx, engine, g_selected_track,
                            0.0f, grid_y, pr_w, pr_h);
            synth_editor_draw(g_nk_ctx, engine, synth_preset_ptr,
                              pr_w, grid_y, se_w, pr_h);
            mixer_view_draw(g_nk_ctx, engine,
                            pr_w + se_w, grid_y, mx_w, pr_h);
        } else {
            /* Piano roll (70%) + synth editor (30%) */
            float pr_w = main_w * 0.70f;
            se_w = main_w - pr_w;
            piano_roll_draw(g_nk_ctx, engine, g_selected_track,
                            0.0f, grid_y, pr_w, pr_h);
            synth_editor_draw(g_nk_ctx, engine, synth_preset_ptr,
                              pr_w, grid_y, se_w, pr_h);
        }
    } else {
        /* ── Normal layout: drum grid + optional bottom panels ─── */
        float grid_h, bottom_h;
        bool has_bottom = show_synth_editor || g_show_mixer;
        if (has_bottom) {
            bottom_h = 280.0f;
            /* Shrink bottom panel when keyboard is also visible */
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

        drum_grid_draw(g_nk_ctx, engine,
                       0.0f, grid_y, main_w, grid_h);

        if (has_bottom) {
            float bottom_y = grid_y + grid_h;

            if (show_synth_editor && synth_preset_idx >= 0 && g_show_mixer) {
                float pr_w = main_w * 0.40f;
                float se_w = main_w * 0.30f;
                float mx_w = main_w - pr_w - se_w;
                piano_roll_draw(g_nk_ctx, engine, g_selected_track,
                                0.0f, bottom_y, pr_w, bottom_h);
                synth_editor_draw(g_nk_ctx, engine, synth_preset_ptr,
                                  pr_w, bottom_y, se_w, bottom_h);
                mixer_view_draw(g_nk_ctx, engine,
                                pr_w + se_w, bottom_y, mx_w, bottom_h);
            } else if (show_synth_editor && synth_preset_idx >= 0) {
                float pr_w = main_w * 0.55f;
                float se_w = main_w - pr_w;
                piano_roll_draw(g_nk_ctx, engine, g_selected_track,
                                0.0f, bottom_y, pr_w, bottom_h);
                synth_editor_draw(g_nk_ctx, engine, synth_preset_ptr,
                                  pr_w, bottom_y, se_w, bottom_h);
            } else if (g_show_mixer) {
                mixer_view_draw(g_nk_ctx, engine,
                                0.0f, bottom_y, main_w, bottom_h);
            }
        }
    }

    if (g_show_browser) {
        sample_browser_draw(g_nk_ctx, engine,
                            main_w, grid_y, browser_w, total_h);
        if (sample_browser_close_requested()) {
            g_show_browser = false;
        }
    }

    /* ── Virtual keyboard (bottom strip) ─────────────────────── */
    if (g_show_keyboard) {
        float kb_h = 120.0f;
        /* Find synth preset: use selected track or first synth track */
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
        /* Default to preset 0 if no synth track found */
        if (kb_preset < 0) kb_preset = 0;
        virtual_keyboard_draw(g_nk_ctx, engine, kb_preset,
                              0.0f, (float)g_win_height - kb_h,
                              main_w, kb_h);
    }

    /* Export dialog (floating window) */
    if (export_dialog_visible()) {
        export_dialog_draw(g_nk_ctx, engine);
    }

    /* Pattern presets dialog (floating window) */
    pattern_presets_draw(g_nk_ctx, engine);

    /* ── Render Pass 1: all content windows ──────────────────── */
    {
        float bg[4];
        theme_get_clear_color(bg);
        int w, h;
        SDL_GetWindowSize(g_window, &w, &h);
        glViewport(0, 0, w, h);
        glClearColor(bg[0], bg[1], bg[2], bg[3]);
        glClear(GL_COLOR_BUFFER_BIT);

        nk_sdl_render(NK_ANTI_ALIASING_ON, MAX_VERTEX_BUFFER, MAX_ELEMENT_BUFFER);
    }

    /* ── Render Pass 2: Toolbar overlay (display-only, input via hit-test) ── */
    {
        if (nk_begin(g_nk_ctx, "Toolbar",
                     nk_rect(0, 0, (float)g_win_width, toolbar_h),
                     NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_BORDER | NK_WINDOW_NO_INPUT))
        {
            /* Hover highlight: draw a subtle rectangle behind the hovered column */
            {
                int hmx, hmy;
                SDL_GetMouseState(&hmx, &hmy);
                if (hmy >= 0 && hmy < (int)toolbar_h && hmx >= 0 && hmx < g_win_width) {
                    float col_w = (float)g_win_width / 15.0f;
                    int hover_col = (int)((float)hmx / col_w);
                    if (hover_col >= 0 && hover_col < 15) {
                        struct nk_command_buffer *canvas = nk_window_get_canvas(g_nk_ctx);
                        struct nk_rect hr = nk_rect(col_w * hover_col, 0,
                                                    col_w, toolbar_h);
                        nk_fill_rect(canvas, hr, 0, nk_rgba(255, 255, 255, 20));
                    }
                }
            }

            nk_layout_row_dynamic(g_nk_ctx, 35, 15);

            /* PLAY/STOP — display only */
            nk_button_label(g_nk_ctx, engine->transport.playing ? "STOP" : "PLAY");

            /* REC — display only */
            {
                struct nk_style_button rec_style = g_nk_ctx->style.button;
                if (engine->recording) {
                    rec_style.normal = nk_style_item_color(nk_rgba(200, 30, 30, 255));
                    rec_style.hover  = nk_style_item_color(nk_rgba(220, 50, 50, 255));
                    rec_style.text_normal = nk_rgb(255, 255, 255);
                }
                nk_button_label_styled(g_nk_ctx, &rec_style,
                                       engine->recording ? "REC *" : "REC");
            }

            /* BPM */
            nk_labelf(g_nk_ctx, NK_TEXT_CENTERED, "BPM: %.0f", engine->transport.bpm);
            float bpm_f = (float)engine->transport.bpm;
            nk_slider_float(g_nk_ctx, 40.0f, &bpm_f, 300.0f, 1.0f);
            engine->transport.bpm = (double)bpm_f;

            /* Swing */
            nk_labelf(g_nk_ctx, NK_TEXT_CENTERED, "Sw:%.0f%%",
                      engine->transport.swing * 100.0f);
            nk_slider_float(g_nk_ctx, 0.0f, &engine->transport.swing, 1.0f, 0.01f);

            /* Volume */
            nk_labelf(g_nk_ctx, NK_TEXT_CENTERED, "Vol: %.0f%%",
                      engine->master_volume * 100.0f);
            nk_slider_float(g_nk_ctx, 0.0f, &engine->master_volume, 1.0f, 0.01f);

            /* EXPORT — display only */
            nk_button_label(g_nk_ctx, "EXPORT");

            /* PRESETS — display only */
            {
                int *vis = pattern_presets_visible_ptr();
                struct nk_style_button preset_style = g_nk_ctx->style.button;
                if (*vis) {
                    preset_style.normal = nk_style_item_color(nk_rgba(180, 140, 60, 255));
                    preset_style.hover  = nk_style_item_color(nk_rgba(200, 160, 80, 255));
                }
                nk_button_label_styled(g_nk_ctx, &preset_style,
                                       *vis ? "PRESETS [ON]" : "PRESETS");
            }

            /* PIANO (virtual keyboard) — display only */
            {
                struct nk_style_button pr_style = g_nk_ctx->style.button;
                if (g_show_keyboard) {
                    pr_style.normal = nk_style_item_color(nk_rgba(60, 120, 180, 255));
                    pr_style.hover  = nk_style_item_color(nk_rgba(80, 140, 200, 255));
                }
                nk_button_label_styled(g_nk_ctx, &pr_style,
                                       g_show_keyboard ? "PIANO [ON]" : "PIANO");
            }

            /* PAT/SONG/PERF — display only */
            {
                const char *mode_labels[] = {"PAT", "SONG", "PERF"};
                struct nk_style_button mode_style = g_nk_ctx->style.button;
                if (engine->transport.mode == MODE_SONG) {
                    mode_style.normal = nk_style_item_color(nk_rgba(60, 100, 180, 255));
                    mode_style.hover  = nk_style_item_color(nk_rgba(80, 120, 200, 255));
                } else if (engine->transport.mode == MODE_PERFORM) {
                    mode_style.normal = nk_style_item_color(nk_rgba(180, 60, 100, 255));
                    mode_style.hover  = nk_style_item_color(nk_rgba(200, 80, 120, 255));
                }
                nk_button_label_styled(g_nk_ctx, &mode_style,
                                       mode_labels[engine->transport.mode]);
            }

            /* FX — display only */
            {
                struct nk_style_button fx_style = g_nk_ctx->style.button;
                if (g_show_mixer) {
                    fx_style.normal = nk_style_item_color(nk_rgba(130, 80, 160, 255));
                    fx_style.hover  = nk_style_item_color(nk_rgba(150, 100, 180, 255));
                }
                nk_button_label_styled(g_nk_ctx, &fx_style,
                                       g_show_mixer ? "FX [ON]" : "FX");
            }

            /* BROWSE — display only */
            {
                struct nk_style_button browse_style = g_nk_ctx->style.button;
                if (g_show_browser) {
                    browse_style.normal = nk_style_item_color(nk_rgba(60, 130, 60, 255));
                    browse_style.hover  = nk_style_item_color(nk_rgba(80, 150, 80, 255));
                }
                nk_button_label_styled(g_nk_ctx, &browse_style,
                                       g_show_browser ? "BROWSE [ON]" : "BROWSE");
            }

            /* EXIT — display only */
            {
                struct nk_style_button red_btn = g_nk_ctx->style.button;
                red_btn.normal  = nk_style_item_color(nk_rgb(160, 30, 30));
                red_btn.hover   = nk_style_item_color(nk_rgb(200, 50, 50));
                red_btn.active  = nk_style_item_color(nk_rgb(220, 70, 70));
                red_btn.text_normal = nk_rgb(255, 255, 255);
                red_btn.text_hover  = nk_rgb(255, 255, 255);
                red_btn.text_active = nk_rgb(255, 255, 255);
                nk_button_label_styled(g_nk_ctx, &red_btn, "EXIT");
            }

            /* Status message */
            if (g_status_timer > 0) {
                nk_layout_row_dynamic(g_nk_ctx, 18, 1);
                nk_label_colored(g_nk_ctx, g_save_status, NK_TEXT_CENTERED,
                                 nk_rgb(100, 255, 100));
                g_status_timer--;
            }
        }
        nk_end(g_nk_ctx);

        /* Render toolbar on top (no GL clear) */
        nk_sdl_render(NK_ANTI_ALIASING_ON, MAX_VERTEX_BUFFER, MAX_ELEMENT_BUFFER);
    }

    SDL_GL_SwapWindow(g_window);

    return quit;
}

void gui_shutdown(void)
{
    nk_sdl_shutdown();
    if (g_gl_ctx) SDL_GL_DeleteContext(g_gl_ctx);
    if (g_window) SDL_DestroyWindow(g_window);
    SDL_Quit();
    LOG_INFO("GUI shut down");
}
