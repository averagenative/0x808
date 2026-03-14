/*
 * gtk_export.c — WAV/MP3 export dialog.
 */

#include "gtk_gui.h"
#include "engine/export.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static GtkWidget *s_dialog = NULL;
static GtkWidget *s_filename_entry = NULL;
static GtkWidget *s_format_dropdown = NULL;
static GtkWidget *s_bars_scale = NULL;
static GtkWidget *s_duration_label = NULL;
static GtkWidget *s_status_label = NULL;

static void update_duration(void)
{
    if (!s_bars_scale || !s_duration_label) return;
    int bars = (int)gtk_range_get_value(GTK_RANGE(s_bars_scale));
    double bpm = g_gtk.engine->transport.bpm;
    char text[64];
    snprintf(text, sizeof(text), "Duration: %d bars at %.0f BPM (%.1fs)",
             bars, bpm, bars * 4.0 * 60.0 / bpm);
    gtk_label_set_text(GTK_LABEL(s_duration_label), text);
}

static void on_bars_changed(GtkRange *range, gpointer user_data)
{
    (void)range; (void)user_data;
    update_duration();
}

static void on_export_clicked(GtkButton *btn, gpointer user_data)
{
    (void)btn; (void)user_data;
    sq_engine_t *engine = g_gtk.engine;

    const char *filename = gtk_editable_get_text(GTK_EDITABLE(s_filename_entry));
    int bars = (int)gtk_range_get_value(GTK_RANGE(s_bars_scale));
    int format_idx = (int)gtk_drop_down_get_selected(GTK_DROP_DOWN(s_format_dropdown));

    /* Build full path */
    char path[1024];
    if (filename[0] == '/')
        snprintf(path, sizeof(path), "%s", filename);
    else
        snprintf(path, sizeof(path), "%s%s", engine->base_dir, filename);

    /* Configure export */
    sq_export_config_t config = {0};
    config.sample_rate = engine->sample_rate;
    config.num_bars = bars;
    config.pattern_index = engine->transport.current_pattern;

    /* Bit depth / format */
    int bit_depth = 16;
    int mp3_bitrate = 0;
    switch (format_idx) {
    case 0: bit_depth = 16; break;
    case 1: bit_depth = 24; break;
    case 2: bit_depth = 32; break;
    case 3: mp3_bitrate = 128; break;
    case 4: mp3_bitrate = 192; break;
    case 5: mp3_bitrate = 256; break;
    case 6: mp3_bitrate = 320; break;
    }
    config.bit_depth = (uint32_t)bit_depth;

    /* Render */
    sq_export_result_t result = {0};
    if (sq_export_render(engine, &config, &result) != 0) {
        gtk_label_set_text(GTK_LABEL(s_status_label), "Render failed!");
        return;
    }

    /* Write to file */
    int write_ok;
    if (mp3_bitrate > 0)
        write_ok = sq_export_write_mp3(path, &result, mp3_bitrate);
    else
        write_ok = sq_export_write_wav(path, &result, bit_depth);

    if (write_ok == 0) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Exported: %s (%.1fs, peak=%.2f)",
                 filename, (double)result.num_frames / result.sample_rate,
                 result.peak_level);
        gtk_label_set_text(GTK_LABEL(s_status_label), msg);
        sq_app_set_status(&g_gtk.app, "Export complete!", 120);
    } else {
        char msg[128];
        snprintf(msg, sizeof(msg), "Error writing %s", filename);
        gtk_label_set_text(GTK_LABEL(s_status_label), msg);
    }

    free(result.data);
}

static void on_close_clicked(GtkButton *btn, gpointer user_data)
{
    (void)btn; (void)user_data;
    if (s_dialog)
        gtk_window_close(GTK_WINDOW(s_dialog));
}

static void on_dialog_destroy(GtkWidget *widget, gpointer user_data)
{
    (void)widget; (void)user_data;
    s_dialog = NULL;
    s_filename_entry = NULL;
    s_format_dropdown = NULL;
    s_bars_scale = NULL;
    s_duration_label = NULL;
    s_status_label = NULL;
}

void gtk_export_show(GtkWidget *parent)
{
    /* Toggle: if open, close it */
    if (s_dialog) {
        gtk_window_destroy(GTK_WINDOW(s_dialog));
        return;
    }

    s_dialog = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(s_dialog), "Export Audio");
    gtk_window_set_default_size(GTK_WINDOW(s_dialog), 420, 300);
    gtk_window_set_transient_for(GTK_WINDOW(s_dialog), GTK_WINDOW(parent));
    gtk_window_set_modal(GTK_WINDOW(s_dialog), FALSE);
    g_signal_connect(s_dialog, "destroy", G_CALLBACK(on_dialog_destroy), NULL);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_start(box, 16);
    gtk_widget_set_margin_end(box, 16);
    gtk_widget_set_margin_top(box, 12);
    gtk_widget_set_margin_bottom(box, 12);
    gtk_window_set_child(GTK_WINDOW(s_dialog), box);

    /* Filename */
    GtkWidget *fn_label = gtk_label_new("Filename:");
    gtk_widget_set_halign(fn_label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(box), fn_label);

    s_filename_entry = gtk_entry_new();
    gtk_editable_set_text(GTK_EDITABLE(s_filename_entry), "output.wav");
    gtk_box_append(GTK_BOX(box), s_filename_entry);

    /* Format */
    GtkWidget *fmt_label = gtk_label_new("Format:");
    gtk_widget_set_halign(fmt_label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(box), fmt_label);

    const char *formats[] = {
        "WAV 16-bit", "WAV 24-bit", "WAV 32-float",
        "MP3 128k", "MP3 192k", "MP3 256k", "MP3 320k", NULL
    };
    GtkStringList *fmt_model = gtk_string_list_new(formats);
    s_format_dropdown = gtk_drop_down_new(G_LIST_MODEL(fmt_model), NULL);
    gtk_box_append(GTK_BOX(box), s_format_dropdown);

    /* Bars */
    GtkWidget *bars_label = gtk_label_new("Number of bars:");
    gtk_widget_set_halign(bars_label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(box), bars_label);

    s_bars_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 1, 32, 1);
    gtk_range_set_value(GTK_RANGE(s_bars_scale), 4);
    gtk_scale_set_draw_value(GTK_SCALE(s_bars_scale), TRUE);
    g_signal_connect(s_bars_scale, "value-changed", G_CALLBACK(on_bars_changed), NULL);
    gtk_box_append(GTK_BOX(box), s_bars_scale);

    /* Duration */
    s_duration_label = gtk_label_new("");
    gtk_widget_set_halign(s_duration_label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(box), s_duration_label);
    update_duration();

    /* Buttons */
    GtkWidget *btn_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_append(GTK_BOX(box), btn_box);

    GtkWidget *export_btn = gtk_button_new_with_label("Export");
    g_signal_connect(export_btn, "clicked", G_CALLBACK(on_export_clicked), NULL);
    gtk_box_append(GTK_BOX(btn_box), export_btn);

    GtkWidget *close_btn = gtk_button_new_with_label("Close");
    g_signal_connect(close_btn, "clicked", G_CALLBACK(on_close_clicked), NULL);
    gtk_box_append(GTK_BOX(btn_box), close_btn);

    /* Status */
    s_status_label = gtk_label_new("");
    gtk_widget_set_halign(s_status_label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(box), s_status_label);

    gtk_window_present(GTK_WINDOW(s_dialog));
}
