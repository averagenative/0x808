/*
 * gtk_browser.c — Sample browser showing loaded samples.
 */

#include "gtk_gui.h"
#include <stdio.h>

static void on_sample_selected(GtkListBox *list, GtkListBoxRow *row,
                                gpointer user_data)
{
    (void)list; (void)user_data;
    if (!row) return;

    int idx = gtk_list_box_row_get_index(row);
    int sel = g_gtk.app.selected_track;
    if (sel < 0) return;

    sq_engine_t *engine = g_gtk.engine;
    int pi = engine->transport.current_pattern;
    if (pi < 0 || (uint32_t)pi >= engine->num_patterns) return;
    sq_pattern_t *pat = &engine->patterns[pi];
    if ((uint32_t)sel >= pat->num_tracks) return;
    if (pat->tracks[sel].type != TRACK_SAMPLER) return;

    pat->tracks[sel].sample_index = idx;
    char msg[64];
    snprintf(msg, sizeof(msg), "Track %d → %s", sel + 1,
             engine->samples[idx].name);
    sq_app_set_status(&g_gtk.app, msg, 90);
}

GtkWidget *gtk_browser_new(void)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_margin_start(box, 4);
    gtk_widget_set_margin_end(box, 4);
    gtk_widget_set_margin_top(box, 4);

    GtkWidget *title = gtk_label_new("Samples");
    gtk_box_append(GTK_BOX(box), title);

    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_box_append(GTK_BOX(box), scroll);

    GtkWidget *list = gtk_list_box_new();
    g_signal_connect(list, "row-activated",
                     G_CALLBACK(on_sample_selected), NULL);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), list);

    /* Populate with loaded samples */
    sq_engine_t *engine = g_gtk.engine;
    for (uint32_t i = 0; i < engine->num_samples; i++) {
        GtkWidget *label = gtk_label_new(engine->samples[i].name);
        gtk_widget_set_halign(label, GTK_ALIGN_START);
        gtk_list_box_append(GTK_LIST_BOX(list), label);
    }

    return box;
}
