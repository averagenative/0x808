/*
 * gtk_mixer.c — Mixer with per-track channel strips, LED level meters,
 *               effects panel (Master + per-track, 3 slots each),
 *               and master output meter.
 *
 * Layout: [Track Meters] [Effects Panel] [Master Meter]
 */

#include "gtk_gui.h"
#include "engine/effects.h"
#include <stdio.h>
#include <math.h>

#define MAX_MIXER_STRIPS 16

/* ─── State ──────────────────────────────────────────────────────────────── */

static GtkWidget *s_vol_scales[MAX_MIXER_STRIPS];
static GtkWidget *s_mute_btns[MAX_MIXER_STRIPS];
static GtkWidget *s_strip_box;
static int s_num_strips = 0;

/* Level meter drawing areas */
static GtkWidget *s_track_meter_area;
static GtkWidget *s_master_meter_area;
static GtkWidget *s_strip_scroll;

/* Track meter scroll */
static int s_meter_scroll = 0;

/* Effects panel state */
static int s_fx_tab = 0;  /* 0 = Master, 1+ = track index+1 */
static GtkWidget *s_fx_label;
static GtkWidget *s_fx_prev_btn;
static GtkWidget *s_fx_next_btn;
static GtkWidget *s_fx_slots_box;

static const char *s_effect_type_names[] = {
    "None", "Filter", "Delay", "Reverb", "Overdrive", "Fuzz", "Chorus",
    "Bitcrusher", "Compressor", "Phaser", "Flanger", "Tremolo",
    "Ring Mod", "Tape", "Shimmer", "EQ", "Limiter", "Foldback", NULL
};
static const char *s_filter_mode_names[] = { "LowPass", "HiPass", "BandPass", NULL };
static const char *s_delay_sync_names[]  = { "1/1", "1/2", "1/4", "1/8", "1/16", NULL };

/* ─── Channel strip callbacks ────────────────────────────────────────────── */

static void on_vol_changed(GtkRange *range, gpointer user_data)
{
    int track = GPOINTER_TO_INT(user_data);
    sq_engine_t *engine = g_gtk.engine;
    int pi = engine->transport.current_pattern;
    if (pi < 0 || (uint32_t)pi >= engine->num_patterns) return;
    sq_pattern_t *pat = &engine->patterns[pi];
    if ((uint32_t)track >= pat->num_tracks) return;
    pat->tracks[track].volume = (float)gtk_range_get_value(range) / 100.0f;
}


static void on_mute_clicked(GtkButton *btn, gpointer user_data)
{
    (void)btn;
    int track = GPOINTER_TO_INT(user_data);
    sq_engine_t *engine = g_gtk.engine;
    int pi = engine->transport.current_pattern;
    if (pi < 0 || (uint32_t)pi >= engine->num_patterns) return;
    sq_pattern_t *pat = &engine->patterns[pi];
    if ((uint32_t)track >= pat->num_tracks) return;
    pat->tracks[track].mute = !pat->tracks[track].mute;
}

/* ─── LED-style level meter (Cairo) ──────────────────────────────────────── */

static void draw_led_meter(cairo_t *cr, double x, double y,
                           double w, double h, float level)
{
    /* Background */
    cairo_set_source_rgb(cr, 0.08, 0.08, 0.10);
    cairo_rectangle(cr, x, y, w, h);
    cairo_fill(cr);

    if (level > 1.5f) level = 1.5f;
    if (level < 0.0f) level = 0.0f;

    int segs = 12;
    double seg_gap = 1.0;
    double seg_h = (h - seg_gap * (segs - 1)) / segs;
    if (seg_h < 1.0) seg_h = 1.0;

    for (int i = 0; i < segs; i++) {
        double seg_y = y + h - (double)(i + 1) * (seg_h + seg_gap);
        float seg_level = (float)(i + 1) / (float)segs;

        if (seg_level <= level) {
            /* Lit */
            if (seg_level < 0.6f)
                cairo_set_source_rgba(cr, 0.16, 0.71, 0.16, 0.86);  /* green */
            else if (seg_level < 0.85f)
                cairo_set_source_rgba(cr, 0.78, 0.78, 0.16, 0.86);  /* yellow */
            else
                cairo_set_source_rgba(cr, 0.86, 0.16, 0.16, 0.86);  /* red */
        } else {
            /* Unlit (dim) */
            if (seg_level < 0.6f)
                cairo_set_source_rgb(cr, 0.06, 0.16, 0.06);
            else if (seg_level < 0.85f)
                cairo_set_source_rgb(cr, 0.16, 0.16, 0.06);
            else
                cairo_set_source_rgb(cr, 0.16, 0.06, 0.06);
        }

        cairo_rectangle(cr, x, seg_y, w, seg_h);
        cairo_fill(cr);
    }
}

/* ─── Track level meters draw callback ───────────────────────────────────── */

