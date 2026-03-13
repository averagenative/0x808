/*
 * plugin_gui.c — Embedded GUI for the DAW plugin.
 *
 * Creates an SDL2+OpenGL+Nuklear rendering surface inside the host DAW's
 * native window. Reuses all the same sub-component drawing code (drum grid,
 * piano roll, synth editor, etc.) as the standalone GUI.
 *
 * Threading model:
 *   - Host creates/destroys GUI on its UI thread
 *   - A render thread runs at ~60fps, polling SDL events and drawing
 *   - Audio runs on the host's audio thread via cplug_process()
 *
 * Platform notes:
 *   - Linux:   host provides an X11 Window ID
 *   - Windows: host provides an HWND
 *   - macOS:   host provides an NSView*
 *   SDL_CreateWindowFrom() handles all three cases.
 */

/* Load GL3 function pointers (Windows needs runtime loading via SDL) */
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

#include "plugin/plugin_gui.h"
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
#include "formats/project.h"
#include "engine/export.h"

#define LOG_TAG "plugin_gui"
#include "core/log.h"

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdatomic.h>
#include <SDL2/SDL_syswm.h>

/* Max vertex/element buffer sizes for Nuklear rendering */
#define MAX_VERTEX_BUFFER  (512 * 1024)
#define MAX_ELEMENT_BUFFER (128 * 1024)
#define STEPS_PER_BEAT 4
#define RENDER_FPS 60

/* ─── Globals required by sub-components (drum_grid.c, piano_roll.c, etc.) ── */

int g_win_width  = 1280;
int g_win_height = 720;
int g_visual_step = 0;
int g_selected_track = -1;

/* ─── Plugin GUI instance ────────────────────────────────────────────────── */

struct sq_plugin_gui {
    sq_engine_t      *engine;
    SDL_Window       *window;
    SDL_GLContext      gl_ctx;
    struct nk_context *nk_ctx;

    uint32_t width;
    uint32_t height;
    float    scale_factor;

    /* Render thread */
    SDL_Thread *render_thread;
    atomic_int  running;

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

    /* Manual toolbar click edge detection */
    bool tb_mouse_was_down;

    /* Slider drag state: -1=none, 0=BPM, 1=swing, 2=volume */
    int dragging_slider;
};

/* ─── Nuklear style setup (same dark theme as standalone) ────────────────── */

static void setup_nk_style(struct nk_context *ctx)
{
    struct nk_color table[NK_COLOR_COUNT];
    table[NK_COLOR_TEXT]                  = nk_rgba(210, 210, 210, 255);
    table[NK_COLOR_WINDOW]                = nk_rgba(35,  35,  38,  255);
    table[NK_COLOR_HEADER]                = nk_rgba(45,  45,  48,  255);
    table[NK_COLOR_BORDER]                = nk_rgba(60,  60,  65,  255);
    table[NK_COLOR_BUTTON]                = nk_rgba(55,  55,  60,  255);
    table[NK_COLOR_BUTTON_HOVER]          = nk_rgba(70,  70,  75,  255);
    table[NK_COLOR_BUTTON_ACTIVE]         = nk_rgba(80,  80,  85,  255);
    table[NK_COLOR_TOGGLE]                = nk_rgba(50,  50,  55,  255);
    table[NK_COLOR_TOGGLE_HOVER]          = nk_rgba(60,  60,  65,  255);
    table[NK_COLOR_TOGGLE_CURSOR]         = nk_rgba(100, 180, 255, 255);
    table[NK_COLOR_SELECT]                = nk_rgba(45,  45,  48,  255);
    table[NK_COLOR_SELECT_ACTIVE]         = nk_rgba(100, 180, 255, 255);
    table[NK_COLOR_SLIDER]                = nk_rgba(45,  45,  48,  255);
    table[NK_COLOR_SLIDER_CURSOR]         = nk_rgba(100, 180, 255, 255);
    table[NK_COLOR_SLIDER_CURSOR_HOVER]   = nk_rgba(120, 200, 255, 255);
    table[NK_COLOR_SLIDER_CURSOR_ACTIVE]  = nk_rgba(140, 220, 255, 255);
    table[NK_COLOR_PROPERTY]              = nk_rgba(45,  45,  48,  255);
    table[NK_COLOR_EDIT]                  = nk_rgba(40,  40,  43,  255);
    table[NK_COLOR_EDIT_CURSOR]           = nk_rgba(210, 210, 210, 255);
    table[NK_COLOR_COMBO]                 = nk_rgba(45,  45,  48,  255);
    table[NK_COLOR_CHART]                 = nk_rgba(45,  45,  48,  255);
    table[NK_COLOR_CHART_COLOR]           = nk_rgba(100, 180, 255, 255);
    table[NK_COLOR_CHART_COLOR_HIGHLIGHT] = nk_rgba(255, 100, 100, 255);
    table[NK_COLOR_SCROLLBAR]             = nk_rgba(40,  40,  43,  255);
    table[NK_COLOR_SCROLLBAR_CURSOR]      = nk_rgba(70,  70,  75,  255);
    table[NK_COLOR_SCROLLBAR_CURSOR_HOVER]= nk_rgba(90,  90,  95,  255);
    table[NK_COLOR_SCROLLBAR_CURSOR_ACTIVE]=nk_rgba(100, 100, 105, 255);
    table[NK_COLOR_TAB_HEADER]            = nk_rgba(45,  45,  48,  255);
    nk_style_from_table(ctx, table);
}

