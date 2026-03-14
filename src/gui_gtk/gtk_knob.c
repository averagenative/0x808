/*
 * gtk_knob.c — Custom rotary knob widget using GtkDrawingArea + Cairo.
 */

#include "gtk_gui.h"
#include <math.h>

#define KNOB_RADIUS   20.0
#define KNOB_ARC_START (M_PI * 0.75)
#define KNOB_ARC_END   (M_PI * 2.25)

typedef struct {
    float *value;
    float  min, max;
    char   label[32];
    double drag_start_y;
    float  drag_start_val;
    gboolean dragging;
} knob_data_t;

static void knob_draw(GtkDrawingArea *area, cairo_t *cr,
                      int width, int height, gpointer user_data)
{
    (void)area;
    knob_data_t *kd = (knob_data_t *)user_data;

    double cx = width / 2.0;
    double cy = height / 2.0;
    double r = KNOB_RADIUS;
    if (r > cx - 2) r = cx - 2;
    if (r > cy - 8) r = cy - 8;

    /* Normalize value to 0-1 */
    float range = kd->max - kd->min;
    float norm = (range > 0) ? (*kd->value - kd->min) / range : 0;
    if (norm < 0) norm = 0;
    if (norm > 1) norm = 1;

    double angle = KNOB_ARC_START + norm * (KNOB_ARC_END - KNOB_ARC_START);

    /* Track arc (background) */
    cairo_set_source_rgba(cr, 0.2, 0.2, 0.25, 0.6);
    cairo_set_line_width(cr, 3.0);
    cairo_arc(cr, cx, cy, r, KNOB_ARC_START, KNOB_ARC_END);
    cairo_stroke(cr);

    /* Value arc — bipolar knobs (min < 0 < max) fill from center (top),
     * unipolar knobs fill from the start (lower-left) */
    cairo_set_source_rgba(cr, 0.2, 1.0, 0.2, 0.8);
    cairo_set_line_width(cr, 3.0);
    bool bipolar = (kd->min < 0 && kd->max > 0);
    if (bipolar) {
        double center_angle = KNOB_ARC_START +
            (-kd->min / range) * (KNOB_ARC_END - KNOB_ARC_START);
        if (angle > center_angle)
            cairo_arc(cr, cx, cy, r, center_angle, angle);
        else
            cairo_arc_negative(cr, cx, cy, r, center_angle, angle);
    } else {
        cairo_arc(cr, cx, cy, r, KNOB_ARC_START, angle);
    }
    cairo_stroke(cr);

    /* Knob circle */
    cairo_set_source_rgb(cr, 0.15, 0.15, 0.18);
    cairo_arc(cr, cx, cy, r * 0.7, 0, 2 * M_PI);
    cairo_fill(cr);

    /* Indicator line */
    double ix = cx + cos(angle) * r * 0.5;
    double iy = cy + sin(angle) * r * 0.5;
    cairo_set_source_rgba(cr, 0.2, 1.0, 0.2, 1.0);
    cairo_set_line_width(cr, 2.0);
    cairo_move_to(cr, cx, cy);
    cairo_line_to(cr, ix, iy);
    cairo_stroke(cr);

    /* Label */
    cairo_set_source_rgba(cr, 0.6, 0.6, 0.6, 0.9);
    cairo_set_font_size(cr, 8.0);
    cairo_text_extents_t te;
    cairo_text_extents(cr, kd->label, &te);
    cairo_move_to(cr, cx - te.width / 2, height - 1);
    cairo_show_text(cr, kd->label);
}

static void on_drag_begin(GtkGestureDrag *gesture, double x, double y,
                          gpointer user_data)
{
    (void)gesture; (void)x;
    knob_data_t *kd = (knob_data_t *)user_data;
    kd->dragging = TRUE;
    kd->drag_start_y = y;
    kd->drag_start_val = *kd->value;
}

static void on_drag_update(GtkGestureDrag *gesture, double dx, double dy,
                           gpointer user_data)
{
    (void)gesture; (void)dx;
    knob_data_t *kd = (knob_data_t *)user_data;
    if (!kd->dragging) return;

    float range = kd->max - kd->min;
    float delta = (float)(-dy / 150.0) * range;

    /* Shift for fine control */
    GdkModifierType state = gtk_event_controller_get_current_event_state(
        GTK_EVENT_CONTROLLER(gesture));
    if (state & GDK_SHIFT_MASK)
        delta *= 0.1f;

    float new_val = kd->drag_start_val + delta;
    if (new_val < kd->min) new_val = kd->min;
    if (new_val > kd->max) new_val = kd->max;
    *kd->value = new_val;

    GtkWidget *w = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
    gtk_widget_queue_draw(w);
}

static void on_drag_end(GtkGestureDrag *gesture, double dx, double dy,
                        gpointer user_data)
{
    (void)gesture; (void)dx; (void)dy;
    knob_data_t *kd = (knob_data_t *)user_data;
    kd->dragging = FALSE;
}

GtkWidget *gtk_knob_new(float min, float max, float *value, const char *label)
{
    knob_data_t *kd = g_new0(knob_data_t, 1);
    kd->min = min;
    kd->max = max;
    kd->value = value;
    snprintf(kd->label, sizeof(kd->label), "%s", label);

    GtkWidget *area = gtk_drawing_area_new();
    gtk_widget_set_size_request(area, 56, 70);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(area), knob_draw, kd, g_free);

    GtkGesture *drag = gtk_gesture_drag_new();
    g_signal_connect_data(drag, "drag-begin", G_CALLBACK(on_drag_begin), kd, NULL, 0);
    g_signal_connect_data(drag, "drag-update", G_CALLBACK(on_drag_update), kd, NULL, 0);
    g_signal_connect_data(drag, "drag-end", G_CALLBACK(on_drag_end), kd, NULL, 0);
    gtk_widget_add_controller(area, GTK_EVENT_CONTROLLER(drag));

    return area;
}