static void on_track_meters_draw(GtkDrawingArea *area, cairo_t *cr,
                                 int width, int height, gpointer user_data)
{
    (void)area; (void)user_data;
    sq_engine_t *engine = g_gtk.engine;
    int pi = engine->transport.current_pattern;
    if (pi < 0 || (uint32_t)pi >= engine->num_patterns) return;
    uint32_t num_tracks = engine->patterns[pi].num_tracks;
    if (num_tracks == 0) return;

    double meter_w = 10.0;
    double meter_gap = 2.0;
    int max_visible = (int)(((double)width - 4.0) / (meter_w + meter_gap));
    if (max_visible < 1) max_visible = 1;

    /* Clamp scroll */
    if (s_meter_scroll > (int)num_tracks - max_visible)
        s_meter_scroll = (int)num_tracks - max_visible;
    if (s_meter_scroll < 0) s_meter_scroll = 0;

    int end = s_meter_scroll + max_visible;
    if (end > (int)num_tracks) end = (int)num_tracks;

    /* Track number labels at top */
    cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 9);
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    for (int t = s_meter_scroll; t < end; t++) {
        int idx = t - s_meter_scroll;
        double mx = 2.0 + idx * (meter_w + meter_gap);
        char num[4];
        snprintf(num, sizeof(num), "%u", t + 1);
        cairo_move_to(cr, mx, 9);
        cairo_show_text(cr, num);
    }

    /* Meters below labels */
    double top_y = 12.0;
    double meter_area_h = height - top_y - 2.0;
    if (meter_area_h < 10.0) meter_area_h = 10.0;

    for (int t = s_meter_scroll; t < end; t++) {
        int idx = t - s_meter_scroll;
        double mx = 2.0 + idx * (meter_w + meter_gap);
        draw_led_meter(cr, mx, top_y, meter_w, meter_area_h,
                       engine->track_peaks[t]);
    }
}

/* ─── Master level meter draw callback ───────────────────────────────────── */

static void on_master_meter_draw(GtkDrawingArea *area, cairo_t *cr,
                                 int width, int height, gpointer user_data)
{
    (void)area; (void)user_data;
    sq_engine_t *engine = g_gtk.engine;

    /* Title */
    cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 9);
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_move_to(cr, 2, 10);
    cairo_show_text(cr, "MST");

    double top_y = 14.0;
    double meter_h = height - top_y - 14.0;
    if (meter_h < 10.0) meter_h = 10.0;

    /* L meter */
    draw_led_meter(cr, 2, top_y, 14, meter_h, engine->master_peak[0]);
    /* R meter */
    draw_led_meter(cr, 20, top_y, 14, meter_h, engine->master_peak[1]);

    /* L/R labels */
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_set_font_size(cr, 8);
    cairo_move_to(cr, 5, height - 1);
    cairo_show_text(cr, "L");
    cairo_move_to(cr, 23, height - 1);
    cairo_show_text(cr, "R");
}

/* ─── Channel strips (volume/pan/mute) ───────────────────────────────────── */

static void rebuild_strips(void)
{
    sq_engine_t *engine = g_gtk.engine;
    int pi = engine->transport.current_pattern;
    if (pi < 0 || (uint32_t)pi >= engine->num_patterns) return;
    sq_pattern_t *pat = &engine->patterns[pi];

    /* Remove old strips */
    GtkWidget *child;
    while ((child = gtk_widget_get_first_child(s_strip_box)) != NULL)
        gtk_box_remove(GTK_BOX(s_strip_box), child);

    s_num_strips = (int)pat->num_tracks;
    if (s_num_strips > MAX_MIXER_STRIPS) s_num_strips = MAX_MIXER_STRIPS;

    for (int t = 0; t < s_num_strips; t++) {
        GtkWidget *strip = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        gtk_widget_set_size_request(strip, 30, -1);
        gtk_widget_set_margin_start(strip, 0);
        gtk_widget_set_margin_end(strip, 0);
        /* Alternating background for visual separation */
        gtk_widget_add_css_class(strip, (t % 2 == 0) ? "strip-even" : "strip-odd");

        /* Track label */
        char label[16];
        snprintf(label, sizeof(label), "%d", t + 1);
        GtkWidget *lbl = gtk_label_new(label);
        gtk_widget_add_css_class(lbl, "strip-label");
        gtk_box_append(GTK_BOX(strip), lbl);

        /* Volume slider (vertical) */
        s_vol_scales[t] = gtk_scale_new_with_range(GTK_ORIENTATION_VERTICAL,
                                                    0, 100, 1);
        gtk_range_set_value(GTK_RANGE(s_vol_scales[t]),
                            pat->tracks[t].volume * 100.0f);
        gtk_range_set_inverted(GTK_RANGE(s_vol_scales[t]), TRUE);
        gtk_scale_set_draw_value(GTK_SCALE(s_vol_scales[t]), FALSE);
        gtk_widget_set_vexpand(s_vol_scales[t], TRUE);
        g_signal_connect(s_vol_scales[t], "value-changed",
                         G_CALLBACK(on_vol_changed), GINT_TO_POINTER(t));
        gtk_box_append(GTK_BOX(strip), s_vol_scales[t]);

        /* Pan knob (mini, range -1.0 to 1.0) */
        GtkWidget *pan_knob = gtk_knob_new(-1.0f, 1.0f,
                                            &pat->tracks[t].pan, "P");
        /* Override to mini size */
        gtk_widget_set_size_request(pan_knob, 30, 40);
        gtk_box_append(GTK_BOX(strip), pan_knob);

        /* Mute button */
        s_mute_btns[t] = gtk_button_new_with_label("M");
        gtk_widget_set_size_request(s_mute_btns[t], -1, 20);
        g_signal_connect(s_mute_btns[t], "clicked",
                         G_CALLBACK(on_mute_clicked), GINT_TO_POINTER(t));
        if (pat->tracks[t].mute)
            gtk_widget_add_css_class(s_mute_btns[t], "active");
        gtk_box_append(GTK_BOX(strip), s_mute_btns[t]);

        gtk_box_append(GTK_BOX(s_strip_box), strip);
    }
}

