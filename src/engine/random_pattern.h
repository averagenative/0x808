/*
 * random_pattern.h — Generate musically-plausible random drum patterns.
 *
 * Pure C99. No GUI deps. Heuristic step probabilities + velocity ranges
 * by track role (kick, snare, hat, percussion). Clears existing step
 * data on the touched tracks; preserves track types, lengths, and
 * sample/synth assignments.
 */

#ifndef SQ_RANDOM_PATTERN_H
#define SQ_RANDOM_PATTERN_H

#include "engine/engine.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Fill a pattern with random drum steps using built-in heuristics.
 * Operates on TRACK_SAMPLER tracks only (synth tracks are left alone).
 *
 * pat:   the pattern to overwrite
 * seed:  RNG seed (use 0 to seed from time(NULL))
 */
void sq_pattern_randomize(sq_pattern_t *pat, uint32_t seed);

#ifdef __cplusplus
}
#endif

#endif
