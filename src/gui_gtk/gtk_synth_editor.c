/*
 * gtk_synth_editor.c — Synth parameter editor with sliders and ADSR.
 *
 * Rebuilds controls dynamically when the selected track changes.
 * Supports subtractive, FM, and wavetable mode parameters.
 */

#include "gtk_gui.h"
#include "engine/synth.h"
#include "engine/sq_midi.h"
#include <math.h>
#include <string.h>
#include <stdio.h>

static const char *synth_mode_names[] = {"Subtractive", "FM", "Wavetable"};
static const char *wave_names[] = {"Saw", "Square", "Triangle", "Sine"};
static const char *filter_names[] = {"LowPass", "HiPass", "BandPass"};
static const char *lfo_dest_names[] = {"None", "Pitch", "Filter", "Amp"};
static const char *lfo_sync_names[] = {"1/1", "1/2", "1/4", "1/8", "1/16", "1/32"};
static const char *fm_alg_names[] = {
    "0: 3>2>1>0",   "1: 2>1>0+3>0", "2: 3>2, 1>0", "3: 3>2>1, 0",
    "4: 3,2,1>0",   "5: 3>2,1,0",   "6: 3,2,1,0",  "7: 3>(1,2),0"
};

/* FM algorithm visualization data — must match fm_algorithms[] in synth.c */
static const struct {
    int  mod_sources[FM_NUM_OPERATORS][FM_NUM_OPERATORS]; /* -1 terminated */
    bool is_carrier[FM_NUM_OPERATORS];
} fm_alg_vis[FM_NUM_ALGORITHMS] = {
    /* 0: 3->2->1->0 */
    {{{1,-1},{2,-1},{3,-1},{-1}},  {true,false,false,false}},
    /* 1: 2->1->0, 3->0 */
    {{{1,3,-1},{2,-1},{-1},{-1}},  {true,false,false,false}},
    /* 2: 3->2, 1->0 */
    {{{1,-1},{-1},{3,-1},{-1}},    {true,false,true,false}},
    /* 3: 3->2->1, 0 */
    {{{-1},{2,-1},{3,-1},{-1}},    {true,true,false,false}},
    /* 4: 3,2, 1->0 */
    {{{1,-1},{-1},{-1},{-1}},      {true,false,true,true}},
    /* 5: 3->2, 1, 0 */
    {{{-1},{-1},{3,-1},{-1}},      {true,true,true,false}},
    /* 6: all carriers */
    {{{-1},{-1},{-1},{-1}},        {true,true,true,true}},
    /* 7: 3->(1,2), 0 */
    {{{-1},{3,-1},{3,-1},{-1}},    {true,true,true,false}},
};

static GtkWidget *s_fm_algo_area = NULL;
static sq_synth_preset_t *s_fm_preset = NULL;

static int s_last_preset_idx = -1;   /* track which preset we built controls for */
static int s_last_selected_track = -1;
static GtkWidget *s_content_box = NULL;  /* inner box that holds all controls */
static GtkWidget *s_adsr_area = NULL;
static sq_adsr_params_t *s_adsr_ptr = NULL;
static int s_adsr_drag_point = -1;  /* -1=none, 0=attack, 1=decay/sustain, 2=release */

static GtkWidget *s_filter_adsr_area = NULL;
static sq_adsr_params_t *s_filter_adsr_ptr = NULL;
static GtkWidget *s_filter_curve_area = NULL;
static sq_synth_preset_t *s_filter_preset = NULL;

static GtkWidget *s_wt_area = NULL;
static sq_synth_preset_t *s_wt_preset = NULL;

static void rebuild_controls(void); /* forward decl */

static void on_preset_selected(GObject *obj, GParamSpec *pspec, gpointer data)
{
    (void)pspec; (void)data;
    guint sel = gtk_drop_down_get_selected(GTK_DROP_DOWN(obj));
    int track = g_gtk.app.selected_track;
    if (track < 0) return;
    sq_engine_t *eng = g_gtk.engine;
    int pi = eng->transport.current_pattern;
    if (pi < 0 || (uint32_t)pi >= eng->num_patterns) return;
    sq_pattern_t *pat = &eng->patterns[pi];
    if ((uint32_t)track >= pat->num_tracks) return;
    if (pat->tracks[track].type != TRACK_SYNTH) return;
    pat->tracks[track].synth_preset = (int)sel;
    /* Force rebuild on next tick */
    s_last_preset_idx = -1;
}

/* ─── ADSR visualization ──────────────────────────────────────────────────── */

static void adsr_draw(GtkDrawingArea *area, cairo_t *cr,
                      int width, int height, gpointer user_data)
{
    (void)area; (void)user_data;
    if (!s_adsr_ptr) return;

    float a = s_adsr_ptr->attack, d = s_adsr_ptr->decay;
    float s = s_adsr_ptr->sustain, r = s_adsr_ptr->release;
    float total = a + d + 0.3f + r;
    if (total < 0.01f) total = 0.01f;

    double pad = 6;
    double w = width - pad * 2;
    double h = height - pad * 2;

    cairo_set_source_rgb(cr, 0.06, 0.06, 0.08);
    cairo_rectangle(cr, 0, 0, width, height);
    cairo_fill(cr);

    double x0 = pad, y0 = pad + h;

    cairo_set_source_rgba(cr, 0.2, 1.0, 0.2, 0.8);
    cairo_set_line_width(cr, 2.0);

    cairo_move_to(cr, x0, y0);
    double ax = x0 + (a / total) * w;
    cairo_line_to(cr, ax, pad);
    double dx = ax + (d / total) * w;
    double sy = pad + h * (1.0 - s);
    cairo_line_to(cr, dx, sy);
    double sx = dx + (0.3f / total) * w;
    cairo_line_to(cr, sx, sy);
    double rx = sx + (r / total) * w;
    cairo_line_to(cr, rx, y0);
    cairo_stroke(cr);

    /* Control point dots */
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.6);
    cairo_arc(cr, ax, pad, 3, 0, 2 * M_PI);
    cairo_fill(cr);
    cairo_arc(cr, dx, sy, 3, 0, 2 * M_PI);
    cairo_fill(cr);

    cairo_set_source_rgba(cr, 0.5, 0.5, 0.5, 0.7);
    cairo_set_font_size(cr, 9.0);
    cairo_move_to(cr, x0 + 2, height - 2);
    cairo_show_text(cr, "A");
    cairo_move_to(cr, ax + 2, height - 2);
    cairo_show_text(cr, "D");
    cairo_move_to(cr, dx + 2, height - 2);
    cairo_show_text(cr, "S");
    cairo_move_to(cr, sx + 2, height - 2);
    cairo_show_text(cr, "R");
}