/* ─── Effects panel ──────────────────────────────────────────────────────── */

/* Get the effect slots for the current fx_tab, or NULL */
static sq_effect_slot_t *get_current_fx_slots(void)
{
    sq_engine_t *engine = g_gtk.engine;
    if (s_fx_tab == 0)
        return engine->master_effects;

    int pi = engine->transport.current_pattern;
    if (pi < 0 || (uint32_t)pi >= engine->num_patterns) return NULL;
    uint32_t ti = (uint32_t)(s_fx_tab - 1);
    if (ti >= engine->patterns[pi].num_tracks) return NULL;
    return engine->patterns[pi].tracks[ti].effects;
}

static uint32_t get_num_tracks(void)
{
    sq_engine_t *engine = g_gtk.engine;
    int pi = engine->transport.current_pattern;
    if (pi < 0 || (uint32_t)pi >= engine->num_patterns) return 0;
    return engine->patterns[pi].num_tracks;
}

static void update_fx_label(void)
{
    sq_engine_t *engine = g_gtk.engine;
    uint32_t nt = get_num_tracks();
    char buf[128];

    if (s_fx_tab == 0) {
        snprintf(buf, sizeof(buf), "Master Bus");
    } else {
        uint32_t ti = (uint32_t)(s_fx_tab - 1);
        int pi = engine->transport.current_pattern;
        if (pi >= 0 && (uint32_t)pi < engine->num_patterns && ti < nt) {
            sq_track_t *trk = &engine->patterns[pi].tracks[ti];
            const char *tname = "(empty)";
            const char *ttype = "Sampler";
            if (trk->type == TRACK_SYNTH) {
                ttype = "Synth";
                if (trk->synth_preset >= 0 &&
                    (uint32_t)trk->synth_preset < engine->num_synth_presets)
                    tname = engine->synth_presets[trk->synth_preset].name;
            } else if (trk->type == TRACK_SF2) {
                ttype = "SF2";
                if (trk->sf2_preset >= 0 &&
                    (uint32_t)trk->sf2_preset < engine->num_sf2_presets)
                    tname = engine->sf2_presets[trk->sf2_preset].name;
            } else {
                if (trk->sample_index >= 0 &&
                    (uint32_t)trk->sample_index < engine->num_samples)
                    tname = engine->samples[trk->sample_index].name;
            }
            snprintf(buf, sizeof(buf), "Trk %u: %s (%s)", ti + 1, tname, ttype);
        } else {
            snprintf(buf, sizeof(buf), "Trk %u", ti + 1);
        }
    }

    gtk_label_set_text(GTK_LABEL(s_fx_label), buf);

    /* Always allow cycling (wraps around) */
    gtk_widget_set_sensitive(s_fx_prev_btn, TRUE);
    gtk_widget_set_sensitive(s_fx_next_btn, TRUE);
}

/* ─── Effect slot widget builders ────────────────────────────────────────── */

/* Callback data for effect parameter changes */
typedef struct {
    sq_effect_slot_t *slot;
    int param_id;  /* identifies which parameter */
} fx_param_data_t;

static void on_fx_type_changed(GtkDropDown *dd, GParamSpec *pspec, gpointer user_data)
{
    (void)pspec;
    sq_effect_slot_t *slot = (sq_effect_slot_t *)user_data;
    int new_type = (int)gtk_drop_down_get_selected(dd);
    if ((sq_effect_type_t)new_type != slot->type) {
        effect_init(slot, (sq_effect_type_t)new_type, g_gtk.engine->sample_rate);
        /* Rebuild the effects panel to show new parameters */
        gtk_mixer_rebuild_fx();
    }
}

static void on_fx_bypass_toggled(GtkCheckButton *btn, gpointer user_data)
{
    sq_effect_slot_t *slot = (sq_effect_slot_t *)user_data;
    slot->bypass = gtk_check_button_get_active(btn);
}

