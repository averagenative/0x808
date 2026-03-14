/*
 * gtk_piano_roll.c — Piano roll note grid with Cairo rendering.
 *
 * Features: velocity-colored bars, beat shading, scroll, playhead,
 *           click to place/delete, left-drag to extend note length,
 *           right-drag to erase, bold beat lines.
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

/* Drag-to-extend state (left button) */
static int  s_drag_step     = -1;  /* step index of note being length-dragged */
static int  s_drag_note     = -1;  /* pitch of note being dragged */
static int  s_dragging      = 0;   /* nonzero when left-drag is active */
static int  s_drag_did_move = 0;   /* nonzero if drag moved at least 1 cell */

/* Right-drag erase state */
static int  s_right_dragging   = 0;
static int  s_last_erase_step  = -1;
static int  s_last_erase_note  = -1;

static int is_black_key(int note)
{
    int n = note % 12;
    return (n == 1 || n == 3 || n == 6 || n == 8 || n == 10);
}

/* ─── Coordinate helpers ──────────────────────────────────────────────────── */

/* Compute the top_note for the current scroll/widget geometry */
static int calc_top_note(int widget_height)
{
    int visible_rows = (int)(widget_height / ROW_H);
    int top_note = s_scroll_note + visible_rows / 2;
    if (top_note > BASE_NOTE + NOTE_RANGE - 1) top_note = BASE_NOTE + NOTE_RANGE - 1;
    int bottom_note = top_note - visible_rows + 1;
    if (bottom_note < BASE_NOTE) top_note = BASE_NOTE + visible_rows - 1;
    return top_note;
}

/* Convert pixel coordinates to step/note. Returns 0 on success, -1 if out of range. */
static int xy_to_step_note(double x, double y, sq_track_t *track,
                           int *out_step, int *out_note)
{
    if (x < PIANO_KEY_W) return -1;

    int h = gtk_widget_get_height(g_gtk.piano_roll_area);
    int w = gtk_widget_get_width(g_gtk.piano_roll_area);
    int top_note = calc_top_note(h);

    double grid_w = (double)w - PIANO_KEY_W;
    double cell_w = grid_w / (double)track->length;

    int step = (int)((x - PIANO_KEY_W) / cell_w);
    int row  = (int)(y / ROW_H);
    int note = top_note - row;

    if (step < 0 || (uint32_t)step >= track->length) return -1;
    if (note < BASE_NOTE || note >= BASE_NOTE + NOTE_RANGE) return -1;

    *out_step = step;
    *out_note = note;
    return 0;
}

/* Find a note at the given position, including notes that start earlier and
 * extend into this step via their length field.  Returns the originating
 * step index, or -1 if nothing found. */
static int find_note_at(sq_track_t *track, int step, int note)
{
    if (step >= 0 && (uint32_t)step < track->length) {
        sq_step_t *s = &track->steps[step];
        if (s->velocity > 0 && s->note == (uint8_t)note)
            return step;
    }
    /* Check earlier steps whose length extends into this position */
    for (int s = step - 1; s >= 0 && s >= step - 16; s--) {
        sq_step_t *st = &track->steps[s];
        if (st->velocity > 0 && st->note == (uint8_t)note) {
            float end = (float)s + st->length;
            if ((float)step < end)
                return s;
        }
    }
    return -1;
}

/* ─── Get current track (shared by handlers) ──────────────────────────────── */

static sq_track_t *get_current_track(void)
{
    sq_engine_t *engine = g_gtk.engine;
    if (!engine) return NULL;

    int pi = engine->transport.current_pattern;
    if (pi < 0 || (uint32_t)pi >= engine->num_patterns) return NULL;
    sq_pattern_t *pat = &engine->patterns[pi];

    int sel_track = g_gtk.app.selected_track;
    if (sel_track < 0 || (uint32_t)sel_track >= pat->num_tracks) return NULL;
    return &pat->tracks[sel_track];
}

/* ─── Drawing ─────────────────────────────────────────────────────────────── */

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

/* ─── Left-click: place note or begin drag-to-extend ──────────────────────── */

static void on_click(GtkGestureClick *gesture, int n_press,
                     double x, double y, gpointer user_data)
{
    (void)gesture; (void)n_press; (void)user_data;
    sq_engine_t *engine = g_gtk.engine;
    if (!engine) return;

    sq_track_t *track = get_current_track();
    if (!track) return;

    int step, note;
    if (xy_to_step_note(x, y, track, &step, &note) < 0) return;

    undo_push(engine);

    int existing = find_note_at(track, step, note);
    if (existing >= 0) {
        /* Clicked on an existing note — start drag-to-extend */
        s_drag_step    = existing;
        s_drag_note    = note;
        s_dragging     = 1;
        s_drag_did_move = 0;
        /* Do NOT toggle the note here; release handler decides */
    } else {
        /* Empty cell — place a new note and start drag-to-extend */
        track->steps[step].velocity = 100;
        track->steps[step].note     = (uint8_t)note;
        track->steps[step].length   = 1.0f;

        s_drag_step    = step;
        s_drag_note    = note;
        s_dragging     = 1;
        s_drag_did_move = 0;

        gtk_widget_queue_draw(g_gtk.piano_roll_area);
    }
}

