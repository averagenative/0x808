/*
 * gtk_piano_roll.c — Piano roll note grid with Cairo rendering.
 *
 * Features: velocity-colored bars, beat shading, scroll, playhead,
 *           click to place/delete, bold beat lines.
 */

#include "gtk_gui.h"
#include "gui/undo.h"
#include <math.h>
#include <stdio.h>

#define PIANO_KEY_W  45.0
#define NOTE_RANGE   48   /* C2 to B5 */
#define BASE_NOTE    36   /* C2 = MIDI 36 */
#define ROW_H        14.0

static int s_scroll_note = 60; /* center on C4 */

static int is_black_key(int note)
{
    int n = note % 12;
    return (n == 1 || n == 3 || n == 6 || n == 8 || n == 10);
}

static void on_draw(GtkDrawingArea *area, cairo_t *cr,
                    int width, int height, gpointer user_data)
{
    (void)area; (void)user_data;
    sq_engine_t *engine = g_gtk.engine;
    if (!engine) return;

    int pi = engine->transport.current_pattern;
    if (pi < 0 || (uint32_t)pi >= engine->num_patterns) return;
    sq_pattern_t *pat = &engine->patterns[pi];

    int sel_track = g_gtk.app.selected_track;
    if (sel_track < 0 || (uint32_t)sel_track >= pat->num_tracks) return;
    sq_track_t *track = &pat->tracks[sel_track];

    /* Compute visible note range centered on s_scroll_note */
    int visible_rows = (int)(height / ROW_H);
    int top_note = s_scroll_note + visible_rows / 2;
    if (top_note > BASE_NOTE + NOTE_RANGE - 1) top_note = BASE_NOTE + NOTE_RANGE - 1;
    int bottom_note = top_note - visible_rows + 1;
    if (bottom_note < BASE_NOTE) { bottom_note = BASE_NOTE; top_note = bottom_note + visible_rows - 1; }

    double grid_x = PIANO_KEY_W;
    double grid_w = (double)width - PIANO_KEY_W;
    double cell_w = grid_w / (double)track->length;

    /* Background */
    cairo_set_source_rgb(cr, 0.05, 0.05, 0.07);
    cairo_rectangle(cr, 0, 0, width, height);
    cairo_fill(cr);

    cairo_select_font_face(cr, "monospace", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);

    /* Draw rows (piano keys + grid) */
    for (int i = 0; i < visible_rows; i++) {
        int note = top_note - i;
        if (note < BASE_NOTE || note >= BASE_NOTE + NOTE_RANGE) continue;
        double ry = i * ROW_H;

        /* Piano key column */
        if (is_black_key(note)) {
            cairo_set_source_rgb(cr, 0.07, 0.07, 0.09);
        } else {
            cairo_set_source_rgb(cr, 0.12, 0.12, 0.14);
        }
        cairo_rectangle(cr, 0, ry, PIANO_KEY_W - 2, ROW_H - 1);
        cairo_fill(cr);

        /* Note label on C notes */
        if (note % 12 == 0) {
            cairo_set_source_rgba(cr, 0.6, 0.6, 0.6, 0.9);
            cairo_set_font_size(cr, 9.0);
            char label[8];
            snprintf(label, sizeof(label), "C%d", (note / 12) - 1);
            cairo_move_to(cr, 2, ry + ROW_H - 3);
            cairo_show_text(cr, label);
        }

        /* Grid row background — alternating beat shading */
        for (uint32_t s = 0; s < track->length; s++) {
            double sx = grid_x + s * cell_w;
            double bg;
            if (is_black_key(note))
                bg = (s % 8 < 4) ? 0.06 : 0.07;
            else
                bg = (s % 8 < 4) ? 0.09 : 0.10;
            cairo_set_source_rgb(cr, bg, bg, bg + 0.01);
            cairo_rectangle(cr, sx, ry, cell_w - 0.5, ROW_H - 0.5);
            cairo_fill(cr);
        }

        /* Row divider */
        double line_alpha = (note % 12 == 0) ? 0.25 : 0.08;
        cairo_set_source_rgba(cr, 0.3, 0.3, 0.35, line_alpha);
        cairo_set_line_width(cr, 0.5);
        cairo_move_to(cr, grid_x, ry + ROW_H);
        cairo_line_to(cr, width, ry + ROW_H);
        cairo_stroke(cr);
    }

    /* Vertical grid lines — bold every 4 steps */
    for (uint32_t s = 0; s <= track->length; s++) {
        double lx = grid_x + s * cell_w;
        if (s % 4 == 0) {
            cairo_set_source_rgba(cr, 0.35, 0.35, 0.4, 0.6);
            cairo_set_line_width(cr, 1.0);
        } else {
            cairo_set_source_rgba(cr, 0.2, 0.2, 0.25, 0.3);
            cairo_set_line_width(cr, 0.5);
        }
        cairo_move_to(cr, lx, 0);
        cairo_line_to(cr, lx, height);
        cairo_stroke(cr);
    }

    /* Draw notes */
    for (uint32_t s = 0; s < track->length; s++) {
        if (track->steps[s].velocity == 0) continue;

        int note = track->steps[s].note;
        if (note < bottom_note || note > top_note) continue;

        int row = top_note - note;
        double nx = grid_x + s * cell_w + 1;
        double nw = cell_w * track->steps[s].length;
        if (nw < cell_w - 2) nw = cell_w - 2;

        /* Height varies by velocity */
        double vel_frac = (double)track->steps[s].velocity / 127.0;
        double nh = ROW_H * (0.5 + 0.5 * vel_frac) - 2;
        double ny = row * ROW_H + (ROW_H - nh) * 0.5;

        /* Velocity-colored */
        double alpha = 0.4 + 0.6 * vel_frac;
        cairo_set_source_rgba(cr, 0.3, 0.7, 1.0, alpha);

        /* Rounded rect */
        double rad = 2.0;
        cairo_new_sub_path(cr);
        cairo_arc(cr, nx + nw - rad, ny + rad, rad, -M_PI/2, 0);
        cairo_arc(cr, nx + nw - rad, ny + nh - rad, rad, 0, M_PI/2);
        cairo_arc(cr, nx + rad, ny + nh - rad, rad, M_PI/2, M_PI);
        cairo_arc(cr, nx + rad, ny + rad, rad, M_PI, 3*M_PI/2);
        cairo_close_path(cr);
        cairo_fill(cr);

        /* Velocity text on wide notes */
        if (nw > 22) {
            cairo_set_source_rgba(cr, 1, 1, 1, 0.7);
            cairo_set_font_size(cr, 8.0);
            char vstr[4];
            snprintf(vstr, sizeof(vstr), "%d", track->steps[s].velocity);
            cairo_move_to(cr, nx + 3, ny + nh - 2);
            cairo_show_text(cr, vstr);
        }
    }

    /* Playhead */
    if (engine->transport.playing) {
        double px = grid_x + g_gtk.app.visual_step * cell_w;
        cairo_set_source_rgba(cr, 1.0, 0.4, 0.4, 0.6);
        cairo_set_line_width(cr, 2.0);
        cairo_move_to(cr, px, 0);
        cairo_line_to(cr, px, height);
        cairo_stroke(cr);
    }
}

