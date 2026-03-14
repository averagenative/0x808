/*
 * gtk_arrangement.c — Song/perform arrangement with section management.
 */

#include "gtk_gui.h"
#include <stdio.h>
#include <string.h>

static const double SECTION_COLORS[][3] = {
    {0.39, 0.63, 0.86},  /* blue */
    {0.86, 0.51, 0.24},  /* orange */
    {0.31, 0.78, 0.47},  /* green */
    {0.78, 0.31, 0.71},  /* pink */
    {0.86, 0.78, 0.24},  /* yellow */
    {0.47, 0.31, 0.78},  /* purple */
    {0.24, 0.78, 0.78},  /* cyan */
    {0.78, 0.39, 0.39},  /* salmon */
};
#define NUM_SECTION_COLORS 8

static GtkWidget *s_sections_box = NULL;
static GtkWidget *s_status_label = NULL;
static GtkWidget *s_editor_box = NULL;
static int s_selected_section = -1;

static void rebuild_sections(void);

static void on_section_clicked(GtkButton *btn, gpointer user_data)
{
    (void)btn;
    int idx = GPOINTER_TO_INT(user_data);
    sq_engine_t *engine = g_gtk.engine;

    if (engine->transport.mode == MODE_PERFORM) {
        engine->transport.queued_section = idx;
    } else {
        engine->transport.current_section = idx;
        s_selected_section = idx;
        if ((uint32_t)idx < engine->arrangement.num_sections)
            engine->transport.current_pattern =
                engine->arrangement.sections[idx].pattern_index;
    }
    rebuild_sections();
}

static void on_add_section(GtkButton *btn, gpointer user_data)
{
    (void)btn; (void)user_data;
    sq_engine_t *engine = g_gtk.engine;
    if (engine->arrangement.num_sections >= SQ_MAX_SECTIONS) return;

    int idx = (int)engine->arrangement.num_sections;
    engine->arrangement.sections[idx].pattern_index = engine->transport.current_pattern;
    engine->arrangement.sections[idx].repeat_count = 1;
    engine->arrangement.num_sections++;
    s_selected_section = idx;
    rebuild_sections();
}

static void on_remove_section(GtkButton *btn, gpointer user_data)
{
    (void)btn; (void)user_data;
    sq_engine_t *engine = g_gtk.engine;
    if (s_selected_section < 0 || (uint32_t)s_selected_section >= engine->arrangement.num_sections)
        return;

    for (uint32_t i = (uint32_t)s_selected_section; i < engine->arrangement.num_sections - 1; i++)
        engine->arrangement.sections[i] = engine->arrangement.sections[i + 1];
    engine->arrangement.num_sections--;
    if (s_selected_section >= (int)engine->arrangement.num_sections)
        s_selected_section = (int)engine->arrangement.num_sections - 1;
    rebuild_sections();
}

static void on_repeat_changed(GtkRange *range, gpointer user_data)
{
    (void)user_data;
    sq_engine_t *engine = g_gtk.engine;
    if (s_selected_section < 0 || (uint32_t)s_selected_section >= engine->arrangement.num_sections)
        return;
    engine->arrangement.sections[s_selected_section].repeat_count =
        (int)gtk_range_get_value(range);
}

static void on_section_pattern_changed(GObject *obj, GParamSpec *pspec, gpointer user_data)
{
    (void)pspec; (void)user_data;
    sq_engine_t *engine = g_gtk.engine;
    if (s_selected_section < 0 || (uint32_t)s_selected_section >= engine->arrangement.num_sections)
        return;
    engine->arrangement.sections[s_selected_section].pattern_index =
        (int)gtk_drop_down_get_selected(GTK_DROP_DOWN(obj));
}