/* Generic slider callback — uses param_id to route to correct field */
static void on_fx_slider_changed(GtkRange *range, gpointer user_data)
{
    fx_param_data_t *pd = (fx_param_data_t *)user_data;
    float val = (float)gtk_range_get_value(range);

    switch (pd->slot->type) {
    case EFFECT_FILTER:
        switch (pd->param_id) {
        case 0: pd->slot->filter.cutoff    = val; break;
        case 1: pd->slot->filter.resonance = val; break;
        case 2: pd->slot->filter.wet       = val / 100.0f; break;
        }
        break;
    case EFFECT_DELAY:
        switch (pd->param_id) {
        case 0: pd->slot->delay.time     = val; break;
        case 1: pd->slot->delay.feedback = val / 100.0f; break;
        case 2: pd->slot->delay.wet      = val / 100.0f; break;
        }
        break;
    case EFFECT_REVERB:
        switch (pd->param_id) {
        case 0: pd->slot->reverb.room_size = val / 100.0f; break;
        case 1: pd->slot->reverb.damping   = val / 100.0f; break;
        case 2: pd->slot->reverb.wet       = val / 100.0f; break;
        }
        break;
    case EFFECT_OVERDRIVE:
        switch (pd->param_id) {
        case 0: pd->slot->overdrive.drive = val / 100.0f; break;
        case 1: pd->slot->overdrive.tone  = val / 100.0f; break;
        case 2: pd->slot->overdrive.mix   = val / 100.0f; break;
        }
        break;
    case EFFECT_FUZZ:
        switch (pd->param_id) {
        case 0: pd->slot->fuzz.gain = val / 100.0f; break;
        case 1: pd->slot->fuzz.tone = val / 100.0f; break;
        case 2: pd->slot->fuzz.mix  = val / 100.0f; break;
        }
        break;
    case EFFECT_CHORUS:
        switch (pd->param_id) {
        case 0: pd->slot->chorus.rate  = val; break;
        case 1: pd->slot->chorus.depth = val / 100.0f; break;
        case 2: pd->slot->chorus.mix   = val / 100.0f; break;
        }
        break;
    case EFFECT_BITCRUSHER:
        switch (pd->param_id) {
        case 0: pd->slot->bitcrusher.bits       = val; break;
        case 1: pd->slot->bitcrusher.downsample = val; break;
        case 2: pd->slot->bitcrusher.mix        = val / 100.0f; break;
        }
        break;
    case EFFECT_COMPRESSOR:
        switch (pd->param_id) {
        case 0: pd->slot->compressor.threshold = val / 100.0f; break;
        case 1: pd->slot->compressor.ratio     = val; break;
        case 2: pd->slot->compressor.attack    = val; break;
        case 3: pd->slot->compressor.release   = val; break;
        case 4: pd->slot->compressor.makeup    = val / 100.0f; break;
        }
        break;
    case EFFECT_PHASER:
        switch (pd->param_id) {
        case 0: pd->slot->phaser.rate     = val; break;
        case 1: pd->slot->phaser.depth    = val / 100.0f; break;
        case 2: pd->slot->phaser.feedback = val / 100.0f; break;
        case 3: pd->slot->phaser.mix      = val / 100.0f; break;
        }
        break;
    case EFFECT_FLANGER:
        switch (pd->param_id) {
        case 0: pd->slot->flanger.rate     = val; break;
        case 1: pd->slot->flanger.depth    = val / 100.0f; break;
        case 2: pd->slot->flanger.feedback = val / 100.0f; break;
        case 3: pd->slot->flanger.mix      = val / 100.0f; break;
        }
        break;
    case EFFECT_TREMOLO:
        switch (pd->param_id) {
        case 0: pd->slot->tremolo.rate  = val; break;
        case 1: pd->slot->tremolo.depth = val / 100.0f; break;
        case 2: pd->slot->tremolo.wave  = (int)val; break;
        }
        break;
    case EFFECT_RINGMOD:
        switch (pd->param_id) {
        case 0: pd->slot->ringmod.freq = val; break;
        case 1: pd->slot->ringmod.mix  = val / 100.0f; break;
        }
        break;
    case EFFECT_TAPE:
        switch (pd->param_id) {
        case 0: pd->slot->tape.drive  = val / 100.0f; break;
        case 1: pd->slot->tape.warmth = val / 100.0f; break;
        case 2: pd->slot->tape.mix    = val / 100.0f; break;
        }
        break;
    case EFFECT_SHIMMER:
        switch (pd->param_id) {
        case 0: pd->slot->shimmer.decay   = val / 100.0f; break;
        case 1: pd->slot->shimmer.shimmer = val / 100.0f; break;
        case 2: pd->slot->shimmer.mix     = val / 100.0f; break;
        }
        break;
    default:
        break;
    }
}

static void on_filter_mode_changed(GtkDropDown *dd, GParamSpec *pspec, gpointer user_data)
{
    (void)pspec;
    sq_effect_slot_t *slot = (sq_effect_slot_t *)user_data;
    slot->filter.mode = (sq_efx_filter_mode_t)gtk_drop_down_get_selected(dd);
}

static void on_delay_sync_toggled(GtkCheckButton *btn, gpointer user_data)
{
    sq_effect_slot_t *slot = (sq_effect_slot_t *)user_data;
    slot->delay.bpm_sync = gtk_check_button_get_active(btn);
    /* Rebuild to show/hide sync division */
    gtk_mixer_rebuild_fx();
}

static void on_delay_div_changed(GtkDropDown *dd, GParamSpec *pspec, gpointer user_data)
{
    (void)pspec;
    sq_effect_slot_t *slot = (sq_effect_slot_t *)user_data;
    slot->delay.sync_division = (int)gtk_drop_down_get_selected(dd);
}

