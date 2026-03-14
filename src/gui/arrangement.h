/*
 * arrangement.h — Song arrangement view.
 *
 * Shows sections as colored blocks in a horizontal timeline.
 * Supports song mode (linear) and perform mode (trigger sections).
 */

#ifndef SQ_ARRANGEMENT_H
#define SQ_ARRANGEMENT_H

#include "engine/engine.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Draw the arrangement panel at the given position. */
void arrangement_draw(sq_engine_t *engine,
                      float x, float y, float w, float h);

#ifdef __cplusplus
}
#endif

#endif /* SQ_ARRANGEMENT_H */
