/*
 * knobs.c — Custom rotary knob widget with arc rendering.
 *
 * Draws circular arcs using Nuklear canvas. The arc sweeps 270 degrees
 * from ~135 degrees (bottom-left) to ~405 degrees (bottom-right).
 *
 * The knob supports:
 * - Vertical drag to change value
 * - Shift+drag for fine adjustment (1/10th rate)
 * - Double-click to reset to default
 * - Value text display
 */

#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#include "nuklear.h"

#include "gui/knobs.h"

#include <SDL.h>
#include <math.h>

/* Arc angles in radians.
 * Start at bottom-left (135 deg = 2.356 rad), sweep clockwise 270 degrees
 * to bottom-right (405 deg = -0.785 rad in our coordinate system).
 * We use math convention: angles measured counter-clockwise from +X axis,
 * but screen Y is flipped so we negate sin for Y coords. */
#define ARC_START_ANGLE  2.356194f   /* 135 degrees */
#define ARC_END_ANGLE   -0.785398f   /* -45 degrees (= 315 degrees going CW) */
#define ARC_RANGE        3.141593f   /* 270 degrees in radians */
#define ARC_SEGMENTS     32

/* Colors */
static const struct nk_color knob_bg_color    = {50, 50, 55, 255};
static const struct nk_color knob_track_color = {35, 35, 40, 255};
static const struct nk_color knob_accent      = {80, 180, 220, 255};
static const struct nk_color knob_dot_color   = {220, 220, 230, 255};

/* ─── Internal: draw an arc on the canvas ──────────────────────────────────── */

static void draw_arc(struct nk_command_buffer *canvas,
                     float cx, float cy, float radius,
                     float from_angle, float to_angle,
                     int segments, float thickness,
                     struct nk_color color)
{
    if (segments < 1) return;
    float range = from_angle - to_angle;
    for (int i = 0; i < segments; i++) {
        float a1 = from_angle - (float)i / (float)segments * range;
        float a2 = from_angle - (float)(i + 1) / (float)segments * range;
        float x1 = cx + radius * cosf(a1);
        float y1 = cy - radius * sinf(a1);
        float x2 = cx + radius * cosf(a2);
        float y2 = cy - radius * sinf(a2);
        nk_stroke_line(canvas, x1, y1, x2, y2, thickness, color);
    }
}

/* ─── Internal: shared knob logic ──────────────────────────────────────────── */

/*
 * Draw and interact with an arc knob at the given widget bounds.
 * Returns 1 if value changed.
 */
static int knob_core(struct nk_context *ctx,
                     float *value, float min, float max,
                     float default_val, float step,
                     struct nk_rect bounds)
{
    float old_val = *value;
    struct nk_command_buffer *canvas = nk_window_get_canvas(ctx);

    /* Knob geometry */
    float knob_size = bounds.w < bounds.h ? bounds.w : bounds.h;
    float radius = knob_size * 0.4f;
    float cx = bounds.x + bounds.w * 0.5f;
    float cy = bounds.y + bounds.h * 0.5f;

    /* Normalized value 0..1 */
    float norm = 0.0f;
    if (max > min)
        norm = (*value - min) / (max - min);
    if (norm < 0.0f) norm = 0.0f;
    if (norm > 1.0f) norm = 1.0f;

    /* Draw filled circle background */
    nk_fill_circle(canvas,
                   nk_rect(cx - radius - 2, cy - radius - 2,
                           (radius + 2) * 2, (radius + 2) * 2),
                   knob_bg_color);

    /* Draw background track arc (full sweep, dark) */
    draw_arc(canvas, cx, cy, radius,
             ARC_START_ANGLE, ARC_END_ANGLE,
             ARC_SEGMENTS, 3.0f, knob_track_color);

    /* Draw value arc (partial sweep, colored) */
    if (norm > 0.001f) {
        float value_angle = ARC_START_ANGLE - (norm * ARC_RANGE);
        int val_segs = (int)(norm * ARC_SEGMENTS);
        if (val_segs < 1) val_segs = 1;
        draw_arc(canvas, cx, cy, radius,
                 ARC_START_ANGLE, value_angle,
                 val_segs, 3.0f, knob_accent);
    }

    /* Draw indicator dot at current value position */
    {
        float dot_angle = ARC_START_ANGLE - (norm * ARC_RANGE);
        float dot_r = radius * 0.7f;
        float dx = cx + dot_r * cosf(dot_angle);
        float dy = cy - dot_r * sinf(dot_angle);
        nk_fill_circle(canvas,
                       nk_rect(dx - 2, dy - 2, 4, 4),
                       knob_dot_color);
    }

    /* ─── Mouse interaction ────────────────────────────────────────────── */

    /* Check shift for fine adjustment */
    float actual_step = step;
    const Uint8 *keys = SDL_GetKeyboardState(NULL);
    if (keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT]) {
        actual_step = step * 0.1f;
    }

    /* Vertical drag: mouse delta Y changes value */
    if (nk_input_is_mouse_hovering_rect(&ctx->input, bounds)) {
        if (nk_input_is_mouse_down(&ctx->input, NK_BUTTON_LEFT)) {
            float dy = ctx->input.mouse.delta.y;
            if (dy != 0.0f) {
                /* Drag up (negative dy) = increase value */
                *value -= dy * actual_step * 0.5f;
                if (*value < min) *value = min;
                if (*value > max) *value = max;
            }
        }

        /* Double-click to reset */
        if (nk_input_is_mouse_click_in_rect(&ctx->input, NK_BUTTON_DOUBLE,
                                             bounds)) {
            *value = default_val;
        }
    }

    return (*value != old_val) ? 1 : 0;
}

/* ─── Public API ───────────────────────────────────────────────────────────── */

int knob_float(struct nk_context *ctx, const char *label,
               float *value, float min, float max,
               float default_val, float step)
{
    /* Label row */
    nk_layout_row_dynamic(ctx, 14, 1);
    nk_labelf(ctx, NK_TEXT_CENTERED, "%s", label);

    /* Knob drawing area */
    nk_layout_row_dynamic(ctx, 40, 1);
    struct nk_rect bounds = nk_widget_bounds(ctx);
    /* Reserve the widget space */
    nk_spacing(ctx, 1);

    int changed = knob_core(ctx, value, min, max, default_val, step, bounds);

    /* Value display */
    nk_layout_row_dynamic(ctx, 12, 1);
    nk_labelf(ctx, NK_TEXT_CENTERED, "%.1f", *value);

    return changed;
}

int knob_mini(struct nk_context *ctx, const char *label,
              float *value, float min, float max,
              float default_val, float step,
              float total_height)
{
    /* Allocate a single widget region for the mini knob */
    float knob_h = total_height - 10.0f; /* reserve space for label */
    if (knob_h < 16.0f) knob_h = 16.0f;

    /* Label */
    nk_layout_row_dynamic(ctx, 10, 1);
    nk_labelf(ctx, NK_TEXT_CENTERED, "%s", label);

    /* Knob area */
    nk_layout_row_dynamic(ctx, knob_h, 1);
    struct nk_rect bounds = nk_widget_bounds(ctx);
    nk_spacing(ctx, 1);

    return knob_core(ctx, value, min, max, default_val, step, bounds);
}

int knob_inline(struct nk_context *ctx,
                float *value, float min, float max,
                float default_val, float step)
{
    /* Get bounds for the next widget slot in the current layout row,
     * then consume the slot with nk_spacing. */
    struct nk_rect bounds = nk_widget_bounds(ctx);
    nk_spacing(ctx, 1);
    return knob_core(ctx, value, min, max, default_val, step, bounds);
}
