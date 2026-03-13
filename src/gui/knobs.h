/*
 * knobs.h — Custom rotary knob widget for Nuklear.
 *
 * Provides arc/radial knobs that respond to vertical mouse drag.
 * Drag up = increase, drag down = decrease.
 * Shift+drag = fine adjustment (1/10th rate).
 */

#ifndef SQ_KNOBS_H
#define SQ_KNOBS_H

/* Forward declare Nuklear context */
struct nk_context;

/*
 * Draw a rotary knob with arc rendering.
 *
 * Returns 1 if the value changed, 0 otherwise.
 *
 * label:       text displayed below the knob
 * value:       pointer to the current value (modified by interaction)
 * min/max:     value range
 * default_val: value to reset to on double-click
 * step:        normal drag sensitivity
 */
int knob_float(struct nk_context *ctx, const char *label,
               float *value, float min, float max,
               float default_val, float step);

/*
 * Mini knob for tight spaces (e.g., drum grid track controls).
 * Draws a small arc knob (~25px) with label above.
 *
 * Returns 1 if the value changed, 0 otherwise.
 *
 * total_height: total height available for knob + label
 */
int knob_mini(struct nk_context *ctx, const char *label,
              float *value, float min, float max,
              float default_val, float step,
              float total_height);

/*
 * Inline knob: draws an arc knob in the next widget slot of the
 * current layout row. Call this where you would call nk_slider_float.
 * The widget bounds are obtained from nk_widget_bounds + nk_spacing.
 *
 * Returns 1 if the value changed, 0 otherwise.
 */
int knob_inline(struct nk_context *ctx,
                float *value, float min, float max,
                float default_val, float step);

#endif /* SQ_KNOBS_H */
