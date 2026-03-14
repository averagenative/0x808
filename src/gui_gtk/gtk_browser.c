/*
 * gtk_browser.c — Sample browser with directory navigation and file loading.
 *
 * Layout:
 * +--------------------------------------------------+
 * | Sample Browser                                    |
 * | Path: /home/user/samples                         |
 * | [Up] [Refresh] [Load Sample]    Samples: n/128   |
 * +--------------------------------------------------+
 * | > subdirectory/                          <DIR>    |
 * | kick_01.wav                             48.2 KB   |
 * | snare_tight.wav                         96.0 KB   |
 * +--------------------------------------------------+
 * | Loaded Samples                                    |
 * | 0: kick_01        1: snare_tight                  |
 * +--------------------------------------------------+
 * | Preview: kick_01                                  |
 * | [~~~~~~~~waveform~~~~~~~~]                        |
 * | 1.23s  |  44100 Hz  |  mono                      |
 * | [> Audition]                                      |
 * +--------------------------------------------------+
 */

#include "gtk_gui.h"
#include "formats/sample_io.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <stdlib.h>

/* --- Constants --- */
#define MAX_DIR_ENTRIES 256
#define MAX_PATH_LEN    1024
#define MAX_FULL_PATH   (MAX_PATH_LEN + 256 + 2)  /* path + name + slash + NUL */

/* --- Directory entry --- */
typedef struct {
    char   name[256];
    int    is_dir;
    off_t  size;
} browser_entry_t;

/* --- Browser state --- */
static char s_current_path[MAX_PATH_LEN] = "";
static browser_entry_t s_entries[MAX_DIR_ENTRIES];
static int s_num_entries = 0;
static int s_needs_refresh = 1;

/* --- Waveform preview state --- */
static int s_preview_sample_idx = -1;  /* index into engine->samples[] */

/* --- Audition button widget --- */
static GtkWidget *s_audition_btn = NULL;

/* --- Widgets that need updating --- */
static GtkWidget *s_path_label       = NULL;
static GtkWidget *s_file_list        = NULL;
static GtkWidget *s_loaded_list      = NULL;
static GtkWidget *s_count_label      = NULL;
static GtkWidget *s_waveform_area    = NULL;
static GtkWidget *s_waveform_name    = NULL;
static GtkWidget *s_waveform_info    = NULL;

/* --- Helpers --- */

static int is_audio_file(const char *name)
{
    const char *ext = strrchr(name, '.');
    if (!ext) return 0;
    return (g_ascii_strcasecmp(ext, ".wav") == 0 ||
            g_ascii_strcasecmp(ext, ".mp3") == 0 ||
            g_ascii_strcasecmp(ext, ".flac") == 0);
}

static int entry_cmp(const void *a, const void *b)
{
    const browser_entry_t *ea = (const browser_entry_t *)a;
    const browser_entry_t *eb = (const browser_entry_t *)b;
    if (ea->is_dir && !eb->is_dir) return -1;
    if (!ea->is_dir && eb->is_dir) return 1;
    return g_ascii_strcasecmp(ea->name, eb->name);
}

static void format_size(off_t size, char *buf, size_t buflen)
{
    if (size < 1024)
        snprintf(buf, buflen, "%ld B", (long)size);
    else if (size < 1024 * 1024)
        snprintf(buf, buflen, "%.1f KB", (double)size / 1024.0);
    else
        snprintf(buf, buflen, "%.1f MB", (double)size / (1024.0 * 1024.0));
}

/* --- Directory scanning --- */

