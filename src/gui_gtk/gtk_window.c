/*
 * gtk_window.c — Main window layout, toolbar, keyboard shortcuts.
 *
 * Layout (matching ImGui frontend):
 *   ┌─────────────────────────────────────────┐
 *   │ Toolbar: PLAY BPM Swing Vol | panels    │
 *   ├──────────────────────────┬──────────────┤
 *   │                          │              │
 *   │   Drum Grid              │  Browser     │
 *   │                          │  (optional)  │
 *   ├──────────────────────────┤              │
 *   │  Piano Roll | Synth Ed   │              │
 *   │  (optional bottom)       │              │
 *   ├──────────────────────────┴──────────────┤
 *   │  Virtual Keyboard (optional)            │
 *   └─────────────────────────────────────────┘
 */

#include "gtk_gui.h"
#include "gui/undo.h"
#include "formats/project.h"
#include "engine/export.h"
#include "engine/synth.h"
#include "engine/kits.h"

#include <SDL2/SDL.h>
#include <stdio.h>
#include <math.h>

#define LOG_TAG "gtk_window"
#include "core/log.h"

/* Forward declarations */
static void update_pattern_buttons(void);
static GtkWidget *s_rec_btn = NULL;
static GtkWidget *s_undo_btn = NULL;
static GtkWidget *s_redo_btn = NULL;
static int s_status_flash_frames = 0;
static uint32_t s_prev_status_timer = 0;
static GtkWidget *s_logo_area = NULL;
static GtkWidget *s_wc_maximize_btn = NULL;

/* ─── Panel visibility sync ───────────────────────────────────────────────── */

static void sync_panel_visibility(void)
{
    /* Guard: widgets not yet created */
    if (!g_gtk.window) return;

    /* Arrangement panel (SONG/PERFORM mode) */
    if (g_gtk.arrangement_area && GTK_IS_WIDGET(g_gtk.arrangement_area)) {
        gtk_widget_set_visible(g_gtk.arrangement_area,
                               g_gtk.engine->transport.mode != MODE_PATTERN);
    }

    /* Browser panel */
    if (g_gtk.browser_box && GTK_IS_WIDGET(g_gtk.browser_box)) {
        gtk_widget_set_visible(g_gtk.browser_box,
                               g_gtk.app.panels[SQ_PANEL_BROWSER]);
    }

    /* Bottom panel area (piano roll + synth editor + mixer) */
    bool show_bottom = g_gtk.app.panels[SQ_PANEL_PIANO_ROLL] ||
                       g_gtk.app.panels[SQ_PANEL_MIXER];

    /* Piano roll */
    if (g_gtk.piano_roll_area) {
        gtk_widget_set_visible(g_gtk.piano_roll_area,
                               g_gtk.app.panels[SQ_PANEL_PIANO_ROLL]);
    }

    /* Synth editor — show when piano roll is shown and a synth track selected */
    if (g_gtk.synth_editor_box) {
        bool show_synth = g_gtk.app.panels[SQ_PANEL_PIANO_ROLL];
        if (show_synth && g_gtk.app.selected_track >= 0) {
            int pi = g_gtk.engine->transport.current_pattern;
            if (pi >= 0 && (uint32_t)pi < g_gtk.engine->num_patterns) {
                sq_pattern_t *pat = &g_gtk.engine->patterns[pi];
                if ((uint32_t)g_gtk.app.selected_track < pat->num_tracks &&
                    pat->tracks[g_gtk.app.selected_track].type != TRACK_SYNTH)
                    show_synth = false;
            }
        }
        gtk_widget_set_visible(g_gtk.synth_editor_box, show_synth);
    }

    /* Mixer */
    bool show_mixer = g_gtk.app.panels[SQ_PANEL_MIXER];
    if (g_gtk.mixer_box) {
        gtk_widget_set_visible(g_gtk.mixer_box, show_mixer);
    }

    /* Bottom pane container */
    if (g_gtk.grid_area) {
        /* The bottom_box is the second child of grid_area (after drum_grid) */
        GtkWidget *bottom = gtk_widget_get_last_child(g_gtk.grid_area);
        if (bottom && bottom != g_gtk.drum_grid_area)
            gtk_widget_set_visible(bottom, show_bottom);
    }

    /* Dynamic width allocation matching ImGui percentages:
     *   Piano + Synth + Mixer: 40% / 30% / 30%
     *   Piano + Synth:         55% / 45%
     *   Piano + Mixer:         70% / 30%
     *   Mixer only:            100%  */
    if (show_bottom && g_gtk.window) {
        int win_w = gtk_widget_get_width(g_gtk.window);
        int browser_w = (g_gtk.app.panels[SQ_PANEL_BROWSER]) ? 300 : 0;
        int main_w = win_w - browser_w;
        if (main_w < 400) main_w = 400;

        bool show_synth_ed = gtk_widget_get_visible(g_gtk.synth_editor_box);

        if (show_synth_ed && show_mixer) {
            /* 40% piano, 30% synth, 30% mixer */
            gtk_widget_set_size_request(g_gtk.synth_editor_box, main_w * 30 / 100, -1);
            gtk_widget_set_size_request(g_gtk.mixer_box, main_w * 30 / 100, -1);
        } else if (show_synth_ed) {
            /* 55% piano, 45% synth */
            gtk_widget_set_size_request(g_gtk.synth_editor_box, main_w * 45 / 100, -1);
        } else if (show_mixer) {
            /* 70% piano (if visible), 30% mixer */
            gtk_widget_set_size_request(g_gtk.mixer_box, main_w * 30 / 100, -1);
        }
    }

    /* Virtual keyboard */
    if (g_gtk.keyboard_area) {
        gtk_widget_set_visible(g_gtk.keyboard_area,
                               g_gtk.app.panels[SQ_PANEL_KEYBOARD]);
    }
}

/* Static floats for knob widgets (synced to engine in on_redraw_tick) */
static float s_bpm_float   = 120.0f;
static float s_swing_float = 0.0f;
static float s_vol_float   = 80.0f;

/* ─── Periodic redraw timer ───────────────────────────────────────────────── */

