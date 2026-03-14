/*
 * mixer_view.h — Mixer view with per-track effects controls.
 */

#ifndef SQ_MIXER_VIEW_H
#define SQ_MIXER_VIEW_H

#include "engine/engine.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Draw the mixer/effects panel. */
void mixer_view_draw(sq_engine_t *engine,
                     float x, float y, float w, float h);

#ifdef __cplusplus
}
#endif

#endif /* SQ_MIXER_VIEW_H */
