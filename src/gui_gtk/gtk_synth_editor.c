/*
 * gtk_synth_editor.c — Synth parameter editor with sliders and ADSR.
 *
 * Rebuilds controls dynamically when the selected track changes.
 * Supports subtractive, FM, and wavetable mode parameters.
 */

#include "gtk_gui.h"
#include "engine/synth.h"
#include <math.h>
#include <string.h>
#include <stdio.h>

static const char *synth_mode_names[] = {"Subtractive", "FM", "Wavetable"};
static const char *wave_names[] = {"Saw", "Square", "Triangle", "Sine"};
static const char *filter_names[] = {"LowPass", "HiPass", "BandPass"};
static const char *lfo_dest_names[] = {"None", "Pitch", "Filter", "Amp"};
static const char *lfo_sync_names[] = {"1/1", "1/2", "1/4", "1/8", "1/16", "1/32"};

static int s_last_preset_idx = -1;   /* track which preset we built controls for */
static int s_last_selected_track = -1;
static GtkWidget *s_content_box = NULL;  /* inner box that holds all controls */
static GtkWidget *s_adsr_area = NULL;
static sq_adsr_params_t *s_adsr_ptr = NULL;

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

/* ─── Slider callback ─────────────────────────────────────────────────────── */

static void on_slider_changed(GtkRange *range, gpointer user_data)
{
    (void)user_data;
    float *ptr = (float *)g_object_get_data(G_OBJECT(range), "value_ptr");
    if (ptr) *ptr = (float)gtk_range_get_value(range);
}

static void on_int_slider_changed(GtkRange *range, gpointer user_data)
{
    (void)user_data;
    int *ptr = (int *)g_object_get_data(G_OBJECT(range), "int_ptr");
    if (ptr) *ptr = (int)gtk_range_get_value(range);
}