static gboolean on_redraw_tick(gpointer user_data)
{
    (void)user_data;
    sq_engine_t *engine = g_gtk.engine;

    /* Guard: don't touch widgets before window is fully built */
    if (!g_gtk.window || !gtk_widget_get_realized(g_gtk.window))
        return G_SOURCE_CONTINUE;

    /* Sync knob values → engine */
    engine->transport.bpm = (double)s_bpm_float;
    engine->transport.swing = s_swing_float / 100.0f;
    engine->master_volume = s_vol_float / 100.0f;

    /* Update playhead */
    sq_app_update_playhead(&g_gtk.app, engine,
                           SDL_GetPerformanceCounter(),
                           SDL_GetPerformanceFrequency());

    /* Update status timer */
    sq_app_update_status(&g_gtk.app);
    if (g_gtk.status_label && GTK_IS_LABEL(g_gtk.status_label)) {
        if (g_gtk.app.status_msg[0])
            gtk_label_set_text(GTK_LABEL(g_gtk.status_label), g_gtk.app.status_msg);
        else
            gtk_label_set_text(GTK_LABEL(g_gtk.status_label), "");
    }

    /* Update play button label */
    if (g_gtk.play_btn && GTK_IS_LABEL(g_gtk.play_btn)) {
        gtk_label_set_text(GTK_LABEL(g_gtk.play_btn),
                           engine->transport.playing ? "STOP" : "PLAY");
    }

    /* Update REC button state */
    if (s_rec_btn && GTK_IS_LABEL(s_rec_btn)) {
        if (engine->recording) {
            gtk_label_set_text(GTK_LABEL(s_rec_btn), "REC *");
            gtk_widget_add_css_class(s_rec_btn, "recording");
        } else {
            gtk_label_set_text(GTK_LABEL(s_rec_btn), "REC");
            gtk_widget_remove_css_class(s_rec_btn, "recording");
        }
    }

    /* Update undo/redo button enabled state */
    if (s_undo_btn) {
        if (undo_can_undo()) {
            gtk_widget_set_opacity(s_undo_btn, 1.0);
            gtk_widget_set_sensitive(s_undo_btn, TRUE);
        } else {
            gtk_widget_set_opacity(s_undo_btn, 0.35);
            gtk_widget_set_sensitive(s_undo_btn, FALSE);
        }
    }
    if (s_redo_btn) {
        if (undo_can_redo()) {
            gtk_widget_set_opacity(s_redo_btn, 1.0);
            gtk_widget_set_sensitive(s_redo_btn, TRUE);
        } else {
            gtk_widget_set_opacity(s_redo_btn, 0.35);
            gtk_widget_set_sensitive(s_redo_btn, FALSE);
        }
    }

    /* Status flash effect — highlight status label briefly on new messages */
    if (g_gtk.status_label && GTK_IS_LABEL(g_gtk.status_label)) {
        /* Detect new status message: timer jumped up (was decreasing or zero) */
        if (g_gtk.app.status_timer > s_prev_status_timer && g_gtk.app.status_msg[0]) {
            gtk_widget_add_css_class(g_gtk.status_label, "status-flash");
            s_status_flash_frames = 20; /* ~320ms at 60fps */
        }
        s_prev_status_timer = g_gtk.app.status_timer;

        if (s_status_flash_frames > 0) {
            s_status_flash_frames--;
            if (s_status_flash_frames == 0)
                gtk_widget_remove_css_class(g_gtk.status_label, "status-flash");
        }
    }

    /* Sync panel visibility + pattern buttons */
    sync_panel_visibility();
    update_pattern_buttons();

    /* Rebuild synth editor if selected track changed */
    gtk_synth_editor_update();

    /* Update maximize/restore button label */
    if (s_wc_maximize_btn && GTK_IS_LABEL(s_wc_maximize_btn)) {
        bool is_max = gtk_window_is_maximized(GTK_WINDOW(g_gtk.window));
        gtk_label_set_text(GTK_LABEL(s_wc_maximize_btn),
                           is_max ? "\xe2\x96\xa3" : "\xe2\x96\xa1");
    }

    /* Redraw logo (pulse animation during playback) */
    if (s_logo_area)
        gtk_widget_queue_draw(s_logo_area);

    /* Redraw visible drawing areas */
    if (g_gtk.drum_grid_area)
        gtk_widget_queue_draw(g_gtk.drum_grid_area);

    if (g_gtk.piano_roll_area && gtk_widget_get_visible(g_gtk.piano_roll_area))
        gtk_widget_queue_draw(g_gtk.piano_roll_area);

    if (g_gtk.keyboard_area && gtk_widget_get_visible(g_gtk.keyboard_area))
        gtk_widget_queue_draw(g_gtk.keyboard_area);

    if (g_gtk.synth_editor_box && gtk_widget_get_visible(g_gtk.synth_editor_box)) {
        /* Redraw ADSR in synth editor */
        GtkWidget *child = gtk_widget_get_last_child(g_gtk.synth_editor_box);
        if (child) gtk_widget_queue_draw(child);
    }

    /* Redraw mixer level meters */
    if (g_gtk.mixer_box && gtk_widget_get_visible(g_gtk.mixer_box))
        gtk_mixer_queue_redraw();

    return G_SOURCE_CONTINUE;
}

/* ─── Toolbar callbacks ───────────────────────────────────────────────────── */

static void on_play_clicked(GtkWidget *btn, gpointer user_data)
{
    (void)btn; (void)user_data;
    sq_engine_t *engine = g_gtk.engine;
    engine->transport.playing = !engine->transport.playing;
    engine->transport.current_beat = 0.0;
    engine->transport.sample_position = 0;
    engine->transport.current_step = 0;
    g_gtk.app.visual_step = 0;
    if (engine->transport.playing) {
        g_gtk.app.play_start_ticks = SDL_GetPerformanceCounter();
    } else {
        /* Release all synth voices so they fade out naturally */
        synth_release_all(engine);
    }
}

static void on_undo_clicked(GtkWidget *btn, gpointer user_data)
{
    (void)btn; (void)user_data;
    if (undo_undo(g_gtk.engine))
        sq_app_set_status(&g_gtk.app, "Undo", 90);
}

static void on_redo_clicked(GtkWidget *btn, gpointer user_data)
{
    (void)btn; (void)user_data;
    if (undo_redo(g_gtk.engine))
        sq_app_set_status(&g_gtk.app, "Redo", 90);
}

static void on_rec_clicked(GtkWidget *btn, gpointer user_data)
{
    (void)btn; (void)user_data;
    sq_engine_t *engine = g_gtk.engine;

    char rec_path[600];
    snprintf(rec_path, sizeof(rec_path), "%srecording.wav",
             engine->base_dir[0] ? engine->base_dir : "");

    if (engine->recording) {
        engine->recording = false;
        if (engine->rec_frames > 0) {
            sq_export_result_t rec_result;
            memset(&rec_result, 0, sizeof(rec_result));
            rec_result.data = engine->rec_buffer;
            rec_result.num_frames = engine->rec_frames;
            rec_result.sample_rate = engine->sample_rate;
            sq_export_write_wav(rec_path, &rec_result, 16);
            LOG_INFO("Saved recording: %u frames -> %s", engine->rec_frames, rec_path);
            char msg[256];
            snprintf(msg, sizeof(msg), "Saved: %s", rec_path);
            sq_app_set_status(&g_gtk.app, msg, 300);
        }
        engine->rec_frames = 0;
    } else {
        sq_engine_start_recording(engine);
        char msg[256];
        snprintf(msg, sizeof(msg), "REC -> %s", rec_path);
        sq_app_set_status(&g_gtk.app, msg, 0);
    }
}


static GtkWidget *s_panel_btns[SQ_PANEL_COUNT] = {0};