/* Pool of param data structs (freed on rebuild) */
static fx_param_data_t *s_param_pool = NULL;
static int s_param_pool_count = 0;
static int s_param_pool_cap = 0;

static fx_param_data_t *alloc_param_data(sq_effect_slot_t *slot, int param_id)
{
    if (s_param_pool_count >= s_param_pool_cap) {
        s_param_pool_cap = s_param_pool_cap ? s_param_pool_cap * 2 : 32;
        s_param_pool = realloc(s_param_pool, sizeof(fx_param_data_t) * s_param_pool_cap);
    }
    fx_param_data_t *pd = &s_param_pool[s_param_pool_count++];
    pd->slot = slot;
    pd->param_id = param_id;
    return pd;
}

/* Helper: add a labeled slider to a box (label + slider on one row) */
static void add_fx_slider(GtkWidget *box, const char *label_text,
                          sq_effect_slot_t *slot, int param_id,
                          float min, float max, float current, float step)
{
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_box_append(GTK_BOX(box), row);

    GtkWidget *lbl = gtk_label_new(label_text);
    gtk_widget_set_size_request(lbl, 55, -1);
    gtk_widget_set_halign(lbl, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(row), lbl);

    GtkWidget *scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL,
                                                 min, max, step);
    gtk_range_set_value(GTK_RANGE(scale), current);
    gtk_widget_set_hexpand(scale, TRUE);
    gtk_scale_set_draw_value(GTK_SCALE(scale), FALSE);

    fx_param_data_t *pd = alloc_param_data(slot, param_id);
    g_signal_connect(scale, "value-changed",
                     G_CALLBACK(on_fx_slider_changed), pd);
    gtk_box_append(GTK_BOX(row), scale);
}

