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

#include <SDL2/SDL.h>
#include <stdio.h>
#include <math.h>

#define LOG_TAG "gtk_window"
#include "core/log.h"

/* Forward declarations */
static void update_pattern_buttons(void);

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
    if (g_gtk.mixer_box) {
        gtk_widget_set_visible(g_gtk.mixer_box,
                               g_gtk.app.panels[SQ_PANEL_MIXER]);
    }

    /* Bottom pane container */
    if (g_gtk.grid_area) {
        /* The bottom_box is the second child of grid_area (after drum_grid) */
        GtkWidget *bottom = gtk_widget_get_last_child(g_gtk.grid_area);
        if (bottom && bottom != g_gtk.drum_grid_area)
            gtk_widget_set_visible(bottom, show_bottom);
    }

    /* Virtual keyboard */
    if (g_gtk.keyboard_area) {
        gtk_widget_set_visible(g_gtk.keyboard_area,
                               g_gtk.app.panels[SQ_PANEL_KEYBOARD]);
    }
}

/* ─── Periodic redraw timer ───────────────────────────────────────────────── */

static gboolean on_redraw_tick(gpointer user_data)
{
    (void)user_data;
    sq_engine_t *engine = g_gtk.engine;

    /* Guard: don't touch widgets before window is fully built */
    if (!g_gtk.window || !gtk_widget_get_realized(g_gtk.window))
        return G_SOURCE_CONTINUE;

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

    /* Sync panel visibility + pattern buttons */
    sync_panel_visibility();
    update_pattern_buttons();

    /* Rebuild synth editor if selected track changed */
    gtk_synth_editor_update();

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
    if (engine->transport.playing)
        g_gtk.app.play_start_ticks = SDL_GetPerformanceCounter();
}

static void on_bpm_changed(GtkRange *range, gpointer user_data)
{
    (void)user_data;
    g_gtk.engine->transport.bpm = gtk_range_get_value(range);
}

static void on_swing_changed(GtkRange *range, gpointer user_data)
{
    (void)user_data;
    g_gtk.engine->transport.swing = (float)gtk_range_get_value(range) / 100.0f;
}

