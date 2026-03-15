/*
 * gtk_settings.c — Settings panel (GTK 4.0).
 *
 * Audio device selection and recording configuration.
 */

#include "gtk_gui.h"
#include "engine/sq_midi.h"
#include <SDL2/SDL.h>
#include <string.h>

#define LOG_TAG "gtk_settings"
#include "core/log.h"

/* ─── Audio device cache ─────────────────────────────────────────────────── */

#define MAX_DEVICES 32

static char  s_device_names[MAX_DEVICES][128];
static int   s_device_count = 0;

static GtkWidget *s_device_dropdown = NULL;
static GtkWidget *s_sample_rate_label = NULL;
static GtkWidget *s_dir_entry = NULL;
static GtkWidget *s_prefix_entry = NULL;
static GtkWidget *s_depth_dropdown = NULL;

static void refresh_devices(void)
{
    int n = SDL_GetNumAudioDevices(0);
    if (n > MAX_DEVICES) n = MAX_DEVICES;
    s_device_count = n;
    for (int i = 0; i < n; i++) {
        const char *name = SDL_GetAudioDeviceName(i, 0);
        if (name)
            snprintf(s_device_names[i], sizeof(s_device_names[i]), "%s", name);
        else
            snprintf(s_device_names[i], sizeof(s_device_names[i]), "Device %d", i);
    }
}

/* ─── Callbacks ──────────────────────────────────────────────────────────── */

static void on_apply_audio(GtkWidget *btn, gpointer user_data)
{
    (void)btn; (void)user_data;
    sq_audio_config_t *acfg = &g_gtk.app.audio_config;

    if (s_device_dropdown) {
        guint sel = gtk_drop_down_get_selected(GTK_DROP_DOWN(s_device_dropdown));
        if (sel == 0) {
            /* Default */
            acfg->device_name[0] = '\0';
            acfg->device_index = -1;
        } else if ((int)(sel - 1) < s_device_count) {
            snprintf(acfg->device_name, SQ_DEVICE_NAME_LEN, "%s",
                     s_device_names[sel - 1]);
            acfg->device_index = (int)(sel - 1);
        }
    }

    if (g_gtk.app.audio_restart_fn) {
        g_gtk.app.audio_restart_fn(g_gtk.app.audio_restart_userdata);
    }
}

static void on_refresh_devices(GtkWidget *btn, gpointer user_data)
{
    (void)btn; (void)user_data;
    refresh_devices();
    /* Rebuild the dropdown model */
    if (s_device_dropdown) {
        GtkStringList *model = gtk_string_list_new(NULL);
        gtk_string_list_append(model, "Default");
        for (int i = 0; i < s_device_count; i++)
            gtk_string_list_append(model, s_device_names[i]);
        gtk_drop_down_set_model(GTK_DROP_DOWN(s_device_dropdown),
                                G_LIST_MODEL(model));
        g_object_unref(model);
    }
}

static void on_dir_changed(GtkEditable *editable, gpointer user_data)
{
    (void)user_data;
    const char *text = gtk_editable_get_text(editable);
    if (text)
        snprintf(g_gtk.app.rec_config.output_dir, SQ_REC_DIR_LEN, "%s", text);
}

static void on_prefix_changed(GtkEditable *editable, gpointer user_data)
{
    (void)user_data;
    const char *text = gtk_editable_get_text(editable);
    if (text)
        snprintf(g_gtk.app.rec_config.prefix, SQ_REC_PREFIX_LEN, "%s", text);
}

static void on_depth_changed(GObject *obj, GParamSpec *pspec, gpointer user_data)
{
    (void)pspec; (void)user_data;
    guint sel = gtk_drop_down_get_selected(GTK_DROP_DOWN(obj));
    switch (sel) {
    case 0: g_gtk.app.rec_config.bit_depth = 16; break;
    case 1: g_gtk.app.rec_config.bit_depth = 24; break;
    case 2: g_gtk.app.rec_config.bit_depth = 32; break;
    }
}