static void on_panel_toggled(GtkWidget *btn, gpointer user_data)
{
    (void)btn;
    sq_panel_t panel = (sq_panel_t)GPOINTER_TO_INT(user_data);
    sq_app_toggle_panel(&g_gtk.app, panel);

    /* Update button active state */
    for (int i = 0; i < SQ_PANEL_COUNT; i++) {
        if (!s_panel_btns[i]) continue;
        if (g_gtk.app.panels[i])
            gtk_widget_add_css_class(s_panel_btns[i], "active");
        else
            gtk_widget_remove_css_class(s_panel_btns[i], "active");
    }

    sync_panel_visibility();
}

/* ─── Toolbar action wrappers (for sq_flat_button_new callbacks) ───────────── */

static void on_presets_clicked(GtkWidget *btn, gpointer data)
{
    (void)btn; (void)data;
    gtk_presets_show_save(g_gtk.window);
}

static void on_export_clicked(GtkWidget *btn, gpointer data)
{
    (void)btn; (void)data;
    gtk_export_show(g_gtk.window);
}

/* ─── Help popover menu ───────────────────────────────────────────────────── */

static GtkWidget *s_help_popover = NULL;

static void on_help_clicked(GtkWidget *btn, gpointer data)
{
    (void)data;

    /* If popover already exists, just toggle it */
    if (s_help_popover && gtk_widget_get_parent(s_help_popover) == btn) {
        gtk_popover_popup(GTK_POPOVER(s_help_popover));
        return;
    }

    /* Destroy old popover if parent changed */
    if (s_help_popover) {
        gtk_widget_unparent(s_help_popover);
        s_help_popover = NULL;
    }

    /* Create popover */
    s_help_popover = gtk_popover_new();
    gtk_widget_set_parent(s_help_popover, btn);

    /* Scrolled window so content doesn't overflow */
    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(scroll, 300, 380);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_margin_start(box, 8);
    gtk_widget_set_margin_end(box, 8);
    gtk_widget_set_margin_top(box, 6);
    gtk_widget_set_margin_bottom(box, 6);

    /* Helper macros */
    #define HELP_HEADER(text) { \
        GtkWidget *h = gtk_label_new(NULL); \
        gtk_label_set_markup(GTK_LABEL(h), "<b>" text "</b>"); \
        gtk_widget_set_halign(h, GTK_ALIGN_START); \
        gtk_widget_set_margin_top(h, 6); \
        gtk_box_append(GTK_BOX(box), h); \
        GtkWidget *s = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL); \
        gtk_box_append(GTK_BOX(box), s); \
    }

    #define HELP_ROW(key, desc) { \
        GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8); \
        GtkWidget *k = gtk_label_new(NULL); \
        gtk_label_set_markup(GTK_LABEL(k), "<b>" key "</b>"); \
        gtk_widget_set_halign(k, GTK_ALIGN_START); \
        gtk_widget_set_size_request(k, 120, -1); \
        gtk_box_append(GTK_BOX(row), k); \
        GtkWidget *d = gtk_label_new(desc); \
        gtk_widget_set_halign(d, GTK_ALIGN_START); \
        gtk_box_append(GTK_BOX(row), d); \
        gtk_box_append(GTK_BOX(box), row); \
    }

    HELP_HEADER("Keyboard Shortcuts");
    HELP_ROW("Space",          "Play / Stop");
    HELP_ROW("1-9",            "Select pattern");
    HELP_ROW("Ctrl+Z",         "Undo");
    HELP_ROW("Ctrl+Shift+Z",   "Redo");
    HELP_ROW("Ctrl+T",         "Cycle themes");
    HELP_ROW("Ctrl+S",         "Save project");
    HELP_ROW("Ctrl+O",         "Open project");
    HELP_ROW("Escape",         "Quit");

    HELP_HEADER("QWERTY Piano (when KEYS panel open)");
    HELP_ROW("Z/X/C/V/B/N/M",  "Lower octave white keys");
    HELP_ROW("Q/W/E/R/T/Y/U/I/O/P", "Upper octave white keys");

    HELP_HEADER("Mouse Controls");
    HELP_ROW("Left-click",      "Toggle step / Place note");
    HELP_ROW("Left-drag",       "Paint steps / Extend note");
    HELP_ROW("Right-click",     "Velocity/pitch editor");
    HELP_ROW("Right-drag",      "Erase notes (piano roll)");
    HELP_ROW("Scroll wheel",    "Scroll / Cycle values");
    HELP_ROW("Double-click knob", "Reset to default");
    HELP_ROW("Drag knob",       "Adjust value (Shift=fine)");

    #undef HELP_HEADER
    #undef HELP_ROW

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), box);
    gtk_popover_set_child(GTK_POPOVER(s_help_popover), scroll);
    gtk_popover_popup(GTK_POPOVER(s_help_popover));
}

/* ─── Theme popover menu ──────────────────────────────────────────────────── */

static GtkWidget *s_theme_popover = NULL;

static void on_theme_item_clicked(GtkWidget *item_btn, gpointer user_data)
{
    (void)item_btn;
    int theme_idx = GPOINTER_TO_INT(user_data);
    gtk_theme_apply_index(g_gtk.window, theme_idx);

    /* Dismiss the popover */
    if (s_theme_popover)
        gtk_popover_popdown(GTK_POPOVER(s_theme_popover));

    /* Re-flatten buttons since theme CSS reload can affect them */
    gtk_theme_flatten_buttons(g_gtk.main_box);
}

static void on_user_theme_item_clicked(GtkWidget *item_btn, gpointer user_data)
{
    (void)item_btn;
    int user_idx = GPOINTER_TO_INT(user_data);
    gtk_theme_apply_user(g_gtk.window, user_idx);

    if (s_theme_popover)
        gtk_popover_popdown(GTK_POPOVER(s_theme_popover));

    gtk_theme_flatten_buttons(g_gtk.main_box);
}