static void on_volume_changed(GtkRange *range, gpointer user_data)
{
    (void)user_data;
    g_gtk.engine->master_volume = (float)gtk_range_get_value(range) / 100.0f;
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

static void on_theme_clicked(GtkWidget *btn, gpointer data)
{
    (void)btn; (void)data;
    gtk_theme_cycle(g_gtk.window);
}

/* ─── Keyboard shortcut handler ───────────────────────────────────────────── */

static gboolean on_key_pressed(GtkEventControllerKey *ctrl,
                                guint keyval, guint keycode,
                                GdkModifierType state, gpointer user_data)
{
    (void)ctrl; (void)keycode; (void)user_data;

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

    /* Logo */
    GtkWidget *logo = gtk_label_new("0x808");
    gtk_widget_add_css_class(logo, "logo");
    gtk_box_append(GTK_BOX(row1), logo);

    /* Play/Stop */
    g_gtk.play_btn = sq_flat_button_new("PLAY",
        G_CALLBACK(on_play_clicked), NULL);
    gtk_box_append(GTK_BOX(row1), g_gtk.play_btn);

    /* BPM */
    GtkWidget *bpm_label = gtk_label_new("BPM:");
    gtk_box_append(GTK_BOX(row1), bpm_label);
    g_gtk.bpm_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL,
                                                40.0, 300.0, 1.0);
    gtk_range_set_value(GTK_RANGE(g_gtk.bpm_scale), g_gtk.engine->transport.bpm);
    gtk_widget_set_size_request(g_gtk.bpm_scale, 100, -1);
    gtk_scale_set_draw_value(GTK_SCALE(g_gtk.bpm_scale), TRUE);
    g_signal_connect(g_gtk.bpm_scale, "value-changed",
                     G_CALLBACK(on_bpm_changed), NULL);
    gtk_box_append(GTK_BOX(row1), g_gtk.bpm_scale);

    /* Swing */
    GtkWidget *swing_label = gtk_label_new("Sw:");
    gtk_box_append(GTK_BOX(row1), swing_label);
    g_gtk.swing_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL,
                                                  0.0, 100.0, 1.0);
    gtk_range_set_value(GTK_RANGE(g_gtk.swing_scale),
                        g_gtk.engine->transport.swing * 100.0f);
    gtk_widget_set_size_request(g_gtk.swing_scale, 70, -1);
    g_signal_connect(g_gtk.swing_scale, "value-changed",
                     G_CALLBACK(on_swing_changed), NULL);
    gtk_box_append(GTK_BOX(row1), g_gtk.swing_scale);

    /* Volume */
    GtkWidget *vol_label = gtk_label_new("Vol:");
    gtk_box_append(GTK_BOX(row1), vol_label);
    g_gtk.volume_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL,
                                                   0.0, 100.0, 1.0);
    gtk_range_set_value(GTK_RANGE(g_gtk.volume_scale), 80.0);
    gtk_widget_set_size_request(g_gtk.volume_scale, 70, -1);
    g_signal_connect(g_gtk.volume_scale, "value-changed",
                     G_CALLBACK(on_volume_changed), NULL);
    gtk_box_append(GTK_BOX(row1), g_gtk.volume_scale);

    /* Separator */
    GtkWidget *vsep1 = gtk_separator_new(GTK_ORIENTATION_VERTICAL);
    gtk_box_append(GTK_BOX(row1), vsep1);

    /* Mode buttons: PAT / SONG / PERF */
    const char *mode_names[] = {"PAT", "SONG", "PERF"};
    for (int i = 0; i < 3; i++) {
        s_mode_btns[i] = sq_flat_button_new(mode_names[i],
            G_CALLBACK(on_mode_clicked), GINT_TO_POINTER(i));
        gtk_box_append(GTK_BOX(row1), s_mode_btns[i]);
        if (i == (int)g_gtk.engine->transport.mode)
            gtk_widget_add_css_class(s_mode_btns[i], "active");
    }

    /* Separator */
    GtkWidget *vsep2 = gtk_separator_new(GTK_ORIENTATION_VERTICAL);
    gtk_box_append(GTK_BOX(row1), vsep2);

    /* Presets button */
    GtkWidget *presets_btn = sq_flat_button_new("PRESETS",
        G_CALLBACK(on_presets_clicked), NULL);
    gtk_box_append(GTK_BOX(row1), presets_btn);

    /* Export button */
    GtkWidget *export_btn = sq_flat_button_new("EXPORT",
        G_CALLBACK(on_export_clicked), NULL);
    gtk_box_append(GTK_BOX(row1), export_btn);

    /* Theme button */
    GtkWidget *theme_btn = sq_flat_button_new("THEME",
        G_CALLBACK(on_theme_clicked), NULL);
    gtk_box_append(GTK_BOX(row1), theme_btn);

    /* Panel toggle buttons */
    const char *panel_names[] = {"BROWSE", "MIXER", "PIANO", "KEYS"};
    for (int i = 0; i < SQ_PANEL_COUNT; i++) {
        s_panel_btns[i] = sq_flat_button_new(panel_names[i],
            G_CALLBACK(on_panel_toggled), GINT_TO_POINTER(i));
        gtk_box_append(GTK_BOX(row1), s_panel_btns[i]);
        if (g_gtk.app.panels[i])
            gtk_widget_add_css_class(s_panel_btns[i], "active");
    }

    /* Status label */
    g_gtk.status_label = gtk_label_new("");
    gtk_widget_add_css_class(g_gtk.status_label, "status");
    gtk_widget_set_hexpand(g_gtk.status_label, TRUE);
    gtk_widget_set_halign(g_gtk.status_label, GTK_ALIGN_END);
    gtk_box_append(GTK_BOX(row1), g_gtk.status_label);

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

    /* Drum grid */
    g_gtk.drum_grid_area = gtk_drum_grid_new();
    gtk_widget_set_vexpand(g_gtk.drum_grid_area, TRUE);
    gtk_widget_set_hexpand(g_gtk.drum_grid_area, TRUE);
    gtk_box_append(GTK_BOX(g_gtk.grid_area), g_gtk.drum_grid_area);

    /* ── Bottom panel area (piano roll + synth editor + mixer) ────────── */
    GtkWidget *bottom_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_size_request(bottom_box, -1, 250);
    gtk_widget_set_visible(bottom_box, FALSE);
    gtk_box_append(GTK_BOX(g_gtk.grid_area), bottom_box);

    /* Piano roll */
    g_gtk.piano_roll_area = gtk_piano_roll_new();
    gtk_widget_set_hexpand(g_gtk.piano_roll_area, TRUE);
    gtk_widget_set_visible(g_gtk.piano_roll_area, FALSE);
    gtk_box_append(GTK_BOX(bottom_box), g_gtk.piano_roll_area);

    /* Synth editor */
    g_gtk.synth_editor_box = gtk_synth_editor_new();
    gtk_widget_set_size_request(g_gtk.synth_editor_box, 250, -1);
    gtk_widget_set_visible(g_gtk.synth_editor_box, FALSE);
    gtk_box_append(GTK_BOX(bottom_box), g_gtk.synth_editor_box);

    /* Mixer */
    g_gtk.mixer_box = gtk_mixer_new();
    gtk_widget_set_hexpand(g_gtk.mixer_box, TRUE);
    gtk_widget_set_visible(g_gtk.mixer_box, FALSE);
    gtk_box_append(GTK_BOX(bottom_box), g_gtk.mixer_box);

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
