/*
 * sequencer.h — Pattern playback: walk tracks, trigger voices on active steps.
 */

#ifndef SQ_SEQUENCER_H
#define SQ_SEQUENCER_H

#include "engine/engine.h"

/*
 * Called once per audio buffer. Checks if the transport has advanced to
 * a new step, and if so, triggers voices for all active steps in the
 * current pattern.
 *
 * Note: In the current design, transport_advance() is called from
 * engine.c, and sequencer_trigger_step() is called when a new step
 * is reached. sequencer_tick() is kept for future per-buffer work.
 */
void sequencer_tick(sq_engine_t *engine);

/*
 * Trigger voices for all active tracks at the given step number.
 * Called by engine.c when the transport crosses a step boundary.
 */
void sequencer_trigger_step(sq_engine_t *engine, int step);

#endif /* SQ_SEQUENCER_H */
