/*
 * sample_io.c — Load WAV/MP3/FLAC files into float buffers.
 *
 * Each dr_libs decoder is a single-header library. We #define the
 * IMPLEMENTATION macro exactly once here to generate the actual code.
 * Every other file that includes these headers gets just the declarations.
 */

#define DR_WAV_IMPLEMENTATION
#include "dr_wav.h"

#define DR_MP3_IMPLEMENTATION
#include "dr_mp3.h"

#define DR_FLAC_IMPLEMENTATION
#include "dr_flac.h"

#include "formats/sample_io.h"
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

/* ─── Helper: detect format from file extension ───────────────────────────── */

typedef enum {
    FORMAT_WAV,
    FORMAT_MP3,
    FORMAT_FLAC,
    FORMAT_UNKNOWN
} audio_format_t;

static audio_format_t detect_format(const char *filepath)
{
    const char *dot = strrchr(filepath, '.');
    if (!dot) return FORMAT_UNKNOWN;

    if (strcmp(dot, ".wav") == 0 || strcmp(dot, ".WAV") == 0)
        return FORMAT_WAV;
    if (strcmp(dot, ".mp3") == 0 || strcmp(dot, ".MP3") == 0)
        return FORMAT_MP3;
    if (strcmp(dot, ".flac") == 0 || strcmp(dot, ".FLAC") == 0)
        return FORMAT_FLAC;

    return FORMAT_UNKNOWN;
}

/* ─── Helper: extract just the filename from a path ───────────────────────── */

static const char *basename_from_path(const char *filepath)
{
    const char *slash = strrchr(filepath, '/');
    if (!slash) slash = strrchr(filepath, '\\');
    return slash ? slash + 1 : filepath;
}

/* ─── Public API ──────────────────────────────────────────────────────────── */

int sample_io_load(const char *filepath, sq_sample_t *sample)
{
    if (!filepath || !sample) return -1;

    /* Zero out the sample struct */
    memset(sample, 0, sizeof(sq_sample_t));

    audio_format_t fmt = detect_format(filepath);

    unsigned int channels = 0;
    unsigned int sample_rate = 0;
    drwav_uint64 total_frames = 0;
    float *decoded = NULL;

    switch (fmt) {
    case FORMAT_WAV: {
        decoded = drwav_open_file_and_read_pcm_frames_f32(
            filepath, &channels, &sample_rate, &total_frames, NULL);
        break;
    }
    case FORMAT_MP3: {
        drmp3_config config;
        drmp3_uint64 mp3_frames;
        decoded = drmp3_open_file_and_read_pcm_frames_f32(
            filepath, &config, &mp3_frames, NULL);
        if (decoded) {
            channels = config.channels;
            sample_rate = config.sampleRate;
            total_frames = mp3_frames;
        }
        break;
    }
    case FORMAT_FLAC: {
        decoded = drflac_open_file_and_read_pcm_frames_f32(
            filepath, &channels, &sample_rate, &total_frames, NULL);
        break;
    }
    case FORMAT_UNKNOWN:
        return -1;
    }

    if (!decoded) return -1;

    /* Reject files too large for uint32_t frame count */
    if (total_frames > UINT32_MAX) {
        free(decoded);
        return -1;
    }

    /* Fill in the sample struct */
    sample->data = decoded;
    sample->num_frames = (uint32_t)total_frames;
    sample->num_channels = channels;
    sample->sample_rate = sample_rate;

    /* Set the name from the filename */
    const char *name = basename_from_path(filepath);
    strncpy(sample->name, name, SQ_SAMPLE_NAME_LEN - 1);
    sample->name[SQ_SAMPLE_NAME_LEN - 1] = '\0';

    /* Store full path for project save/reload */
    strncpy(sample->filepath, filepath, sizeof(sample->filepath) - 1);
    sample->filepath[sizeof(sample->filepath) - 1] = '\0';

    return 0;
}

void sample_io_free(sq_sample_t *sample)
{
    if (sample && sample->data) {
        drwav_free(sample->data, NULL);
        sample->data = NULL;
        sample->num_frames = 0;
    }
}