static void on_theme_clicked(GtkWidget *btn, gpointer data)
{
    (void)data;

    /* Always rebuild the popover to ensure checkmarks are correct */
    if (s_theme_popover) {
        gtk_widget_unparent(s_theme_popover);
        s_theme_popover = NULL;
    }

    /* Create popover */
    s_theme_popover = gtk_popover_new();
    gtk_widget_set_parent(s_theme_popover, btn);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_margin_start(box, 4);
    gtk_widget_set_margin_end(box, 4);
    gtk_widget_set_margin_top(box, 4);
    gtk_widget_set_margin_bottom(box, 4);

    /* Header */
    GtkWidget *header = gtk_label_new("Select Theme:");
    gtk_widget_set_halign(header, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(box), header);

    GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_append(GTK_BOX(box), sep);

    /* Built-in theme entries */
    int cur = gtk_theme_current();
    int is_user = gtk_theme_is_user_active();
    int count = gtk_theme_count();
    for (int i = 0; i < count; i++) {
        char buf[64];
        if (!is_user && i == cur)
            snprintf(buf, sizeof(buf), ">> %s", gtk_theme_name(i));
        else
            snprintf(buf, sizeof(buf), "   %s", gtk_theme_name(i));

        GtkWidget *item = gtk_button_new_with_label(buf);
        gtk_widget_add_css_class(item, "flat");
        gtk_widget_set_size_request(item, 160, -1);
        g_signal_connect(item, "clicked",
                         G_CALLBACK(on_theme_item_clicked), GINT_TO_POINTER(i));
        gtk_box_append(GTK_BOX(box), item);
    }

    /* User themes (after separator) */
    int num_user = gtk_theme_num_user_themes();
    if (num_user > 0) {
        GtkWidget *sep2 = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
        gtk_box_append(GTK_BOX(box), sep2);

        GtkWidget *user_header = gtk_label_new("User Themes:");
        gtk_widget_set_halign(user_header, GTK_ALIGN_START);
        gtk_widget_set_sensitive(user_header, FALSE);
        gtk_box_append(GTK_BOX(box), user_header);

        int active_user = gtk_theme_active_user_index();
        for (int u = 0; u < num_user; u++) {
            char buf[64];
            if (is_user && u == active_user)
                snprintf(buf, sizeof(buf), ">> %s", gtk_theme_user_name(u));
            else
                snprintf(buf, sizeof(buf), "   %s", gtk_theme_user_name(u));

            GtkWidget *item = gtk_button_new_with_label(buf);
            gtk_widget_add_css_class(item, "flat");
            gtk_widget_set_size_request(item, 160, -1);
            g_signal_connect(item, "clicked",
                             G_CALLBACK(on_user_theme_item_clicked),
                             GINT_TO_POINTER(u));
            gtk_box_append(GTK_BOX(box), item);
        }
    }

    gtk_popover_set_child(GTK_POPOVER(s_theme_popover), box);
    gtk_popover_popup(GTK_POPOVER(s_theme_popover));
}

/* ─── QWERTY keyboard → MIDI note mapping ─────────────────────────────────── */

/* Two octaves mapped across QWERTY keyboard (matches ImGui frontend):
 *
 * Lower octave (Z row = white keys, A/S row = black keys):
 *   Z  X  C  V  B  N  M  ,  .  /
 *   C  D  E  F  G  A  B  C  D  E
 *     S  D     G  H  J     L  ;
 *     C# D#    F# G# A#    C# D#
 *
 * Upper octave (Q row = white keys, number row = black keys):
 *   Q  W  E  R  T  Y  U  I  O  P
 *   C  D  E  F  G  A  B  C  D  E
 *     2  3     5  6  7     9  0
 *     C# D#    F# G# A#    C# D#
 */

/* Map GDK keyval to semitone offset from base octave.
 * Returns -1 if the key is not mapped.
 * octave_out: 0 = lower octave (Z row), 1 = upper octave (Q row) */
static int gdk_keyval_to_semitone(guint keyval, int *octave_out)
{
    *octave_out = 0;
    switch (keyval) {
    /* Lower octave — white keys (Z row) */
    case GDK_KEY_z: return 0;   /* C */
    case GDK_KEY_x: return 2;   /* D */
    case GDK_KEY_c: return 4;   /* E */
    case GDK_KEY_v: return 5;   /* F */
    case GDK_KEY_b: return 7;   /* G */
    case GDK_KEY_n: return 9;   /* A */
    case GDK_KEY_m: return 11;  /* B */
    case GDK_KEY_comma:     return 12; /* C+1 */
    case GDK_KEY_period:    return 14; /* D+1 */
    case GDK_KEY_slash:     return 16; /* E+1 */

    /* Lower octave — black keys (A/S row) */
    case GDK_KEY_s: return 1;   /* C# */
    case GDK_KEY_d: return 3;   /* D# */
    case GDK_KEY_g: return 6;   /* F# */
    case GDK_KEY_h: return 8;   /* G# */
    case GDK_KEY_j: return 10;  /* A# */
    case GDK_KEY_l:         return 13; /* C#+1 */
    case GDK_KEY_semicolon: return 15; /* D#+1 */

    /* Upper octave — white keys (Q row) */
    case GDK_KEY_q: *octave_out = 1; return 0;
    case GDK_KEY_w: *octave_out = 1; return 2;
    case GDK_KEY_e: *octave_out = 1; return 4;
    case GDK_KEY_r: *octave_out = 1; return 5;
    case GDK_KEY_t: *octave_out = 1; return 7;
    case GDK_KEY_y: *octave_out = 1; return 9;
    case GDK_KEY_u: *octave_out = 1; return 11;
    case GDK_KEY_i: *octave_out = 1; return 12;
    case GDK_KEY_o: *octave_out = 1; return 14;
    case GDK_KEY_p: *octave_out = 1; return 16;

    /* Upper octave — black keys (number row) */
    case GDK_KEY_2: *octave_out = 1; return 1;
    case GDK_KEY_3: *octave_out = 1; return 3;
    case GDK_KEY_5: *octave_out = 1; return 6;
    case GDK_KEY_6: *octave_out = 1; return 8;
    case GDK_KEY_7: *octave_out = 1; return 10;
    case GDK_KEY_9: *octave_out = 1; return 13;
    case GDK_KEY_0: *octave_out = 1; return 15;

    default: return -1;
    }
}

/* Track which MIDI notes are held by QWERTY keys */
#define MAX_QWERTY_HELD 16
static struct { guint keyval; int midi_note; } s_qwerty_held[MAX_QWERTY_HELD];
static int s_qwerty_held_count = 0;

/* Base note for QWERTY keyboard (C3 = MIDI 48, matching gtk_keyboard.c) */
#define QWERTY_BASE_NOTE 48

static void qwerty_release_note(sq_engine_t *engine, int midi_note)
{
    float freq = 440.0f * powf(2.0f, ((float)midi_note - 69.0f) / 12.0f);
    for (int v = 0; v < SQ_MAX_SYNTH_VOICES; v++) {
        if (engine->synth_voices[v].active &&
            fabsf(engine->synth_voices[v].frequency - freq) < 0.1f)
            engine->synth_voices[v].amp_env.stage = ENV_RELEASE;
    }
}

/* Handle a QWERTY key press/release for piano playing.
 * Returns TRUE if the key was consumed (mapped to a note). */
