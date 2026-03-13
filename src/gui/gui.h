/*
 * gui.h — GUI initialization, main loop, and top-level layout.
 *
 * Uses Nuklear (immediate-mode GUI) with SDL2 + OpenGL 3.3 backend.
 * The GUI runs on the main thread. Audio runs on miniaudio's thread.
 */

#ifndef SQ_GUI_H
#define SQ_GUI_H

#include "engine/engine.h"

/*
 * Initialize the GUI: create SDL2 window, OpenGL context, Nuklear.
 * Returns 0 on success, -1 on failure.
 */
int gui_init(int width, int height, const char *title);

/*
 * Run one frame of the GUI: poll events, draw widgets, swap buffers.
 * Returns 0 to continue, 1 if the user wants to quit.
 */
int gui_frame(sq_engine_t *engine);

/*
 * Shut down the GUI: destroy Nuklear, OpenGL context, SDL2 window.
 */
void gui_shutdown(void);

/* Current window dimensions (updated each frame, read by drum_grid popup) */
extern int g_win_width;
extern int g_win_height;

/* Visual playhead step — driven by wall-clock time on the GUI thread,
 * so it stays in sync with what the user sees regardless of audio latency. */
extern int g_visual_step;

/* Currently selected track index (-1 = none).
 * When a synth track is selected, the synth editor panel is shown. */
extern int g_selected_track;

#endif /* SQ_GUI_H */
