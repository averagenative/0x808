/*
 * gui_globals.cpp — Shared GUI global variables.
 *
 * Defined once here, linked via libsq_gui into both standalone and plugin targets.
 * Declared as `extern` in gui.h.
 */

#include <cstddef>

extern "C" {

int g_win_width    = 1280;
int g_win_height   = 720;
int g_visual_step  = 0;
int g_selected_track = -1;
/* g_pat_scroll is defined in toolbar.cpp */

/* MIDI handle for learn mode — set by standalone gui.cpp, NULL in plugin */
static struct sq_midi *s_gui_midi = NULL;

/* Global tooltip enable flag — checked by all SetTooltip calls */
int g_tooltips_enabled = 1;

void gui_set_midi(struct sq_midi *midi) { s_gui_midi = midi; }
struct sq_midi *gui_get_midi(void) { return s_gui_midi; }

}