static gboolean qwerty_key_event(guint keyval, gboolean pressed)
{
    sq_engine_t *engine = g_gtk.engine;
    int preset = sq_app_get_keyboard_preset(&g_gtk.app, engine);
    if (preset < 0) preset = 0;

    int octave;
    int semitone = gdk_keyval_to_semitone(keyval, &octave);
    if (semitone < 0) return FALSE;

    int midi_note = QWERTY_BASE_NOTE + octave * 12 + semitone;
    if (midi_note < 0 || midi_note > 127) return FALSE;

    if (pressed) {
        /* Don't re-trigger if already held (key repeat) */
        for (int i = 0; i < s_qwerty_held_count; i++) {
            if (s_qwerty_held[i].keyval == keyval) return TRUE;
        }

        /* Trigger note */
        synth_trigger(engine, preset, 0.8f, 0, 0.7f, 0.0f, (uint8_t)midi_note);

        if (s_qwerty_held_count < MAX_QWERTY_HELD) {
            s_qwerty_held[s_qwerty_held_count].keyval = keyval;
            s_qwerty_held[s_qwerty_held_count].midi_note = midi_note;
            s_qwerty_held_count++;
        }
    } else {
        /* Release: find and remove from held list, send note-off */
        for (int i = 0; i < s_qwerty_held_count; i++) {
            if (s_qwerty_held[i].keyval == keyval) {
                qwerty_release_note(engine, s_qwerty_held[i].midi_note);
                s_qwerty_held[i] = s_qwerty_held[s_qwerty_held_count - 1];
                s_qwerty_held_count--;
                break;
            }
        }
    }

    return TRUE;
}

/* ─── Keyboard shortcut handler ───────────────────────────────────────────── */

static gboolean on_key_released(GtkEventControllerKey *ctrl,
                                 guint keyval, guint keycode,
                                 GdkModifierType state, gpointer user_data)
{
    (void)ctrl; (void)keycode; (void)state; (void)user_data;

    /* QWERTY piano note-off */
    if (g_gtk.app.panels[SQ_PANEL_KEYBOARD] &&
        !(state & GDK_CONTROL_MASK) && !(state & GDK_ALT_MASK))
    {
        if (qwerty_key_event(keyval, FALSE))
            return TRUE;
    }

    return FALSE;
}

static gboolean on_key_pressed(GtkEventControllerKey *ctrl,
                                guint keyval, guint keycode,
                                GdkModifierType state, gpointer user_data)
{
    (void)ctrl; (void)keycode; (void)user_data;

    /* QWERTY piano — active when keyboard panel is shown, no Ctrl/Alt */
    if (g_gtk.app.panels[SQ_PANEL_KEYBOARD] &&
        !(state & GDK_CONTROL_MASK) && !(state & GDK_ALT_MASK))
    {
        if (qwerty_key_event(keyval, TRUE))
            return TRUE;
    }

    int sq_key = gdk_to_sq_key(keyval);
    int sq_mod = gdk_to_sq_mod(state);

    sq_app_action_t action = sq_app_handle_key(&g_gtk.app, g_gtk.engine,
                                                sq_key, sq_mod, true);

    if (sq_key == SQ_KEY_SPACE && g_gtk.engine->transport.playing)
        g_gtk.app.play_start_ticks = SDL_GetPerformanceCounter();

    switch (action) {
    case SQ_ACTION_QUIT:
        gtk_window_close(GTK_WINDOW(g_gtk.window));
        return TRUE;
    case SQ_ACTION_SAVE: {
        char path[512];
        snprintf(path, sizeof(path), "%sproject.sqproj", g_gtk.engine->base_dir);
        if (project_save(g_gtk.engine, path) == 0)
            sq_app_set_status(&g_gtk.app, "Saved!", 90);
        else
            sq_app_set_status(&g_gtk.app, "Save FAILED!", 90);
        return TRUE;
    }
    case SQ_ACTION_LOAD: {
        char path[512];
        snprintf(path, sizeof(path), "%sproject.sqproj", g_gtk.engine->base_dir);
        if (project_load(g_gtk.engine, path) == 0) {
            undo_clear();
            sq_app_set_status(&g_gtk.app, "Loaded!", 90);
        } else {
            sq_app_set_status(&g_gtk.app, "Load FAILED!", 90);
        }
        return TRUE;
    }
    case SQ_ACTION_TOGGLE_THEME:
        gtk_theme_cycle(g_gtk.window);
        return TRUE;
    case SQ_ACTION_NONE:
    default:
        break;
    }

    return (sq_key != SQ_KEY_NONE) ? TRUE : FALSE;
}

/* ─── Pattern selector callbacks ───────────────────────────────────────────── */

#define MAX_PAT_BTNS 16
static GtkWidget *s_pat_btns[MAX_PAT_BTNS] = {0};

static void on_pattern_clicked(GtkWidget *btn, gpointer user_data)
{
    (void)btn;
    int pat = GPOINTER_TO_INT(user_data);
    sq_engine_t *engine = g_gtk.engine;
    if ((uint32_t)pat < engine->num_patterns) {
        engine->transport.current_pattern = pat;
        char msg[32];
        snprintf(msg, sizeof(msg), "Pattern %d", pat + 1);
        sq_app_set_status(&g_gtk.app, msg, 60);
    }
}

static void on_add_pattern(GtkWidget *btn, gpointer user_data)
{
    (void)btn; (void)user_data;
    sq_engine_t *engine = g_gtk.engine;
    if (engine->num_patterns < SQ_MAX_PATTERNS) {
        int ni = (int)engine->num_patterns;
        engine->num_patterns++;
        sq_app_init_new_pattern(engine, ni);
        engine->transport.current_pattern = ni;
        char msg[32];
        snprintf(msg, sizeof(msg), "New pattern %d", ni + 1);
        sq_app_set_status(&g_gtk.app, msg, 60);
    }
}

/* ─── Mode selector callbacks ─────────────────────────────────────────────── */

static GtkWidget *s_mode_btns[3] = {0};

static void on_mode_clicked(GtkWidget *btn, gpointer user_data)
{
    (void)btn;
    int mode = GPOINTER_TO_INT(user_data);
    g_gtk.engine->transport.mode = (sq_play_mode_t)mode;

    for (int i = 0; i < 3; i++) {
        if (!s_mode_btns[i]) continue;
        if (i == mode)
            gtk_widget_add_css_class(s_mode_btns[i], "active");
        else
            gtk_widget_remove_css_class(s_mode_btns[i], "active");
    }
}

/* ─── Update pattern button active states ─────────────────────────────────── */

static void update_pattern_buttons(void)
{
    int cur = g_gtk.engine->transport.current_pattern;
    for (int i = 0; i < MAX_PAT_BTNS && i < (int)g_gtk.engine->num_patterns; i++) {
        if (!s_pat_btns[i]) continue;
        if (i == cur)
            gtk_widget_add_css_class(s_pat_btns[i], "active");
        else
            gtk_widget_remove_css_class(s_pat_btns[i], "active");
    }
}

/* ─── Logo drawing area with pulsing glow ─────────────────────────────────── */

