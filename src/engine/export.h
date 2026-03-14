/*
 * export.h — Offline audio rendering for WAV/MP3 export.
 *
 * Runs sq_engine_process() in a tight loop to render audio faster
 * than real-time, producing a float buffer suitable for writing to disk.
 */

#ifndef SQ_EXPORT_H
#define SQ_EXPORT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "engine/engine.h"
#include <stdint.h>

/* Export configuration */
typedef struct {
    uint32_t sample_rate;       /* output sample rate (44100 or 48000) */
    uint32_t bit_depth;         /* 16 or 24 (for WAV), ignored for float */
    int      num_bars;          /* how many bars to render (0 = auto from arrangement) */
    int      pattern_index;     /* which pattern to render (-1 = use arrangement) */
} sq_export_config_t;

/* Result of an export operation */
typedef struct {
    float   *data;              /* interleaved stereo float buffer (caller must free) */
    uint32_t num_frames;        /* total frames rendered */
    uint32_t sample_rate;       /* sample rate used */
    float    peak_level;        /* peak absolute sample value */
} sq_export_result_t;

/*
 * Render the engine offline into a float buffer.
 * The engine's transport is started from the beginning and run until
 * the configured number of bars are complete.
 *
 * Returns 0 on success, -1 on failure (malloc fail, etc.).
 * On success, result->data must be freed by the caller.
 */
int sq_export_render(sq_engine_t *engine, const sq_export_config_t *config,
                     sq_export_result_t *result);

/*
 * Write a rendered result to a WAV file.
 * bit_depth: 16, 24, or 32 (32 = float).
 * Returns 0 on success, -1 on failure.
 */
int sq_export_write_wav(const char *filepath, const sq_export_result_t *result,
                        int bit_depth);

/*
 * Write a rendered result to an MP3 file.
 * Uses the shine fixed-point encoder (cross-platform, no external deps).
 * bitrate: 128, 192, 256, or 320 kbps.
 * Returns 0 on success, -1 on failure.
 */
int sq_export_write_mp3(const char *filepath, const sq_export_result_t *result,
                        int bitrate);

#ifdef __cplusplus
}
#endif

#endif /* SQ_EXPORT_H */
