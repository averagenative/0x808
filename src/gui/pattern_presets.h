/*
 * pattern_presets.h — Drum pattern and bass line preset library.
 *
 * Provides a collection of industry-standard drum patterns and bass line
 * generators that can be applied to the current pattern with one click.
 */

#ifndef SQ_PATTERN_PRESETS_H
#define SQ_PATTERN_PRESETS_H

#include "engine/engine.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Draw the preset selector dropdown and apply buttons.
 * Call from the main GUI toolbar area.
 */
void pattern_presets_draw(sq_engine_t *engine);

/* Return pointer to the visibility flag (used by gui.c for keyboard toggle) */
int *pattern_presets_visible_ptr(void);

#ifdef __cplusplus
}
#endif

#endif /* SQ_PATTERN_PRESETS_H */
