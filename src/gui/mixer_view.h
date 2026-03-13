/*
 * mixer_view.h — Mixer view with per-track effects controls.
 */

#ifndef SQ_MIXER_VIEW_H
#define SQ_MIXER_VIEW_H

#include "engine/engine.h"

struct nk_context;

/* Draw the mixer/effects panel. */
void mixer_view_draw(struct nk_context *ctx, sq_engine_t *engine,
                     float x, float y, float w, float h);

#endif /* SQ_MIXER_VIEW_H */