/* Build parameter controls for one effect slot */
static GtkWidget *build_slot_widget(sq_effect_slot_t *slot, int slot_index)
{
    GtkWidget *frame = gtk_frame_new(NULL);
    gtk_widget_set_size_request(frame, 100, -1);
    gtk_widget_set_valign(frame, GTK_ALIGN_START);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 1);
    gtk_widget_set_margin_start(vbox, 3);
    gtk_widget_set_margin_end(vbox, 3);
    gtk_widget_set_margin_top(vbox, 2);
    gtk_widget_set_margin_bottom(vbox, 2);
    gtk_frame_set_child(GTK_FRAME(frame), vbox);

    /* Slot label */
    char title[32];
    snprintf(title, sizeof(title), "Slot %d", slot_index + 1);
    GtkWidget *title_lbl = gtk_label_new(title);
    gtk_widget_set_halign(title_lbl, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(vbox), title_lbl);

    /* Type selector dropdown */
    GtkStringList *type_model = gtk_string_list_new(s_effect_type_names);
    GtkWidget *type_dd = gtk_drop_down_new(G_LIST_MODEL(type_model), NULL);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(type_dd), (guint)slot->type);
    g_signal_connect(type_dd, "notify::selected",
                     G_CALLBACK(on_fx_type_changed), slot);
    gtk_box_append(GTK_BOX(vbox), type_dd);

    if (slot->type == EFFECT_NONE)
        return frame;

    /* Bypass checkbox */
    GtkWidget *bypass_cb = gtk_check_button_new_with_label("Bypass");
    gtk_check_button_set_active(GTK_CHECK_BUTTON(bypass_cb), slot->bypass);
    g_signal_connect(bypass_cb, "toggled",
                     G_CALLBACK(on_fx_bypass_toggled), slot);
    gtk_box_append(GTK_BOX(vbox), bypass_cb);

    /* Type-specific parameters */
    switch (slot->type) {
    case EFFECT_FILTER: {
        /* Mode dropdown */
        GtkStringList *mode_model = gtk_string_list_new(s_filter_mode_names);
        GtkWidget *mode_dd = gtk_drop_down_new(G_LIST_MODEL(mode_model), NULL);
        gtk_drop_down_set_selected(GTK_DROP_DOWN(mode_dd), (guint)slot->filter.mode);
        g_signal_connect(mode_dd, "notify::selected",
                         G_CALLBACK(on_filter_mode_changed), slot);
        gtk_box_append(GTK_BOX(vbox), mode_dd);

        add_fx_slider(vbox, "Cutoff", slot, 0,
                      20.0f, 20000.0f, slot->filter.cutoff, 1.0f);
        add_fx_slider(vbox, "Resonance", slot, 1,
                      0.5f, 20.0f, slot->filter.resonance, 0.1f);
        add_fx_slider(vbox, "Wet %", slot, 2,
                      0, 100, slot->filter.wet * 100.0f, 1.0f);
        break;
    }
    case EFFECT_DELAY: {
        add_fx_slider(vbox, "Time (s)", slot, 0,
                      0.01f, 2.0f, slot->delay.time, 0.01f);
        add_fx_slider(vbox, "Feedback %", slot, 1,
                      0, 95, slot->delay.feedback * 100.0f, 1.0f);
        add_fx_slider(vbox, "Wet %", slot, 2,
                      0, 100, slot->delay.wet * 100.0f, 1.0f);

        /* BPM sync checkbox */
        GtkWidget *sync_cb = gtk_check_button_new_with_label("BPM Sync");
        gtk_check_button_set_active(GTK_CHECK_BUTTON(sync_cb), slot->delay.bpm_sync);
        g_signal_connect(sync_cb, "toggled",
                         G_CALLBACK(on_delay_sync_toggled), slot);
        gtk_box_append(GTK_BOX(vbox), sync_cb);

        if (slot->delay.bpm_sync) {
            GtkStringList *div_model = gtk_string_list_new(s_delay_sync_names);
            GtkWidget *div_dd = gtk_drop_down_new(G_LIST_MODEL(div_model), NULL);
            gtk_drop_down_set_selected(GTK_DROP_DOWN(div_dd),
                                       (guint)slot->delay.sync_division);
            g_signal_connect(div_dd, "notify::selected",
                             G_CALLBACK(on_delay_div_changed), slot);
            gtk_box_append(GTK_BOX(vbox), div_dd);
        }
        break;
    }
    case EFFECT_REVERB:
        add_fx_slider(vbox, "Room %", slot, 0,
                      0, 100, slot->reverb.room_size * 100.0f, 1.0f);
        add_fx_slider(vbox, "Damping %", slot, 1,
                      0, 100, slot->reverb.damping * 100.0f, 1.0f);
        add_fx_slider(vbox, "Wet %", slot, 2,
                      0, 100, slot->reverb.wet * 100.0f, 1.0f);
        break;
    case EFFECT_OVERDRIVE:
        add_fx_slider(vbox, "Drive %", slot, 0,
                      0, 100, slot->overdrive.drive * 100.0f, 1.0f);
        add_fx_slider(vbox, "Tone %", slot, 1,
                      0, 100, slot->overdrive.tone * 100.0f, 1.0f);
        add_fx_slider(vbox, "Mix %", slot, 2,
                      0, 100, slot->overdrive.mix * 100.0f, 1.0f);
        break;
    case EFFECT_FUZZ:
        add_fx_slider(vbox, "Gain %", slot, 0,
                      0, 100, slot->fuzz.gain * 100.0f, 1.0f);
        add_fx_slider(vbox, "Tone %", slot, 1,
                      0, 100, slot->fuzz.tone * 100.0f, 1.0f);
        add_fx_slider(vbox, "Mix %", slot, 2,
                      0, 100, slot->fuzz.mix * 100.0f, 1.0f);
        break;
    case EFFECT_CHORUS:
        add_fx_slider(vbox, "Rate (Hz)", slot, 0,
                      0.1f, 10.0f, slot->chorus.rate, 0.1f);
        add_fx_slider(vbox, "Depth %", slot, 1,
                      0, 100, slot->chorus.depth * 100.0f, 1.0f);
        add_fx_slider(vbox, "Mix %", slot, 2,
                      0, 100, slot->chorus.mix * 100.0f, 1.0f);
        break;
    case EFFECT_BITCRUSHER:
        add_fx_slider(vbox, "Bits", slot, 0,
                      1.0f, 16.0f, slot->bitcrusher.bits, 1.0f);
        add_fx_slider(vbox, "Downsamp", slot, 1,
                      1.0f, 32.0f, slot->bitcrusher.downsample, 1.0f);
        add_fx_slider(vbox, "Mix %", slot, 2,
                      0, 100, slot->bitcrusher.mix * 100.0f, 1.0f);
        break;
    case EFFECT_COMPRESSOR:
        add_fx_slider(vbox, "Thresh %", slot, 0,
                      0, 100, slot->compressor.threshold * 100.0f, 1.0f);
        add_fx_slider(vbox, "Ratio", slot, 1,
                      1.0f, 20.0f, slot->compressor.ratio, 0.1f);
        add_fx_slider(vbox, "Attack (s)", slot, 2,
                      0.001f, 0.5f, slot->compressor.attack, 0.001f);
        add_fx_slider(vbox, "Release (s)", slot, 3,
                      0.01f, 1.0f, slot->compressor.release, 0.01f);
        add_fx_slider(vbox, "Makeup %", slot, 4,
                      0, 200, slot->compressor.makeup * 100.0f, 1.0f);
        break;
    case EFFECT_PHASER:
        add_fx_slider(vbox, "Rate (Hz)", slot, 0,
                      0.1f, 10.0f, slot->phaser.rate, 0.1f);
        add_fx_slider(vbox, "Depth %", slot, 1,
                      0, 100, slot->phaser.depth * 100.0f, 1.0f);
        add_fx_slider(vbox, "Feedback %", slot, 2,
                      0, 90, slot->phaser.feedback * 100.0f, 1.0f);
        add_fx_slider(vbox, "Mix %", slot, 3,
                      0, 100, slot->phaser.mix * 100.0f, 1.0f);
        break;
    case EFFECT_FLANGER:
        add_fx_slider(vbox, "Rate (Hz)", slot, 0,
                      0.1f, 10.0f, slot->flanger.rate, 0.1f);
        add_fx_slider(vbox, "Depth %", slot, 1,
                      0, 100, slot->flanger.depth * 100.0f, 1.0f);
        add_fx_slider(vbox, "Feedback %", slot, 2,
                      -90, 90, slot->flanger.feedback * 100.0f, 1.0f);
        add_fx_slider(vbox, "Mix %", slot, 3,
                      0, 100, slot->flanger.mix * 100.0f, 1.0f);
        break;
    case EFFECT_TREMOLO:
        add_fx_slider(vbox, "Rate (Hz)", slot, 0,
                      0.1f, 20.0f, slot->tremolo.rate, 0.1f);
        add_fx_slider(vbox, "Depth %", slot, 1,
                      0, 100, slot->tremolo.depth * 100.0f, 1.0f);
        add_fx_slider(vbox, "Wave", slot, 2,
                      0, 2, (float)slot->tremolo.wave, 1.0f);
        break;
    case EFFECT_RINGMOD:
        add_fx_slider(vbox, "Freq (Hz)", slot, 0,
                      20.0f, 5000.0f, slot->ringmod.freq, 1.0f);
        add_fx_slider(vbox, "Mix %", slot, 1,
                      0, 100, slot->ringmod.mix * 100.0f, 1.0f);
        break;
    case EFFECT_TAPE:
        add_fx_slider(vbox, "Drive %", slot, 0,
                      0, 100, slot->tape.drive * 100.0f, 1.0f);
        add_fx_slider(vbox, "Warmth %", slot, 1,
                      0, 100, slot->tape.warmth * 100.0f, 1.0f);
        add_fx_slider(vbox, "Mix %", slot, 2,
                      0, 100, slot->tape.mix * 100.0f, 1.0f);
        break;
    case EFFECT_SHIMMER:
        add_fx_slider(vbox, "Decay %", slot, 0,
                      0, 100, slot->shimmer.decay * 100.0f, 1.0f);
        add_fx_slider(vbox, "Shimmer %", slot, 1,
                      0, 100, slot->shimmer.shimmer * 100.0f, 1.0f);
        add_fx_slider(vbox, "Mix %", slot, 2,
                      0, 100, slot->shimmer.mix * 100.0f, 1.0f);
        break;
    default:
        break;
    }

    return frame;
}