#if GTK_CHECK_VERSION(4, 10, 0)
static void on_folder_selected(GObject *source, GAsyncResult *result, gpointer user_data)
{
    (void)user_data;
    GtkFileDialog *dialog = GTK_FILE_DIALOG(source);
    GFile *folder = gtk_file_dialog_select_folder_finish(dialog, result, NULL);
    if (folder) {
        char *path = g_file_get_path(folder);
        if (path) {
            snprintf(g_gtk.app.rec_config.output_dir, SQ_REC_DIR_LEN, "%s", path);
            if (s_dir_entry)
                gtk_editable_set_text(GTK_EDITABLE(s_dir_entry),
                                      g_gtk.app.rec_config.output_dir);
            g_free(path);
        }
        g_object_unref(folder);
    }
}

static void on_browse_folder(GtkWidget *btn, gpointer user_data)
{
    (void)btn; (void)user_data;
    GtkFileDialog *dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, "Select Recording Folder");

    GFile *init = g_file_new_for_path(g_gtk.app.rec_config.output_dir);
    gtk_file_dialog_set_initial_folder(dialog, init);
    g_object_unref(init);

    gtk_file_dialog_select_folder(dialog, GTK_WINDOW(g_gtk.window), NULL,
                                  on_folder_selected, NULL);
}
#endif

/* ─── Constructor ────────────────────────────────────────────────────────── */

static void on_midi_device_changed(GObject *obj, GParamSpec *pspec, gpointer user_data)
{
    (void)pspec; (void)user_data;
    sq_midi_t *m = (sq_midi_t *)g_gtk.midi;
    if (!m) return;
    guint sel = gtk_drop_down_get_selected(GTK_DROP_DOWN(obj));
    if (sel == 0) {
        sq_midi_close_port(m);
        g_gtk.app.midi_port_index = -1;
    } else {
        int port = (int)(sel - 1);
        sq_midi_open_port(m, port);
        g_gtk.app.midi_port_index = port;
    }
}

