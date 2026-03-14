/*
 * gtk_presets.c — Pattern preset library dialog.
 *
 * 12 drum patterns + 10 bass lines, matching the ImGui frontend exactly.
 */

#include "gtk_gui.h"
#include "gui/undo.h"
#include <string.h>
#include <stdio.h>

/* ─── Drum preset data (matches pattern_presets.cpp) ──────────────────────── */

typedef struct {
    const char *name;
    uint8_t tracks[6][16];
    float   suggested_bpm;
} drum_preset_t;

static const drum_preset_t s_drum_presets[] = {
    {"House", {{120,0,0,0,110,0,0,0,120,0,0,0,110,0,0,0},{0,0,0,0,127,0,0,0,0,0,0,0,127,0,0,0},{0,0,100,0,0,0,100,0,0,0,100,0,0,0,100,0},{0,0,0,0,100,0,0,0,0,0,0,0,100,0,0,0},{0,0,0,0,0,0,0,80,0,0,0,0,0,0,0,80},{60,40,60,40,60,40,60,40,60,40,60,40,60,40,60,40}}, 124.0f},
    {"Boom Bap", {{120,0,0,0,0,0,0,0,0,0,110,0,120,0,0,0},{0,0,0,0,127,0,0,50,0,0,0,0,127,0,0,0},{90,0,70,0,90,0,70,0,90,0,70,0,90,0,70,0},{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0,0,0,0,70,0,0,0,0},{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}}, 92.0f},
    {"Trap", {{120,0,0,0,0,0,100,0,0,0,0,110,0,0,100,0},{0,0,0,0,0,0,0,0,127,0,0,0,0,0,0,0},{100,60,80,60,100,60,80,60,100,60,80,60,100,60,80,90},{0,0,0,0,0,0,0,0,110,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,80},{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}}, 140.0f},
    {"DnB", {{120,0,0,0,0,0,0,0,0,0,110,0,0,0,0,0},{0,0,0,0,127,0,0,0,0,0,0,0,127,0,0,0},{90,60,70,60,90,60,70,60,90,60,70,60,90,60,70,60},{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,80,0,0,0,0,0,0,0,80},{70,0,70,0,70,0,70,0,70,0,70,0,70,0,70,0}}, 174.0f},
    {"Reggaeton", {{120,0,0,100,0,0,0,0,120,0,0,100,0,0,0,0},{0,0,0,0,0,0,120,0,0,0,0,0,0,0,120,0},{80,0,80,0,80,0,80,0,80,0,80,0,80,0,80,0},{0,0,0,0,0,0,100,0,0,0,0,0,0,0,100,0},{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}}, 95.0f},
    {"Disco", {{120,0,0,0,120,0,0,0,120,0,0,0,120,0,0,0},{0,0,0,0,120,0,0,0,0,0,0,0,120,0,0,0},{100,70,80,70,100,70,80,70,100,70,80,70,100,70,80,70},{0,0,0,0,110,0,0,0,0,0,0,0,110,0,0,0},{0,0,80,0,0,0,80,0,0,0,80,0,0,0,80,0},{60,0,60,0,60,0,60,0,60,0,60,0,60,0,60,0}}, 120.0f},
    {"Techno", {{127,0,0,0,120,0,0,0,127,0,0,0,120,0,0,0},{0,0,0,0,0,0,80,0,0,0,0,0,0,0,80,0},{100,70,80,70,100,70,80,70,100,70,80,70,100,70,80,70},{0,0,0,0,110,0,0,0,0,0,0,0,110,0,0,0},{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}}, 130.0f},
    {"Breakbeat", {{120,0,0,0,0,0,0,100,0,0,110,0,0,0,0,0},{0,0,0,0,127,0,0,0,0,0,0,0,0,0,127,0},{90,0,70,0,90,0,70,0,90,0,70,0,90,0,70,0},{0,0,0,0,100,0,0,0,0,0,0,0,0,0,100,0},{0,0,0,0,0,0,0,0,0,0,0,80,0,0,0,0},{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}}, 130.0f},
    {"Lo-fi", {{100,0,0,0,0,0,90,0,0,0,0,80,100,0,0,0},{0,0,0,0,100,0,0,40,0,0,0,0,100,0,0,40},{70,0,50,0,70,0,50,0,70,0,50,0,70,0,50,0},{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0,0,0,0,60,0,0,0,0},{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}}, 80.0f},
    {"Rock", {{120,0,0,0,0,0,0,0,120,0,100,0,0,0,0,0},{0,0,0,0,127,0,0,0,0,0,0,0,127,0,0,0},{100,0,80,0,100,0,80,0,100,0,80,0,100,0,80,0},{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0,0,0,0,0,0,0,80,0},{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}}, 120.0f},
    {"Afrobeat", {{120,0,0,0,0,0,100,0,0,0,0,0,120,0,0,0},{0,0,0,40,110,0,0,0,0,0,40,0,110,0,0,0},{90,60,80,60,90,60,80,60,90,60,80,60,90,60,80,60},{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,80,0,0,0,0,0,0,0,80},{70,50,70,50,70,50,70,50,70,50,70,50,70,50,70,50}}, 108.0f},
    {"Bossa Nova", {{100,0,0,90,0,0,100,0,0,0,90,0,0,0,0,0},{0,0,80,0,0,0,80,0,0,0,80,0,0,0,80,0},{70,0,70,0,70,0,70,0,70,0,70,0,70,0,70,0},{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},{50,40,50,40,50,40,50,40,50,40,50,40,50,40,50,40}}, 140.0f},
};
#define NUM_DRUM_PRESETS 12