/* Rebuild the 3 effect slot widgets */
void gtk_mixer_rebuild_fx(void)
{
    if (!s_fx_slots_box) return;

    /* Free param data pool */
    s_param_pool_count = 0;

    /* Clear old slot widgets */
    GtkWidget *child;
    while ((child = gtk_widget_get_first_child(s_fx_slots_box)) != NULL)
        gtk_box_remove(GTK_BOX(s_fx_slots_box), child);

    /* Clamp fx_tab */
    uint32_t nt = get_num_tracks();
    if (s_fx_tab > (int)nt) s_fx_tab = 0;

    update_fx_label();

    sq_effect_slot_t *slots = get_current_fx_slots();
    if (!slots) return;

    for (int i = 0; i < MAX_TRACK_EFFECTS; i++) {
        GtkWidget *slot_widget = build_slot_widget(&slots[i], i);
        gtk_box_append(GTK_BOX(s_fx_slots_box), slot_widget);
    }
}

static void on_fx_prev_clicked(GtkWidget *btn, gpointer user_data)
{
    (void)btn; (void)user_data;
    if (s_fx_tab > 0) {
        s_fx_tab--;
    } else {
        /* Wrap: master → last track */
        uint32_t nt = get_num_tracks();
        s_fx_tab = (int)nt;
    }
    gtk_mixer_rebuild_fx();
}

static void on_fx_next_clicked(GtkWidget *btn, gpointer user_data)
{
    (void)btn; (void)user_data;
    uint32_t nt = get_num_tracks();
    if (s_fx_tab < (int)nt) {
        s_fx_tab++;
    } else {
        /* Wrap: last track → master */
        s_fx_tab = 0;
    }
    gtk_mixer_rebuild_fx();
}

void gtk_mixer_set_fx_track(int track_index)
{
    /* -1 = master bus (s_fx_tab 0), 0+ = track (s_fx_tab = track_index + 1) */
    s_fx_tab = (track_index >= 0) ? track_index + 1 : 0;
    gtk_mixer_rebuild_fx();
}

/* ─── Public API ─────────────────────────────────────────────────────────── */

static void on_meter_scroll_left(GtkWidget *btn, gpointer user_data)
{
    (void)btn; (void)user_data;
    if (s_meter_scroll > 0) s_meter_scroll--;
}

static void on_meter_scroll_right(GtkWidget *btn, gpointer user_data)
{
    (void)btn; (void)user_data;
    s_meter_scroll++;
}

static void on_strip_scroll_left(GtkWidget *btn, gpointer user_data)
{
    (void)btn; (void)user_data;
    /* Scroll both strips and meters together */
    if (s_meter_scroll > 0) s_meter_scroll--;
    if (!s_strip_scroll) return;
    GtkAdjustment *adj = gtk_scrolled_window_get_hadjustment(
        GTK_SCROLLED_WINDOW(s_strip_scroll));
    double val = gtk_adjustment_get_value(adj);
    gtk_adjustment_set_value(adj, val - 32.0);
}

