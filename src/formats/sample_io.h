/*
 * sample_io.h — Load audio files (WAV, MP3, FLAC) into sq_sample_t.
 *
 * Uses dr_libs (dr_wav, dr_mp3, dr_flac) under the hood.
 * All decoding produces interleaved float buffers.
 */

#ifndef SQ_SAMPLE_IO_H
#define SQ_SAMPLE_IO_H

#include "engine/engine.h"

/*
 * Load an audio file from disk into a sample struct.
 * Supports: .wav, .mp3, .flac (detected by file extension).
 *
 * Returns 0 on success, -1 on failure.
 * On success, sample->data is heap-allocated and must be freed with
 * sample_io_free().
 */
int sample_io_load(const char *filepath, sq_sample_t *sample);

/*
 * Free the audio data inside a sample (the float buffer).
 * Does not free the sq_sample_t struct itself.
 */
void sample_io_free(sq_sample_t *sample);

#endif /* SQ_SAMPLE_IO_H */