/* ─── Filter envelope ADSR visualization ──────────────────────────────────── */

static void filter_adsr_draw(GtkDrawingArea *area, cairo_t *cr,
                              int width, int height, gpointer user_data)
{
    (void)area; (void)user_data;
    if (!s_filter_adsr_ptr) return;

    float a = s_filter_adsr_ptr->attack, d = s_filter_adsr_ptr->decay;
    float s = s_filter_adsr_ptr->sustain, r = s_filter_adsr_ptr->release;
    float total = a + d + 0.3f + r;
    if (total < 0.01f) total = 0.01f;

    double pad = 6;
    double w = width - pad * 2;
    double h = height - pad * 2;

    cairo_set_source_rgb(cr, 0.06, 0.06, 0.08);
    cairo_rectangle(cr, 0, 0, width, height);
    cairo_fill(cr);

    double x0 = pad, y0 = pad + h;

    cairo_set_source_rgba(cr, 0.2, 1.0, 0.2, 0.8);
    cairo_set_line_width(cr, 2.0);

    cairo_move_to(cr, x0, y0);
    double ax = x0 + (a / total) * w;
    cairo_line_to(cr, ax, pad);
    double dx = ax + (d / total) * w;
    double sy = pad + h * (1.0 - s);
    cairo_line_to(cr, dx, sy);
    double sx = dx + (0.3f / total) * w;
    cairo_line_to(cr, sx, sy);
    double rx = sx + (r / total) * w;
    cairo_line_to(cr, rx, y0);
    cairo_stroke(cr);

    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.6);
    cairo_arc(cr, ax, pad, 3, 0, 2 * M_PI);
    cairo_fill(cr);
    cairo_arc(cr, dx, sy, 3, 0, 2 * M_PI);
    cairo_fill(cr);

    cairo_set_source_rgba(cr, 0.5, 0.5, 0.5, 0.7);
    cairo_set_font_size(cr, 9.0);
    cairo_move_to(cr, x0 + 2, height - 2);
    cairo_show_text(cr, "A");
    cairo_move_to(cr, ax + 2, height - 2);
    cairo_show_text(cr, "D");
    cairo_move_to(cr, dx + 2, height - 2);
    cairo_show_text(cr, "S");
    cairo_move_to(cr, sx + 2, height - 2);
    cairo_show_text(cr, "R");
}

/* ─── Filter response curve visualization ─────────────────────────────────── */

static void filter_curve_draw(GtkDrawingArea *area, cairo_t *cr,
                               int width, int height, gpointer user_data)
{
    (void)area; (void)user_data;
    if (!s_filter_preset) return;

    sq_filter_type_t type = s_filter_preset->filter_type;
    float cutoff = s_filter_preset->filter_cutoff;
    float resonance = s_filter_preset->filter_resonance;

    /* Background */
    cairo_set_source_rgb(cr, 0.06, 0.06, 0.08);
    cairo_rectangle(cr, 0, 0, width, height);
    cairo_fill(cr);

    /* Draw filter magnitude response curve */
    cairo_set_source_rgba(cr, 0.2, 1.0, 0.2, 0.8);
    cairo_set_line_width(cr, 1.5);

    float prev_y = 0;
    for (int px = 0; px < width; px++) {
        float t = (float)px / (float)width;
        float freq = 20.0f * powf(1000.0f, t); /* 20 Hz to 20000 Hz */

        float ratio = freq / cutoff;
        float Q = resonance;
        if (Q < 0.5f) Q = 0.5f;

        float denom = sqrtf(1.0f + powf(ratio, 4.0f)
                      - 2.0f * ratio * ratio * (1.0f - 1.0f / (2.0f * Q * Q)));
        if (denom < 0.001f) denom = 0.001f;

        float mag;
        switch (type) {
        case FILTER_LOWPASS:
            mag = 1.0f / denom;
            break;
        case FILTER_HIGHPASS:
            mag = (ratio * ratio) / denom;
            break;
        default: /* bandpass */
            mag = (ratio / Q) / denom;
            break;
        }
        if (mag > 2.0f) mag = 2.0f;
        if (mag < 0.0f) mag = 0.0f;

        float y = (float)height - (mag / 2.0f) * (float)height;
        if (px > 0) {
            cairo_move_to(cr, px - 1, (double)prev_y);
            cairo_line_to(cr, px, (double)y);
            cairo_stroke(cr);
        }
        prev_y = y;
    }

    /* Cutoff frequency indicator line */
    float cutoff_x = (float)width * logf(cutoff / 20.0f) / logf(1000.0f);
    if (cutoff_x > 0 && cutoff_x < (float)width) {
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.25);
        cairo_set_line_width(cr, 1.0);
        cairo_move_to(cr, (double)cutoff_x, 0);
        cairo_line_to(cr, (double)cutoff_x, height);
        cairo_stroke(cr);
    }
}

/* ─── Wavetable waveform visualization ─────────────────────────────────────── */

