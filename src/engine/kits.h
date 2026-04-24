/*
 * kits.h -- Drum kit definitions and switching.
 *
 * Each kit maps 8 drum slots (kick, snare, closed hihat, clap/rimshot,
 * open hihat, cowbell/perc, tom/rimshot, hi tom/extra) to sample files
 * relative to the samples/ directory.
 *
 * Kit switching re-loads samples from disk and re-assigns sample_index
 * on all sampler tracks in the current pattern.
 */

#ifndef SQ_KITS_H
#define SQ_KITS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "engine/engine.h"

#define SQ_KIT_SLOTS     8   /* drum slots per kit */
#define SQ_NUM_KITS      16  /* number of built-in kits */
#define SQ_KIT_NAME_LEN 16   /* max kit name length */
#define SQ_KIT_PATH_LEN 64   /* max sample path length */

typedef struct {
    char name[SQ_KIT_NAME_LEN];
    char paths[SQ_KIT_SLOTS][SQ_KIT_PATH_LEN];
    int  num_slots;  /* how many slots are actually populated */
} sq_kit_def_t;

/* Array of built-in kit definitions */
extern const sq_kit_def_t sq_kits[SQ_NUM_KITS];

/*
 * Load a kit's samples into the engine, replacing existing sampler samples.
 * Updates all sampler tracks in the current pattern to point to the new
 * sample indices.
 *
 * engine:   the engine to modify
 * kit_index: index into sq_kits[] (0 = 808, 1 = 909, etc.)
 * base_dir:  base directory for resolving sample paths (e.g. exe dir or CWD)
 *
 * Returns 0 on success, -1 if kit_index is out of range.
 */
int sq_kit_load(sq_engine_t *engine, int kit_index, const char *base_dir);

/* Get current kit index (returns -1 if unknown/custom) */
extern int sq_current_kit;

#ifdef __cplusplus
}
#endif

#endif /* SQ_KITS_H */
