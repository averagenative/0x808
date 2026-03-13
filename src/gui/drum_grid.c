/*
 * drum_grid.c — Visual drum step grid.
 *
 * Layout:
 * ┌──────────────┬────┬────┬────┬────┬────┬────┬ ... ┬────┐
 * │  Track Name  │ 1  │ 2  │ 3  │ 4  │ 5  │ 6  │     │ 16 │
 * │  [Vol] [Pan] │    │    │ XX │    │ XX │    │     │    │
 * │  [M] [S]     │    │    │    │    │    │    │     │    │
 * ├──────────────┼────┼────┼────┼────┼────┼────┼ ... ┼────┤
 * │  Snare       │    │    │    │    │ XX │    │     │    │
 * │  ...         │    │    │    │    │    │    │     │    │
 * └──────────────┴────┴────┴────┴────┴────┴────┴ ... ┴────┘
 *                              ▲ playback position highlight
 *
 * Interactions:
 *   Left-click:   toggle step on/off (velocity 0 ↔ 100)
 *   Right-click:  open velocity/pitch editor popup at mouse position
 *   Scroll wheel: adjust velocity ±5 per tick when hovering
 */

/* Need Nuklear types — this file is compiled as part of the GUI module
 * which has already defined NK_IMPLEMENTATION in gui.c */
#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#include "nuklear.h"

#include "gui/drum_grid.h"
#include "gui/gui.h"
#include "gui/knobs.h"
#include "gui/undo.h"
#include "engine/sampler.h"

#define LOG_TAG "grid"
#include "core/log.h"

#include <stdio.h>
#include <string.h>
#include <math.h>

/* ─── Colors for the grid ─────────────────────────────────────────────────── */

/* Track color palette — 8 user-selectable colors, cycled via right-click on track name */
#define NUM_TRACK_COLORS 8
static const struct nk_color track_colors[] = {
    {220, 80,  80,  255},  /* 0: red     */
    {80,  180, 220, 255},  /* 1: blue    */
    {80,  200, 120, 255},  /* 2: green   */
    {220, 180, 60,  255},  /* 3: yellow  */
    {180, 100, 220, 255},  /* 4: purple  */
    {220, 140, 60,  255},  /* 5: orange  */
    {100, 200, 200, 255},  /* 6: cyan    */
    {200, 120, 160, 255},  /* 7: pink    */
};

static const struct nk_color cell_inactive = {50, 50, 55, 255};

/* ─── State for right-click velocity/pitch editor ─────────────────────────── */

static int  popup_track = -1;
static int  popup_step  = -1;
static bool popup_open  = false;
static bool popup_just_opened = false; /* true on the frame we first open */

/* Track right-click state ourselves for reliable one-shot detection.
 * Nuklear's nk_input_mouse_clicked requires hovering at release time,
 * which fails if the mouse moves even 1 pixel between press and release. */
static bool rclick_was_down = false;

/* ─── State for left-click drag across pads ───────────────────────────────── */
static bool  drag_active     = false;  /* currently dragging? */
static bool  drag_set_on     = false;  /* true = turning on, false = turning off */
static int   drag_last_track = -1;     /* last toggled cell (avoid re-toggling same) */
static int   drag_last_step  = -1;

/* ─── Draw the grid ───────────────────────────────────────────────────────── */

