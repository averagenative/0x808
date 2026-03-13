/*
 * mixer_view.c — Mixer view with master effects controls and LED level meters.
 *
 * Shows master effects chain (3 slots) with type selector and parameter knobs,
 * plus per-track and master LED-style level meters.
 */

#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#include "nuklear.h"

#include "gui/mixer_view.h"
#include "engine/effects.h"

#define LOG_TAG "mixer"
#include "core/log.h"

#include <stdio.h>
#include <string.h>

static const char *effect_type_names[] = {"None", "Filter", "Delay", "Reverb"};
static const char *filter_mode_names[] = {"LowPass", "HiPass", "BandPass"};
static const char *delay_sync_names[] = {"1/1", "1/2", "1/4", "1/8", "1/16"};

/* ─── LED-style level meter ────────────────────────────────────────────────── */

static void draw_level_meter(struct nk_command_buffer *canvas,
                             struct nk_rect bounds, float level)
{
    /* Background */
    nk_fill_rect(canvas, bounds, 1, nk_rgba(20, 20, 25, 255));

    /* Clamp level */
    if (level > 1.5f) level = 1.5f;
    if (level < 0.0f) level = 0.0f;

    /* Draw from bottom up in segments */
    int segs = 12;
    float seg_gap = 1.0f;
    float seg_h = (bounds.h - seg_gap * (segs - 1)) / (float)segs;
    if (seg_h < 1.0f) seg_h = 1.0f;

    for (int i = 0; i < segs; i++) {
        float seg_y = bounds.y + bounds.h - (float)(i + 1) * (seg_h + seg_gap);
        float seg_level = (float)(i + 1) / (float)segs;

        struct nk_color c;
        if (seg_level <= level) {
            /* Lit segment */
            if (seg_level < 0.6f)
                c = nk_rgba(40, 180, 40, 220);       /* green */
            else if (seg_level < 0.85f)
                c = nk_rgba(200, 200, 40, 220);      /* yellow */
            else
                c = nk_rgba(220, 40, 40, 220);       /* red */
        } else {
            /* Unlit segment (dim) */
            if (seg_level < 0.6f)
                c = nk_rgba(15, 40, 15, 255);
            else if (seg_level < 0.85f)
                c = nk_rgba(40, 40, 15, 255);
            else
                c = nk_rgba(40, 15, 15, 255);
        }

        nk_fill_rect(canvas,
                     nk_rect(bounds.x, seg_y, bounds.w, seg_h),
                     0, c);
    }
}

/* ─── Effect slot drawing ──────────────────────────────────────────────────── */

