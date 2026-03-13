/*
 * virtual_keyboard.h — Clickable piano keyboard for live synth playing.
 */

#ifndef SQ_VIRTUAL_KEYBOARD_H
#define SQ_VIRTUAL_KEYBOARD_H

#include "engine/engine.h"

struct nk_context;

/*
 * Draw a virtual piano keyboard.
 * Clicking keys triggers synth_trigger(); releasing sends note-off.
 * synth_preset: which preset to play (-1 = auto-detect from selected track)
 */
void virtual_keyboard_draw(struct nk_context *ctx, sq_engine_t *engine,
                           int synth_preset,
                           float x, float y, float w, float h);

#endif /* SQ_VIRTUAL_KEYBOARD_H */
