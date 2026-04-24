/*
 * gtk_keyboard.c — Virtual piano keyboard with Cairo rendering.
 * Supports click and drag across keys for glissando.
 * Header row shows octave shift, range, and preset name.
 * Held note name displayed below the keyboard.
 */

#include "gtk_gui.h"
#include "engine/synth.h"
#include <math.h>

#define KB_NUM_OCTAVES  3
#define KB_WHITE_PER_OCT 7
#define KB_NUM_WHITE    (KB_NUM_OCTAVES * KB_WHITE_PER_OCT + 1)
#define KB_DEFAULT_BASE 48  /* C3 */
#define KB_HEADER_H     20  /* header row height in pixels */
#define KB_FOOTER_H     16  /* footer row height for held note name */

static const int WHITE_NOTES[] = {0, 2, 4, 5, 7, 9, 11};
static const int BLACK_NOTES[] = {1, 3, 6, 8, 10};

static const char *NOTE_NAMES[] = {
    "C", "C#", "D", "D#", "E", "F",
    "F#", "G", "G#", "A", "A#", "B"
};

static int s_held_note = -1;
static gboolean s_mouse_down = FALSE;
static int s_octave_offset = 0;  /* -2 to +4 from base C3 */

#define KB_OCT_MIN (-2)
#define KB_OCT_MAX  4

static inline int kb_base_note(void)
{
    return KB_DEFAULT_BASE + s_octave_offset * 12;
}

static void release_note(sq_engine_t *engine, int midi_note)
{
    float freq = 440.0f * powf(2.0f, ((float)midi_note - 69.0f) / 12.0f);
    for (int v = 0; v < SQ_MAX_SYNTH_VOICES; v++) {
        if (engine->synth_voices[v].active &&
            fabsf(engine->synth_voices[v].frequency - freq) < 0.1f)
            engine->synth_voices[v].amp_env.stage = ENV_RELEASE;
    }
}

static int hit_test_note(double mx, double my, int width, int height)
{
    int base = kb_base_note();
    double kb_y = KB_HEADER_H;
    double kb_h = height - KB_HEADER_H - KB_FOOTER_H;
    double ry = my - kb_y;

    if (ry < 0 || ry >= kb_h) return -1;

    double white_w = (double)width / KB_NUM_WHITE;
    double black_w = white_w * 0.6;
    double black_h = kb_h * 0.6;

    /* Check black keys first */
    for (int oct = 0; oct < KB_NUM_OCTAVES; oct++) {
        for (int b = 0; b < 5; b++) {
            int n = BLACK_NOTES[b];
            int left_white = -1;
            for (int i = 0; i < 7; i++) {
                if (WHITE_NOTES[i] < n) left_white = i;
            }
            if (left_white < 0) continue;

            int wki = oct * 7 + left_white;
            double bx = ((double)wki + 1.0) * white_w - black_w * 0.5;
            if (mx >= bx && mx < bx + black_w && ry < black_h)
                return base + oct * 12 + BLACK_NOTES[b];
        }
    }

    /* White keys */
    int wk = (int)(mx / white_w);
    if (wk < 0) wk = 0;
    if (wk >= KB_NUM_WHITE) wk = KB_NUM_WHITE - 1;
    int oct = wk / 7;
    int key = wk % 7;
    return base + oct * 12 + WHITE_NOTES[key];
}

static void trigger_note(int note)
{
    sq_engine_t *engine = g_gtk.engine;
    if (note < 0 || note > 127) return;

    /* Release previous note if different */
    if (s_held_note >= 0 && s_held_note != note)
        release_note(engine, s_held_note);

    if (note != s_held_note) {
        int preset = sq_app_get_keyboard_preset(&g_gtk.app, engine);
        if (preset < 0) preset = 0;
        synth_trigger(engine, preset, 0.8f, 0, 0.7f, 0.0f, (uint8_t)note, -1);
        s_held_note = note;
    }
}

/* Format a MIDI note number as a name string like "C#4" */
static void midi_note_name(int midi, char *buf, int buflen)
{
    int octave = (midi / 12) - 1;
    int note_idx = midi % 12;
    snprintf(buf, buflen, "%s%d", NOTE_NAMES[note_idx], octave);
}

/* ─── Header: draw octave buttons, range, preset name ────────────────────── */