static void refresh_directory(void)
{
    s_num_entries = 0;

    /* Default path: engine base_dir + "samples", fallback to cwd */
    if (s_current_path[0] == '\0') {
        sq_engine_t *engine = g_gtk.engine;
        if (engine->base_dir[0] != '\0') {
            snprintf(s_current_path, sizeof(s_current_path),
                     "%s/samples", engine->base_dir);
        }
        /* If that dir doesn't exist, try base_dir itself */
        struct stat st;
        if (s_current_path[0] == '\0' || stat(s_current_path, &st) != 0) {
            if (engine->base_dir[0] != '\0') {
                strncpy(s_current_path, engine->base_dir,
                        sizeof(s_current_path) - 1);
                s_current_path[sizeof(s_current_path) - 1] = '\0';
            } else {
                char *cwd = g_get_current_dir();
                strncpy(s_current_path, cwd, sizeof(s_current_path) - 1);
                s_current_path[sizeof(s_current_path) - 1] = '\0';
                g_free(cwd);
            }
        }
    }

    GDir *dir = g_dir_open(s_current_path, 0, NULL);
    if (!dir) return;

    const gchar *name;
    while ((name = g_dir_read_name(dir)) != NULL &&
           s_num_entries < MAX_DIR_ENTRIES) {
        /* Skip hidden files */
        if (name[0] == '.') continue;

        char fullpath[MAX_FULL_PATH];
        snprintf(fullpath, sizeof(fullpath), "%s/%s", s_current_path, name);

        struct stat st;
        if (stat(fullpath, &st) != 0) continue;

        int is_dir = S_ISDIR(st.st_mode);
        if (!is_dir && !is_audio_file(name)) continue;

        browser_entry_t *e = &s_entries[s_num_entries];
        strncpy(e->name, name, sizeof(e->name) - 1);
        e->name[sizeof(e->name) - 1] = '\0';
        e->is_dir = is_dir;
        e->size = is_dir ? 0 : st.st_size;
        s_num_entries++;
    }

    g_dir_close(dir);

    if (s_num_entries > 0)
        qsort(s_entries, (size_t)s_num_entries, sizeof(browser_entry_t),
              entry_cmp);

    s_needs_refresh = 0;
}

/* --- UI update helpers --- */

static void update_path_label(void)
{
    if (s_path_label) {
        char buf[MAX_PATH_LEN + 8];
        snprintf(buf, sizeof(buf), "Path: %s", s_current_path);
        gtk_label_set_text(GTK_LABEL(s_path_label), buf);
    }
}

static void update_count_label(void)
{
    if (s_count_label) {
        sq_engine_t *engine = g_gtk.engine;
        char buf[64];
        snprintf(buf, sizeof(buf), "Samples: %u/%d",
                 engine->num_samples, SQ_MAX_SAMPLES);
        gtk_label_set_text(GTK_LABEL(s_count_label), buf);
    }
}

static void rebuild_loaded_list(void)
{
    if (!s_loaded_list) return;

    /* Remove all existing children */
    GtkWidget *child;
    while ((child = gtk_widget_get_first_child(s_loaded_list)) != NULL)
        gtk_list_box_remove(GTK_LIST_BOX(s_loaded_list), child);

    sq_engine_t *engine = g_gtk.engine;
    for (uint32_t i = 0; i < engine->num_samples; i++) {
        char buf[128];
        snprintf(buf, sizeof(buf), "%u: %s", i, engine->samples[i].name);
        GtkWidget *label = gtk_label_new(buf);
        gtk_widget_set_halign(label, GTK_ALIGN_START);
        gtk_list_box_append(GTK_LIST_BOX(s_loaded_list), label);
    }

    update_count_label();
}

/* Rebuild the file list widget from s_entries[] */
static void rebuild_file_list(void)
{
    if (!s_file_list) return;

    /* Remove all existing children */
    GtkWidget *child;
    while ((child = gtk_widget_get_first_child(s_file_list)) != NULL)
        gtk_list_box_remove(GTK_LIST_BOX(s_file_list), child);

    for (int i = 0; i < s_num_entries; i++) {
        browser_entry_t *e = &s_entries[i];

        GtkWidget *row_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        gtk_widget_set_margin_start(row_box, 4);
        gtk_widget_set_margin_end(row_box, 4);

        /* Name label */
        char name_buf[280];
        if (e->is_dir)
            snprintf(name_buf, sizeof(name_buf), "> %s/", e->name);
        else
            snprintf(name_buf, sizeof(name_buf), "  %s", e->name);

        GtkWidget *name_label = gtk_label_new(name_buf);
        gtk_widget_set_halign(name_label, GTK_ALIGN_START);
        gtk_widget_set_hexpand(name_label, TRUE);
        gtk_box_append(GTK_BOX(row_box), name_label);

        /* Size label */
        if (e->is_dir) {
            GtkWidget *size_label = gtk_label_new("<DIR>");
            gtk_widget_set_halign(size_label, GTK_ALIGN_END);
            gtk_box_append(GTK_BOX(row_box), size_label);
        } else {
            char size_str[32];
            format_size(e->size, size_str, sizeof(size_str));
            GtkWidget *size_label = gtk_label_new(size_str);
            gtk_widget_set_halign(size_label, GTK_ALIGN_END);
            gtk_box_append(GTK_BOX(row_box), size_label);
        }

        gtk_list_box_append(GTK_LIST_BOX(s_file_list), row_box);
    }

    if (s_num_entries == 0) {
        GtkWidget *label = gtk_label_new("(no audio files)");
        gtk_widget_set_halign(label, GTK_ALIGN_START);
        gtk_list_box_append(GTK_LIST_BOX(s_file_list), label);
    }

    update_path_label();
}