static void draw_effect_slot(struct nk_context *ctx, sq_effect_slot_t *slot,
                             const char *label, uint32_t sample_rate)
{
    nk_layout_row_dynamic(ctx, 16, 1);
    nk_label(ctx, label, NK_TEXT_LEFT);

    /* Type selector */
    nk_layout_row_dynamic(ctx, 18, 2);
    nk_label(ctx, "Type:", NK_TEXT_LEFT);
    {
        int t = (int)slot->type;
        nk_combobox(ctx, effect_type_names, EFFECT_TYPE_COUNT, &t, 18,
                    nk_vec2(100, 80));
        if ((sq_effect_type_t)t != slot->type) {
            effect_init(slot, (sq_effect_type_t)t, sample_rate);
        }
    }

    if (slot->type == EFFECT_NONE) return;

    /* Bypass toggle */
    nk_layout_row_dynamic(ctx, 16, 2);
    nk_label(ctx, "Bypass:", NK_TEXT_LEFT);
    {
        int bp = slot->bypass ? 1 : 0;
        nk_checkbox_label(ctx, "", &bp);
        slot->bypass = (bp != 0);
    }

    /* Type-specific parameters */
    switch (slot->type) {
    case EFFECT_FILTER:
        nk_layout_row_dynamic(ctx, 18, 2);
        nk_label(ctx, "Mode:", NK_TEXT_LEFT);
        {
            int m = (int)slot->filter.mode;
            nk_combobox(ctx, filter_mode_names, 3, &m, 18, nk_vec2(100, 60));
            slot->filter.mode = (sq_efx_filter_mode_t)m;
        }

        nk_layout_row_dynamic(ctx, 18, 2);
        nk_labelf(ctx, NK_TEXT_LEFT, "Cutoff: %.0f", slot->filter.cutoff);
        nk_slider_float(ctx, 20.0f, &slot->filter.cutoff, 20000.0f, 10.0f);

        nk_layout_row_dynamic(ctx, 18, 2);
        nk_labelf(ctx, NK_TEXT_LEFT, "Reso: %.1f", slot->filter.resonance);
        nk_slider_float(ctx, 0.5f, &slot->filter.resonance, 20.0f, 0.1f);

        nk_layout_row_dynamic(ctx, 18, 2);
        nk_labelf(ctx, NK_TEXT_LEFT, "Wet: %.0f%%", slot->filter.wet * 100);
        nk_slider_float(ctx, 0.0f, &slot->filter.wet, 1.0f, 0.01f);
        break;

    case EFFECT_DELAY:
        nk_layout_row_dynamic(ctx, 18, 2);
        nk_labelf(ctx, NK_TEXT_LEFT, "Time: %.3fs", slot->delay.time);
        nk_slider_float(ctx, 0.01f, &slot->delay.time, 2.0f, 0.01f);

        nk_layout_row_dynamic(ctx, 18, 2);
        nk_labelf(ctx, NK_TEXT_LEFT, "Feedback: %.0f%%", slot->delay.feedback * 100);
        nk_slider_float(ctx, 0.0f, &slot->delay.feedback, 0.95f, 0.01f);

        nk_layout_row_dynamic(ctx, 18, 2);
        nk_labelf(ctx, NK_TEXT_LEFT, "Wet: %.0f%%", slot->delay.wet * 100);
        nk_slider_float(ctx, 0.0f, &slot->delay.wet, 1.0f, 0.01f);

        nk_layout_row_dynamic(ctx, 16, 2);
        nk_label(ctx, "BPM Sync:", NK_TEXT_LEFT);
        {
            int sync = slot->delay.bpm_sync ? 1 : 0;
            nk_checkbox_label(ctx, "", &sync);
            slot->delay.bpm_sync = (sync != 0);
        }
        if (slot->delay.bpm_sync) {
            nk_layout_row_dynamic(ctx, 18, 2);
            nk_label(ctx, "Div:", NK_TEXT_LEFT);
            nk_combobox(ctx, delay_sync_names, 5, &slot->delay.sync_division,
                        18, nk_vec2(80, 100));
        }
        break;

    case EFFECT_REVERB:
        nk_layout_row_dynamic(ctx, 18, 2);
        nk_labelf(ctx, NK_TEXT_LEFT, "Room: %.0f%%", slot->reverb.room_size * 100);
        nk_slider_float(ctx, 0.0f, &slot->reverb.room_size, 1.0f, 0.01f);

        nk_layout_row_dynamic(ctx, 18, 2);
        nk_labelf(ctx, NK_TEXT_LEFT, "Damp: %.0f%%", slot->reverb.damping * 100);
        nk_slider_float(ctx, 0.0f, &slot->reverb.damping, 1.0f, 0.01f);

        nk_layout_row_dynamic(ctx, 18, 2);
        nk_labelf(ctx, NK_TEXT_LEFT, "Wet: %.0f%%", slot->reverb.wet * 100);
        nk_slider_float(ctx, 0.0f, &slot->reverb.wet, 1.0f, 0.01f);
        break;

    default:
        break;
    }
}

/* ─── Main mixer view ──────────────────────────────────────────────────────── */