static void draw_header(cairo_t *cr, int width)
{
    int base = kb_base_note();
    int low_oct = (base / 12) - 1;
    int high_note = base + KB_NUM_OCTAVES * 12;
    int high_oct = (high_note / 12) - 1;

    /* Background */
    cairo_set_source_rgb(cr, 0.15, 0.15, 0.17);
    cairo_rectangle(cr, 0, 0, width, KB_HEADER_H);
    cairo_fill(cr);

    cairo_set_font_size(cr, 11.0);

    /* << button */
    double btn_w = 20.0;
    double btn_h = KB_HEADER_H - 4;
    double bx = 4.0;
    double by = 2.0;

    cairo_set_source_rgb(cr, 0.3, 0.3, 0.35);
    cairo_rectangle(cr, bx, by, btn_w, btn_h);
    cairo_fill(cr);
    cairo_set_source_rgb(cr, 0.8, 0.8, 0.85);
    cairo_move_to(cr, bx + 3, by + btn_h - 3);
    cairo_show_text(cr, "<<");

    /* >> button */
    double bx2 = bx + btn_w + 4;
    cairo_set_source_rgb(cr, 0.3, 0.3, 0.35);
    cairo_rectangle(cr, bx2, by, btn_w, btn_h);
    cairo_fill(cr);
    cairo_set_source_rgb(cr, 0.8, 0.8, 0.85);
    cairo_move_to(cr, bx2 + 3, by + btn_h - 3);
    cairo_show_text(cr, ">>");

    /* Range label: C3-C6 */
    char range[32];
    snprintf(range, sizeof(range), "C%d-C%d", low_oct, high_oct);
    cairo_set_source_rgb(cr, 0.7, 0.7, 0.75);
    cairo_move_to(cr, bx2 + btn_w + 8, by + btn_h - 3);
    cairo_show_text(cr, range);

    /* Preset name */
    sq_engine_t *engine = g_gtk.engine;
    int preset = sq_app_get_keyboard_preset(&g_gtk.app, engine);
    if (preset < 0) preset = 0;
    const char *preset_name = engine->synth_presets[preset].name;

    char preset_label[64];
    snprintf(preset_label, sizeof(preset_label), "| Keyboard: %s", preset_name);

    cairo_text_extents_t range_ext;
    cairo_text_extents(cr, range, &range_ext);
    double preset_x = bx2 + btn_w + 8 + range_ext.x_advance + 8;

    cairo_set_source_rgb(cr, 0.55, 0.55, 0.6);
    cairo_move_to(cr, preset_x, by + btn_h - 3);
    cairo_show_text(cr, preset_label);
}

/* ─── Footer: held note name ─────────────────────────────────────────────── */

static void draw_footer(cairo_t *cr, int width, int height)
{
    double fy = height - KB_FOOTER_H;

    /* Background */
    cairo_set_source_rgb(cr, 0.15, 0.15, 0.17);
    cairo_rectangle(cr, 0, fy, width, KB_FOOTER_H);
    cairo_fill(cr);

    if (s_held_note >= 0) {
        char name[8];
        midi_note_name(s_held_note, name, sizeof(name));

        cairo_set_font_size(cr, 11.0);
        cairo_text_extents_t ext;
        cairo_text_extents(cr, name, &ext);

        double tx = ((double)width - ext.width) / 2.0;
        double ty = fy + (KB_FOOTER_H + ext.height) / 2.0;

        cairo_set_source_rgb(cr, 0.8, 0.85, 1.0);
        cairo_move_to(cr, tx, ty);
        cairo_show_text(cr, name);
    }
}

/* ─── Main draw callback ─────────────────────────────────────────────────── */

static void on_draw(GtkDrawingArea *area, cairo_t *cr,
                    int width, int height, gpointer user_data)
{
    (void)area; (void)user_data;

    int base = kb_base_note();
    double kb_y = KB_HEADER_H;
    double kb_h = height - KB_HEADER_H - KB_FOOTER_H;
    double white_w = (double)width / KB_NUM_WHITE;
    double black_w = white_w * 0.6;
    double black_h = kb_h * 0.6;

    /* Header row */
    draw_header(cr, width);

    /* White keys */
    for (int i = 0; i < KB_NUM_WHITE; i++) {
        int oct = i / 7;
        int key = i % 7;
        int midi = base + oct * 12 + WHITE_NOTES[key];

        if (midi == s_held_note)
            cairo_set_source_rgb(cr, 0.4, 0.7, 1.0);
        else
            cairo_set_source_rgb(cr, 0.86, 0.86, 0.88);

        double kx = i * white_w;
        cairo_rectangle(cr, kx, kb_y, white_w - 1, kb_h - 1);
        cairo_fill(cr);

        cairo_set_source_rgba(cr, 0.3, 0.3, 0.35, 0.8);
        cairo_set_line_width(cr, 0.5);
        cairo_rectangle(cr, kx, kb_y, white_w - 1, kb_h - 1);
        cairo_stroke(cr);

        /* C labels on C white keys */
        if (key == 0 && white_w > 14) {
            char label[8];
            snprintf(label, sizeof(label), "C%d", (midi / 12) - 1);
            cairo_set_source_rgba(cr, 0.3, 0.3, 0.35, 0.8);
            cairo_set_font_size(cr, 9.0);
            cairo_move_to(cr, kx + 2, kb_y + kb_h - 4);
            cairo_show_text(cr, label);
        }
    }

    /* Black keys */
    for (int oct = 0; oct < KB_NUM_OCTAVES; oct++) {
        for (int b = 0; b < 5; b++) {
            int midi = base + oct * 12 + BLACK_NOTES[b];
            int n = BLACK_NOTES[b];

            int left_white = -1;
            for (int i = 0; i < 7; i++) {
                if (WHITE_NOTES[i] < n) left_white = i;
            }
            if (left_white < 0) continue;

            int wki = oct * 7 + left_white;
            double bx = ((double)wki + 1.0) * white_w - black_w * 0.5;

            if (midi == s_held_note)
                cairo_set_source_rgb(cr, 0.2, 0.5, 0.9);
            else
                cairo_set_source_rgb(cr, 0.1, 0.1, 0.12);

            cairo_rectangle(cr, bx, kb_y, black_w, black_h);
            cairo_fill(cr);
        }
    }

    /* Footer: held note name */
    draw_footer(cr, width, height);
}

