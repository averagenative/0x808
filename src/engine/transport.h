/*
 * transport.h — BPM clock and playback state management.
 *
 * The transport converts absolute sample position into musical time
 * (beats, steps). It tells the sequencer when to advance to the next step.
 */

#ifndef SQ_TRANSPORT_H
#define SQ_TRANSPORT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "engine/engine.h"

/*
 * Initialize transport to default state: 120 BPM, stopped, pattern mode.
 */
void transport_init(sq_transport_t *transport);

/*
 * Advance the transport by num_frames samples.
 * Updates current_beat, sample_position, and current_step.
 *
 * Returns: the step number if we crossed into a new step this frame,
 *          or -1 if we're still on the same step.
 */
int transport_advance(sq_transport_t *transport, uint32_t sample_rate,
                      uint32_t num_frames, uint32_t pattern_length);

#ifdef __cplusplus
}
#endif

#endif /* SQ_TRANSPORT_H */