/* ─── Bass preset data ────────────────────────────────────────────────────── */

typedef struct {
    uint8_t note;
    uint8_t velocity;
    float   length;
} bass_step_t;

typedef struct {
    const char *name;
    bass_step_t steps[16];
    int synth_preset;
    float suggested_bpm;
} bass_preset_t;

static const bass_preset_t s_bass_presets[] = {
    {"Octave Bass", {{36,110,1},{48,80,1},{0,0,0},{48,70,1},{36,110,1},{48,80,1},{0,0,0},{48,70,1},{36,110,1},{48,80,1},{0,0,0},{48,70,1},{36,110,1},{48,80,1},{0,0,0},{48,70,1}}, 0, 124.0f},
    {"Root-Fifth", {{36,110,2},{0,0,0},{43,90,2},{0,0,0},{36,100,2},{0,0,0},{43,80,2},{0,0,0},{36,110,2},{0,0,0},{43,90,2},{0,0,0},{36,100,1},{0,0,0},{43,90,1},{41,70,1}}, 0, 120.0f},
    {"Walking Bass", {{36,100,1},{0,0,0},{38,90,1},{0,0,0},{40,100,1},{0,0,0},{41,90,1},{0,0,0},{43,100,1},{0,0,0},{41,90,1},{0,0,0},{40,100,1},{0,0,0},{38,90,1},{0,0,0}}, 14, 100.0f},
    {"Sub Bass", {{36,120,4},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{36,110,2},{0,0,0},{39,100,2},{0,0,0},{43,100,2},{0,0,0},{41,90,2},{0,0,0}}, 46, 128.0f},
    {"808 Trap", {{36,120,3},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{36,100,2},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{39,110,2},{0,0,0},{0,0,0},{36,100,2},{0,0,0}}, 46, 140.0f},
    {"Reese DnB", {{36,110,2},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{43,90,2},{0,0,0},{0,0,0},{41,100,2},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{38,90,2},{0,0,0},{36,80,1}}, 21, 174.0f},
    {"Acid 303", {{36,120,1},{0,0,0},{36,80,1},{48,90,1},{36,110,1},{0,0,0},{39,100,1},{0,0,0},{36,120,1},{41,70,1},{0,0,0},{43,90,1},{36,110,1},{0,0,0},{48,80,1},{36,90,1}}, 15, 130.0f},
    {"Disco Funk", {{36,110,1},{0,0,0},{36,70,1},{43,80,1},{0,0,0},{36,100,1},{0,0,0},{43,80,1},{41,100,1},{0,0,0},{41,70,1},{43,80,1},{0,0,0},{41,100,1},{0,0,0},{36,80,1}}, 14, 120.0f},
    {"Dembow Bass", {{36,120,2},{0,0,0},{0,0,0},{36,80,1},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{36,120,2},{0,0,0},{0,0,0},{36,80,1},{0,0,0},{0,0,0},{0,0,0},{0,0,0}}, 46, 95.0f},
    {"Minimal Pulse", {{36,100,1},{0,0,0},{0,0,0},{0,0,0},{36,90,1},{0,0,0},{0,0,0},{0,0,0},{36,100,1},{0,0,0},{0,0,0},{0,0,0},{36,90,1},{0,0,0},{0,0,0},{38,70,1}}, 0, 120.0f},
};
#define NUM_BASS_PRESETS 10

