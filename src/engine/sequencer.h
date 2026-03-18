/*
 * sequencer.h — Pattern playback: walk tracks, trigger voices on active steps.
 */

#ifndef SQ_SEQUENCER_H
#define SQ_SEQUENCER_H

#ifdef __cplusplus
extern "C" {
#endif

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

/*
 * Randomize a track's step pattern.
 * density: 0.0 (empty) to 1.0 (every step active).
 * Velocity is randomized between 60-127.
 */
void sequencer_randomize_track(sq_engine_t *engine, int track, float density);

/*
 * Fill a track with a Euclidean rhythm (Bjorklund algorithm).
 * pulses: number of hits to distribute.
 * steps: number of steps in the pattern.
 * rotation: rotate the pattern by N steps.
 * velocity: velocity for active steps.
 */
void sequencer_euclidean_fill(sq_engine_t *engine, int track,
                               int pulses, int steps, int rotation,
                               uint8_t velocity);

/*
 * Groove template: per-step timing offset + velocity scale.
 * Apply to a pattern to give it a specific rhythmic feel.
 */
typedef struct {
    const char *name;
    float timing[16];   /* per-step timing offset in steps (-0.5 to +0.5) */
    float velocity[16]; /* per-step velocity multiplier (0.5 to 1.5) */
} sq_groove_template_t;

/* Number of built-in groove templates */
#define SQ_NUM_GROOVE_TEMPLATES 4

/* Get built-in groove template by index. */
const sq_groove_template_t *sequencer_get_groove(int index);

/* Apply a groove template to a pattern (sets micro_offset and scales velocity). */
void sequencer_apply_groove(sq_engine_t *engine, int groove_index);

/*
 * Slice a sample by detecting transients and map slices to steps.
 * Each slice becomes a step with sample_start/end set on the track.
 * Returns number of slices detected.
 */
int sequencer_slice_sample(sq_engine_t *engine, int track, int sample_index,
                            float threshold);

#ifdef __cplusplus
}
#endif

#endif /* SQ_SEQUENCER_H */
