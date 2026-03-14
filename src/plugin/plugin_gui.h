/*
 * plugin_gui.h — Embedded GUI for the DAW plugin.
 *
 * Creates an SDL2+OpenGL surface inside the host's native window.
 * Called from the CPLUG GUI callbacks in plugin.c.
 */

#ifndef SQ_PLUGIN_GUI_H
#define SQ_PLUGIN_GUI_H

#include "engine/engine.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct sq_plugin_gui sq_plugin_gui_t;

/* Create a new plugin GUI instance (no window yet). */
sq_plugin_gui_t *plugin_gui_create(sq_engine_t *engine);

/* Destroy the plugin GUI instance, stopping render thread if running. */
void plugin_gui_destroy(sq_plugin_gui_t *gui);

/* Attach to a host-provided native window handle and start rendering.
 * native_handle is HWND (Windows), NSView* (macOS), or X11 Window (Linux).
 * Returns 0 on success, -1 on failure. */
int plugin_gui_attach(sq_plugin_gui_t *gui, void *native_handle);

/* Detach from the host window and stop rendering. */
void plugin_gui_detach(sq_plugin_gui_t *gui);

/* Set/get the GUI size. */
void plugin_gui_set_size(sq_plugin_gui_t *gui, uint32_t width, uint32_t height);
void plugin_gui_get_size(sq_plugin_gui_t *gui, uint32_t *width, uint32_t *height);

/* Set the scale factor (for HiDPI). */
void plugin_gui_set_scale(sq_plugin_gui_t *gui, float scale);

#ifdef __cplusplus
}
#endif

#endif /* SQ_PLUGIN_GUI_H */
