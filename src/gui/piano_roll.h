/*
 * piano_roll.h — Melodic note editor (pitch x time grid).
 *
 * Shows MIDI notes as horizontal bars on a piano-key vertical axis.
 * Used for synth tracks instead of the drum grid view.
 */

#ifndef SQ_PIANO_ROLL_H
#define SQ_PIANO_ROLL_H

#include "engine/engine.h"

/*
 * Draw the piano roll for a specific track.
 * x,y,w,h define the drawing area.
 */
void piano_roll_draw(sq_engine_t *engine,
                     int track_index,
                     float x, float y, float w, float h);

#endif /* SQ_PIANO_ROLL_H */
