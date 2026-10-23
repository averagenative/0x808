/*
 * sample_io.h — Load audio files (WAV, MP3, FLAC) into sq_sample_t.
 *
 * Uses dr_libs (dr_wav, dr_mp3, dr_flac) under the hood.
 * All decoding produces interleaved float buffers.
 */

#ifndef SQ_SAMPLE_IO_H
#define SQ_SAMPLE_IO_H

#ifdef __cplusplus
extern "C" {
#endif

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
 * Built-in files: a host layer (the plugin, which carries its kits inside
 * its binary) can serve WAV files that sample_io_load() finds by path before
 * it looks on disk. find returns the file's bytes and size and sets
 * *canonical_path to the path to store in the sample, or returns NULL to
 * load from disk. Process-wide; NULL turns it off.
 */
typedef const void *(*sample_io_builtin_fn)(const char *path, size_t *size,
                                           const char **canonical_path);
void sample_io_set_builtin(sample_io_builtin_fn find);

/*
 * Free the audio data inside a sample (the float buffer).
 * Does not free the sq_sample_t struct itself.
 */
void sample_io_free(sq_sample_t *sample);

#ifdef __cplusplus
}
#endif

#endif /* SQ_SAMPLE_IO_H */