/* ─── Apply functions ─────────────────────────────────────────────────────── */

static void apply_drum_preset(sq_engine_t *engine, int idx)
{
    int pi = engine->transport.current_pattern;
    if (pi < 0 || (uint32_t)pi >= engine->num_patterns) return;
    sq_pattern_t *p = &engine->patterns[pi];
    const drum_preset_t *preset = &s_drum_presets[idx];

    undo_push(engine);

    int applied = 0;
    for (uint32_t t = 0; t < p->num_tracks && applied < 6; t++) {
        if (p->tracks[t].type != TRACK_SAMPLER) continue;
        for (int s = 0; s < 16 && (uint32_t)s < p->tracks[t].length; s++)
            p->tracks[t].steps[s].velocity = preset->tracks[applied][s];
        applied++;
    }
}

static void apply_bass_preset(sq_engine_t *engine, int idx)
{
    int pi = engine->transport.current_pattern;
    if (pi < 0 || (uint32_t)pi >= engine->num_patterns) return;
    sq_pattern_t *p = &engine->patterns[pi];
    const bass_preset_t *preset = &s_bass_presets[idx];

    undo_push(engine);

    /* Find first synth track, or create one */
    int target = -1;
    for (uint32_t t = 0; t < p->num_tracks; t++) {
        if (p->tracks[t].type == TRACK_SYNTH) { target = (int)t; break; }
    }
    if (target < 0 && p->num_tracks < SQ_MAX_TRACKS) {
        target = (int)p->num_tracks++;
        p->tracks[target].type = TRACK_SYNTH;
        p->tracks[target].length = 16;
        p->tracks[target].volume = 0.7f;
    }
    if (target < 0) return;

    p->tracks[target].synth_preset = preset->synth_preset;
    for (int s = 0; s < 16 && (uint32_t)s < p->tracks[target].length; s++) {
        p->tracks[target].steps[s].velocity = preset->steps[s].velocity;
        p->tracks[target].steps[s].note = preset->steps[s].note;
        p->tracks[target].steps[s].length = preset->steps[s].length;
        p->tracks[target].steps[s].pitch_offset = 0;
    }
}

/* ─── Dialog state ────────────────────────────────────────────────────────── */

static GtkWidget *s_dialog = NULL;
static GtkWidget *s_drum_dropdown = NULL;
static GtkWidget *s_bass_dropdown = NULL;

static void on_apply_drums(GtkButton *btn, gpointer data)
{
    (void)btn; (void)data;
    int idx = (int)gtk_drop_down_get_selected(GTK_DROP_DOWN(s_drum_dropdown));
    apply_drum_preset(g_gtk.engine, idx);
    sq_app_set_status(&g_gtk.app, "Applied drum preset", 90);
}

static void on_apply_bass(GtkButton *btn, gpointer data)
{
    (void)btn; (void)data;
    int idx = (int)gtk_drop_down_get_selected(GTK_DROP_DOWN(s_bass_dropdown));
    apply_bass_preset(g_gtk.engine, idx);
    sq_app_set_status(&g_gtk.app, "Applied bass preset", 90);
}