void mixer_view_draw(struct nk_context *ctx, sq_engine_t *engine,
                     float x, float y, float w, float h)
{
    if (nk_begin(ctx, "MasterFX",
                 nk_rect(x, y, w, h),
                 NK_WINDOW_BORDER | NK_WINDOW_TITLE | NK_WINDOW_NO_SCROLLBAR))
    {
        struct nk_command_buffer *canvas = nk_window_get_canvas(ctx);

        /* Top section: level meters and effects side by side.
         * Layout: [Track Meters | Effect Slots | Master Meter] */
        float meter_panel_w = 180.0f;
        float master_meter_w = 50.0f;
        float effects_w = w - meter_panel_w - master_meter_w - 30.0f;
        if (effects_w < 200.0f) effects_w = 200.0f;

        float content_h = h - 40.0f;

        /* Use a static row to place the three sections */
        nk_layout_row_begin(ctx, NK_STATIC, content_h, 3);

        /* ── Section 1: Per-track level meters ─────────────────────── */
        nk_layout_row_push(ctx, meter_panel_w);
        if (nk_group_begin(ctx, "TrackMeters", NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR)) {
            int pat_idx = engine->transport.current_pattern;
            uint32_t num_tracks = 0;
            if (pat_idx >= 0 && (uint32_t)pat_idx < engine->num_patterns) {
                num_tracks = engine->patterns[pat_idx].num_tracks;
            }

            nk_layout_row_dynamic(ctx, 14, 1);
            nk_label(ctx, "Track Levels", NK_TEXT_CENTERED);

            if (num_tracks > 0) {
                /* Draw meters in a horizontal row */
                float meter_w = (meter_panel_w - 20.0f) / (float)num_tracks;
                if (meter_w > 16.0f) meter_w = 16.0f;
                float meter_h = content_h - 50.0f;
                if (meter_h < 40.0f) meter_h = 40.0f;

                /* Meter row */
                nk_layout_row_static(ctx, meter_h, (int)meter_w, (int)num_tracks);
                for (uint32_t t = 0; t < num_tracks; t++) {
                    struct nk_rect mb = nk_widget_bounds(ctx);
                    nk_spacing(ctx, 1);
                    draw_level_meter(canvas, mb, engine->track_peaks[t]);
                }

                /* Track number labels */
                nk_layout_row_static(ctx, 12, (int)meter_w, (int)num_tracks);
                for (uint32_t t = 0; t < num_tracks; t++) {
                    char num[4];
                    snprintf(num, sizeof(num), "%u", t + 1);
                    nk_label(ctx, num, NK_TEXT_CENTERED);
                }
            }

            nk_group_end(ctx);
        }

        /* ── Section 2: Master effects slots ───────────────────────── */
        nk_layout_row_push(ctx, effects_w);
        if (nk_group_begin(ctx, "EffectsPanel", NK_WINDOW_NO_SCROLLBAR)) {
            nk_layout_row_dynamic(ctx, content_h - 10, MAX_TRACK_EFFECTS);

            for (int i = 0; i < MAX_TRACK_EFFECTS; i++) {
                char slot_label[32];
                snprintf(slot_label, sizeof(slot_label), "Slot %d", i + 1);
                char group_name[32];
                snprintf(group_name, sizeof(group_name), "MFX_%d", i);

                if (nk_group_begin(ctx, group_name, NK_WINDOW_BORDER)) {
                    draw_effect_slot(ctx, &engine->master_effects[i],
                                     slot_label, engine->sample_rate);
                    nk_group_end(ctx);
                }
            }
            nk_group_end(ctx);
        }

        /* ── Section 3: Master output level meter ──────────────────── */
        nk_layout_row_push(ctx, master_meter_w);
        if (nk_group_begin(ctx, "MasterMeter", NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR)) {
            nk_layout_row_dynamic(ctx, 14, 1);
            nk_label(ctx, "MST", NK_TEXT_CENTERED);

            float meter_h = content_h - 50.0f;
            if (meter_h < 40.0f) meter_h = 40.0f;

            /* Two meters side by side: L and R */
            nk_layout_row_static(ctx, meter_h, 14, 2);
            {
                struct nk_rect ml = nk_widget_bounds(ctx);
                nk_spacing(ctx, 1);
                draw_level_meter(canvas, ml, engine->master_peak[0]);
            }
            {
                struct nk_rect mr = nk_widget_bounds(ctx);
                nk_spacing(ctx, 1);
                draw_level_meter(canvas, mr, engine->master_peak[1]);
            }

            /* L/R labels */
            nk_layout_row_static(ctx, 12, 14, 2);
            nk_label(ctx, "L", NK_TEXT_CENTERED);
            nk_label(ctx, "R", NK_TEXT_CENTERED);

            nk_group_end(ctx);
        }

        nk_layout_row_end(ctx);
    }
    nk_end(ctx);
}