/* --- Waveform drawing --- */

static void waveform_draw_cb(GtkDrawingArea *area, cairo_t *cr,
                             int width, int height, gpointer user_data)
{
    (void)area; (void)user_data;

    /* Dark background */
    cairo_set_source_rgb(cr, 0.10, 0.10, 0.12);
    cairo_rectangle(cr, 0, 0, width, height);
    cairo_fill(cr);

    sq_engine_t *engine = g_gtk.engine;
    if (s_preview_sample_idx < 0 ||
        (uint32_t)s_preview_sample_idx >= engine->num_samples) {
        /* No sample selected — draw placeholder text */
        cairo_set_source_rgb(cr, 0.35, 0.35, 0.40);
        cairo_select_font_face(cr, "monospace",
                               CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, 11.0);
        cairo_move_to(cr, 8, height / 2.0 + 4);
        cairo_show_text(cr, "Select a loaded sample to preview");
        return;
    }

    sq_sample_t *smp = &engine->samples[s_preview_sample_idx];
    if (!smp->data || smp->num_frames == 0) return;

    float *sdata   = smp->data;
    uint32_t nframes = smp->num_frames;
    uint32_t nch     = smp->num_channels;

    /* Center line (dim) */
    double cy = height * 0.5;
    cairo_set_source_rgb(cr, 0.24, 0.24, 0.26);
    cairo_set_line_width(cr, 1.0);
    cairo_move_to(cr, 0, cy);
    cairo_line_to(cr, width, cy);
    cairo_stroke(cr);

    /* Draw waveform: min/max vertical lines per pixel (blue/cyan) */
    cairo_set_source_rgba(cr, 0.31, 0.71, 0.86, 0.85);
    cairo_set_line_width(cr, 1.0);

    int pw = width;
    if (pw <= 0) return;

    for (int px = 0; px < pw; px++) {
        uint32_t start = (uint32_t)((uint64_t)px * nframes / (uint32_t)pw);
        uint32_t end   = (uint32_t)((uint64_t)(px + 1) * nframes / (uint32_t)pw);
        if (end > nframes) end = nframes;

        float mn = 0.0f, mx = 0.0f;
        for (uint32_t f = start; f < end; f++) {
            float v = sdata[f * nch]; /* left channel */
            if (v < mn) mn = v;
            if (v > mx) mx = v;
        }

        double half_h = height * 0.5;
        double y0 = cy - mx * half_h;
        double y1 = cy - mn * half_h;
        if (y1 - y0 < 1.0) y1 = y0 + 1.0;

        cairo_move_to(cr, px + 0.5, y0);
        cairo_line_to(cr, px + 0.5, y1);
    }
    cairo_stroke(cr);
}

static void update_waveform_labels(void)
{
    sq_engine_t *engine = g_gtk.engine;

    if (s_preview_sample_idx < 0 ||
        (uint32_t)s_preview_sample_idx >= engine->num_samples) {
        if (s_waveform_name)
            gtk_label_set_text(GTK_LABEL(s_waveform_name), "Waveform Preview");
        if (s_waveform_info)
            gtk_label_set_text(GTK_LABEL(s_waveform_info), "");
        return;
    }

    sq_sample_t *smp = &engine->samples[s_preview_sample_idx];

    if (s_waveform_name) {
        char buf[128];
        snprintf(buf, sizeof(buf), "Preview: %s", smp->name);
        gtk_label_set_text(GTK_LABEL(s_waveform_name), buf);
    }

    if (s_waveform_info) {
        char buf[128];
        float duration = (smp->sample_rate > 0)
            ? (float)smp->num_frames / (float)smp->sample_rate
            : 0.0f;
        snprintf(buf, sizeof(buf), "%.2fs  |  %u Hz  |  %s",
                 duration, smp->sample_rate,
                 smp->num_channels == 1 ? "mono" : "stereo");
        gtk_label_set_text(GTK_LABEL(s_waveform_info), buf);
    }
}