static void wt_waveform_draw(GtkDrawingArea *area, cairo_t *cr,
                              int width, int height, gpointer user_data)
{
    (void)area; (void)user_data;

    /* Dark background */
    cairo_set_source_rgb(cr, 0.06, 0.06, 0.08);
    cairo_rectangle(cr, 0, 0, width, height);
    cairo_fill(cr);

    if (!s_wt_preset) return;

    sq_engine_t *engine = g_gtk.engine;
    if (!engine) return;

    int bank_idx = s_wt_preset->wt_bank_index;
    float position = s_wt_preset->wt_position;
    double cy = height * 0.5;

    /* Center line */
    cairo_set_source_rgba(cr, 0.24, 0.24, 0.26, 0.6);
    cairo_set_line_width(cr, 0.5);
    cairo_move_to(cr, 0, cy);
    cairo_line_to(cr, width, cy);
    cairo_stroke(cr);

    /* Try to draw real wavetable data if available */
    if (engine->wt_banks && bank_idx >= 0 &&
        (uint32_t)bank_idx < engine->num_wt_banks) {
        const sq_wt_bank_t *bank = &engine->wt_banks[bank_idx];
        if (bank->num_frames >= 1) {
            /* Interpolate between two adjacent frames based on position */
            float fpos = position * (bank->num_frames - 1);
            int frame_a = (int)fpos;
            int frame_b = frame_a + 1;
            float frac = fpos - frame_a;
            if (frame_a < 0) frame_a = 0;
            if (frame_a >= bank->num_frames) frame_a = bank->num_frames - 1;
            if (frame_b >= bank->num_frames) frame_b = bank->num_frames - 1;

            cairo_set_source_rgba(cr, 0.47, 0.78, 0.71, 0.8);
            cairo_set_line_width(cr, 1.5);

            double prev_y = cy;
            for (int px = 0; px < width; px++) {
                int idx = (int)((float)px / (float)width * SQ_WAVETABLE_SIZE);
                if (idx >= SQ_WAVETABLE_SIZE) idx = SQ_WAVETABLE_SIZE - 1;
                float sample_a = bank->frames[frame_a][idx];
                float sample_b = bank->frames[frame_b][idx];
                float sample = sample_a + frac * (sample_b - sample_a);
                double y = cy - sample * (height * 0.45);
                if (px == 0) {
                    cairo_move_to(cr, px, y);
                } else {
                    cairo_line_to(cr, px, y);
                }
                prev_y = y;
            }
            cairo_stroke(cr);
            (void)prev_y;

            /* Position indicator */
            double pos_x = position * width;
            cairo_set_source_rgba(cr, 1.0, 0.78, 0.31, 0.47);
            cairo_set_line_width(cr, 1.0);
            cairo_move_to(cr, pos_x, 0);
            cairo_line_to(cr, pos_x, height);
            cairo_stroke(cr);
            return;
        }
    }

    /* Fallback: draw a synthetic waveform that morphs with position */
    cairo_set_source_rgba(cr, 0.47, 0.78, 0.71, 0.8);
    cairo_set_line_width(cr, 1.5);

    for (int px = 0; px < width; px++) {
        double t = (double)px / (double)width;
        double phase = t * 2.0 * M_PI;
        /* Morph from sine -> saw-ish -> square-ish based on position */
        double sample = sin(phase);
        /* Add harmonics based on position to morph the waveform */
        double harm_amt = position;
        sample += harm_amt * 0.5 * sin(2.0 * phase);
        sample += harm_amt * 0.33 * sin(3.0 * phase);
        sample += harm_amt * 0.25 * sin(4.0 * phase);
        sample += harm_amt * 0.2 * sin(5.0 * phase);
        /* Normalize */
        double max_amp = 1.0 + harm_amt * (0.5 + 0.33 + 0.25 + 0.2);
        sample /= max_amp;

        double y = cy - sample * (height * 0.42);
        if (px == 0)
            cairo_move_to(cr, px, y);
        else
            cairo_line_to(cr, px, y);
    }
    cairo_stroke(cr);

    /* Position indicator */
    double pos_x = position * width;
    cairo_set_source_rgba(cr, 1.0, 0.78, 0.31, 0.47);
    cairo_set_line_width(cr, 1.0);
    cairo_move_to(cr, pos_x, 0);
    cairo_line_to(cr, pos_x, height);
    cairo_stroke(cr);
}

/* ─── FM algorithm diagram visualization ──────────────────────────────────── */

