/*
 * sampler.h — Sample playback engine with polyphonic voice management.
 *
 * The sampler triggers voices (instances of sample playback) and renders
 * them into a float buffer. Supports pitch shifting via resampling with
 * Hermite interpolation.
 */

#ifndef SQ_SAMPLER_H
#define SQ_SAMPLER_H

#include "engine/engine.h"

/*
 * Trigger a new voice to play a sample.
 *
 * Finds a free voice (or steals the oldest if all are busy).
 * Sets up playback position, rate, velocity, and track volume/pan.
 *
 * sample_index: index into engine->samples[]
 * velocity:     0.0 to 1.0 (from step velocity / 127.0)
 * pitch_offset: semitones to shift (-24 to +24)
 * volume:       track volume (0.0 to 1.0)
 * pan:          track pan (-1.0 to 1.0)
 */
void sampler_trigger(sq_engine_t *engine, int sample_index,
                     float velocity, int pitch_offset,
                     float volume, float pan);

/*
 * Render all active voices into the output buffer (additive).
 * Does NOT clear the buffer first — adds to whatever is already there.
 *
 * output:     interleaved stereo float buffer (L,R,L,R,...)
 * num_frames: how many stereo frames to render
 */
void sampler_render(sq_engine_t *engine, float *output, uint32_t num_frames);

#endif /* SQ_SAMPLER_H */