static void on_apply_both(GtkButton *btn, gpointer data)
{
    (void)btn; (void)data;
    int di = (int)gtk_drop_down_get_selected(GTK_DROP_DOWN(s_drum_dropdown));
    int bi = (int)gtk_drop_down_get_selected(GTK_DROP_DOWN(s_bass_dropdown));
    apply_drum_preset(g_gtk.engine, di);
    apply_bass_preset(g_gtk.engine, bi);
    g_gtk.engine->transport.bpm = (double)s_drum_presets[di].suggested_bpm;
    sq_app_set_status(&g_gtk.app, "Applied drum + bass", 90);
}

static void on_drum_bpm(GtkButton *btn, gpointer data)
{
    (void)btn; (void)data;
    int idx = (int)gtk_drop_down_get_selected(GTK_DROP_DOWN(s_drum_dropdown));
    g_gtk.engine->transport.bpm = (double)s_drum_presets[idx].suggested_bpm;
    char msg[32];
    snprintf(msg, sizeof(msg), "BPM: %.0f", s_drum_presets[idx].suggested_bpm);
    sq_app_set_status(&g_gtk.app, msg, 60);
}

static void on_bass_bpm(GtkButton *btn, gpointer data)
{
    (void)btn; (void)data;
    int idx = (int)gtk_drop_down_get_selected(GTK_DROP_DOWN(s_bass_dropdown));
    g_gtk.engine->transport.bpm = (double)s_bass_presets[idx].suggested_bpm;
    char msg[32];
    snprintf(msg, sizeof(msg), "BPM: %.0f", s_bass_presets[idx].suggested_bpm);
    sq_app_set_status(&g_gtk.app, msg, 60);
}

static void on_clear_pattern(GtkButton *btn, gpointer data)
{
    (void)btn; (void)data;
    sq_engine_t *engine = g_gtk.engine;
    int pi = engine->transport.current_pattern;
    if (pi < 0 || (uint32_t)pi >= engine->num_patterns) return;

    undo_push(engine);
    sq_pattern_t *p = &engine->patterns[pi];
    for (uint32_t t = 0; t < p->num_tracks; t++)
        for (uint32_t s = 0; s < p->tracks[t].length; s++)
            memset(&p->tracks[t].steps[s], 0, sizeof(sq_step_t));
    sq_app_set_status(&g_gtk.app, "Pattern cleared", 90);
}

static void on_dialog_destroy(GtkWidget *w, gpointer data)
{
    (void)w; (void)data;
    s_dialog = NULL;
    s_drum_dropdown = NULL;
    s_bass_dropdown = NULL;
}

/* ─── Public API ──────────────────────────────────────────────────────────── */