static void fm_algo_draw(GtkDrawingArea *area, cairo_t *cr,
                          int width, int height, gpointer user_data)
{
    (void)area; (void)user_data;
    if (!s_fm_preset) return;

    int algorithm = s_fm_preset->fm_algorithm;
    if (algorithm < 0 || algorithm >= FM_NUM_ALGORITHMS) algorithm = 0;

    /* Background */
    cairo_set_source_rgb(cr, 0.14, 0.15, 0.18);
    cairo_rectangle(cr, 0, 0, width, height);
    cairo_fill(cr);

    float bw = 28, bh = 20;
    float cx = width * 0.5f;
    float cy = height * 0.5f;
    float top_y = cy - 18;
    float bot_y = cy + 12;

    /* Operator positions per algorithm (matching ImGui layout) */
    struct { float x, y; } ops[4];
    switch (algorithm) {
    case 0:
        ops[3] = (typeof(ops[0])){cx - 54, top_y};
        ops[2] = (typeof(ops[0])){cx - 18, top_y};
        ops[1] = (typeof(ops[0])){cx + 18, top_y};
        ops[0] = (typeof(ops[0])){cx + 54, bot_y};
        break;
    case 1:
        ops[2] = (typeof(ops[0])){cx - 36, top_y};
        ops[1] = (typeof(ops[0])){cx,      top_y};
        ops[3] = (typeof(ops[0])){cx + 36, top_y};
        ops[0] = (typeof(ops[0])){cx,      bot_y};
        break;
    case 2:
        ops[1] = (typeof(ops[0])){cx - 30, top_y};
        ops[0] = (typeof(ops[0])){cx - 30, bot_y};
        ops[3] = (typeof(ops[0])){cx + 30, top_y};
        ops[2] = (typeof(ops[0])){cx + 30, bot_y};
        break;
    case 3:
        ops[3] = (typeof(ops[0])){cx - 36, top_y};
        ops[2] = (typeof(ops[0])){cx,      top_y};
        ops[1] = (typeof(ops[0])){cx + 36, bot_y};
        ops[0] = (typeof(ops[0])){cx - 54, bot_y};
        break;
    case 4:
        ops[1] = (typeof(ops[0])){cx - 18, top_y};
        ops[0] = (typeof(ops[0])){cx - 18, bot_y};
        ops[2] = (typeof(ops[0])){cx + 18, bot_y};
        ops[3] = (typeof(ops[0])){cx + 54, bot_y};
        break;
    case 5:
        ops[3] = (typeof(ops[0])){cx - 18, top_y};
        ops[2] = (typeof(ops[0])){cx - 18, bot_y};
        ops[1] = (typeof(ops[0])){cx + 18, bot_y};
        ops[0] = (typeof(ops[0])){cx + 54, bot_y};
        break;
    case 6:
        ops[0] = (typeof(ops[0])){cx - 54, bot_y};
        ops[1] = (typeof(ops[0])){cx - 18, bot_y};
        ops[2] = (typeof(ops[0])){cx + 18, bot_y};
        ops[3] = (typeof(ops[0])){cx + 54, bot_y};
        break;
    case 7:
        ops[3] = (typeof(ops[0])){cx,      top_y};
        ops[1] = (typeof(ops[0])){cx - 24, bot_y};
        ops[2] = (typeof(ops[0])){cx + 24, bot_y};
        ops[0] = (typeof(ops[0])){cx - 60, bot_y};
        break;
    default:
        for (int i = 0; i < 4; i++)
            ops[i] = (typeof(ops[0])){cx - 54.0f + i * 36.0f, cy};
        break;
    }

    /* Draw modulation arrows (green lines) */
    cairo_set_source_rgba(cr, 0.4, 0.9, 0.3, 0.7);
    cairo_set_line_width(cr, 1.5);
    for (int op = 0; op < FM_NUM_OPERATORS; op++) {
        for (int j = 0; j < FM_NUM_OPERATORS; j++) {
            int src = fm_alg_vis[algorithm].mod_sources[op][j];
            if (src < 0) break;
            float sx = ops[src].x;
            float sy = ops[src].y + bh / 2;
            float dx = ops[op].x;
            float dy = ops[op].y - bh / 2;
            cairo_move_to(cr, sx, sy);
            cairo_line_to(cr, dx, dy);
            cairo_stroke(cr);

            /* Arrowhead */
            float ddx = dx - sx;
            float ddy = dy - sy;
            float len = sqrtf(ddx * ddx + ddy * ddy);
            if (len > 0.01f) {
                ddx /= len; ddy /= len;
                float ax = dx - ddx * 5 - ddy * 3;
                float ay = dy - ddy * 5 + ddx * 3;
                float bx = dx - ddx * 5 + ddy * 3;
                float by = dy - ddy * 5 - ddx * 3;
                cairo_move_to(cr, dx, dy);
                cairo_line_to(cr, ax, ay);
                cairo_stroke(cr);
                cairo_move_to(cr, dx, dy);
                cairo_line_to(cr, bx, by);
                cairo_stroke(cr);
            }
        }
    }

    /* Draw operator boxes */
    for (int i = 0; i < 4; i++) {
        float x = ops[i].x - bw / 2;
        float y = ops[i].y - bh / 2;

        if (fm_alg_vis[algorithm].is_carrier[i])
            cairo_set_source_rgba(cr, 0.24, 0.55, 0.31, 0.8);  /* green = carrier */
        else
            cairo_set_source_rgba(cr, 0.31, 0.39, 0.63, 0.8);  /* blue = modulator */

        /* Rounded rectangle */
        double r = 3.0;
        cairo_new_sub_path(cr);
        cairo_arc(cr, x + bw - r, y + r,      r, -M_PI / 2, 0);
        cairo_arc(cr, x + bw - r, y + bh - r, r, 0,          M_PI / 2);
        cairo_arc(cr, x + r,      y + bh - r, r, M_PI / 2,   M_PI);
        cairo_arc(cr, x + r,      y + r,      r, M_PI,        3 * M_PI / 2);
        cairo_close_path(cr);
        cairo_fill(cr);

        /* Label */
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.86);
        cairo_set_font_size(cr, 11.0);
        char label[4];
        snprintf(label, sizeof(label), "%d", i + 1);
        cairo_move_to(cr, ops[i].x - 3, ops[i].y + 4);
        cairo_show_text(cr, label);
    }

    /* Algorithm label in top-left */
    cairo_set_source_rgba(cr, 0.59, 0.59, 0.59, 0.7);
    cairo_set_font_size(cr, 9.0);
    char alg_label[16];
    snprintf(alg_label, sizeof(alg_label), "Alg %d", algorithm + 1);
    cairo_move_to(cr, 4, 11);
    cairo_show_text(cr, alg_label);
}

/* ─── ADSR drag interaction ───────────────────────────────────────────────── */

static void on_adsr_press(GtkGestureClick *gesture, int n_press,
                           double x, double y, gpointer user_data)
{
    (void)gesture; (void)n_press; (void)user_data;
    if (!s_adsr_ptr || !s_adsr_area) return;

    int width = gtk_widget_get_width(s_adsr_area);
    int height = gtk_widget_get_height(s_adsr_area);
    float a = s_adsr_ptr->attack, d = s_adsr_ptr->decay;
    float r = s_adsr_ptr->release;
    float total = a + d + 0.3f + r;
    if (total < 0.01f) total = 0.01f;

    double pad = 6;
    double w = width - pad * 2;
    double ax = pad + (a / total) * w;
    double dx = ax + (d / total) * w;
    double sx = dx + (0.3f / total) * w;

    /* Find closest control point */
    s_adsr_drag_point = -1;
    double best = 20.0;
    if (fabs(x - ax) < best) { best = fabs(x - ax); s_adsr_drag_point = 0; }
    if (fabs(x - dx) < best) { best = fabs(x - dx); s_adsr_drag_point = 1; }
    if (fabs(x - sx) < best) { best = fabs(x - sx); s_adsr_drag_point = 2; }
    (void)y; (void)height;
}

