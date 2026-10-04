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

/* Draw the FX editor contents (browse bar + effect slots) into the current
 * ImGui window, filling the remaining content region. Shows the effect
 * chain for the currently-selected track (or master). The standalone hosts
 * this in its own OS window — see the FX pop-out section in gui.cpp. */
void fx_panel_draw(sq_engine_t *engine);

/* Set the FX tab to show a specific track's effects.
 * track_index: 0-based track index, or -1 for master bus. */
void mixer_view_set_fx_track(int track_index);

#ifdef __cplusplus
}
#endif

#endif /* SQ_MIXER_VIEW_H */
