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

/*
 * Handle QWERTY keyboard input for piano playing.
 * Call on SDL_KEYDOWN/SDL_KEYUP events.
 * Returns 1 if the key was consumed (mapped to a piano note), 0 otherwise.
 */
int virtual_keyboard_key_event(sq_engine_t *engine, int synth_preset,
                               int sdl_keycode, int pressed);

#endif /* SQ_VIRTUAL_KEYBOARD_H */