static void on_adsr_drag(GtkGestureDrag *gesture, double dx, double dy,
                          gpointer user_data)
{
    (void)gesture; (void)user_data;
    if (!s_adsr_ptr || s_adsr_drag_point < 0) return;

    float scale_x = 0.01f;
    float scale_y = 0.005f;

    switch (s_adsr_drag_point) {
    case 0: /* Attack — drag right = longer */
        s_adsr_ptr->attack += (float)dx * scale_x;
        if (s_adsr_ptr->attack < 0.001f) s_adsr_ptr->attack = 0.001f;
        if (s_adsr_ptr->attack > 2.0f) s_adsr_ptr->attack = 2.0f;
        break;
    case 1: /* Decay/Sustain — drag right = longer decay, drag up = higher sustain */
        s_adsr_ptr->decay += (float)dx * scale_x;
        s_adsr_ptr->sustain -= (float)dy * scale_y;
        if (s_adsr_ptr->decay < 0.001f) s_adsr_ptr->decay = 0.001f;
        if (s_adsr_ptr->decay > 2.0f) s_adsr_ptr->decay = 2.0f;
        if (s_adsr_ptr->sustain < 0) s_adsr_ptr->sustain = 0;
        if (s_adsr_ptr->sustain > 1) s_adsr_ptr->sustain = 1;
        break;
    case 2: /* Release — drag right = longer */
        s_adsr_ptr->release += (float)dx * scale_x;
        if (s_adsr_ptr->release < 0.001f) s_adsr_ptr->release = 0.001f;
        if (s_adsr_ptr->release > 5.0f) s_adsr_ptr->release = 5.0f;
        break;
    }

    gtk_widget_queue_draw(s_adsr_area);
}

static void on_adsr_drag_end(GtkGestureDrag *gesture, double dx, double dy,
                              gpointer user_data)
{
    (void)gesture; (void)dx; (void)dy; (void)user_data;
    s_adsr_drag_point = -1;
}

/* ─── Slider callback ─────────────────────────────────────────────────────── */

static void on_int_slider_changed(GtkRange *range, gpointer user_data)
{
    (void)user_data;
    int *ptr = (int *)g_object_get_data(G_OBJECT(range), "int_ptr");
    if (ptr) *ptr = (int)gtk_range_get_value(range);
}

/* ─── Helper: inline knob with label ──────────────────────────────────────── */

/* Right-click handler for MIDI learn on GTK knobs */
static void on_knob_midi_learn(GtkGestureClick *gesture, int n_press,
                                double x, double y, gpointer user_data)
{
    (void)gesture; (void)n_press; (void)x; (void)y;
    sq_midi_t *midi = (sq_midi_t *)g_gtk.midi;
    if (!midi) return;
    sq_param_id_t param = (sq_param_id_t)(intptr_t)user_data;
    if (sq_midi_learn_active(midi) == param)
        sq_midi_learn_cancel(midi);
    else
        sq_midi_learn_start(midi, param);
    char msg[48];
    if (sq_midi_learn_active(midi) != SQ_PARAM_NONE)
        snprintf(msg, sizeof(msg), "MIDI Learn: wiggle a knob on controller...");
    else
        snprintf(msg, sizeof(msg), "MIDI Learn cancelled");
    sq_app_set_status(&g_gtk.app, msg, 120);
}

static GtkWidget *make_knob(const char *label_text, float min, float max,
                             float *value_ptr)
{
    GtkWidget *knob = gtk_knob_new(min, max, value_ptr, label_text);
    gtk_widget_set_size_request(knob, 44, 50);
    return knob;
}

/* Make a knob with MIDI learn on right-click */
static GtkWidget *make_knob_learn(const char *label_text, float min, float max,
                                   float *value_ptr, sq_param_id_t param)
{
    GtkWidget *knob = make_knob(label_text, min, max, value_ptr);
    GtkGesture *rclick = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(rclick), 3); /* right button */
    g_signal_connect(rclick, "pressed", G_CALLBACK(on_knob_midi_learn),
                     (gpointer)(intptr_t)param);
    gtk_widget_add_controller(knob, GTK_EVENT_CONTROLLER(rclick));
    return knob;
}

/* Row of 4 ADSR knobs */
static GtkWidget *make_adsr_knobs(float *a, float *d, float *s, float *r,
                                   float a_max, float d_max, float r_max)
{
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    gtk_box_append(GTK_BOX(row), make_knob("A", 0.001f, a_max, a));
    gtk_box_append(GTK_BOX(row), make_knob("D", 0.001f, d_max, d));
    gtk_box_append(GTK_BOX(row), make_knob("S", 0, 1, s));
    gtk_box_append(GTK_BOX(row), make_knob("R", 0.001f, r_max, r));
    return row;
}

/* Row of 4 ADSR knobs with MIDI learn */
static GtkWidget *make_adsr_knobs_learn(float *a, float *d, float *s, float *r,
                                         float a_max, float d_max, float r_max,
                                         sq_param_id_t pa, sq_param_id_t pd,
                                         sq_param_id_t ps, sq_param_id_t pr)
{
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    gtk_box_append(GTK_BOX(row), make_knob_learn("A", 0.001f, a_max, a, pa));
    gtk_box_append(GTK_BOX(row), make_knob_learn("D", 0.001f, d_max, d, pd));
    gtk_box_append(GTK_BOX(row), make_knob_learn("S", 0, 1, s, ps));
    gtk_box_append(GTK_BOX(row), make_knob_learn("R", 0.001f, r_max, r, pr));
    return row;
}

/* ─── Helper: scroll wheel cycles dropdown values ─────────────────────────── */

static gboolean on_dropdown_scroll(GtkEventControllerScroll *ctrl,
                                    double dx, double dy, gpointer user_data)
{
    (void)ctrl; (void)dx;
    GtkDropDown *dd = GTK_DROP_DOWN(user_data);
    guint n = g_list_model_get_n_items(gtk_drop_down_get_model(dd));
    if (n == 0) return FALSE;
    int cur = (int)gtk_drop_down_get_selected(dd);
    if (dy > 0) cur++;
    else if (dy < 0) cur--;
    if (cur < 0) cur = 0;
    if (cur >= (int)n) cur = (int)n - 1;
    gtk_drop_down_set_selected(dd, (guint)cur);
    return TRUE;
}

static void add_dropdown_scroll(GtkWidget *dropdown)
{
    GtkEventController *scroll = gtk_event_controller_scroll_new(
        GTK_EVENT_CONTROLLER_SCROLL_VERTICAL);
    g_signal_connect(scroll, "scroll", G_CALLBACK(on_dropdown_scroll), dropdown);
    gtk_widget_add_controller(dropdown, GTK_EVENT_CONTROLLER(scroll));
}

/* ─── Helper: int-backed dropdown ──────────────────────────────────────────── */