static void logo_draw_cb(GtkDrawingArea *area, cairo_t *cr,
                          int width, int height, gpointer data)
{
    (void)area; (void)data;

    /* Get accent color for glow */
    double ar, ag, ab;
    gtk_theme_get_accent_color(&ar, &ag, &ab);

    /* Text setup */
    cairo_select_font_face(cr, "DejaVu Sans Mono",
                           CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 16.0);

    cairo_text_extents_t ext;
    cairo_text_extents(cr, "0x808", &ext);
    double tx = (width - ext.width) / 2.0 - ext.x_bearing;
    double ty = (height - ext.height) / 2.0 - ext.y_bearing;

    /* Compute glow intensity: pulse when playing, off when stopped */
    double glow = 0.0;
    sq_engine_t *engine = g_gtk.engine;
    if (engine && engine->transport.playing) {
        /* Pulse based on fractional beat — peaks on each beat */
        double beat_frac = engine->transport.current_beat;
        beat_frac = beat_frac - floor(beat_frac);  /* 0..1 within one beat */
        /* Sharp attack, exponential decay: bright flash on the beat */
        glow = exp(-beat_frac * 4.0);  /* peaks at 1.0, decays to ~0.02 */
        glow = 0.3 + glow * 0.7;      /* range: 0.3 .. 1.0 */
    }

    if (glow > 0.01) {
        /* Draw glow layers behind text */
        for (int pass = 3; pass >= 1; pass--) {
            double spread = pass * 2.0;
            double alpha = glow * 0.15 / pass;
            cairo_set_source_rgba(cr, ar, ag, ab, alpha);
            cairo_move_to(cr, tx - spread, ty + spread * 0.3);
            cairo_show_text(cr, "0x808");
            cairo_move_to(cr, tx + spread, ty - spread * 0.3);
            cairo_show_text(cr, "0x808");
        }
        /* Inner glow */
        cairo_set_source_rgba(cr, ar, ag, ab, glow * 0.35);
        cairo_move_to(cr, tx, ty);
        cairo_show_text(cr, "0x808");
    }

    /* Draw crisp text on top — use theme text color from widget style */
    GdkRGBA text_color;
    GtkStyleContext *ctx = gtk_widget_get_style_context(GTK_WIDGET(area));
    gtk_style_context_get_color(ctx, &text_color);
    cairo_set_source_rgba(cr, text_color.red, text_color.green,
                          text_color.blue, text_color.alpha);
    cairo_move_to(cr, tx, ty);
    cairo_show_text(cr, "0x808");
}

/* ─── Window control callbacks ─────────────────────────────────────────────── */

static void on_wc_minimize(GtkWidget *btn, gpointer data)
{
    (void)btn; (void)data;
    if (g_gtk.window)
        gtk_window_minimize(GTK_WINDOW(g_gtk.window));
}

static void on_wc_maximize(GtkWidget *btn, gpointer data)
{
    (void)btn; (void)data;
    if (!g_gtk.window) return;
    if (gtk_window_is_maximized(GTK_WINDOW(g_gtk.window)))
        gtk_window_unmaximize(GTK_WINDOW(g_gtk.window));
    else
        gtk_window_maximize(GTK_WINDOW(g_gtk.window));
}

static void on_wc_close(GtkWidget *btn, gpointer data)
{
    (void)btn; (void)data;
    if (g_gtk.window)
        gtk_window_close(GTK_WINDOW(g_gtk.window));
}

/* ─── Build the toolbar ───────────────────────────────────────────────────── */

