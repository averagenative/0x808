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

/* ─── Configurable randomize (TASK-227) ─────────────────────────────────── */

typedef enum {
    SQ_RND_STYLE_ANY = -1,        /* pick a style at random per call */
    SQ_RND_STYLE_FOUR_ON_FLOOR = 0,
    SQ_RND_STYLE_BOOM_BAP,
    SQ_RND_STYLE_TRAP,
    SQ_RND_STYLE_BREAKBEAT,
    SQ_RND_STYLE_COUNT
} sq_rnd_style_t;

typedef struct {
    /* What to roll. Each independent — leave the rest of the pattern
     * untouched.  Default profile is (steps=true, others=false). */
    bool steps;       /* re-roll which sampler steps are on/off */
    bool velocity;    /* re-roll velocities on EXISTING hits only */
    bool micro;       /* per-step micro-timing offsets (humanize feel) */
    bool pitch;       /* per-step pitch_offset (±2 semitones) */
    bool notes;       /* synth-track piano-roll notes within key/octave */
    bool kit;         /* pick a random kit from sq_kits[] (engine reload) */

    int  style;       /* sq_rnd_style_t — bias the step distribution */
    uint32_t seed;    /* 0 = time-based */
} sq_random_options_t;

/* Default option set — only "steps" enabled, style picked at random. */
void sq_random_options_default(sq_random_options_t *opts);

/* Apply a configurable randomize. Wraps the legacy steps-only path
 * when only opts->steps is true so the existing test still passes. */
void sq_pattern_randomize_opts(sq_pattern_t *pat,
                                const sq_random_options_t *opts);

/* Pick a random kit index distinct from the current one, in [0, num_kits).
 * Returns the chosen index. Caller is responsible for sq_kit_load. */
int sq_random_pick_kit(int current_kit, int num_kits);

#ifdef __cplusplus
}
#endif

#endif