static void rebuild_sections(void)
{
    sq_engine_t *engine = g_gtk.engine;

    /* Clear sections box */
    if (s_sections_box) {
        GtkWidget *child;
        while ((child = gtk_widget_get_first_child(s_sections_box)) != NULL)
            gtk_box_remove(GTK_BOX(s_sections_box), child);
    }

    /* Section buttons */
    for (uint32_t i = 0; i < engine->arrangement.num_sections; i++) {
        sq_section_t *sec = &engine->arrangement.sections[i];
        int ci = i % NUM_SECTION_COLORS;

        char label[48];
        int pi = sec->pattern_index;
        if (pi >= 0 && (uint32_t)pi < engine->num_patterns)
            snprintf(label, sizeof(label), "%s x%d",
                     engine->patterns[pi].name, sec->repeat_count);
        else
            snprintf(label, sizeof(label), "P%d x%d", pi + 1, sec->repeat_count);

        GtkWidget *btn = gtk_button_new_with_label(label);

        /* Section color coding — 8-color palette cycled by index */
        char color_class[24];
        snprintf(color_class, sizeof(color_class), "section-color-%d", ci);
        gtk_widget_add_css_class(btn, color_class);

        if ((int)i == s_selected_section || (int)i == engine->transport.current_section)
            gtk_widget_add_css_class(btn, "active");
        g_signal_connect(btn, "clicked", G_CALLBACK(on_section_clicked), GINT_TO_POINTER(i));
        gtk_box_append(GTK_BOX(s_sections_box), btn);
    }

    /* Add button */
    GtkWidget *add_btn = gtk_button_new_with_label("+");
    gtk_widget_set_size_request(add_btn, 30, -1);
    g_signal_connect(add_btn, "clicked", G_CALLBACK(on_add_section), NULL);
    gtk_box_append(GTK_BOX(s_sections_box), add_btn);

    /* Editor for selected section */
    if (s_editor_box) {
        GtkWidget *child;
        while ((child = gtk_widget_get_first_child(s_editor_box)) != NULL)
            gtk_box_remove(GTK_BOX(s_editor_box), child);
    }

    if (s_selected_section >= 0 && (uint32_t)s_selected_section < engine->arrangement.num_sections) {
        sq_section_t *sec = &engine->arrangement.sections[s_selected_section];

        char sec_label[32];
        snprintf(sec_label, sizeof(sec_label), "Section %d:", s_selected_section + 1);
        GtkWidget *lbl = gtk_label_new(sec_label);
        gtk_box_append(GTK_BOX(s_editor_box), lbl);

        /* Pattern selector */
        GtkStringList *model = gtk_string_list_new(NULL);
        for (uint32_t p = 0; p < engine->num_patterns; p++) {
            gtk_string_list_append(model, engine->patterns[p].name);
        }
        GtkWidget *pat_dd = gtk_drop_down_new(G_LIST_MODEL(model), NULL);
        gtk_drop_down_set_selected(GTK_DROP_DOWN(pat_dd), (guint)sec->pattern_index);
        g_signal_connect(pat_dd, "notify::selected",
                         G_CALLBACK(on_section_pattern_changed), NULL);
        gtk_box_append(GTK_BOX(s_editor_box), pat_dd);

        /* Repeat count */
        GtkWidget *rep_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 1, 16, 1);
        gtk_range_set_value(GTK_RANGE(rep_scale), sec->repeat_count);
        gtk_scale_set_draw_value(GTK_SCALE(rep_scale), TRUE);
        g_signal_connect(rep_scale, "value-changed", G_CALLBACK(on_repeat_changed), NULL);
        gtk_box_append(GTK_BOX(s_editor_box), rep_scale);

        /* Remove button */
        GtkWidget *rm_btn = gtk_button_new_with_label("Remove");
        g_signal_connect(rm_btn, "clicked", G_CALLBACK(on_remove_section), NULL);
        gtk_box_append(GTK_BOX(s_editor_box), rm_btn);
    }

    /* Status */
    if (s_status_label) {
        char status[64];
        if (engine->transport.mode == MODE_PERFORM)
            snprintf(status, sizeof(status), "Section %d | Queued: %s",
                     engine->transport.current_section + 1,
                     engine->transport.queued_section >= 0 ? "Yes" : "None");
        else if (engine->transport.mode == MODE_SONG)
            snprintf(status, sizeof(status), "Song: Section %d/%d",
                     engine->transport.current_section + 1,
                     engine->arrangement.num_sections);
        else
            snprintf(status, sizeof(status), "Pattern mode");
        gtk_label_set_text(GTK_LABEL(s_status_label), status);
    }
}

GtkWidget *gtk_arrangement_new(void)
{
    GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_margin_start(outer, 4);
    gtk_widget_set_margin_end(outer, 4);
    gtk_widget_set_margin_top(outer, 4);

    /* Sections row */
    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_NEVER);
    gtk_widget_set_size_request(scroll, -1, 36);
    gtk_box_append(GTK_BOX(outer), scroll);

    s_sections_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), s_sections_box);

    /* Editor area */
    s_editor_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_append(GTK_BOX(outer), s_editor_box);

    /* Status */
    s_status_label = gtk_label_new("");
    gtk_widget_set_halign(s_status_label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(outer), s_status_label);

    rebuild_sections();
    return outer;
}