static void on_int_dropdown_changed(GObject *obj, GParamSpec *pspec, gpointer data)
{
    (void)pspec;
    int *ptr = (int *)data;
    if (ptr) *ptr = (int)gtk_drop_down_get_selected(GTK_DROP_DOWN(obj));
}

static GtkWidget *make_int_dropdown(const char *label_text, const char **items,
                                     int count, int *value_ptr)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);

    GtkWidget *label = gtk_label_new(label_text);
    gtk_widget_set_size_request(label, 36, -1);
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_widget_add_css_class(label, "synth-label");
    gtk_box_append(GTK_BOX(box), label);

    /* Build NULL-terminated array for GtkStringList */
    const char **items_copy = g_new0(const char *, count + 1);
    for (int i = 0; i < count; i++) items_copy[i] = items[i];
    GtkStringList *model = gtk_string_list_new(items_copy);
    g_free(items_copy);

    GtkWidget *dropdown = gtk_drop_down_new(G_LIST_MODEL(model), NULL);
    int val = *value_ptr;
    if (val < 0) val = 0;
    if (val >= count) val = count - 1;
    gtk_drop_down_set_selected(GTK_DROP_DOWN(dropdown), (guint)val);
    gtk_widget_set_hexpand(dropdown, TRUE);
    gtk_box_append(GTK_BOX(box), dropdown);

    g_signal_connect(dropdown, "notify::selected",
                     G_CALLBACK(on_int_dropdown_changed), value_ptr);
    add_dropdown_scroll(dropdown);

    return box;
}

/* ─── Helper: bool toggle ─────────────────────────────────────────────────── */

static void on_toggle_changed(GtkCheckButton *btn, gpointer data)
{
    bool *ptr = (bool *)data;
    if (ptr) *ptr = gtk_check_button_get_active(btn);
}

static GtkWidget *make_toggle(const char *label_text, bool *value_ptr)
{
    GtkWidget *btn = gtk_check_button_new_with_label(label_text);
    gtk_check_button_set_active(GTK_CHECK_BUTTON(btn), *value_ptr);
    g_signal_connect(btn, "toggled", G_CALLBACK(on_toggle_changed), value_ptr);
    return btn;
}

static void add_section(GtkWidget *box, const char *title)
{
    GtkWidget *label = gtk_label_new(title);
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_widget_add_css_class(label, "synth-section");
    gtk_box_append(GTK_BOX(box), label);
}

/* ─── Get current preset ──────────────────────────────────────────────────── */

static sq_synth_preset_t *get_current_preset(int *out_idx)
{
    sq_engine_t *engine = g_gtk.engine;
    if (!engine) return NULL;

    int sel = g_gtk.app.selected_track;
    if (sel < 0) return NULL;

    int pi = engine->transport.current_pattern;
    if (pi < 0 || (uint32_t)pi >= engine->num_patterns) return NULL;

    sq_pattern_t *pat = &engine->patterns[pi];
    if ((uint32_t)sel >= pat->num_tracks) return NULL;
    if (pat->tracks[sel].type != TRACK_SYNTH) return NULL;

    int preset_idx = pat->tracks[sel].synth_preset;
    if (preset_idx < 0 || (uint32_t)preset_idx >= engine->num_synth_presets)
        return NULL;

    if (out_idx) *out_idx = preset_idx;
    return &engine->synth_presets[preset_idx];
}

/* ─── Rebuild controls for current preset ─────────────────────────────────── */