/* ─── Draw one frame (mirrors gui_frame from standalone gui.c) ───────────── */

static void plugin_gui_draw_frame(sq_plugin_gui_t *gui)
{
    sq_engine_t *engine = gui->engine;
    if (!engine) return;

    /* Update global dimensions for sub-components.
     * Also sync SDL window size with host parent window on Windows,
     * since the host may resize the parent without telling us. */
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
                    int cw, ch;
                    SDL_GetWindowSize(gui->window, &cw, &ch);
                    if (cw != pw || ch != ph) {
                        SDL_SetWindowSize(gui->window, pw, ph);
                        SetWindowPos(sdl_hwnd, NULL, 0, 0, pw, ph,
                                     SWP_NOZORDER | SWP_NOMOVE);
                    }
                }
            }
        }
    }
#endif
    {
        int prev_w = g_win_width, prev_h = g_win_height;
        SDL_GetWindowSize(gui->window, &g_win_width, &g_win_height);
        if (g_win_width != prev_w || g_win_height != prev_h)
            nk_clear(gui->nk_ctx);
    }

    /* Poll SDL events */
    SDL_Event evt;
    nk_input_begin(gui->nk_ctx);
    while (SDL_PollEvent(&evt)) {
        /* Handle keyboard shortcuts (subset — no ESCAPE/quit in plugin) */
        if (evt.type == SDL_KEYDOWN) {
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
                    /* Save not supported in plugin context */
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
        nk_sdl_handle_event(&evt);
    }
    nk_input_end(gui->nk_ctx);

    /* ── Visual playhead from wall-clock time ──────────────────── */
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

    /* Toolbar is rendered as a second pass overlay (see end of function).
     * But input is handled HERE in the main pass so button clicks work. */
    float toolbar_h = 80.0f;

    /* Process toolbar input via manual hit-testing using SDL mouse state.
     * We track mouse-down edge ourselves for reliable single-click detection. */
    {
        int mx, my;
        Uint32 buttons = SDL_GetMouseState(&mx, &my);
        bool mouse_down = (buttons & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0;
        bool just_clicked = mouse_down && !gui->tb_mouse_was_down;

        /* Handle active slider drag (works even if mouse leaves toolbar).
         * Row 1 has 7 columns: PLAY | BPM label | BPM slider | Sw label | Sw slider | Vol label | Vol slider */
        if (mouse_down && gui->dragging_slider >= 0) {
            float col_w = (float)g_win_width / 7.0f;
            float slider_col = 0;
            switch (gui->dragging_slider) {
            case 0: slider_col = 2; break; /* BPM slider column */
            case 1: slider_col = 4; break; /* Swing slider column */
            case 2: slider_col = 6; break; /* Volume slider column */
            }
            float slider_x = col_w * slider_col;
            float t = ((float)mx - slider_x) / col_w;
            if (t < 0.0f) t = 0.0f;
            if (t > 1.0f) t = 1.0f;
            switch (gui->dragging_slider) {
            case 0: engine->transport.bpm = 40.0 + t * 260.0; break;
            case 1: engine->transport.swing = t; break;
            case 2: engine->master_volume = t; break;
            }
        }
        if (!mouse_down) gui->dragging_slider = -1;

        if (just_clicked && my < (int)toolbar_h && my >= 0 &&
            mx >= 0 && mx < g_win_width)
        {
            float col_w = (float)g_win_width / 7.0f;
            int row2_y = 36; /* approx start of row 2 (28px row + padding) */
            float row2_col_w = (float)g_win_width / 6.0f;

            if (my < row2_y) {
                /* Row 1: determine column */
                int col = (int)((float)mx / col_w);
                switch (col) {
                case 0: /* PLAY */
                    engine->transport.playing = !engine->transport.playing;
                    engine->transport.current_beat = 0.0;
                    engine->transport.sample_position = 0;
                    engine->transport.current_step = 0;
                    g_visual_step = 0;
                    if (engine->transport.playing)
                        gui->play_start_ticks = SDL_GetPerformanceCounter();
                    break;
                case 2: gui->dragging_slider = 0; break; /* BPM slider */
                case 4: gui->dragging_slider = 1; break; /* Swing slider */
                case 6: gui->dragging_slider = 2; break; /* Volume slider */
                }
            } else {
                /* Row 2: PIANO, KEYS, PAT, FX, BROWSE, status */
                int col = (int)((float)mx / row2_col_w);
                switch (col) {
                case 0: gui->show_piano_roll = !gui->show_piano_roll; break;
                case 1: gui->show_keyboard   = !gui->show_keyboard;   break;
                case 2: engine->transport.mode = (sq_play_mode_t)((engine->transport.mode + 1) % 3); break;
                case 3: gui->show_mixer   = !gui->show_mixer;   break;
                case 4: gui->show_browser = !gui->show_browser; break;
                }
            }
        }

        gui->tb_mouse_was_down = mouse_down;
    }

    /* ── Determine if synth editor should be shown ──────────── */
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

    /* ── Arrangement panel ──────────────────────────────────── */
    float arrange_h = 0.0f;
    if (engine->transport.mode != MODE_PATTERN) {
        arrange_h = 160.0f;
        arrangement_draw(gui->nk_ctx, engine,
                         0.0f, toolbar_h, (float)g_win_width, arrange_h);
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
            piano_roll_draw(gui->nk_ctx, engine, g_selected_track,
                            0.0f, grid_y, pr_w, total_h);
            synth_editor_draw(gui->nk_ctx, engine, synth_preset_ptr,
                              pr_w, grid_y, se_w, total_h);
            mixer_view_draw(gui->nk_ctx, engine,
                            pr_w + se_w, grid_y, mx_w, total_h);
        } else {
            float pr_w = main_w * 0.70f;
            float se_w = main_w - pr_w;
            piano_roll_draw(gui->nk_ctx, engine, g_selected_track,
                            0.0f, grid_y, pr_w, total_h);
            synth_editor_draw(gui->nk_ctx, engine, synth_preset_ptr,
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

        /* Force DrumGrid bounds every frame to prevent overlap */
        nk_window_set_bounds(gui->nk_ctx, "DrumGrid",
                             nk_rect(0.0f, grid_y, main_w, grid_h));
        drum_grid_draw(gui->nk_ctx, engine,
                       0.0f, grid_y, main_w, grid_h);

        if (has_bottom) {
            float bottom_y = grid_y + grid_h;
            if (show_synth_editor && synth_preset_idx >= 0 && gui->show_mixer) {
                float pr_w = main_w * 0.40f;
                float se_w = main_w * 0.30f;
                float mx_w = main_w - pr_w - se_w;
                piano_roll_draw(gui->nk_ctx, engine, g_selected_track,
                                0.0f, bottom_y, pr_w, bottom_h);
                synth_editor_draw(gui->nk_ctx, engine, synth_preset_ptr,
                                  pr_w, bottom_y, se_w, bottom_h);
                mixer_view_draw(gui->nk_ctx, engine,
                                pr_w + se_w, bottom_y, mx_w, bottom_h);
            } else if (show_synth_editor && synth_preset_idx >= 0) {
                float pr_w = main_w * 0.55f;
                float se_w = main_w - pr_w;
                piano_roll_draw(gui->nk_ctx, engine, g_selected_track,
                                0.0f, bottom_y, pr_w, bottom_h);
                synth_editor_draw(gui->nk_ctx, engine, synth_preset_ptr,
                                  pr_w, bottom_y, se_w, bottom_h);
            } else if (gui->show_mixer) {
                mixer_view_draw(gui->nk_ctx, engine,
                                0.0f, bottom_y, main_w, bottom_h);
            }
        }
    }

    /* ── Virtual keyboard (bottom strip) ─────────────────────── */
    if (gui->show_keyboard) {
        float kb_h = 120.0f;
        /* Determine synth preset for the keyboard */
        int kb_preset = -1;
        if (synth_preset_idx >= 0) {
            kb_preset = synth_preset_idx;
        } else {
            /* Find first synth track's preset */
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
        /* Default to preset 0 if no synth track found */
        if (kb_preset < 0) kb_preset = 0;
        virtual_keyboard_draw(gui->nk_ctx, engine, kb_preset,
                              0.0f, (float)g_win_height - kb_h,
                              main_w, kb_h);
    }

    if (gui->show_browser) {
        sample_browser_draw(gui->nk_ctx, engine,
                            main_w, grid_y, browser_w, total_h);
        if (sample_browser_close_requested()) {
            gui->show_browser = false;
        }
    }

    /* Export dialog */
    if (export_dialog_visible())
        export_dialog_draw(gui->nk_ctx, engine);

    /* Pattern presets */
    pattern_presets_draw(gui->nk_ctx, engine);

    /* ── Render Pass 1: all content windows ──────────────────── */
    {
        float bg[4] = {0.12f, 0.12f, 0.13f, 1.0f};
        int w, h;
        SDL_GetWindowSize(gui->window, &w, &h);
        glViewport(0, 0, w, h);
        glClearColor(bg[0], bg[1], bg[2], bg[3]);
        glClear(GL_COLOR_BUFFER_BIT);

        nk_sdl_render(NK_ANTI_ALIASING_ON, MAX_VERTEX_BUFFER, MAX_ELEMENT_BUFFER);
    }

    /* ── Render Pass 2: Toolbar overlay (visual only, input handled above) ── */
    {
        float tb_h = 80.0f;

        /* NO nk_input_begin/end here — that corrupts input state for next frame.
         * The toolbar is display-only; clicks are handled by manual hit-testing. */
        if (nk_begin(gui->nk_ctx, "Toolbar",
                     nk_rect(0, 0, (float)g_win_width, tb_h),
                     NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_BORDER | NK_WINDOW_NO_INPUT))
        {
            /* Hover highlight: draw a subtle rectangle behind the hovered area */
            {
                int hmx, hmy;
                SDL_GetMouseState(&hmx, &hmy);
                if (hmy >= 0 && hmy < (int)tb_h && hmx >= 0 && hmx < g_win_width) {
                    struct nk_command_buffer *canvas = nk_window_get_canvas(gui->nk_ctx);
                    int row2_y = 36;
                    if (hmy < row2_y) {
                        /* Row 1: 7 columns */
                        float col_w = (float)g_win_width / 7.0f;
                        int hover_col = (int)((float)hmx / col_w);
                        if (hover_col >= 0 && hover_col < 7) {
                            struct nk_rect hr = nk_rect(col_w * hover_col, 0,
                                                        col_w, (float)row2_y);
                            nk_fill_rect(canvas, hr, 0, nk_rgba(255, 255, 255, 20));
                        }
                    } else {
                        /* Row 2: 6 columns */
                        float col_w = (float)g_win_width / 6.0f;
                        int hover_col = (int)((float)hmx / col_w);
                        if (hover_col >= 0 && hover_col < 6) {
                            struct nk_rect hr = nk_rect(col_w * hover_col, (float)row2_y,
                                                        col_w, tb_h - (float)row2_y);
                            nk_fill_rect(canvas, hr, 0, nk_rgba(255, 255, 255, 20));
                        }
                    }
                }
            }

            /* Row 1: Transport controls */
            nk_layout_row_dynamic(gui->nk_ctx, 28, 7);

            const char *play_label = engine->transport.playing ? "STOP" : "PLAY";
            nk_button_label(gui->nk_ctx, play_label); /* display only — click handled by hit-test */

            nk_labelf(gui->nk_ctx, NK_TEXT_CENTERED, "BPM: %.0f", engine->transport.bpm);
            float bpm_f = (float)engine->transport.bpm;
            nk_slider_float(gui->nk_ctx, 40.0f, &bpm_f, 300.0f, 1.0f);
            engine->transport.bpm = (double)bpm_f;

            nk_labelf(gui->nk_ctx, NK_TEXT_CENTERED, "Sw:%.0f%%",
                      engine->transport.swing * 100.0f);
            nk_slider_float(gui->nk_ctx, 0.0f, &engine->transport.swing, 1.0f, 0.01f);

            nk_labelf(gui->nk_ctx, NK_TEXT_CENTERED, "Vol: %.0f%%",
                      engine->master_volume * 100.0f);
            nk_slider_float(gui->nk_ctx, 0.0f, &engine->master_volume, 1.0f, 0.01f);

            /* Row 2: Panel toggle buttons */
            nk_layout_row_dynamic(gui->nk_ctx, 24, 6);

            {
                struct nk_style_button pr_style = gui->nk_ctx->style.button;
                if (gui->show_piano_roll) {
                    pr_style.normal = nk_style_item_color(nk_rgba(60, 120, 180, 255));
                    pr_style.hover  = nk_style_item_color(nk_rgba(80, 140, 200, 255));
                }
                nk_button_label_styled(gui->nk_ctx, &pr_style,
                                       gui->show_piano_roll ? "PIANO [ON]" : "PIANO");
            }
            {
                struct nk_style_button kb_style = gui->nk_ctx->style.button;
                if (gui->show_keyboard) {
                    kb_style.normal = nk_style_item_color(nk_rgba(180, 130, 60, 255));
                    kb_style.hover  = nk_style_item_color(nk_rgba(200, 150, 80, 255));
                }
                nk_button_label_styled(gui->nk_ctx, &kb_style,
                                       gui->show_keyboard ? "KEYS [ON]" : "KEYS");
            }
            {
                const char *mode_labels[] = {"PAT", "SONG", "PERF"};
                struct nk_style_button mode_style = gui->nk_ctx->style.button;
                if (engine->transport.mode == MODE_SONG) {
                    mode_style.normal = nk_style_item_color(nk_rgba(60, 100, 180, 255));
                    mode_style.hover  = nk_style_item_color(nk_rgba(80, 120, 200, 255));
                } else if (engine->transport.mode == MODE_PERFORM) {
                    mode_style.normal = nk_style_item_color(nk_rgba(180, 60, 100, 255));
                    mode_style.hover  = nk_style_item_color(nk_rgba(200, 80, 120, 255));
                }
                nk_button_label_styled(gui->nk_ctx, &mode_style,
                                       mode_labels[engine->transport.mode]);
            }
            {
                struct nk_style_button fx_style = gui->nk_ctx->style.button;
                if (gui->show_mixer) {
                    fx_style.normal = nk_style_item_color(nk_rgba(130, 80, 160, 255));
                    fx_style.hover  = nk_style_item_color(nk_rgba(150, 100, 180, 255));
                }
                nk_button_label_styled(gui->nk_ctx, &fx_style,
                                       gui->show_mixer ? "FX [ON]" : "FX");
            }
            {
                struct nk_style_button browse_style = gui->nk_ctx->style.button;
                if (gui->show_browser) {
                    browse_style.normal = nk_style_item_color(nk_rgba(60, 130, 60, 255));
                    browse_style.hover  = nk_style_item_color(nk_rgba(80, 150, 80, 255));
                }
                nk_button_label_styled(gui->nk_ctx, &browse_style,
                                       gui->show_browser ? "BROWSE [ON]" : "BROWSE");
            }

            if (gui->status_timer > 0) {
                nk_label_colored(gui->nk_ctx, gui->save_status, NK_TEXT_CENTERED,
                                 nk_rgb(100, 255, 100));
                gui->status_timer--;
            }
        }
        nk_end(gui->nk_ctx);

        /* Render just the toolbar on top of everything */
        nk_sdl_render(NK_ANTI_ALIASING_ON, MAX_VERTEX_BUFFER, MAX_ELEMENT_BUFFER);
    }

    SDL_GL_SwapWindow(gui->window);
}

/* ─── Render thread ──────────────────────────────────────────────────────── */

static int render_thread_func(void *userdata)
{
    sq_plugin_gui_t *gui = (sq_plugin_gui_t *)userdata;

    LOG_INFO("Plugin GUI render thread started (%d fps)", RENDER_FPS);

    /* Make GL context current on this thread */
    SDL_GL_MakeCurrent(gui->window, gui->gl_ctx);

    while (atomic_load(&gui->running)) {
        Uint32 start = SDL_GetTicks();

        plugin_gui_draw_frame(gui);

        /* Frame rate limiting */
        Uint32 elapsed = SDL_GetTicks() - start;
        Uint32 target_ms = 1000 / RENDER_FPS;
        if (elapsed < target_ms)
            SDL_Delay(target_ms - elapsed);
    }

    /* Release GL context from this thread */
    SDL_GL_MakeCurrent(gui->window, NULL);

    LOG_INFO("Plugin GUI render thread exiting");
    return 0;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Public API (called from plugin.c CPLUG callbacks)
 * ═══════════════════════════════════════════════════════════════════════════ */

sq_plugin_gui_t *plugin_gui_create(sq_engine_t *engine)
{
    sq_plugin_gui_t *gui = (sq_plugin_gui_t *)calloc(1, sizeof(sq_plugin_gui_t));
    if (!gui) return NULL;

    gui->engine = engine;
    gui->width  = 1280;
    gui->height = 720;
    gui->scale_factor = 1.0f;
    gui->dragging_slider = -1;
    atomic_store(&gui->running, 0);

    return gui;
}

void plugin_gui_destroy(sq_plugin_gui_t *gui)
{
    if (!gui) return;

    /* Stop render thread if running */
    plugin_gui_detach(gui);

    free(gui);
}

/* ─── Plugin file logger (mirrors plugin.c — Windows: file, Linux: stderr) ─ */

#ifdef _WIN32
#include <windows.h>

/* Defined in plugin.c — shared timestamped log file */
extern const char *sq_plugin_log_path(void);

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

int plugin_gui_attach(sq_plugin_gui_t *gui, void *native_handle)
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

    /* Create an SDL-owned OpenGL window and reparent it into the host HWND.
     * SDL_CreateWindowFrom() doesn't set SDL_WINDOW_OPENGL internally,
     * so GL context creation fails. Instead we create a normal SDL window
     * and use platform APIs to make it a child of the host window. */
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
            gui->window = NULL;
            return -1;
        }
        HWND sdl_hwnd = wminfo.info.win.window;
        HWND host_hwnd = (HWND)native_handle;

        /* Make SDL window a child of the host window */
        SetParent(sdl_hwnd, host_hwnd);
        LONG style = GetWindowLong(sdl_hwnd, GWL_STYLE);
        style = (style & ~WS_POPUP) | WS_CHILD;
        SetWindowLong(sdl_hwnd, GWL_STYLE, style);
        SetWindowPos(sdl_hwnd, NULL, 0, 0,
                     (int)gui->width, (int)gui->height,
                     SWP_NOZORDER | SWP_FRAMECHANGED);
        ShowWindow(sdl_hwnd, SW_SHOW);
        GLOG_INFO("Reparented SDL HWND %p into host HWND %p", (void*)sdl_hwnd, (void*)host_hwnd);
    }
#else
    /* On Linux/macOS, SDL_CreateWindowFrom should work directly */
    (void)native_handle;
#endif

    SDL_ShowWindow(gui->window);

    /* Create OpenGL context */
    GLOG_INFO("Creating GL context...");
    gui->gl_ctx = SDL_GL_CreateContext(gui->window);
    if (!gui->gl_ctx) {
        GLOG_ERROR("SDL_GL_CreateContext failed: %s", SDL_GetError());
        SDL_DestroyWindow(gui->window);
        gui->window = NULL;
        return -1;
    }
    GLOG_INFO("GL context created OK");

    /* Enable VSync */
    SDL_GL_SetSwapInterval(1);

    /* Load GL3 extension functions (needed on Windows) */
    gl3_loader_init();
    GLOG_DEBUG("GL3 loader init done");

    /* Initialize Nuklear */
    gui->nk_ctx = nk_sdl_init(gui->window);
    GLOG_INFO("Nuklear init done: nk_ctx=%p", (void*)gui->nk_ctx);
    {
        struct nk_font_atlas *atlas;
        nk_sdl_font_stash_begin(&atlas);
        nk_sdl_font_stash_end();
    }
    setup_nk_style(gui->nk_ctx);

    /* Release GL context from this thread so the render thread can use it */
    SDL_GL_MakeCurrent(gui->window, NULL);

    /* Start render thread */
    atomic_store(&gui->running, 1);
    gui->render_thread = SDL_CreateThread(render_thread_func, "sq_plugin_gui", gui);
    if (!gui->render_thread) {
        LOG_ERROR("Failed to create render thread: %s", SDL_GetError());
        nk_sdl_shutdown();
        SDL_GL_DeleteContext(gui->gl_ctx);
        SDL_DestroyWindow(gui->window);
        gui->gl_ctx = NULL;
        gui->window = NULL;
        atomic_store(&gui->running, 0);
        return -1;
    }

    LOG_INFO("Plugin GUI attached to host window, render thread started");
    return 0;
}

void plugin_gui_detach(sq_plugin_gui_t *gui)
{
    if (!gui) return;

    /* Stop render thread */
    if (atomic_load(&gui->running)) {
        atomic_store(&gui->running, 0);
        if (gui->render_thread) {
            SDL_WaitThread(gui->render_thread, NULL);
            gui->render_thread = NULL;
        }
    }

    /* Clean up GL/Nuklear */
    if (gui->nk_ctx) {
        /* Need GL context current to clean up Nuklear GPU resources */
        if (gui->window && gui->gl_ctx)
            SDL_GL_MakeCurrent(gui->window, gui->gl_ctx);
        nk_sdl_shutdown();
        gui->nk_ctx = NULL;
    }
    if (gui->gl_ctx) {
        SDL_GL_DeleteContext(gui->gl_ctx);
        gui->gl_ctx = NULL;
    }
    if (gui->window) {
        SDL_DestroyWindow(gui->window);
        gui->window = NULL;
    }

    LOG_INFO("Plugin GUI detached from host window");
}

void plugin_gui_set_size(sq_plugin_gui_t *gui, uint32_t width, uint32_t height)
{
    if (!gui) return;
    gui->width = width;
    gui->height = height;
    g_win_width = (int)width;
    g_win_height = (int)height;

    /* Resize the SDL window to match host's new size */
    if (gui->window)
        SDL_SetWindowSize(gui->window, (int)width, (int)height);
}

void plugin_gui_get_size(sq_plugin_gui_t *gui, uint32_t *width, uint32_t *height)
{
    if (!gui) {
        *width = 1280;
        *height = 720;
        return;
    }
    *width = gui->width;
    *height = gui->height;
}

void plugin_gui_set_scale(sq_plugin_gui_t *gui, float scale)
{
    if (gui) gui->scale_factor = scale;
}
