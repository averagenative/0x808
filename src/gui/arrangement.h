/*
 * arrangement.h — Song arrangement view.
 *
 * Shows sections as colored blocks in a horizontal timeline.
 * Supports song mode (linear) and perform mode (trigger sections).
 */

#ifndef SQ_ARRANGEMENT_H
#define SQ_ARRANGEMENT_H

#include "engine/engine.h"

struct nk_context;

/* Draw the arrangement panel at the given position.
 * Returns 1 if a pattern change was triggered. */
int arrangement_draw(struct nk_context *ctx, sq_engine_t *engine,
                     float x, float y, float w, float h);

#endif /* SQ_ARRANGEMENT_H */