/* ─── Header click detection for << >> buttons ───────────────────────────── */

static gboolean header_hit_test(double x, double y, int *delta)
{
    if (y > KB_HEADER_H) return FALSE;

    double btn_w = 20.0;
    double bx = 4.0;
    double by = 2.0;
    double btn_h = KB_HEADER_H - 4;

    /* << button */
    if (x >= bx && x < bx + btn_w && y >= by && y < by + btn_h) {
        *delta = -1;
        return TRUE;
    }

    /* >> button */
    double bx2 = bx + btn_w + 4;
    if (x >= bx2 && x < bx2 + btn_w && y >= by && y < by + btn_h) {
        *delta = 1;
        return TRUE;
    }

    return FALSE;
}

/* ─── Click: press a key ──────────────────────────────────────────────────── */

static void on_click(GtkGestureClick *gesture, int n_press,
                     double x, double y, gpointer user_data)
{
    (void)gesture; (void)n_press; (void)user_data;

    /* Check for octave button clicks */
    int delta = 0;
    if (header_hit_test(x, y, &delta)) {
        int new_offset = s_octave_offset + delta;
        if (new_offset >= KB_OCT_MIN && new_offset <= KB_OCT_MAX) {
            s_octave_offset = new_offset;
            gtk_widget_queue_draw(g_gtk.keyboard_area);
        }
        return;
    }

    int w = gtk_widget_get_width(g_gtk.keyboard_area);
    int h = gtk_widget_get_height(g_gtk.keyboard_area);

    int note = hit_test_note(x, y, w, h);
    if (note < 0) return;
    s_mouse_down = TRUE;
    trigger_note(note);
    gtk_widget_queue_draw(g_gtk.keyboard_area);
}

/* ─── Release: note off ───────────────────────────────────────────────────── */

static void on_release(GtkGestureClick *gesture, int n_press,
                       double x, double y, gpointer user_data)
{
    (void)gesture; (void)n_press; (void)x; (void)y; (void)user_data;
    s_mouse_down = FALSE;
    if (s_held_note >= 0) {
        release_note(g_gtk.engine, s_held_note);
        s_held_note = -1;
        gtk_widget_queue_draw(g_gtk.keyboard_area);
    }
}

/* ─── Drag: glissando across keys ─────────────────────────────────────────── */

static void on_drag_update(GtkGestureDrag *gesture, double dx, double dy,
                           gpointer user_data)
{
    (void)user_data;
    if (!s_mouse_down) return;

    double start_x, start_y;
    gtk_gesture_drag_get_start_point(gesture, &start_x, &start_y);

    double x = start_x + dx;
    double y = start_y + dy;

    int w = gtk_widget_get_width(g_gtk.keyboard_area);
    int h = gtk_widget_get_height(g_gtk.keyboard_area);

    if (x < 0 || x >= w || y < 0 || y >= h) return;

    int note = hit_test_note(x, y, w, h);
    if (note >= 0 && note != s_held_note) {
        trigger_note(note);
        gtk_widget_queue_draw(g_gtk.keyboard_area);
    }
}

static void on_drag_end(GtkGestureDrag *gesture, double dx, double dy,
                        gpointer user_data)
{
    (void)gesture; (void)dx; (void)dy; (void)user_data;
    s_mouse_down = FALSE;
    if (s_held_note >= 0) {
        release_note(g_gtk.engine, s_held_note);
        s_held_note = -1;
        gtk_widget_queue_draw(g_gtk.keyboard_area);
    }
}

/* ─── Constructor ─────────────────────────────────────────────────────────── */

GtkWidget *gtk_keyboard_new(void)
{
    GtkWidget *area = gtk_drawing_area_new();
    gtk_widget_set_size_request(area, -1, 120 + KB_HEADER_H + KB_FOOTER_H);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(area), on_draw, NULL, NULL);

    /* Click for initial press + release for note off */
    GtkGesture *click = gtk_gesture_click_new();
    g_signal_connect(click, "pressed", G_CALLBACK(on_click), NULL);
    g_signal_connect(click, "released", G_CALLBACK(on_release), NULL);
    gtk_widget_add_controller(area, GTK_EVENT_CONTROLLER(click));

    /* Drag for glissando across keys */
    GtkGesture *drag = gtk_gesture_drag_new();
    g_signal_connect(drag, "drag-update", G_CALLBACK(on_drag_update), NULL);
    g_signal_connect(drag, "drag-end", G_CALLBACK(on_drag_end), NULL);
    gtk_widget_add_controller(area, GTK_EVENT_CONTROLLER(drag));

    return area;
}