GtkWidget *gtk_settings_new(void)
{
    refresh_devices();

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_start(box, 12);
    gtk_widget_set_margin_end(box, 12);
    gtk_widget_set_margin_top(box, 8);
    gtk_widget_set_margin_bottom(box, 8);

    /* Title */
    GtkWidget *title = gtk_label_new("Settings");
    gtk_widget_add_css_class(title, "title-3");
    gtk_widget_set_halign(title, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(box), title);

    gtk_box_append(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

    /* ── Audio Device ─────────────────────────────────────────────── */
    GtkWidget *audio_label = gtk_label_new("Audio Device");
    gtk_widget_add_css_class(audio_label, "title-4");
    gtk_widget_set_halign(audio_label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(box), audio_label);

    /* Device dropdown */
    GtkStringList *dev_model = gtk_string_list_new(NULL);
    gtk_string_list_append(dev_model, "Default");
    for (int i = 0; i < s_device_count; i++)
        gtk_string_list_append(dev_model, s_device_names[i]);

    s_device_dropdown = gtk_drop_down_new(G_LIST_MODEL(dev_model), NULL);
    g_object_unref(dev_model);
    gtk_box_append(GTK_BOX(box), s_device_dropdown);

    /* Refresh + Apply buttons */
    GtkWidget *btn_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    GtkWidget *refresh_btn = gtk_button_new_with_label("Refresh");
    g_signal_connect(refresh_btn, "clicked", G_CALLBACK(on_refresh_devices), NULL);
    gtk_box_append(GTK_BOX(btn_box), refresh_btn);

    GtkWidget *apply_btn = gtk_button_new_with_label("Apply");
    g_signal_connect(apply_btn, "clicked", G_CALLBACK(on_apply_audio), NULL);
    gtk_box_append(GTK_BOX(btn_box), apply_btn);
    gtk_box_append(GTK_BOX(box), btn_box);

    /* Sample rate */
    char sr_text[64];
    snprintf(sr_text, sizeof(sr_text), "Sample Rate: %u Hz",
             g_gtk.engine ? g_gtk.engine->sample_rate : 44100);
    s_sample_rate_label = gtk_label_new(sr_text);
    gtk_widget_set_halign(s_sample_rate_label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(box), s_sample_rate_label);

    gtk_box_append(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

    /* ── MIDI Input ───────────────────────────────────────────────── */
    sq_midi_t *midi = (sq_midi_t *)g_gtk.midi;
    if (midi) {
        GtkWidget *midi_label = gtk_label_new("MIDI Input");
        gtk_widget_add_css_class(midi_label, "title-4");
        gtk_widget_set_halign(midi_label, GTK_ALIGN_START);
        gtk_box_append(GTK_BOX(box), midi_label);

        /* Refresh MIDI port list */
        int midi_ports = sq_midi_get_port_count(midi);
        GtkStringList *midi_model = gtk_string_list_new(NULL);
        gtk_string_list_append(midi_model, "None");
        for (int i = 0; i < midi_ports; i++)
            gtk_string_list_append(midi_model, sq_midi_get_port_name(midi, i));

        static GtkWidget *s_midi_dropdown = NULL;
        s_midi_dropdown = gtk_drop_down_new(G_LIST_MODEL(midi_model), NULL);
        g_object_unref(midi_model);

        /* Set current selection */
        int open_port = sq_midi_get_open_port(midi);
        gtk_drop_down_set_selected(GTK_DROP_DOWN(s_midi_dropdown),
                                   (open_port >= 0) ? (guint)(open_port + 1) : 0);

        g_signal_connect(s_midi_dropdown, "notify::selected",
                         G_CALLBACK(on_midi_device_changed), NULL);

        gtk_box_append(GTK_BOX(box), s_midi_dropdown);

        gtk_box_append(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    }

    /* ── Recording ────────────────────────────────────────────────── */
    GtkWidget *rec_label = gtk_label_new("Recording");
    gtk_widget_add_css_class(rec_label, "title-4");
    gtk_widget_set_halign(rec_label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(box), rec_label);

    /* Output directory */
    GtkWidget *dir_label = gtk_label_new("Output Directory:");
    gtk_widget_set_halign(dir_label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(box), dir_label);

    GtkWidget *dir_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    s_dir_entry = gtk_entry_new();
    gtk_editable_set_text(GTK_EDITABLE(s_dir_entry),
                          g_gtk.app.rec_config.output_dir);
    gtk_widget_set_hexpand(s_dir_entry, TRUE);
    g_signal_connect(s_dir_entry, "changed", G_CALLBACK(on_dir_changed), NULL);
    gtk_box_append(GTK_BOX(dir_box), s_dir_entry);

#if GTK_CHECK_VERSION(4, 10, 0)
    GtkWidget *browse_btn = gtk_button_new_with_label("Browse...");
    g_signal_connect(browse_btn, "clicked", G_CALLBACK(on_browse_folder), NULL);
    gtk_box_append(GTK_BOX(dir_box), browse_btn);
#endif
    gtk_box_append(GTK_BOX(box), dir_box);

    /* Filename prefix */
    GtkWidget *prefix_label = gtk_label_new("Filename Prefix:");
    gtk_widget_set_halign(prefix_label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(box), prefix_label);

    s_prefix_entry = gtk_entry_new();
    gtk_editable_set_text(GTK_EDITABLE(s_prefix_entry),
                          g_gtk.app.rec_config.prefix);
    g_signal_connect(s_prefix_entry, "changed", G_CALLBACK(on_prefix_changed), NULL);
    gtk_box_append(GTK_BOX(box), s_prefix_entry);

    /* Bit depth */
    GtkWidget *depth_label = gtk_label_new("Bit Depth:");
    gtk_widget_set_halign(depth_label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(box), depth_label);

    const char *depths[] = {"16-bit", "24-bit", "32-bit float", NULL};
    s_depth_dropdown = gtk_drop_down_new_from_strings(depths);
    int depth_idx = 0;
    if (g_gtk.app.rec_config.bit_depth == 24) depth_idx = 1;
    else if (g_gtk.app.rec_config.bit_depth == 32) depth_idx = 2;
    gtk_drop_down_set_selected(GTK_DROP_DOWN(s_depth_dropdown), depth_idx);
    g_signal_connect(s_depth_dropdown, "notify::selected",
                     G_CALLBACK(on_depth_changed), NULL);
    gtk_box_append(GTK_BOX(box), s_depth_dropdown);

    return box;
}

void gtk_settings_update(void)
{
    if (s_sample_rate_label && g_gtk.engine) {
        char sr_text[64];
        snprintf(sr_text, sizeof(sr_text), "Sample Rate: %u Hz",
                 g_gtk.engine->sample_rate);
        gtk_label_set_text(GTK_LABEL(s_sample_rate_label), sr_text);
    }
}
