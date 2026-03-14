/*
 * sample_browser.h — File browser for loading audio samples.
 *
 * Shows a directory tree filtered to WAV/MP3/FLAC files.
 * Click to select, double-click or button to load into engine.
 */

#ifndef SQ_SAMPLE_BROWSER_H
#define SQ_SAMPLE_BROWSER_H

#include "engine/engine.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Draw the sample browser panel at the given position/size. */
void sample_browser_draw(sq_engine_t *engine,
                         float x, float y, float w, float h);

/* Returns 1 if the user clicked the close button in the browser.
 * Resets the flag after reading (one-shot). */
int sample_browser_close_requested(void);

#ifdef __cplusplus
}
#endif

#endif /* SQ_SAMPLE_BROWSER_H */
