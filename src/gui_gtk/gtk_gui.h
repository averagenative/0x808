/*
 * gtk_gui.h — GTK 4.0 frontend shared types and declarations.
 */

#ifndef SQ_GTK_GUI_H
#define SQ_GTK_GUI_H

#include <gtk/gtk.h>
#include "app/sq_app.h"
#include "engine/engine.h"

/* Audio thread types — opaque to component files, only main_gtk.c uses them */

/* ─── Application-wide state ──────────────────────────────────────────────── */

typedef struct {
    GtkApplication *gtk_app;
    GtkWidget      *window;
    sq_engine_t    *engine;
    sq_app_t        app;

    /* Layout containers */
    GtkWidget *main_box;        /* vertical: toolbar | content */
    GtkWidget *content_paned;   /* horizontal: grid area | browser */
    GtkWidget *grid_area;       /* vertical: drum grid | bottom panels */

    /* Panel widgets */
    GtkWidget *toolbar_box;
    GtkWidget *drum_grid_area;
    GtkWidget *piano_roll_area;
    GtkWidget *synth_editor_box;
    GtkWidget *mixer_box;
    GtkWidget *browser_box;
    GtkWidget *keyboard_area;
    GtkWidget *arrangement_area;

    /* Toolbar controls */
    GtkWidget *play_btn;
    GtkWidget *bpm_scale;
    GtkWidget *swing_scale;
    GtkWidget *volume_scale;
    GtkWidget *status_label;

    /* Audio thread (managed by main_gtk.c — opaque here) */
    uint32_t  audio_dev;
    void     *audio_thread;
    int       audio_running;

    /* Redraw timer */
    guint redraw_timer_id;

} sq_gtk_state_t;

/* Global state (single-instance GTK app) */
extern sq_gtk_state_t g_gtk;

/* ─── Component init/draw functions ───────────────────────────────────────── */

/* gtk_window.c */
void gtk_window_setup(GtkApplication *app, gpointer user_data);

/* gtk_theme.c */
void gtk_theme_apply_dark(GtkWidget *widget);
void gtk_theme_apply_light(GtkWidget *widget);
void gtk_theme_cycle(GtkWidget *widget);
void gtk_theme_flatten_buttons(GtkWidget *root);

/* Create a clickable label that looks like a flat button (no Adwaita chrome).
 * Uses GtkLabel + GtkGestureClick — no GtkButton border artifacts. */
GtkWidget *sq_flat_button_new(const char *label, GCallback callback, gpointer data);
int  gtk_theme_current(void);
const char *gtk_theme_current_name(void);

/* gtk_drum_grid.c */
GtkWidget *gtk_drum_grid_new(void);
void       gtk_drum_grid_queue_redraw(void);

/* gtk_piano_roll.c */
GtkWidget *gtk_piano_roll_new(void);

/* gtk_knob.c */
GtkWidget *gtk_knob_new(float min, float max, float *value, const char *label);

/* gtk_synth_editor.c */
GtkWidget *gtk_synth_editor_new(void);
void       gtk_synth_editor_update(void);

/* gtk_mixer.c */
GtkWidget *gtk_mixer_new(void);

/* gtk_browser.c */
GtkWidget *gtk_browser_new(void);

/* gtk_keyboard.c */
GtkWidget *gtk_keyboard_new(void);

/* gtk_arrangement.c */
GtkWidget *gtk_arrangement_new(void);

/* gtk_presets.c */
void gtk_presets_show_save(GtkWidget *parent);
void gtk_presets_show_load(GtkWidget *parent);

/* gtk_export.c */
void gtk_export_show(GtkWidget *parent);

/* ─── GDK keyval → SQ_KEY translation ────────────────────────────────────── */

static inline int gdk_to_sq_key(guint keyval)
{
    switch (keyval) {
    case GDK_KEY_space:      return SQ_KEY_SPACE;
    case GDK_KEY_Escape:     return SQ_KEY_ESCAPE;
    case GDK_KEY_1:          return SQ_KEY_1;
    case GDK_KEY_2:          return SQ_KEY_2;
    case GDK_KEY_3:          return SQ_KEY_3;
    case GDK_KEY_4:          return SQ_KEY_4;
    case GDK_KEY_5:          return SQ_KEY_5;
    case GDK_KEY_6:          return SQ_KEY_6;
    case GDK_KEY_7:          return SQ_KEY_7;
    case GDK_KEY_8:          return SQ_KEY_8;
    case GDK_KEY_9:          return SQ_KEY_9;
    case GDK_KEY_c:          return SQ_KEY_C;
    case GDK_KEY_o:          return SQ_KEY_O;
    case GDK_KEY_s:          return SQ_KEY_S;
    case GDK_KEY_t:          return SQ_KEY_T;
    case GDK_KEY_v:          return SQ_KEY_V;
    case GDK_KEY_z:          return SQ_KEY_Z;
    case GDK_KEY_equal:
    case GDK_KEY_plus:       return SQ_KEY_EQUALS;
    default:                 return SQ_KEY_NONE;
    }
}

static inline int gdk_to_sq_mod(GdkModifierType state)
{
    int mod = 0;
    if (state & GDK_CONTROL_MASK) mod |= SQ_MOD_CTRL;
    if (state & GDK_SHIFT_MASK)   mod |= SQ_MOD_SHIFT;
    if (state & GDK_ALT_MASK)     mod |= SQ_MOD_ALT;
    return mod;
}

#endif /* SQ_GTK_GUI_H */