static GtkWidget *make_slider(const char *label_text, float min, float max,
                               float *value_ptr)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);

    GtkWidget *label = gtk_label_new(label_text);
    gtk_widget_set_size_request(label, 36, -1);
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_widget_add_css_class(label, "synth-label");
    gtk_box_append(GTK_BOX(box), label);

    GtkWidget *scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL,
                                                 (double)min, (double)max,
                                                 (double)(max - min) / 100.0);
    gtk_range_set_value(GTK_RANGE(scale), (double)*value_ptr);
    gtk_scale_set_draw_value(GTK_SCALE(scale), FALSE);
    gtk_widget_set_hexpand(scale, TRUE);
    gtk_box_append(GTK_BOX(box), scale);

    g_object_set_data(G_OBJECT(scale), "value_ptr", value_ptr);
    g_signal_connect(scale, "value-changed",
                     G_CALLBACK(on_slider_changed), NULL);

    return box;
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
    gtk_box_append(GTK_BOX(col1), make_slider("Mix", 0, 1, &p->osc_mix));
    gtk_box_append(GTK_BOX(col1), make_slider("Detune", -24, 24, &p->osc2_detune));
    {
        GtkWidget *uni_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
        GtkWidget *uni_lbl = gtk_label_new("Uni");
        gtk_widget_set_size_request(uni_lbl, 36, -1);
        gtk_widget_add_css_class(uni_lbl, "synth-label");
        gtk_box_append(GTK_BOX(uni_box), uni_lbl);
        GtkWidget *uni_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 1, 7, 1);
        gtk_range_set_value(GTK_RANGE(uni_scale), p->unison_voices);
        gtk_widget_set_hexpand(uni_scale, TRUE);
        g_object_set_data(G_OBJECT(uni_scale), "int_ptr", &p->unison_voices);
        g_signal_connect(uni_scale, "value-changed", G_CALLBACK(on_int_slider_changed), NULL);
        gtk_box_append(GTK_BOX(uni_box), uni_scale);
        gtk_box_append(GTK_BOX(col1), uni_box);
    }
    if (p->unison_voices > 1)
        gtk_box_append(GTK_BOX(col1), make_slider("Spread", 0, 50, &p->unison_detune));

    /* Column 2: Filter */
    GtkWidget *col2 = gtk_box_new(GTK_ORIENTATION_VERTICAL, 1);
    gtk_widget_set_hexpand(col2, TRUE);
    gtk_box_append(GTK_BOX(cols), col2);

    add_section(col2, "Filter");
    gtk_box_append(GTK_BOX(col2),
                  make_int_dropdown("Type", filter_names, 3, (int *)&p->filter_type));
    gtk_box_append(GTK_BOX(col2), make_slider("Cutoff", 20, 20000, &p->filter_cutoff));
    gtk_box_append(GTK_BOX(col2), make_slider("Reso", 0.5, 20, &p->filter_resonance));
    gtk_box_append(GTK_BOX(col2), make_slider("EnvDep", -10000, 10000, &p->filter_env_depth));
    gtk_box_append(GTK_BOX(col2), make_slider("F.Atk", 0.001, 2, &p->filter_env.attack));
    gtk_box_append(GTK_BOX(col2), make_slider("F.Dec", 0.001, 2, &p->filter_env.decay));
    gtk_box_append(GTK_BOX(col2), make_slider("F.Sus", 0, 1, &p->filter_env.sustain));
    gtk_box_append(GTK_BOX(col2), make_slider("F.Rel", 0.001, 5, &p->filter_env.release));

    /* Column 3: Amp Envelope */
    GtkWidget *col3 = gtk_box_new(GTK_ORIENTATION_VERTICAL, 1);
    gtk_widget_set_hexpand(col3, TRUE);
    gtk_box_append(GTK_BOX(cols), col3);

    add_section(col3, "Amp Envelope");
    gtk_box_append(GTK_BOX(col3), make_slider("A", 0.001, 2, &p->amp_env.attack));
    gtk_box_append(GTK_BOX(col3), make_slider("D", 0.001, 2, &p->amp_env.decay));
    gtk_box_append(GTK_BOX(col3), make_slider("S", 0, 1, &p->amp_env.sustain));
    gtk_box_append(GTK_BOX(col3), make_slider("R", 0.001, 5, &p->amp_env.release));

    s_adsr_ptr = &p->amp_env;
    s_adsr_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(s_adsr_area, -1, 60);
    gtk_widget_set_vexpand(s_adsr_area, TRUE);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(s_adsr_area),
                                   adsr_draw, NULL, NULL);
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
    gtk_box_append(GTK_BOX(col4), make_slider("Rate", 0, 50, &p->lfo.rate));
    gtk_box_append(GTK_BOX(col4), make_slider("Depth", 0, 1, &p->lfo.depth));
    gtk_box_append(GTK_BOX(col4), make_toggle("BPM Sync", &p->lfo_bpm_sync));
    if (p->lfo_bpm_sync)
        gtk_box_append(GTK_BOX(col4),
                      make_int_dropdown("Div", lfo_sync_names, 6, &p->lfo_sync_division));

    /* ── FM parameters ────────────────────────────────────────────── */
    if (p->synth_mode == SYNTH_FM) {
        add_section(s_content_box, "FM");
        for (int op = 0; op < FM_NUM_OPERATORS; op++) {
            char ol[16];
            snprintf(ol, sizeof(ol), "Op %d", op + 1);
            add_section(s_content_box, ol);
            snprintf(ol, sizeof(ol), "Ratio%d", op+1);
            gtk_box_append(GTK_BOX(s_content_box), make_slider(ol, 0.5, 16, &p->fm_ops[op].freq_ratio));
            snprintf(ol, sizeof(ol), "Level%d", op+1);
            gtk_box_append(GTK_BOX(s_content_box), make_slider(ol, 0, 1, &p->fm_ops[op].level));
            snprintf(ol, sizeof(ol), "FB%d", op+1);
            gtk_box_append(GTK_BOX(s_content_box), make_slider(ol, 0, 1, &p->fm_ops[op].feedback));
            snprintf(ol, sizeof(ol), "%d.A", op+1);
            gtk_box_append(GTK_BOX(s_content_box), make_slider(ol, 0.001, 5, &p->fm_ops[op].env.attack));
            snprintf(ol, sizeof(ol), "%d.D", op+1);
            gtk_box_append(GTK_BOX(s_content_box), make_slider(ol, 0.001, 5, &p->fm_ops[op].env.decay));
            snprintf(ol, sizeof(ol), "%d.S", op+1);
            gtk_box_append(GTK_BOX(s_content_box), make_slider(ol, 0, 1, &p->fm_ops[op].env.sustain));
            snprintf(ol, sizeof(ol), "%d.R", op+1);
            gtk_box_append(GTK_BOX(s_content_box), make_slider(ol, 0.001, 5, &p->fm_ops[op].env.release));
        }
    }

    /* ── Wavetable parameters ─────────────────────────────────────── */
    if (p->synth_mode == SYNTH_WAVETABLE) {
        add_section(s_content_box, "Wavetable");
        gtk_box_append(GTK_BOX(s_content_box), make_slider("Pos", 0, 1, &p->wt_position));
        gtk_box_append(GTK_BOX(s_content_box), make_slider("EnvDep", -1, 1, &p->wt_env_depth));
        gtk_box_append(GTK_BOX(s_content_box), make_slider("LFODep", 0, 1, &p->wt_lfo_depth));
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
}

/* ─── Constructor ─────────────────────────────────────────────────────────── */

GtkWidget *gtk_synth_editor_new(void)
{
    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);

    s_content_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
    gtk_widget_set_margin_start(s_content_box, 6);
    gtk_widget_set_margin_end(s_content_box, 6);
    gtk_widget_set_margin_top(s_content_box, 4);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), s_content_box);

    /* Build initial controls */
    rebuild_controls();

    return scroll;
}