static void on_strip_scroll_right(GtkWidget *btn, gpointer user_data)
{
    (void)btn; (void)user_data;
    /* Scroll both strips and meters together */
    s_meter_scroll++;
    if (!s_strip_scroll) return;
    GtkAdjustment *adj = gtk_scrolled_window_get_hadjustment(
        GTK_SCROLLED_WINDOW(s_strip_scroll));
    double val = gtk_adjustment_get_value(adj);
    gtk_adjustment_set_value(adj, val + 32.0);
}

void gtk_mixer_queue_redraw(void)
{
    if (s_track_meter_area)
        gtk_widget_queue_draw(s_track_meter_area);
    if (s_master_meter_area)
        gtk_widget_queue_draw(s_master_meter_area);
}

GtkWidget *gtk_mixer_new(void)
{
    /*
     * Compact vertical layout matching ImGui's ~30% width allocation:
     *   [< Master Bus >]        ← browse bar
     *   [Slot 1] [Slot 2] [Slot 3]  ← effect dropdowns (horizontal)
     *   [Track meters | Strips | Master meter]  ← bottom row
     */
    GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_margin_start(outer, 2);
    gtk_widget_set_margin_end(outer, 2);

    /* ── Effects browse bar: [<] [label] [>] ── */
    GtkWidget *browse_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    gtk_box_append(GTK_BOX(outer), browse_bar);

    s_fx_prev_btn = gtk_button_new_with_label("<");
    gtk_widget_set_size_request(s_fx_prev_btn, 24, -1);
    g_signal_connect(s_fx_prev_btn, "clicked",
                     G_CALLBACK(on_fx_prev_clicked), NULL);
    gtk_box_append(GTK_BOX(browse_bar), s_fx_prev_btn);

    s_fx_label = gtk_label_new("Master Bus");
    gtk_widget_set_hexpand(s_fx_label, TRUE);
    gtk_label_set_ellipsize(GTK_LABEL(s_fx_label), PANGO_ELLIPSIZE_END);
    gtk_box_append(GTK_BOX(browse_bar), s_fx_label);

    s_fx_next_btn = gtk_button_new_with_label(">");
    gtk_widget_set_size_request(s_fx_next_btn, 24, -1);
    g_signal_connect(s_fx_next_btn, "clicked",
                     G_CALLBACK(on_fx_next_clicked), NULL);
    gtk_box_append(GTK_BOX(browse_bar), s_fx_next_btn);

    /* ── Effect slots (horizontal row, scrollable) ── */
    GtkWidget *slots_scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(slots_scroll),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(slots_scroll, TRUE);
    gtk_box_append(GTK_BOX(outer), slots_scroll);

    s_fx_slots_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(slots_scroll), s_fx_slots_box);

    gtk_mixer_rebuild_fx();

    /* ── Track selector row: [<] [1 2 3 4 5 ...] [>] [MST] ── */
    GtkWidget *track_sel = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    gtk_box_append(GTK_BOX(outer), track_sel);

    GtkWidget *sel_left = gtk_button_new_with_label("<");
    gtk_widget_set_size_request(sel_left, 20, 20);
    g_signal_connect(sel_left, "clicked",
                     G_CALLBACK(on_strip_scroll_left), NULL);
    gtk_box_append(GTK_BOX(track_sel), sel_left);

    /* Placeholder label — updated in rebuild */
    GtkWidget *sel_label = gtk_label_new("Tracks");
    gtk_widget_set_hexpand(sel_label, TRUE);
    gtk_box_append(GTK_BOX(track_sel), sel_label);

    GtkWidget *sel_right = gtk_button_new_with_label(">");
    gtk_widget_set_size_request(sel_right, 20, 20);
    g_signal_connect(sel_right, "clicked",
                     G_CALLBACK(on_strip_scroll_right), NULL);
    gtk_box_append(GTK_BOX(track_sel), sel_right);

    /* ── VU meters (full width) ── */
    s_track_meter_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(s_track_meter_area, -1, 30);
    gtk_widget_set_hexpand(s_track_meter_area, TRUE);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(s_track_meter_area),
                                   on_track_meters_draw, NULL, NULL);
    gtk_box_append(GTK_BOX(outer), s_track_meter_area);

    /* ── Channel strips (full width, scrollable) ── */
    GtkWidget *strip_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_vexpand(strip_row, TRUE);
    gtk_box_append(GTK_BOX(outer), strip_row);

    s_strip_scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(s_strip_scroll),
                                   GTK_POLICY_EXTERNAL, GTK_POLICY_NEVER);
    gtk_widget_set_hexpand(s_strip_scroll, TRUE);
    gtk_box_append(GTK_BOX(strip_row), s_strip_scroll);

    s_strip_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(s_strip_scroll), s_strip_box);
    rebuild_strips();

    /* Master meter (right of strips) */
    s_master_meter_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(s_master_meter_area, 36, -1);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(s_master_meter_area),
                                   on_master_meter_draw, NULL, NULL);
    gtk_box_append(GTK_BOX(strip_row), s_master_meter_area);

    return outer;
}