static void rebuild_controls(void)
{
    if (!s_content_box) return;

    /* Clear existing children */
    GtkWidget *child;
    while ((child = gtk_widget_get_first_child(s_content_box)) != NULL)
        gtk_box_remove(GTK_BOX(s_content_box), child);

    s_adsr_area = NULL;
    s_adsr_ptr = NULL;
    s_filter_adsr_area = NULL;
    s_filter_adsr_ptr = NULL;
    s_filter_curve_area = NULL;
    s_filter_preset = NULL;
    s_fm_algo_area = NULL;
    s_fm_preset = NULL;
    s_wt_area = NULL;
    s_wt_preset = NULL;

    int preset_idx = -1;
    sq_synth_preset_t *p = get_current_preset(&preset_idx);

    if (!p) {
        GtkWidget *lbl = gtk_label_new("Select a synth track");
        gtk_box_append(GTK_BOX(s_content_box), lbl);
        s_last_preset_idx = -1;
        return;
    }

    s_last_preset_idx = preset_idx;
    s_last_selected_track = g_gtk.app.selected_track;

    /* ── Preset selector dropdown ─────────────────────────────────── */
    {
        GtkWidget *sel_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);

        GtkWidget *sel_label = gtk_label_new("Preset:");
        gtk_box_append(GTK_BOX(sel_box), sel_label);

        /* Build string list of all presets */
        sq_engine_t *engine = g_gtk.engine;
        GtkStringList *model = gtk_string_list_new(NULL);
        for (uint32_t i = 0; i < engine->num_synth_presets; i++) {
            char item[48];
            snprintf(item, sizeof(item), "%d: %s", (int)i, engine->synth_presets[i].name);
            gtk_string_list_append(model, item);
        }

        GtkWidget *dropdown = gtk_drop_down_new(G_LIST_MODEL(model), NULL);
        gtk_drop_down_set_selected(GTK_DROP_DOWN(dropdown), (guint)preset_idx);
        gtk_widget_set_hexpand(dropdown, TRUE);
        gtk_box_append(GTK_BOX(sel_box), dropdown);

        g_signal_connect(dropdown, "notify::selected",
                         G_CALLBACK(on_preset_selected), NULL);
        add_dropdown_scroll(dropdown);

        gtk_box_append(GTK_BOX(s_content_box), sel_box);
    }

    /* Mode selector */
    gtk_box_append(GTK_BOX(s_content_box),
                  make_int_dropdown("Mode", synth_mode_names, 3, (int *)&p->synth_mode));

    /* ── 4-column layout (matching ImGui) ─────────────────────────── */
    GtkWidget *cols = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    gtk_widget_set_vexpand(cols, TRUE);
    gtk_box_append(GTK_BOX(s_content_box), cols);

    /* Column 1: Oscillators */
    GtkWidget *col1 = gtk_box_new(GTK_ORIENTATION_VERTICAL, 1);
    gtk_widget_set_hexpand(col1, TRUE);
    gtk_box_append(GTK_BOX(cols), col1);

    add_section(col1, "Oscillators");
    gtk_box_append(GTK_BOX(col1),
                  make_int_dropdown("Osc1", wave_names, 4, (int *)&p->osc1_wave));
    gtk_box_append(GTK_BOX(col1),
                  make_int_dropdown("Osc2", wave_names, 4, (int *)&p->osc2_wave));
    {
        GtkWidget *osc_knobs = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
        gtk_box_append(GTK_BOX(osc_knobs), make_knob("Mix", 0, 1, &p->osc_mix));
        gtk_box_append(GTK_BOX(osc_knobs), make_knob("Det", -24, 24, &p->osc2_detune));
        gtk_box_append(GTK_BOX(col1), osc_knobs);
    }
    {
        GtkWidget *uni_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
        GtkWidget *uni_lbl = gtk_label_new("Uni");
        gtk_widget_set_size_request(uni_lbl, 28, -1);
        gtk_widget_add_css_class(uni_lbl, "synth-label");
        gtk_box_append(GTK_BOX(uni_box), uni_lbl);
        GtkWidget *uni_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 1, 7, 1);
        gtk_range_set_value(GTK_RANGE(uni_scale), p->unison_voices);
        gtk_scale_set_draw_value(GTK_SCALE(uni_scale), FALSE);
        gtk_widget_set_hexpand(uni_scale, TRUE);
        g_object_set_data(G_OBJECT(uni_scale), "int_ptr", &p->unison_voices);
        g_signal_connect(uni_scale, "value-changed", G_CALLBACK(on_int_slider_changed), NULL);
        gtk_box_append(GTK_BOX(uni_box), uni_scale);
        gtk_box_append(GTK_BOX(col1), uni_box);
    }
    if (p->unison_voices > 1)
        gtk_box_append(GTK_BOX(col1), make_knob("Sprd", 0, 50, &p->unison_detune));

    /* Column 2: Filter */
    GtkWidget *col2 = gtk_box_new(GTK_ORIENTATION_VERTICAL, 1);
    gtk_widget_set_hexpand(col2, TRUE);
    gtk_box_append(GTK_BOX(cols), col2);

    add_section(col2, "Filter");
    gtk_box_append(GTK_BOX(col2),
                  make_int_dropdown("Type", filter_names, 3, (int *)&p->filter_type));
    {
        GtkWidget *filt_row1 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
        gtk_box_append(GTK_BOX(filt_row1), make_knob_learn("C", 20, 20000, &p->filter_cutoff, SQ_PARAM_FILTER_CUTOFF));
        gtk_box_append(GTK_BOX(filt_row1), make_knob_learn("R", 0.5, 20, &p->filter_resonance, SQ_PARAM_FILTER_RESONANCE));
        gtk_box_append(GTK_BOX(col2), filt_row1);

        GtkWidget *filt_row2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
        gtk_box_append(GTK_BOX(filt_row2), make_knob("E", -10000, 10000, &p->filter_env_depth));
        gtk_box_append(GTK_BOX(col2), filt_row2);
    }
    add_section(col2, "Filter Env");
    gtk_box_append(GTK_BOX(col2),
                  make_adsr_knobs(&p->filter_env.attack, &p->filter_env.decay,
                                  &p->filter_env.sustain, &p->filter_env.release,
                                  2.0f, 2.0f, 5.0f));

    /* Filter envelope ADSR visualization */
    s_filter_adsr_ptr = &p->filter_env;
    s_filter_adsr_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(s_filter_adsr_area, -1, 50);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(s_filter_adsr_area),
                                   filter_adsr_draw, NULL, NULL);
    gtk_box_append(GTK_BOX(col2), s_filter_adsr_area);

    /* Filter response curve */
    s_filter_preset = p;
    s_filter_curve_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(s_filter_curve_area, -1, 50);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(s_filter_curve_area),
                                   filter_curve_draw, NULL, NULL);
    gtk_box_append(GTK_BOX(col2), s_filter_curve_area);

    /* Column 3: Amp Envelope */
    GtkWidget *col3 = gtk_box_new(GTK_ORIENTATION_VERTICAL, 1);
    gtk_widget_set_hexpand(col3, TRUE);
    gtk_box_append(GTK_BOX(cols), col3);

    add_section(col3, "Amp Envelope");
    gtk_box_append(GTK_BOX(col3),
                  make_adsr_knobs_learn(&p->amp_env.attack, &p->amp_env.decay,
                                        &p->amp_env.sustain, &p->amp_env.release,
                                        2.0f, 2.0f, 5.0f,
                                        SQ_PARAM_AMP_ATTACK, SQ_PARAM_AMP_DECAY,
                                        SQ_PARAM_AMP_SUSTAIN, SQ_PARAM_AMP_RELEASE));

    s_adsr_ptr = &p->amp_env;
    s_adsr_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(s_adsr_area, -1, 60);
    gtk_widget_set_vexpand(s_adsr_area, TRUE);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(s_adsr_area),
                                   adsr_draw, NULL, NULL);

    /* Interactive drag on ADSR control points */
    GtkGesture *adsr_click = gtk_gesture_click_new();
    g_signal_connect(adsr_click, "pressed", G_CALLBACK(on_adsr_press), NULL);
    gtk_widget_add_controller(s_adsr_area, GTK_EVENT_CONTROLLER(adsr_click));

    GtkGesture *adsr_drag = gtk_gesture_drag_new();
    g_signal_connect(adsr_drag, "drag-update", G_CALLBACK(on_adsr_drag), NULL);
    g_signal_connect(adsr_drag, "drag-end", G_CALLBACK(on_adsr_drag_end), NULL);
    gtk_widget_add_controller(s_adsr_area, GTK_EVENT_CONTROLLER(adsr_drag));

    gtk_box_append(GTK_BOX(col3), s_adsr_area);

    /* Column 4: LFO */
    GtkWidget *col4 = gtk_box_new(GTK_ORIENTATION_VERTICAL, 1);
    gtk_widget_set_hexpand(col4, TRUE);
    gtk_box_append(GTK_BOX(cols), col4);

    add_section(col4, "LFO");
    gtk_box_append(GTK_BOX(col4),
                  make_int_dropdown("Wave", wave_names, 4, (int *)&p->lfo.waveform));
    gtk_box_append(GTK_BOX(col4),
                  make_int_dropdown("Dest", lfo_dest_names, 4, (int *)&p->lfo.dest));
    {
        GtkWidget *lfo_knobs = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
        gtk_box_append(GTK_BOX(lfo_knobs), make_knob("Rate", 0, 50, &p->lfo.rate));
        gtk_box_append(GTK_BOX(lfo_knobs), make_knob("Dep", 0, 1, &p->lfo.depth));
        gtk_box_append(GTK_BOX(col4), lfo_knobs);
    }
    gtk_box_append(GTK_BOX(col4), make_toggle("BPM Sync", &p->lfo_bpm_sync));
    if (p->lfo_bpm_sync)
        gtk_box_append(GTK_BOX(col4),
                      make_int_dropdown("Div", lfo_sync_names, 6, &p->lfo_sync_division));

    /* ── FM parameters ────────────────────────────────────────────── */
    if (p->synth_mode == SYNTH_FM) {
        add_section(s_content_box, "FM");

        /* Algorithm selector dropdown */
        gtk_box_append(GTK_BOX(s_content_box),
                      make_int_dropdown("Alg", fm_alg_names, FM_NUM_ALGORITHMS,
                                        &p->fm_algorithm));

        /* Algorithm diagram (80px tall Cairo drawing area) */
        s_fm_preset = p;
        s_fm_algo_area = gtk_drawing_area_new();
        gtk_widget_set_size_request(s_fm_algo_area, -1, 80);
        gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(s_fm_algo_area),
                                       fm_algo_draw, NULL, NULL);
        gtk_box_append(GTK_BOX(s_content_box), s_fm_algo_area);

        /* Operator controls in a 4-column horizontal layout */
        GtkWidget *fm_cols = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
        gtk_box_append(GTK_BOX(s_content_box), fm_cols);

        for (int op = 0; op < FM_NUM_OPERATORS; op++) {
            GtkWidget *op_col = gtk_box_new(GTK_ORIENTATION_VERTICAL, 1);
            gtk_widget_set_hexpand(op_col, TRUE);
            gtk_box_append(GTK_BOX(fm_cols), op_col);

            char ol[16];
            snprintf(ol, sizeof(ol), "Op %d", op + 1);
            add_section(op_col, ol);

            /* Ratio, Level, Feedback knobs */
            GtkWidget *knob_row1 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
            snprintf(ol, sizeof(ol), "Rat");
            gtk_box_append(GTK_BOX(knob_row1), make_knob(ol, 0.5f, 16, &p->fm_ops[op].freq_ratio));
            snprintf(ol, sizeof(ol), "Lvl");
            gtk_box_append(GTK_BOX(knob_row1), make_knob(ol, 0, 1, &p->fm_ops[op].level));
            snprintf(ol, sizeof(ol), "FB");
            gtk_box_append(GTK_BOX(knob_row1), make_knob(ol, 0, 1, &p->fm_ops[op].feedback));
            gtk_box_append(GTK_BOX(op_col), knob_row1);

            /* ADSR knobs */
            gtk_box_append(GTK_BOX(op_col),
                          make_adsr_knobs(&p->fm_ops[op].env.attack,
                                          &p->fm_ops[op].env.decay,
                                          &p->fm_ops[op].env.sustain,
                                          &p->fm_ops[op].env.release,
                                          5.0f, 5.0f, 5.0f));
        }
    }

    /* ── Wavetable parameters ─────────────────────────────────────── */
    if (p->synth_mode == SYNTH_WAVETABLE) {
        add_section(s_content_box, "Wavetable");

        /* Waveform visualizer */
        s_wt_preset = p;
        s_wt_area = gtk_drawing_area_new();
        gtk_widget_set_size_request(s_wt_area, -1, 60);
        gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(s_wt_area),
                                       wt_waveform_draw, NULL, NULL);
        gtk_box_append(GTK_BOX(s_content_box), s_wt_area);

        /* Knobs: Pos, EnvDep, LFODep */
        GtkWidget *wt_knobs = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
        gtk_box_append(GTK_BOX(wt_knobs), make_knob("Pos", 0, 1, &p->wt_position));
        gtk_box_append(GTK_BOX(wt_knobs), make_knob("EnvDep", -1, 1, &p->wt_env_depth));
        gtk_box_append(GTK_BOX(wt_knobs), make_knob("LFODep", 0, 1, &p->wt_lfo_depth));
        gtk_box_append(GTK_BOX(s_content_box), wt_knobs);
    }
}

