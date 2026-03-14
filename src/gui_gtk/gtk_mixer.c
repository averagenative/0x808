/*
 * gtk_mixer.c — Per-track channel strips with volume/pan sliders.
 */

#include "gtk_gui.h"
#include <stdio.h>

#define MAX_MIXER_STRIPS 16

static GtkWidget *s_vol_scales[MAX_MIXER_STRIPS];
static GtkWidget *s_pan_scales[MAX_MIXER_STRIPS];
static GtkWidget *s_mute_btns[MAX_MIXER_STRIPS];
static GtkWidget *s_strip_box;
static int s_num_strips = 0;

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

static void on_pan_changed(GtkRange *range, gpointer user_data)
{
    int track = GPOINTER_TO_INT(user_data);
    sq_engine_t *engine = g_gtk.engine;
    int pi = engine->transport.current_pattern;
    if (pi < 0 || (uint32_t)pi >= engine->num_patterns) return;
    sq_pattern_t *pat = &engine->patterns[pi];
    if ((uint32_t)track >= pat->num_tracks) return;
    pat->tracks[track].pan = (float)gtk_range_get_value(range) / 100.0f;
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
        GtkWidget *strip = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
        gtk_widget_set_size_request(strip, 60, -1);
        gtk_widget_set_margin_start(strip, 2);
        gtk_widget_set_margin_end(strip, 2);

        /* Track label */
        char label[16];
        snprintf(label, sizeof(label), "%d", t + 1);
        GtkWidget *lbl = gtk_label_new(label);
        gtk_box_append(GTK_BOX(strip), lbl);

        /* Volume slider (vertical) */
        s_vol_scales[t] = gtk_scale_new_with_range(GTK_ORIENTATION_VERTICAL,
                                                    0, 100, 1);
        gtk_range_set_value(GTK_RANGE(s_vol_scales[t]),
                            pat->tracks[t].volume * 100.0f);
        gtk_range_set_inverted(GTK_RANGE(s_vol_scales[t]), TRUE);
        gtk_widget_set_vexpand(s_vol_scales[t], TRUE);
        g_signal_connect(s_vol_scales[t], "value-changed",
                         G_CALLBACK(on_vol_changed), GINT_TO_POINTER(t));
        gtk_box_append(GTK_BOX(strip), s_vol_scales[t]);

        /* Pan slider */
        s_pan_scales[t] = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL,
                                                    -100, 100, 1);
        gtk_range_set_value(GTK_RANGE(s_pan_scales[t]),
                            pat->tracks[t].pan * 100.0f);
        g_signal_connect(s_pan_scales[t], "value-changed",
                         G_CALLBACK(on_pan_changed), GINT_TO_POINTER(t));
        gtk_box_append(GTK_BOX(strip), s_pan_scales[t]);

        /* Mute button */
        s_mute_btns[t] = gtk_button_new_with_label("M");
        g_signal_connect(s_mute_btns[t], "clicked",
                         G_CALLBACK(on_mute_clicked), GINT_TO_POINTER(t));
        if (pat->tracks[t].mute)
            gtk_widget_add_css_class(s_mute_btns[t], "active");
        gtk_box_append(GTK_BOX(strip), s_mute_btns[t]);

        gtk_box_append(GTK_BOX(s_strip_box), strip);
    }
}

GtkWidget *gtk_mixer_new(void)
{
    GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_margin_start(outer, 4);
    gtk_widget_set_margin_end(outer, 4);

    GtkWidget *title = gtk_label_new("Mixer");
    gtk_box_append(GTK_BOX(outer), title);

    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_NEVER);
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_box_append(GTK_BOX(outer), scroll);

    s_strip_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), s_strip_box);

    /* Build initial strips */
    rebuild_strips();

    return outer;
}
