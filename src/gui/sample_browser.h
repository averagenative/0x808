/*
 * sample_browser.h — File browser for loading audio samples.
 *
 * Shows a directory tree filtered to WAV/MP3/FLAC files.
 * Click to select, double-click or button to load into engine.
 */

#ifndef SQ_SAMPLE_BROWSER_H
#define SQ_SAMPLE_BROWSER_H

#include "engine/engine.h"

struct nk_context;

/* Draw the sample browser panel.
 * If a sample is loaded, returns the sample index (>= 0).
 * Otherwise returns -1. */
int sample_browser_draw(struct nk_context *ctx, sq_engine_t *engine,
                        float x, float y, float w, float h);

/* Returns true if the user clicked the close button in the browser.
 * Resets the flag after reading (one-shot). */
bool sample_browser_close_requested(void);

#endif /* SQ_SAMPLE_BROWSER_H */
