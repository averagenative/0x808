/*
 * gui_globals.cpp — Shared GUI global variables.
 *
 * Defined once here, linked via libsq_gui into both standalone and plugin targets.
 * Declared as `extern` in gui.h.
 */

extern "C" {

int g_win_width    = 1280;
int g_win_height   = 720;
int g_visual_step  = 0;
int g_selected_track = -1;
/* g_pat_scroll is defined in toolbar.cpp */

}