void drum_grid_draw(struct nk_context *ctx, sq_engine_t *engine,
                    float x, float y, float w, float h)
{
    int pat_idx = engine->transport.current_pattern;
    if (pat_idx < 0 || (uint32_t)pat_idx >= engine->num_patterns) return;
    sq_pattern_t *pattern = &engine->patterns[pat_idx];
    if (pattern->num_tracks == 0) return;

    /* Layout constants */
    float track_panel_w = 160.0f;  /* width for track name + controls */
    float cell_pad = 2.0f;
    uint32_t num_steps = pattern->tracks[0].length;
    /* Account for window border/padding and per-cell padding in width calc */
    float grid_w = w - track_panel_w - 20.0f;
    float cell_w = grid_w / (float)num_steps;
    float row_h = 55.0f;
    /* Use the GUI's wall-clock-driven visual step instead of the audio
     * thread's current_step — avoids audio device/RDP/WSL latency issues */
    int current_step = g_visual_step;

    /* Main grid window — force bounds every frame so resizing works */
    nk_window_set_bounds(ctx, "DrumGrid", nk_rect(x, y, w, h));
    if (nk_begin(ctx, "DrumGrid",
                 nk_rect(x, y, w, h),
                 NK_WINDOW_BORDER))
    {
        /* Draw each track as a row, with a separator between types */
        bool drew_separator = false;
        for (uint32_t t = 0; t < pattern->num_tracks; t++) {
            sq_track_t *track = &pattern->tracks[t];
            struct nk_color base_color = track_colors[track->color_index % NUM_TRACK_COLORS];

            /* Draw separator line before first synth/SF2 track */
            if ((track->type == TRACK_SYNTH || track->type == TRACK_SF2) && !drew_separator) {
                drew_separator = true;
                nk_layout_row_dynamic(ctx, 2, 1);
                struct nk_rect sep = nk_widget_bounds(ctx);
                nk_spacing(ctx, 1);
                struct nk_command_buffer *canvas = nk_window_get_canvas(ctx);
                nk_fill_rect(canvas, sep, 0,
                             nk_rgba(100, 180, 255, 120));

                nk_layout_row_dynamic(ctx, 14, 1);
                nk_label(ctx, "── SYNTH ──", NK_TEXT_LEFT);
            }

            /* Row layout: track controls + step cells */
            nk_layout_row_begin(ctx, NK_STATIC, row_h - 4, (int)(1 + num_steps));

            /* ── Track controls column ─────────────────────────────── */
            nk_layout_row_push(ctx, track_panel_w - 10);

            /* Determine track display name */
            const char *track_name = "(empty)";
            if (track->type == TRACK_SYNTH &&
                track->synth_preset >= 0 &&
                (uint32_t)track->synth_preset < engine->num_synth_presets) {
                track_name = engine->synth_presets[track->synth_preset].name;
            } else if (track->type == TRACK_SAMPLER &&
                       track->sample_index >= 0 &&
                       (uint32_t)track->sample_index < engine->num_samples) {
                track_name = engine->samples[track->sample_index].name;
            } else if (track->type == TRACK_SF2 &&
                       track->sf2_preset >= 0 &&
                       (uint32_t)track->sf2_preset < engine->num_sf2_presets) {
                track_name = engine->sf2_presets[track->sf2_preset].name;
            }

            /* Unique group name per track (track_name may repeat) */
            char group_id[64];
            snprintf(group_id, sizeof(group_id), "Track_%u", t);

            if (nk_group_begin(ctx, group_id, NK_WINDOW_NO_SCROLLBAR))
            {
                nk_layout_row_dynamic(ctx, 14, 3);

                /* Track name — left-click to select, right-click to cycle color */
                {
                    struct nk_style_button name_style = ctx->style.button;
                    /* Tint name button background with track color */
                    struct nk_color tint = base_color;
                    if (g_selected_track == (int)t) {
                        name_style.normal = nk_style_item_color(nk_rgba(tint.r/3+30, tint.g/3+30, tint.b/3+60, 255));
                        name_style.hover  = nk_style_item_color(nk_rgba(tint.r/3+40, tint.g/3+40, tint.b/3+70, 255));
                    } else {
                        name_style.normal = nk_style_item_color(nk_rgba(tint.r/5+35, tint.g/5+35, tint.b/5+38, 255));
                        name_style.hover  = nk_style_item_color(nk_rgba(tint.r/5+45, tint.g/5+45, tint.b/5+48, 255));
                    }
                    name_style.border = 0;
                    name_style.rounding = 2.0f;
                    struct nk_rect name_bounds = nk_widget_bounds(ctx);
                    if (nk_button_label_styled(ctx, &name_style, track_name)) {
                        g_selected_track = (g_selected_track == (int)t) ? -1 : (int)t;
                        LOG_DEBUG("Selected track %d", g_selected_track);
                    }
                    /* Right-click on track name: cycle through colors */
                    {
                        bool rb = nk_input_is_mouse_down(&ctx->input, NK_BUTTON_RIGHT);
                        if (rb && !rclick_was_down &&
                            nk_input_is_mouse_hovering_rect(&ctx->input, name_bounds)) {
                            track->color_index = (track->color_index + 1) % NUM_TRACK_COLORS;
                            LOG_DEBUG("Track %u color -> %d", t, track->color_index);
                        }
                    }
                }

                /* Mute button */
                {
                    struct nk_style_button mute_style = ctx->style.button;
                    if (track->mute) {
                        mute_style.normal = nk_style_item_color(nk_rgba(180, 60, 60, 255));
                        mute_style.hover  = nk_style_item_color(nk_rgba(200, 80, 80, 255));
                        mute_style.active = nk_style_item_color(nk_rgba(160, 40, 40, 255));
                    }
                    if (nk_button_label_styled(ctx, &mute_style, "M")) {
                        track->mute = !track->mute;
                    }
                }

                /* Solo button */
                {
                    struct nk_style_button solo_style = ctx->style.button;
                    if (track->solo) {
                        solo_style.normal = nk_style_item_color(nk_rgba(60, 160, 60, 255));
                        solo_style.hover  = nk_style_item_color(nk_rgba(80, 180, 80, 255));
                        solo_style.active = nk_style_item_color(nk_rgba(40, 140, 40, 255));
                    }
                    if (nk_button_label_styled(ctx, &solo_style, "S")) {
                        track->solo = !track->solo;
                    }
                }

                /* Volume + Humanize arc knobs */
                nk_layout_row_dynamic(ctx, 12, 4);
                nk_labelf(ctx, NK_TEXT_LEFT, "V:%.0f", track->volume * 100);
                knob_inline(ctx, &track->volume, 0.0f, 1.0f, 0.8f, 0.01f);
                nk_labelf(ctx, NK_TEXT_LEFT, "H:%.0f%%", track->humanize * 100);
                knob_inline(ctx, &track->humanize, 0.0f, 1.0f, 0.0f, 0.01f);

                /* Track type toggle (Sampler / Synth / SF2) */
                nk_layout_row_dynamic(ctx, 12, 1);
                {
                    const char *type_label;
                    struct nk_style_button type_style = ctx->style.button;
                    if (track->type == TRACK_SYNTH) {
                        type_label = "Synth";
                        type_style.normal = nk_style_item_color(nk_rgba(80, 60, 160, 255));
                        type_style.hover  = nk_style_item_color(nk_rgba(100, 80, 180, 255));
                    } else if (track->type == TRACK_SF2) {
                        type_label = "SF2";
                        type_style.normal = nk_style_item_color(nk_rgba(160, 120, 40, 255));
                        type_style.hover  = nk_style_item_color(nk_rgba(180, 140, 60, 255));
                    } else {
                        type_label = "Sampler";
                        type_style.normal = nk_style_item_color(nk_rgba(60, 100, 60, 255));
                        type_style.hover  = nk_style_item_color(nk_rgba(80, 120, 80, 255));
                    }
                    type_style.rounding = 2.0f;
                    if (nk_button_label_styled(ctx, &type_style, type_label)) {
                        if (track->type == TRACK_SAMPLER) {
                            track->type = TRACK_SYNTH;
                            if (track->synth_preset < 0)
                                track->synth_preset = 0;
                            LOG_INFO("Track %u -> SYNTH (preset %d)",
                                     t, track->synth_preset);
                        } else if (track->type == TRACK_SYNTH) {
                            track->type = TRACK_SF2;
                            if (track->sf2_preset < 0)
                                track->sf2_preset = 0;
                            LOG_INFO("Track %u -> SF2", t);
                        } else {
                            track->type = TRACK_SAMPLER;
                            LOG_INFO("Track %u -> SAMPLER", t);
                        }
                    }
                }

                /* Sample/Preset selector */
                if (track->type == TRACK_SAMPLER && engine->num_samples > 0) {
                    /* Build sample name list */
                    static const char *sample_names[SQ_MAX_SAMPLES];
                    for (uint32_t si = 0; si < engine->num_samples; si++)
                        sample_names[si] = engine->samples[si].name;

                    nk_layout_row_dynamic(ctx, 12, 1);
                    int sel = track->sample_index;
                    if (sel < 0) sel = 0;
                    nk_combobox(ctx, sample_names, (int)engine->num_samples,
                                &sel, 12, nk_vec2(140, 120));
                    track->sample_index = sel;
                } else if (track->type == TRACK_SYNTH && engine->num_synth_presets > 0) {
                    static const char *preset_names[SQ_MAX_SYNTH_PRESETS];
                    for (uint32_t pi = 0; pi < engine->num_synth_presets; pi++)
                        preset_names[pi] = engine->synth_presets[pi].name;

                    nk_layout_row_dynamic(ctx, 12, 1);
                    int sel = track->synth_preset;
                    if (sel < 0) sel = 0;
                    nk_combobox(ctx, preset_names, (int)engine->num_synth_presets,
                                &sel, 12, nk_vec2(140, 120));
                    track->synth_preset = sel;
                } else if (track->type == TRACK_SF2 && engine->num_sf2_presets > 0) {
                    static const char *sf2_names[SQ_MAX_SF2_PRESETS];
                    for (uint32_t pi = 0; pi < engine->num_sf2_presets; pi++)
                        sf2_names[pi] = engine->sf2_presets[pi].name;

                    nk_layout_row_dynamic(ctx, 12, 1);
                    int sel = track->sf2_preset;
                    if (sel < 0) sel = 0;
                    nk_combobox(ctx, sf2_names, (int)engine->num_sf2_presets,
                                &sel, 12, nk_vec2(140, 120));
                    track->sf2_preset = sel;
                }

                nk_group_end(ctx);
            }

            /* ── Step cells ────────────────────────────────────────── */
            for (uint32_t s = 0; s < num_steps; s++) {
                nk_layout_row_push(ctx, cell_w - cell_pad);

                sq_step_t *step = &track->steps[s];
                bool is_active = (step->velocity > 0);
                bool is_playhead = (engine->transport.playing &&
                                    (int)s == current_step);

                /* Determine cell color */
                struct nk_color cell_color;
                if (is_active) {
                    /* Active: track color, brightness scaled by velocity */
                    float bright = 0.3f + 0.7f * ((float)step->velocity / 127.0f);
                    cell_color.r = (nk_byte)(base_color.r * bright);
                    cell_color.g = (nk_byte)(base_color.g * bright);
                    cell_color.b = (nk_byte)(base_color.b * bright);
                    cell_color.a = 255;
                } else {
                    cell_color = cell_inactive;
                }

                /* Playhead highlight: brighten the current step column */
                if (is_playhead) {
                    cell_color.r = (nk_byte)((int)cell_color.r + 40 > 255 ? 255 : cell_color.r + 40);
                    cell_color.g = (nk_byte)((int)cell_color.g + 40 > 255 ? 255 : cell_color.g + 40);
                    cell_color.b = (nk_byte)((int)cell_color.b + 40 > 255 ? 255 : cell_color.b + 40);
                }

                /* Draw the cell as a colored button */
                {
                    struct nk_style_button cell_style = ctx->style.button;
                    cell_style.normal  = nk_style_item_color(cell_color);
                    cell_style.hover   = nk_style_item_color(
                        nk_rgba(cell_color.r + 20, cell_color.g + 20,
                                cell_color.b + 20, 255));
                    cell_style.active  = nk_style_item_color(
                        nk_rgba(cell_color.r + 30, cell_color.g + 30,
                                cell_color.b + 30, 255));
                    cell_style.border_color = nk_rgba(40, 40, 45, 255);
                    cell_style.border  = 1.0f;
                    cell_style.rounding = 3.0f;

                    /* Cell label: show velocity if active */
                    char label[16] = "";
                    if (is_active && step->velocity > 0) {
                        snprintf(label, sizeof(label), "%d", step->velocity);
                    }

                    /* Get bounds BEFORE the button so we have the correct rect */
                    struct nk_rect bounds = nk_widget_bounds(ctx);

                    /* Draw the cell as a styled button (display only — no toggle logic here) */
                    nk_button_label_styled(ctx, &cell_style, label);

                    /* Left-click drag: toggle pads as mouse drags across them */
                    {
                        bool lb_down = nk_input_is_mouse_down(&ctx->input, NK_BUTTON_LEFT);
                        bool in_bounds = nk_input_is_mouse_hovering_rect(&ctx->input, bounds);

                        if (lb_down && in_bounds) {
                            if (!drag_active) {
                                /* Start of a new drag — decide direction based on first cell */
                                drag_active = true;
                                drag_set_on = !is_active; /* if OFF, we turn ON; if ON, turn OFF */
                                drag_last_track = -1;
                                drag_last_step = -1;
                            }

                            /* Only toggle if this is a new cell (avoid re-toggling same cell) */
                            if ((int)t != drag_last_track || (int)s != drag_last_step) {
                                if (drag_set_on && !is_active) {
                                    undo_push(engine);
                                    step->velocity = 100;
                                } else if (!drag_set_on && is_active) {
                                    undo_push(engine);
                                    step->velocity = 0;
                                }
                                drag_last_track = (int)t;
                                drag_last_step = (int)s;
                            }
                        }
                    }

                    /* Right click: detect using one-shot press detection. */
                    {
                        bool rb_down = nk_input_is_mouse_down(&ctx->input, NK_BUTTON_RIGHT);
                        bool in_bounds = nk_input_is_mouse_hovering_rect(&ctx->input, bounds);

                        if (!popup_open && rb_down && !rclick_was_down && in_bounds) {
                            if (!is_active) {
                                step->velocity = 100;
                            }
                            popup_track = (int)t;
                            popup_step  = (int)s;
                            popup_open  = true;
                            popup_just_opened = true;
                            LOG_DEBUG("RIGHT click: track %u step %u -> popup (vel=%d)",
                                     t, s, step->velocity);
                        }
                    }

                    /* Scroll wheel: adjust velocity when hovering over a cell */
                    if (nk_input_is_mouse_hovering_rect(&ctx->input, bounds)) {
                        float scroll = ctx->input.mouse.scroll_delta.y;
                        if (scroll != 0.0f) {
                            int new_vel = (int)step->velocity + (int)(scroll * 5);
                            if (new_vel < 0) new_vel = 0;
                            if (new_vel > 127) new_vel = 127;
                            step->velocity = (uint8_t)new_vel;
                            LOG_DEBUG("SCROLL: track %u step %u -> vel=%d",
                                     t, s, new_vel);
                        }
                    }
                }
            }

            nk_layout_row_end(ctx);
        }

        /* ── Smooth playhead line (TASK-206) ───────────────────────────── */
        if (engine->transport.playing) {
            struct nk_command_buffer *canvas = nk_window_get_canvas(ctx);
            struct nk_rect win_bounds = nk_window_get_bounds(ctx);
            /* Compute fractional position within the current step */
            double beats_per_step = 0.25; /* 16th notes */
            double step_frac = fmod(engine->transport.current_beat, beats_per_step) / beats_per_step;
            if (step_frac < 0.0) step_frac = 0.0;
            if (step_frac > 1.0) step_frac = 1.0;
            float playhead_pos = (float)current_step + (float)step_frac;
            /* Grid starts after the track panel */
            float grid_start_x = win_bounds.x + track_panel_w;
            float playhead_x = grid_start_x + playhead_pos * cell_w;
            float grid_top = win_bounds.y;
            float grid_bot = win_bounds.y + win_bounds.h;
            nk_stroke_line(canvas, playhead_x, grid_top,
                           playhead_x, grid_bot,
                           2.0f, nk_rgba(255, 255, 255, 180));
        }
    }
    nk_end(ctx);

    /* Update right-click state for one-shot detection */
    rclick_was_down = nk_input_is_mouse_down(&ctx->input, NK_BUTTON_RIGHT);

    /* Reset drag state when left mouse is released */
    if (!nk_input_is_mouse_down(&ctx->input, NK_BUTTON_LEFT)) {
        drag_active = false;
    }

    /* ── Velocity / Pitch editor (separate window) ───────────────────────── */
    if (popup_open && popup_track >= 0 && popup_step >= 0) {
        sq_track_t *pt = &pattern->tracks[popup_track];
        sq_step_t *ps = &pt->steps[popup_step];
        bool want_close = false;

        /* Only set position + focus on the frame we first open */
        if (popup_just_opened) {
            popup_just_opened = false;
            nk_window_show(ctx, "StepEdit", NK_SHOWN);
            nk_window_set_focus(ctx, "StepEdit");
            struct nk_vec2 pos = {(float)g_win_width / 2 - 130,
                                  (float)g_win_height / 2 - 90};
            nk_window_set_position(ctx, "StepEdit", pos);
        }

        if (nk_begin(ctx, "StepEdit",
                     nk_rect((float)g_win_width / 2 - 130,
                             (float)g_win_height / 2 - 90, 260, 180),
                     NK_WINDOW_TITLE | NK_WINDOW_BORDER |
                     NK_WINDOW_MOVABLE | NK_WINDOW_CLOSABLE))
        {
            /* Header: which step we're editing */
            nk_layout_row_dynamic(ctx, 20, 1);
            nk_labelf(ctx, NK_TEXT_CENTERED, "Track %d, Step %d",
                      popup_track + 1, popup_step + 1);

            nk_layout_row_dynamic(ctx, 25, 2);

            /* Velocity slider */
            nk_label(ctx, "Velocity:", NK_TEXT_LEFT);
            int vel = ps->velocity;
            nk_slider_int(ctx, 1, &vel, 127, 1);
            ps->velocity = (uint8_t)vel;

            /* Pitch offset slider */
            nk_label(ctx, "Pitch:", NK_TEXT_LEFT);
            int pitch = ps->pitch_offset;
            nk_slider_int(ctx, -24, &pitch, 24, 1);
            ps->pitch_offset = (int8_t)pitch;

            /* Display values */
            nk_layout_row_dynamic(ctx, 20, 1);
            nk_labelf(ctx, NK_TEXT_CENTERED, "Vel: %d  |  Pitch: %+d st",
                      ps->velocity, ps->pitch_offset);

            /* Close button */
            nk_layout_row_dynamic(ctx, 25, 1);
            if (nk_button_label(ctx, "Done")) {
                want_close = true;
            }
        } else {
            /* nk_begin returned false = X button was clicked */
            want_close = true;
        }
        /* Capture StepEdit bounds BEFORE nk_end (while window is current) */
        struct nk_rect popup_bounds = nk_window_get_bounds(ctx);
        bool was_just_opened = popup_just_opened;
        nk_end(ctx);

        /* Close if user clicked anywhere outside the StepEdit window.
         * Skip on the frame we just opened — the right-click that opened
         * the popup would immediately trigger the close detection. */
        if (!want_close && !was_just_opened) {
            bool any_click = nk_input_is_mouse_pressed(&ctx->input, NK_BUTTON_LEFT);
            if (any_click) {
                struct nk_vec2 mouse = ctx->input.mouse.pos;
                if (!NK_INBOX(mouse.x, mouse.y, popup_bounds.x, popup_bounds.y,
                              popup_bounds.w, popup_bounds.h)) {
                    want_close = true;
                }
            }
        }

        /* Clean up after nk_end so we're not modifying an active window */
        if (want_close) {
            LOG_DEBUG("popup CLOSING (track=%d step=%d)",
                     popup_track, popup_step);
            nk_window_close(ctx, "StepEdit");
            popup_open = false;
            popup_track = -1;
            popup_step  = -1;
            /* Return focus to the drum grid so clicks work again */
            nk_window_set_focus(ctx, "DrumGrid");
        }
    }
}