/* --- Callbacks --- */

static void on_file_activated(GtkListBox *list, GtkListBoxRow *row,
                              gpointer user_data)
{
    (void)list; (void)user_data;
    if (!row) return;

    int idx = gtk_list_box_row_get_index(row);
    if (idx < 0 || idx >= s_num_entries) return;

    browser_entry_t *e = &s_entries[idx];

    if (e->is_dir) {
        /* Navigate into directory */
        char newpath[MAX_FULL_PATH];
        snprintf(newpath, sizeof(newpath), "%s/%s", s_current_path, e->name);

        char *resolved = realpath(newpath, NULL);
        if (resolved) {
            strncpy(s_current_path, resolved, sizeof(s_current_path) - 1);
            s_current_path[sizeof(s_current_path) - 1] = '\0';
            free(resolved);
        } else {
            strncpy(s_current_path, newpath, sizeof(s_current_path) - 1);
            s_current_path[sizeof(s_current_path) - 1] = '\0';
        }

        s_needs_refresh = 1;
        refresh_directory();
        rebuild_file_list();
        return;
    }

    /* Audio file: load into engine */
    sq_engine_t *engine = g_gtk.engine;
    if (engine->num_samples >= SQ_MAX_SAMPLES) {
        sq_app_set_status(&g_gtk.app, "Sample slots full", 120);
        return;
    }

    char filepath[MAX_FULL_PATH];
    snprintf(filepath, sizeof(filepath), "%s/%s", s_current_path, e->name);

    int si = (int)engine->num_samples;
    if (sample_io_load(filepath, &engine->samples[si]) == 0) {
        engine->num_samples++;

        /* Assign to selected track if it's a sampler track */
        int sel = g_gtk.app.selected_track;
        if (sel >= 0) {
            int pi = engine->transport.current_pattern;
            if (pi >= 0 && (uint32_t)pi < engine->num_patterns) {
                sq_pattern_t *pat = &engine->patterns[pi];
                if ((uint32_t)sel < pat->num_tracks &&
                    pat->tracks[sel].type == TRACK_SAMPLER) {
                    pat->tracks[sel].sample_index = si;
                }
            }
        }

        char msg[128];
        snprintf(msg, sizeof(msg), "Loaded: %s [%d]",
                 engine->samples[si].name, si);
        sq_app_set_status(&g_gtk.app, msg, 120);

        rebuild_loaded_list();
    } else {
        sq_app_set_status(&g_gtk.app, "Failed to load sample", 120);
    }
}

static void on_loaded_sample_selected(GtkListBox *list, GtkListBoxRow *row,
                                      gpointer user_data)
{
    (void)list; (void)user_data;
    if (!row) return;

    int idx = gtk_list_box_row_get_index(row);
    sq_engine_t *engine = g_gtk.engine;

    /* Update waveform preview */
    if (idx >= 0 && (uint32_t)idx < engine->num_samples) {
        s_preview_sample_idx = idx;
        update_waveform_labels();
        if (s_waveform_area)
            gtk_widget_queue_draw(s_waveform_area);
    }

    /* Assign to selected track if it's a sampler track */
    int sel = g_gtk.app.selected_track;
    if (sel < 0) return;

    int pi = engine->transport.current_pattern;
    if (pi < 0 || (uint32_t)pi >= engine->num_patterns) return;
    sq_pattern_t *pat = &engine->patterns[pi];
    if ((uint32_t)sel >= pat->num_tracks) return;
    if (pat->tracks[sel].type != TRACK_SAMPLER) return;

    pat->tracks[sel].sample_index = idx;
    char msg[128];
    snprintf(msg, sizeof(msg), "Track %d -> %s", sel + 1,
             engine->samples[idx].name);
    sq_app_set_status(&g_gtk.app, msg, 90);
}

static void on_up_clicked(GtkWidget *btn, gpointer data)
{
    (void)btn; (void)data;

    char *parent = g_path_get_dirname(s_current_path);
    if (parent) {
        strncpy(s_current_path, parent, sizeof(s_current_path) - 1);
        s_current_path[sizeof(s_current_path) - 1] = '\0';
        g_free(parent);
    }

    s_needs_refresh = 1;
    refresh_directory();
    rebuild_file_list();
}

