/*
 * mixer.h — Mix all active voices into a stereo output buffer.
 *
 * The mixer's job is simple: clear the output buffer, tell the sampler
 * to render voices into it (additive), then apply master volume.
 */

#ifndef SQ_MIXER_H
#define SQ_MIXER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "engine/engine.h"

/*
 * Mix all audio sources into the output buffer.
 *
 * Steps:
 * 1. Clear the output buffer to silence
 * 2. Call sampler_render() to add all active voices
 * 3. Apply master volume
 *
 * output:     interleaved stereo float buffer (L,R,L,R,...)
 * num_frames: how many stereo frames to produce
 */
void mixer_process(sq_engine_t *engine, float *output, uint32_t num_frames);

#ifdef __cplusplus
}
#endif

#endif /* SQ_MIXER_H */