void gtk_presets_show_save(GtkWidget *parent)
{
    /* Toggle: if open, close it */
    if (s_dialog) {
        gtk_window_destroy(GTK_WINDOW(s_dialog));
        return;
    }

    s_dialog = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(s_dialog), "Pattern Presets");
    gtk_window_set_default_size(GTK_WINDOW(s_dialog), 450, 380);
    gtk_window_set_transient_for(GTK_WINDOW(s_dialog), GTK_WINDOW(parent));
    gtk_window_set_modal(GTK_WINDOW(s_dialog), FALSE);
    g_signal_connect(s_dialog, "destroy", G_CALLBACK(on_dialog_destroy), NULL);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_margin_start(box, 12);
    gtk_widget_set_margin_end(box, 12);
    gtk_widget_set_margin_top(box, 8);
    gtk_widget_set_margin_bottom(box, 8);
    gtk_window_set_child(GTK_WINDOW(s_dialog), box);

    /* ── Drum presets ─────────────────────────────────────────── */
    GtkWidget *drum_label = gtk_label_new("Drum Patterns:");
    gtk_widget_set_halign(drum_label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(box), drum_label);

    GtkWidget *drum_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_append(GTK_BOX(box), drum_row);

    /* Drum dropdown */
    GtkStringList *drum_model = gtk_string_list_new(NULL);
    for (int i = 0; i < NUM_DRUM_PRESETS; i++)
        gtk_string_list_append(drum_model, s_drum_presets[i].name);
    s_drum_dropdown = gtk_drop_down_new(G_LIST_MODEL(drum_model), NULL);
    gtk_widget_set_hexpand(s_drum_dropdown, TRUE);
    gtk_box_append(GTK_BOX(drum_row), s_drum_dropdown);

    GtkWidget *apply_drums = gtk_button_new_with_label("Apply Drums");
    g_signal_connect(apply_drums, "clicked", G_CALLBACK(on_apply_drums), NULL);
    gtk_box_append(GTK_BOX(drum_row), apply_drums);

    GtkWidget *drum_bpm = gtk_button_new_with_label("BPM");
    g_signal_connect(drum_bpm, "clicked", G_CALLBACK(on_drum_bpm), NULL);
    gtk_box_append(GTK_BOX(drum_row), drum_bpm);

    /* Separator */
    gtk_box_append(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

    /* ── Bass presets ─────────────────────────────────────────── */
    GtkWidget *bass_label = gtk_label_new("Bass Lines:");
    gtk_widget_set_halign(bass_label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(box), bass_label);

    GtkWidget *bass_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_append(GTK_BOX(box), bass_row);

    GtkStringList *bass_model = gtk_string_list_new(NULL);
    for (int i = 0; i < NUM_BASS_PRESETS; i++)
        gtk_string_list_append(bass_model, s_bass_presets[i].name);
    s_bass_dropdown = gtk_drop_down_new(G_LIST_MODEL(bass_model), NULL);
    gtk_widget_set_hexpand(s_bass_dropdown, TRUE);
    gtk_box_append(GTK_BOX(bass_row), s_bass_dropdown);

    GtkWidget *apply_bass = gtk_button_new_with_label("Apply Bass");
    g_signal_connect(apply_bass, "clicked", G_CALLBACK(on_apply_bass), NULL);
    gtk_box_append(GTK_BOX(bass_row), apply_bass);

    GtkWidget *bass_bpm = gtk_button_new_with_label("BPM");
    g_signal_connect(bass_bpm, "clicked", G_CALLBACK(on_bass_bpm), NULL);
    gtk_box_append(GTK_BOX(bass_row), bass_bpm);

    /* Separator */
    gtk_box_append(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

    /* ── Combined actions ─────────────────────────────────────── */
    GtkWidget *action_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_append(GTK_BOX(box), action_row);

    GtkWidget *apply_both = gtk_button_new_with_label("Apply Both");
    g_signal_connect(apply_both, "clicked", G_CALLBACK(on_apply_both), NULL);
    gtk_box_append(GTK_BOX(action_row), apply_both);

    GtkWidget *clear_btn = gtk_button_new_with_label("Clear Pattern");
    g_signal_connect(clear_btn, "clicked", G_CALLBACK(on_clear_pattern), NULL);
    gtk_box_append(GTK_BOX(action_row), clear_btn);

    /* Tips */
    GtkWidget *tip1 = gtk_label_new("Tip: Select a synth track to see the piano roll");
    gtk_widget_set_halign(tip1, GTK_ALIGN_START);
    gtk_widget_add_css_class(tip1, "status");
    gtk_box_append(GTK_BOX(box), tip1);

    GtkWidget *tip2 = gtk_label_new("Click notes in piano roll to edit");
    gtk_widget_set_halign(tip2, GTK_ALIGN_START);
    gtk_widget_add_css_class(tip2, "status");
    gtk_box_append(GTK_BOX(box), tip2);

    gtk_window_present(GTK_WINDOW(s_dialog));
}

void gtk_presets_show_load(GtkWidget *parent)
{
    /* Same dialog for now */
    gtk_presets_show_save(parent);
}