static void on_refresh_clicked(GtkWidget *btn, gpointer data)
{
    (void)btn; (void)data;

    s_needs_refresh = 1;
    refresh_directory();
    rebuild_file_list();
}

/* GtkFileDialog async callback for Load Sample button */
static void on_file_dialog_response(GObject *source, GAsyncResult *result,
                                    gpointer user_data)
{
    (void)user_data;
    GtkFileDialog *dialog = GTK_FILE_DIALOG(source);
    GFile *file = gtk_file_dialog_open_finish(dialog, result, NULL);
    if (!file) return;

    char *filepath = g_file_get_path(file);
    g_object_unref(file);
    if (!filepath) return;

    sq_engine_t *engine = g_gtk.engine;
    if (engine->num_samples >= SQ_MAX_SAMPLES) {
        sq_app_set_status(&g_gtk.app, "Sample slots full", 120);
        g_free(filepath);
        return;
    }

    int si = (int)engine->num_samples;
    if (sample_io_load(filepath, &engine->samples[si]) == 0) {
        engine->num_samples++;

        char msg[128];
        snprintf(msg, sizeof(msg), "Loaded: %s [%d]",
                 engine->samples[si].name, si);
        sq_app_set_status(&g_gtk.app, msg, 120);

        rebuild_loaded_list();
    } else {
        sq_app_set_status(&g_gtk.app, "Failed to load sample", 120);
    }

    g_free(filepath);
}

static void on_load_sample_clicked(GtkWidget *btn, gpointer data)
{
    (void)btn; (void)data;

    GtkFileDialog *dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, "Load Sample");

    /* Filter for audio files */
    GtkFileFilter *filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "Audio files (*.wav, *.mp3, *.flac)");
    gtk_file_filter_add_pattern(filter, "*.wav");
    gtk_file_filter_add_pattern(filter, "*.WAV");
    gtk_file_filter_add_pattern(filter, "*.mp3");
    gtk_file_filter_add_pattern(filter, "*.MP3");
    gtk_file_filter_add_pattern(filter, "*.flac");
    gtk_file_filter_add_pattern(filter, "*.FLAC");

    GListStore *filters = g_list_store_new(GTK_TYPE_FILE_FILTER);
    g_list_store_append(filters, filter);
    g_object_unref(filter);

    gtk_file_dialog_set_filters(dialog, G_LIST_MODEL(filters));
    g_object_unref(filters);

    /* Set initial directory */
    if (s_current_path[0] != '\0') {
        GFile *init_dir = g_file_new_for_path(s_current_path);
        gtk_file_dialog_set_initial_folder(dialog, init_dir);
        g_object_unref(init_dir);
    }

    gtk_file_dialog_open(dialog, GTK_WINDOW(g_gtk.window), NULL,
                         on_file_dialog_response, NULL);
    g_object_unref(dialog);
}

static void on_audition_clicked(GtkWidget *btn, gpointer data)
{
    (void)btn; (void)data;

    sq_engine_t *engine = g_gtk.engine;
    if (s_preview_sample_idx < 0 ||
        (uint32_t)s_preview_sample_idx >= engine->num_samples)
        return;

    /* Stop any voice already auditioning this sample */
    for (int v = 0; v < SQ_MAX_VOICES; v++) {
        if (engine->voices[v].active &&
            engine->voices[v].sample_index == s_preview_sample_idx) {
            engine->voices[v].active = false;
        }
    }

    /* Trigger on first free voice */
    for (int v = 0; v < SQ_MAX_VOICES; v++) {
        if (!engine->voices[v].active) {
            engine->voices[v].active       = true;
            engine->voices[v].sample_index = s_preview_sample_idx;
            engine->voices[v].position     = 0.0;
            engine->voices[v].rate         = 1.0;
            engine->voices[v].velocity     = 0.8f;
            engine->voices[v].volume       = 0.8f;
            engine->voices[v].pan          = 0.0f;
            engine->voices[v].start_time   = 0;
            break;
        }
    }
}

/* --- Public API --- */