/* ─── Click interaction ───────────────────────────────────────────────────── */

static void on_click(GtkGestureClick *gesture, int n_press,
                     double x, double y, gpointer user_data)
{
    (void)gesture; (void)n_press; (void)user_data;
    sq_engine_t *engine = g_gtk.engine;
    if (!engine) return;

    int pi = engine->transport.current_pattern;
    if (pi < 0 || (uint32_t)pi >= engine->num_patterns) return;
    sq_pattern_t *pat = &engine->patterns[pi];

    int sel_track = g_gtk.app.selected_track;
    if (sel_track < 0 || (uint32_t)sel_track >= pat->num_tracks) return;
    sq_track_t *track = &pat->tracks[sel_track];

    if (x < PIANO_KEY_W) return;

    int h = gtk_widget_get_height(g_gtk.piano_roll_area);
    int w = gtk_widget_get_width(g_gtk.piano_roll_area);
    int visible_rows = (int)(h / ROW_H);
    int top_note = s_scroll_note + visible_rows / 2;
    if (top_note > BASE_NOTE + NOTE_RANGE - 1) top_note = BASE_NOTE + NOTE_RANGE - 1;

    double grid_w = (double)w - PIANO_KEY_W;
    double cell_w = grid_w / (double)track->length;

    int step = (int)((x - PIANO_KEY_W) / cell_w);
    int row  = (int)(y / ROW_H);
    int note = top_note - row;

    if (step < 0 || (uint32_t)step >= track->length) return;
    if (note < BASE_NOTE || note >= BASE_NOTE + NOTE_RANGE) return;

    undo_push(engine);

    if (track->steps[step].velocity > 0 && track->steps[step].note == (uint8_t)note) {
        track->steps[step].velocity = 0;
        track->steps[step].note = 0;
    } else {
        track->steps[step].velocity = 100;
        track->steps[step].note = (uint8_t)note;
        track->steps[step].length = 1.0f;
    }

    gtk_widget_queue_draw(g_gtk.piano_roll_area);
}

/* ─── Scroll for pitch navigation ─────────────────────────────────────────── */

static gboolean on_scroll(GtkEventControllerScroll *ctrl,
                           double dx, double dy, gpointer user_data)
{
    (void)ctrl; (void)dx; (void)user_data;
    s_scroll_note -= (int)(dy * 2);
    if (s_scroll_note < BASE_NOTE + 6) s_scroll_note = BASE_NOTE + 6;
    if (s_scroll_note > BASE_NOTE + NOTE_RANGE - 6) s_scroll_note = BASE_NOTE + NOTE_RANGE - 6;
    gtk_widget_queue_draw(g_gtk.piano_roll_area);
    return TRUE;
}

/* ─── Constructor ─────────────────────────────────────────────────────────── */

GtkWidget *gtk_piano_roll_new(void)
{
    GtkWidget *area = gtk_drawing_area_new();
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(area), on_draw, NULL, NULL);

    GtkGesture *click = gtk_gesture_click_new();
    g_signal_connect(click, "pressed", G_CALLBACK(on_click), NULL);
    gtk_widget_add_controller(area, GTK_EVENT_CONTROLLER(click));

    GtkEventController *scroll = gtk_event_controller_scroll_new(
        GTK_EVENT_CONTROLLER_SCROLL_VERTICAL);
    g_signal_connect(scroll, "scroll", G_CALLBACK(on_scroll), NULL);
    gtk_widget_add_controller(area, scroll);

    return area;
}