/* Left-button drag motion — extend note length */
static void on_drag_update(GtkGestureDrag *gesture, double offset_x,
                           double offset_y, gpointer user_data)
{
    (void)gesture; (void)offset_y; (void)user_data;
    if (!s_dragging || s_drag_step < 0) return;

    sq_track_t *track = get_current_track();
    if (!track) return;

    /* Get start point of the drag */
    double start_x, start_y;
    gtk_gesture_drag_get_start_point(gesture, &start_x, &start_y);

    double cur_x = start_x + offset_x;

    int cur_step, cur_note;
    if (xy_to_step_note(cur_x, start_y, track, &cur_step, &cur_note) < 0)
        return;

    /* Only extend rightward from the note's origin step */
    float new_len = (float)(cur_step - s_drag_step + 1);
    if (new_len < 1.0f) new_len = 1.0f;

    /* Clamp to remaining steps in the track */
    float max_len = (float)(track->length - (uint32_t)s_drag_step);
    if (new_len > max_len) new_len = max_len;

    if (new_len != track->steps[s_drag_step].length) {
        s_drag_did_move = 1;
        track->steps[s_drag_step].length = new_len;
        gtk_widget_queue_draw(g_gtk.piano_roll_area);
    }
}

/* Left-button release — if no drag movement on an existing note, delete it */
static void on_drag_end(GtkGestureDrag *gesture, double offset_x,
                        double offset_y, gpointer user_data)
{
    (void)gesture; (void)offset_x; (void)offset_y; (void)user_data;

    if (s_dragging && !s_drag_did_move && s_drag_step >= 0) {
        /* Click without drag on an existing note — delete it */
        sq_track_t *track = get_current_track();
        if (track && (uint32_t)s_drag_step < track->length) {
            sq_step_t *st = &track->steps[s_drag_step];
            if (st->velocity > 0 && st->note == (uint8_t)s_drag_note) {
                st->velocity = 0;
                st->note     = 0;
                st->length   = 0;
                gtk_widget_queue_draw(g_gtk.piano_roll_area);
            }
        }
    }

    s_dragging     = 0;
    s_drag_step    = -1;
    s_drag_note    = -1;
    s_drag_did_move = 0;
}

/* ─── Right-click + drag to erase ─────────────────────────────────────────── */

static void on_right_click(GtkGestureClick *gesture, int n_press,
                           double x, double y, gpointer user_data)
{
    (void)gesture; (void)n_press; (void)user_data;
    sq_engine_t *engine = g_gtk.engine;
    if (!engine) return;

    sq_track_t *track = get_current_track();
    if (!track) return;

    int step, note;
    if (xy_to_step_note(x, y, track, &step, &note) < 0) return;

    undo_push(engine);
    s_right_dragging  = 1;

    /* Erase note under cursor immediately */
    int existing = find_note_at(track, step, note);
    if (existing >= 0) {
        track->steps[existing].velocity = 0;
        track->steps[existing].note     = 0;
        track->steps[existing].length   = 0;
        gtk_widget_queue_draw(g_gtk.piano_roll_area);
    }
    s_last_erase_step = step;
    s_last_erase_note = note;
}

/* Right-button drag motion — erase notes under cursor */
static void on_right_drag_update(GtkGestureDrag *gesture, double offset_x,
                                 double offset_y, gpointer user_data)
{
    (void)gesture; (void)user_data;
    if (!s_right_dragging) return;

    sq_track_t *track = get_current_track();
    if (!track) return;

    double start_x, start_y;
    gtk_gesture_drag_get_start_point(gesture, &start_x, &start_y);

    double cur_x = start_x + offset_x;
    double cur_y = start_y + offset_y;

    int step, note;
    if (xy_to_step_note(cur_x, cur_y, track, &step, &note) < 0) return;

    /* Only erase if we moved to a new cell */
    if (step == s_last_erase_step && note == s_last_erase_note) return;

    int existing = find_note_at(track, step, note);
    if (existing >= 0) {
        track->steps[existing].velocity = 0;
        track->steps[existing].note     = 0;
        track->steps[existing].length   = 0;
        gtk_widget_queue_draw(g_gtk.piano_roll_area);
    }
    s_last_erase_step = step;
    s_last_erase_note = note;
}

static void on_right_drag_end(GtkGestureDrag *gesture, double offset_x,
                              double offset_y, gpointer user_data)
{
    (void)gesture; (void)offset_x; (void)offset_y; (void)user_data;
    s_right_dragging  = 0;
    s_last_erase_step = -1;
    s_last_erase_note = -1;
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

    /* Left-click: place note / begin drag-to-extend */
    GtkGesture *click = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), 1);
    g_signal_connect(click, "pressed", G_CALLBACK(on_click), NULL);
    gtk_widget_add_controller(area, GTK_EVENT_CONTROLLER(click));

    /* Left-drag: extend note length */
    GtkGesture *drag = gtk_gesture_drag_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(drag), 1);
    g_signal_connect(drag, "drag-update", G_CALLBACK(on_drag_update), NULL);
    g_signal_connect(drag, "drag-end",    G_CALLBACK(on_drag_end),    NULL);
    gtk_widget_add_controller(area, GTK_EVENT_CONTROLLER(drag));

    /* Right-click + drag: erase notes */
    GtkGesture *right_click = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(right_click), 3);
    g_signal_connect(right_click, "pressed", G_CALLBACK(on_right_click), NULL);
    gtk_widget_add_controller(area, GTK_EVENT_CONTROLLER(right_click));

    GtkGesture *right_drag = gtk_gesture_drag_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(right_drag), 3);
    g_signal_connect(right_drag, "drag-update", G_CALLBACK(on_right_drag_update), NULL);
    g_signal_connect(right_drag, "drag-end",    G_CALLBACK(on_right_drag_end),    NULL);
    gtk_widget_add_controller(area, GTK_EVENT_CONTROLLER(right_drag));

    GtkEventController *scroll = gtk_event_controller_scroll_new(
        GTK_EVENT_CONTROLLER_SCROLL_VERTICAL);
    g_signal_connect(scroll, "scroll", G_CALLBACK(on_scroll), NULL);
    gtk_widget_add_controller(area, scroll);

    return area;
}