GtkWidget *gtk_browser_new(void)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_margin_start(box, 4);
    gtk_widget_set_margin_end(box, 4);
    gtk_widget_set_margin_top(box, 4);

    /* Title */
    GtkWidget *title = gtk_label_new("Sample Browser");
    PangoAttrList *attrs = pango_attr_list_new();
    pango_attr_list_insert(attrs, pango_attr_weight_new(PANGO_WEIGHT_BOLD));
    gtk_label_set_attributes(GTK_LABEL(title), attrs);
    pango_attr_list_unref(attrs);
    gtk_box_append(GTK_BOX(box), title);

    /* Path display */
    s_path_label = gtk_label_new("Path: ");
    gtk_widget_set_halign(s_path_label, GTK_ALIGN_START);
    gtk_label_set_ellipsize(GTK_LABEL(s_path_label), PANGO_ELLIPSIZE_START);
    gtk_box_append(GTK_BOX(box), s_path_label);

    /* Toolbar: Up, Refresh, Load Sample, count */
    GtkWidget *toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);

    GtkWidget *up_btn = gtk_button_new_with_label("Up");
    g_signal_connect(up_btn, "clicked", G_CALLBACK(on_up_clicked), NULL);
    gtk_box_append(GTK_BOX(toolbar), up_btn);

    GtkWidget *refresh_btn = gtk_button_new_with_label("Refresh");
    g_signal_connect(refresh_btn, "clicked",
                     G_CALLBACK(on_refresh_clicked), NULL);
    gtk_box_append(GTK_BOX(toolbar), refresh_btn);

    GtkWidget *load_btn = gtk_button_new_with_label("Load Sample");
    g_signal_connect(load_btn, "clicked",
                     G_CALLBACK(on_load_sample_clicked), NULL);
    gtk_box_append(GTK_BOX(toolbar), load_btn);

    s_count_label = gtk_label_new("Samples: 0/128");
    gtk_widget_set_hexpand(s_count_label, TRUE);
    gtk_widget_set_halign(s_count_label, GTK_ALIGN_END);
    gtk_box_append(GTK_BOX(toolbar), s_count_label);

    gtk_box_append(GTK_BOX(box), toolbar);

    /* Separator */
    gtk_box_append(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

    /* File list (directory contents) */
    GtkWidget *file_scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(file_scroll),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(file_scroll, TRUE);

    s_file_list = gtk_list_box_new();
    g_signal_connect(s_file_list, "row-activated",
                     G_CALLBACK(on_file_activated), NULL);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(file_scroll),
                                  s_file_list);
    gtk_box_append(GTK_BOX(box), file_scroll);

    /* Separator */
    gtk_box_append(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

    /* Loaded samples section */
    GtkWidget *loaded_title = gtk_label_new("Loaded Samples");
    gtk_widget_set_halign(loaded_title, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(box), loaded_title);

    GtkWidget *loaded_scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(loaded_scroll),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(loaded_scroll, -1, 120);

    s_loaded_list = gtk_list_box_new();
    g_signal_connect(s_loaded_list, "row-activated",
                     G_CALLBACK(on_loaded_sample_selected), NULL);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(loaded_scroll),
                                  s_loaded_list);
    gtk_box_append(GTK_BOX(box), loaded_scroll);

    /* Separator before waveform */
    gtk_box_append(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

    /* Waveform preview section */
    s_waveform_name = gtk_label_new("Waveform Preview");
    gtk_widget_set_halign(s_waveform_name, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(box), s_waveform_name);

    s_waveform_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(s_waveform_area, -1, 40);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(s_waveform_area),
                                   waveform_draw_cb, NULL, NULL);
    gtk_box_append(GTK_BOX(box), s_waveform_area);

    /* Sample info label below waveform */
    s_waveform_info = gtk_label_new("");
    gtk_widget_set_halign(s_waveform_info, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(box), s_waveform_info);

    /* Audition button */
    s_audition_btn = gtk_button_new_with_label("\u25B6 Audition");
    gtk_widget_set_halign(s_audition_btn, GTK_ALIGN_START);
    g_signal_connect(s_audition_btn, "clicked",
                     G_CALLBACK(on_audition_clicked), NULL);
    gtk_box_append(GTK_BOX(box), s_audition_btn);

    /* Initial population */
    refresh_directory();
    rebuild_file_list();
    rebuild_loaded_list();

    return box;
}
