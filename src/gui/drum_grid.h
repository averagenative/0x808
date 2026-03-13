/*
 * drum_grid.h — Visual step grid for drum/sample tracks.
 *
 * Draws a grid where rows = tracks and columns = steps.
 * Click to toggle steps. Right-click to edit velocity/pitch.
 * Current playback step is highlighted.
 */

#ifndef SQ_DRUM_GRID_H
#define SQ_DRUM_GRID_H

#include "engine/engine.h"

/* Forward declare Nuklear context to avoid including the full header */
struct nk_context;

/*
 * Draw the drum grid at the specified position and size.
 * Handles all interaction (clicking cells, adjusting track controls).
 */
void drum_grid_draw(struct nk_context *ctx, sq_engine_t *engine,
                    float x, float y, float w, float h);

#endif /* SQ_DRUM_GRID_H */