static GtkWidget *build_toolbar(void)
{
    GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);

    /* ── Row 1: Transport + Knobs + Panels ────────────────────────── */
    GtkWidget *row1 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_margin_start(row1, 8);
    gtk_widget_set_margin_end(row1, 8);
    gtk_widget_set_margin_top(row1, 4);
    gtk_box_append(GTK_BOX(outer), row1);

    /* Logo (Cairo drawing area with pulsing glow during playback) */
    s_logo_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(s_logo_area, 60, 30);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(s_logo_area),
                                   logo_draw_cb, NULL, NULL);
    gtk_box_append(GTK_BOX(row1), s_logo_area);

    /* Play/Stop */
    g_gtk.play_btn = sq_flat_button_new("PLAY",
        G_CALLBACK(on_play_clicked), NULL);
    gtk_box_append(GTK_BOX(row1), g_gtk.play_btn);

    /* REC */
    s_rec_btn = sq_flat_button_new("REC",
        G_CALLBACK(on_rec_clicked), NULL);
    gtk_box_append(GTK_BOX(row1), s_rec_btn);

    /* Undo / Redo */
    s_undo_btn = sq_flat_button_new("UNDO",
        G_CALLBACK(on_undo_clicked), NULL);
    gtk_widget_set_opacity(s_undo_btn, 0.35);
    gtk_box_append(GTK_BOX(row1), s_undo_btn);

    s_redo_btn = sq_flat_button_new("REDO",
        G_CALLBACK(on_redo_clicked), NULL);
    gtk_widget_set_opacity(s_redo_btn, 0.35);
    gtk_box_append(GTK_BOX(row1), s_redo_btn);

    /* BPM knob */
    s_bpm_float = (float)g_gtk.engine->transport.bpm;
    GtkWidget *bpm_knob = gtk_knob_new(60, 200, &s_bpm_float, "BPM");
    gtk_widget_set_size_request(bpm_knob, 50, 55);
    gtk_box_append(GTK_BOX(row1), bpm_knob);

    /* Swing knob */
    s_swing_float = g_gtk.engine->transport.swing * 100.0f;
    GtkWidget *swing_knob = gtk_knob_new(0, 100, &s_swing_float, "Sw");
    gtk_widget_set_size_request(swing_knob, 50, 55);
    gtk_box_append(GTK_BOX(row1), swing_knob);

    /* Volume knob */
    s_vol_float = g_gtk.engine->master_volume * 100.0f;
    GtkWidget *vol_knob = gtk_knob_new(0, 100, &s_vol_float, "Vol");
    gtk_widget_set_size_request(vol_knob, 50, 55);
    gtk_box_append(GTK_BOX(row1), vol_knob);

    /* Separator */
    GtkWidget *vsep1 = gtk_separator_new(GTK_ORIENTATION_VERTICAL);
    gtk_box_append(GTK_BOX(row1), vsep1);

    /* Toolbar button order matches ImGui:
     * EXPORT | PRESETS | PIANO | KEYS | PAT/SONG/PERF | MIXER/FX | BROWSE | THEME */

    /* Export button */
    GtkWidget *export_btn = sq_flat_button_new("EXPORT",
        G_CALLBACK(on_export_clicked), NULL);
    gtk_box_append(GTK_BOX(row1), export_btn);

    /* Presets button */
    GtkWidget *presets_btn = sq_flat_button_new("PRESETS",
        G_CALLBACK(on_presets_clicked), NULL);
    gtk_box_append(GTK_BOX(row1), presets_btn);

    /* PIANO + KEYS panel buttons */
    {
        const int first_panels[] = { SQ_PANEL_PIANO_ROLL, SQ_PANEL_KEYBOARD };
        const char *first_names[] = { "PIANO", "KEYS" };
        for (int i = 0; i < 2; i++) {
            int pi = first_panels[i];
            s_panel_btns[pi] = sq_flat_button_new(first_names[i],
                G_CALLBACK(on_panel_toggled), GINT_TO_POINTER(pi));
            gtk_box_append(GTK_BOX(row1), s_panel_btns[pi]);
            if (g_gtk.app.panels[pi])
                gtk_widget_add_css_class(s_panel_btns[pi], "active");
        }
    }

    /* Mode buttons: PAT / SONG / PERF */
    const char *mode_names[] = {"PAT", "SONG", "PERF"};
    for (int i = 0; i < 3; i++) {
        s_mode_btns[i] = sq_flat_button_new(mode_names[i],
            G_CALLBACK(on_mode_clicked), GINT_TO_POINTER(i));
        gtk_box_append(GTK_BOX(row1), s_mode_btns[i]);
        if (i == (int)g_gtk.engine->transport.mode)
            gtk_widget_add_css_class(s_mode_btns[i], "active");
    }

    /* MIXER/FX + BROWSE panel buttons */
    {
        const int last_panels[] = { SQ_PANEL_MIXER, SQ_PANEL_BROWSER };
        const char *last_names[] = { "MIXER/FX", "BROWSE" };
        for (int i = 0; i < 2; i++) {
            int pi = last_panels[i];
            s_panel_btns[pi] = sq_flat_button_new(last_names[i],
                G_CALLBACK(on_panel_toggled), GINT_TO_POINTER(pi));
            gtk_box_append(GTK_BOX(row1), s_panel_btns[pi]);
            if (g_gtk.app.panels[pi])
                gtk_widget_add_css_class(s_panel_btns[pi], "active");
        }
    }

    /* Theme button */
    GtkWidget *theme_btn = sq_flat_button_new("THEME",
        G_CALLBACK(on_theme_clicked), NULL);
    gtk_box_append(GTK_BOX(row1), theme_btn);

    /* Help button */
    GtkWidget *help_btn = sq_flat_button_new("?",
        G_CALLBACK(on_help_clicked), NULL);
    gtk_widget_set_size_request(help_btn, 28, -1);
    gtk_box_append(GTK_BOX(row1), help_btn);

    /* Status label */
    g_gtk.status_label = gtk_label_new("");
    gtk_widget_add_css_class(g_gtk.status_label, "status");
    gtk_widget_set_hexpand(g_gtk.status_label, TRUE);
    gtk_widget_set_halign(g_gtk.status_label, GTK_ALIGN_END);
    gtk_box_append(GTK_BOX(row1), g_gtk.status_label);

    /* ── Window controls: _ [] X ──────────────────────────────────── */
    {
        GtkWidget *wc_min = sq_flat_button_new("\xe2\x80\x95",
            G_CALLBACK(on_wc_minimize), NULL);
        gtk_widget_set_size_request(wc_min, 32, -1);
        gtk_widget_add_css_class(wc_min, "wc-btn");
        gtk_box_append(GTK_BOX(row1), wc_min);

        s_wc_maximize_btn = sq_flat_button_new("\xe2\x96\xa1",
            G_CALLBACK(on_wc_maximize), NULL);
        gtk_widget_set_size_request(s_wc_maximize_btn, 32, -1);
        gtk_widget_add_css_class(s_wc_maximize_btn, "wc-btn");
        gtk_box_append(GTK_BOX(row1), s_wc_maximize_btn);

        GtkWidget *wc_close = sq_flat_button_new("\xc3\x97",
            G_CALLBACK(on_wc_close), NULL);
        gtk_widget_set_size_request(wc_close, 32, -1);
        gtk_widget_add_css_class(wc_close, "wc-btn");
        gtk_widget_add_css_class(wc_close, "wc-close");
        gtk_box_append(GTK_BOX(row1), wc_close);
    }

    /* ── Row 2: Pattern selector ──────────────────────────────────── */
    GtkWidget *row2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);
    gtk_widget_set_margin_start(row2, 8);
    gtk_widget_set_margin_end(row2, 8);
    gtk_widget_set_margin_bottom(row2, 2);
    gtk_box_append(GTK_BOX(outer), row2);

    for (uint32_t i = 0; i < g_gtk.engine->num_patterns && i < MAX_PAT_BTNS; i++) {
        char plbl[8];
        snprintf(plbl, sizeof(plbl), "%d", i + 1);
        s_pat_btns[i] = sq_flat_button_new(plbl,
            G_CALLBACK(on_pattern_clicked), GINT_TO_POINTER(i));
        gtk_widget_set_size_request(s_pat_btns[i], 28, 22);
        gtk_box_append(GTK_BOX(row2), s_pat_btns[i]);
        if ((int)i == g_gtk.engine->transport.current_pattern)
            gtk_widget_add_css_class(s_pat_btns[i], "active");
    }

    /* Add pattern button */
    GtkWidget *add_btn = sq_flat_button_new("+",
        G_CALLBACK(on_add_pattern), NULL);
    gtk_widget_set_size_request(add_btn, 28, 22);
    gtk_box_append(GTK_BOX(row2), add_btn);

    return outer;
}

/* ─── Kit selector callback ────────────────────────────────────────────────── */

static void on_kit_selected(GtkDropDown *dropdown, GParamSpec *pspec, gpointer data)
{
    (void)pspec; (void)data;
    sq_engine_t *engine = g_gtk.engine;
    if (!engine) return;

    guint idx = gtk_drop_down_get_selected(dropdown);
    if (idx == GTK_INVALID_LIST_POSITION) return;
    int kit_idx = (int)idx;
    if (kit_idx == sq_current_kit) return;

    if (sq_kit_load(engine, kit_idx, engine->base_dir) == 0) {
        char msg[64];
        snprintf(msg, sizeof(msg), "Kit: %s", sq_kits[kit_idx].name);
        sq_app_set_status(&g_gtk.app, msg, 90);
        gtk_drum_grid_queue_redraw();
    }
}

/* ─── Window activation ───────────────────────────────────────────────────── */