/* ─── Called from redraw timer to check if rebuild needed ──────────────────── */

void gtk_synth_editor_update(void)
{
    int idx = -1;
    get_current_preset(&idx);
    if (idx != s_last_preset_idx || g_gtk.app.selected_track != s_last_selected_track)
        rebuild_controls();

    /* Redraw ADSR visualization */
    if (s_adsr_area && s_adsr_ptr)
        gtk_widget_queue_draw(s_adsr_area);

    /* Redraw filter envelope ADSR and filter curve */
    if (s_filter_adsr_area && s_filter_adsr_ptr)
        gtk_widget_queue_draw(s_filter_adsr_area);
    if (s_filter_curve_area && s_filter_preset)
        gtk_widget_queue_draw(s_filter_curve_area);

    /* Redraw wavetable waveform */
    if (s_wt_area && s_wt_preset)
        gtk_widget_queue_draw(s_wt_area);

    /* Redraw FM algorithm diagram */
    if (s_fm_algo_area && s_fm_preset)
        gtk_widget_queue_draw(s_fm_algo_area);
}

/* ─── Constructor ─────────────────────────────────────────────────────────── */

GtkWidget *gtk_synth_editor_new(void)
{
    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_propagate_natural_width(
        GTK_SCROLLED_WINDOW(scroll), FALSE);

    s_content_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_margin_start(s_content_box, 4);
    gtk_widget_set_margin_end(s_content_box, 4);
    gtk_widget_set_margin_top(s_content_box, 4);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), s_content_box);

    /* Build initial controls */
    rebuild_controls();

    return scroll;
}
