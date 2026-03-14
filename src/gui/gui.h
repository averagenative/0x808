/*
 * gui.h — GUI initialization, main loop, and top-level layout.
 *
 * Uses Dear ImGui (immediate-mode GUI) with SDL2 + OpenGL 3.3 backend.
 * The GUI runs on the main thread. Audio runs on its own thread.
 */

#ifndef SQ_GUI_H
#define SQ_GUI_H

#include "engine/engine.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Initialize the GUI: create SDL2 window, OpenGL context, Dear ImGui.
 * Returns 0 on success, -1 on failure.
 */
int gui_init(int width, int height, const char *title);

/*
 * Run one frame of the GUI: poll events, draw widgets, swap buffers.
 * Returns 0 to continue, 1 if the user wants to quit.
 */
int gui_frame(sq_engine_t *engine);

/*
 * Shut down the GUI: destroy ImGui, OpenGL context, SDL2 window.
 */
void gui_shutdown(void);

/* Current window dimensions (updated each frame) */
extern int g_win_width;
extern int g_win_height;

/* Visual playhead step — driven by wall-clock time on the GUI thread */
extern int g_visual_step;

/* Currently selected track index (-1 = none) */
extern int g_selected_track;

#ifdef __cplusplus
}
#endif

#endif /* SQ_GUI_H */