void gtk_window_setup(GtkApplication *app, gpointer user_data)
{
    (void)user_data;

    /* Create main window */
    g_gtk.window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(g_gtk.window), "0x808");
    gtk_window_set_default_size(GTK_WINDOW(g_gtk.window), 1280, 720);
    gtk_widget_set_size_request(g_gtk.window, 800, 500);

    /* Apply light theme (default) */
    gtk_theme_apply_light(g_gtk.window);

    /* Keyboard event controller */
    GtkEventController *key_ctrl = gtk_event_controller_key_new();
    g_signal_connect(key_ctrl, "key-pressed",
                     G_CALLBACK(on_key_pressed), NULL);
    g_signal_connect(key_ctrl, "key-released",
                     G_CALLBACK(on_key_released), NULL);
    gtk_widget_add_controller(g_gtk.window, key_ctrl);

    /* ── Top-level vertical layout: toolbar | content | keyboard ──────── */
    g_gtk.main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_window_set_child(GTK_WINDOW(g_gtk.window), g_gtk.main_box);

    /* Toolbar */
    g_gtk.toolbar_box = build_toolbar();
    gtk_box_append(GTK_BOX(g_gtk.main_box), g_gtk.toolbar_box);

    GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_append(GTK_BOX(g_gtk.main_box), sep);

    /* ── Arrangement panel (visible in SONG/PERFORM modes) ────────────── */
    g_gtk.arrangement_area = gtk_arrangement_new();
    gtk_widget_set_visible(g_gtk.arrangement_area,
                           g_gtk.engine->transport.mode != MODE_PATTERN);
    gtk_box_append(GTK_BOX(g_gtk.main_box), g_gtk.arrangement_area);

    /* ── Content area: horizontal split [main area | browser] ─────────── */
    GtkWidget *content_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_vexpand(content_box, TRUE);
    gtk_widget_set_hexpand(content_box, TRUE);
    gtk_box_append(GTK_BOX(g_gtk.main_box), content_box);

    /* ── Main area: vertical split [drum grid | bottom panels] ────────── */
    g_gtk.grid_area = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_hexpand(g_gtk.grid_area, TRUE);
    gtk_widget_set_vexpand(g_gtk.grid_area, TRUE);
    gtk_box_append(GTK_BOX(content_box), g_gtk.grid_area);

    /* Kit selector bar */
    {
        GtkWidget *kit_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        gtk_widget_set_margin_start(kit_bar, 6);
        gtk_widget_set_margin_end(kit_bar, 6);
        gtk_widget_set_margin_top(kit_bar, 2);
        gtk_widget_set_margin_bottom(kit_bar, 2);

        GtkWidget *kit_lbl = gtk_label_new("Kit:");
        gtk_box_append(GTK_BOX(kit_bar), kit_lbl);

        /* Build NULL-terminated string list of kit names */
        const char *kit_names[SQ_NUM_KITS + 1];
        for (int ki = 0; ki < SQ_NUM_KITS; ki++)
            kit_names[ki] = sq_kits[ki].name;
        kit_names[SQ_NUM_KITS] = NULL;

        GtkStringList *kit_model = gtk_string_list_new(kit_names);

        GtkWidget *kit_dropdown = gtk_drop_down_new(
            G_LIST_MODEL(kit_model), NULL);
        gtk_drop_down_set_selected(GTK_DROP_DOWN(kit_dropdown),
            (sq_current_kit >= 0 && sq_current_kit < SQ_NUM_KITS)
                ? (guint)sq_current_kit : 0);
        g_signal_connect(kit_dropdown, "notify::selected",
                         G_CALLBACK(on_kit_selected), NULL);
        gtk_box_append(GTK_BOX(kit_bar), kit_dropdown);

        /* Sample count label */
        GtkWidget *samp_lbl = gtk_label_new(NULL);
        if (g_gtk.engine) {
            char sbuf[48];
            snprintf(sbuf, sizeof(sbuf), "(%u samples loaded)",
                     g_gtk.engine->num_samples);
            gtk_label_set_text(GTK_LABEL(samp_lbl), sbuf);
        }
        gtk_widget_set_opacity(samp_lbl, 0.5);
        gtk_box_append(GTK_BOX(kit_bar), samp_lbl);

        gtk_box_append(GTK_BOX(g_gtk.grid_area), kit_bar);
    }

    /* Drum grid */
    g_gtk.drum_grid_area = gtk_drum_grid_new();
    gtk_widget_set_vexpand(g_gtk.drum_grid_area, TRUE);
    gtk_widget_set_hexpand(g_gtk.drum_grid_area, TRUE);
    gtk_box_append(GTK_BOX(g_gtk.grid_area), g_gtk.drum_grid_area);

    /* Add track buttons below the drum grid */
    {
        GtkWidget *add_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
        gtk_widget_set_margin_start(add_row, 4);
        gtk_widget_set_margin_top(add_row, 2);
        gtk_widget_set_margin_bottom(add_row, 2);

        GtkWidget *add_smp = sq_flat_button_new("+ Sampler",
            G_CALLBACK(gtk_drum_grid_add_sampler_track), NULL);
        gtk_box_append(GTK_BOX(add_row), add_smp);

        GtkWidget *add_syn = sq_flat_button_new("+ Synth",
            G_CALLBACK(gtk_drum_grid_add_synth_track), NULL);
        gtk_box_append(GTK_BOX(add_row), add_syn);

        gtk_box_append(GTK_BOX(g_gtk.grid_area), add_row);
    }

    /* ── Bottom panel area (piano roll + synth editor + mixer) ────────── */
    GtkWidget *bottom_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_size_request(bottom_box, -1, 140);
    gtk_widget_set_visible(bottom_box, FALSE);
    gtk_box_append(GTK_BOX(g_gtk.grid_area), bottom_box);

    /* Piano roll */
    g_gtk.piano_roll_area = gtk_piano_roll_new();
    gtk_widget_set_hexpand(g_gtk.piano_roll_area, TRUE);
    gtk_widget_set_visible(g_gtk.piano_roll_area, FALSE);
    gtk_box_append(GTK_BOX(bottom_box), g_gtk.piano_roll_area);

    /* Synth editor — width set dynamically in sync_panel_visibility */
    g_gtk.synth_editor_box = gtk_synth_editor_new();
    gtk_widget_set_hexpand(g_gtk.synth_editor_box, FALSE);
    gtk_widget_set_visible(g_gtk.synth_editor_box, FALSE);
    gtk_box_append(GTK_BOX(bottom_box), g_gtk.synth_editor_box);

    /* Mixer — wrapped in scrolled window to prevent height overflow */
    {
        GtkWidget *mixer_scroll = gtk_scrolled_window_new();
        gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(mixer_scroll),
                                       GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
        gtk_widget_set_hexpand(mixer_scroll, FALSE);
        gtk_widget_set_visible(mixer_scroll, FALSE);

        GtkWidget *mixer_inner = gtk_mixer_new();
        gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(mixer_scroll), mixer_inner);

        g_gtk.mixer_box = mixer_scroll;
        gtk_box_append(GTK_BOX(bottom_box), g_gtk.mixer_box);
    }

    /* ── Browser (right side panel) ───────────────────────────────────── */
    g_gtk.browser_box = gtk_browser_new();
    gtk_widget_set_size_request(g_gtk.browser_box, 300, -1);
    gtk_widget_set_visible(g_gtk.browser_box, FALSE);
    gtk_box_append(GTK_BOX(content_box), g_gtk.browser_box);

    /* ── Virtual keyboard (bottom of entire window) ───────────────────── */
    g_gtk.keyboard_area = gtk_keyboard_new();
    gtk_widget_set_visible(g_gtk.keyboard_area, FALSE);
    gtk_box_append(GTK_BOX(g_gtk.main_box), g_gtk.keyboard_area);

    /* Flatten all buttons (remove Adwaita's thick borders) */
    gtk_theme_flatten_buttons(g_gtk.main_box);

    /* Start redraw timer (~60fps) */
    g_gtk.redraw_timer_id = g_timeout_add(16, on_redraw_tick, NULL);

    /* Show window */
    gtk_window_present(GTK_WINDOW(g_gtk.window));

    LOG_INFO("GTK window created (1280x720, dark theme, all panels ready)");
}
