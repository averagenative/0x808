/*
 * mixer_view.h — Mixer view with per-track effects controls.
 */

#ifndef SQ_MIXER_VIEW_H
#define SQ_MIXER_VIEW_H

#include "engine/engine.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Draw the mixer/effects panel (track strips + VU meters + oscilloscope). */
void mixer_view_draw(sq_engine_t *engine,
                     float x, float y, float w, float h);

/* Draw the FX editor as an independent floating window the user can drag
 * and resize. Shows the effect chain for the currently-selected track
 * (or master). Safe to call every frame — remembers window state.
 * Writes false to *open when the user clicks the window's X close button
 * so the caller can toggle off the panel flag. */
void fx_window_draw(sq_engine_t *engine, float default_x, float default_y,
                    bool *open);

/* Set the FX tab to show a specific track's effects.
 * track_index: 0-based track index, or -1 for master bus. */
void mixer_view_set_fx_track(int track_index);

#ifdef __cplusplus
}
#endif

#endif /* SQ_MIXER_VIEW_H */
