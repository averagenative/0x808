/*
 * audio_test.c — Minimal PulseAudio test: play a 440 Hz sine wave.
 * Compile: gcc -o audio_test tests/audio_test.c -lpulse-simple -lpulse -lm
 */
#include <pulse/simple.h>
#include <pulse/error.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    printf("PulseAudio direct test — 440 Hz sine for 2 seconds\n");

    /* Configure stream */
    pa_sample_spec ss = {
        .format   = PA_SAMPLE_FLOAT32LE,
        .rate     = 44100,
        .channels = 2
    };

    pa_buffer_attr attr = {
        .maxlength = (uint32_t)-1,
        .tlength   = 44100 * 2 * sizeof(float) / 10, /* ~100ms target buffer */
        .prebuf    = (uint32_t)-1,
        .minreq    = (uint32_t)-1,
        .fragsize  = (uint32_t)-1
    };

    int error;
    pa_simple *s = pa_simple_new(
        NULL,               /* default server */
        "SequencerC Test",  /* app name */
        PA_STREAM_PLAYBACK,
        NULL,               /* default sink */
        "test tone",        /* stream name */
        &ss,
        NULL,               /* default channel map */
        &attr,
        &error
    );

    if (!s) {
        fprintf(stderr, "pa_simple_new failed: %s\n", pa_strerror(error));
        return 1;
    }

    printf("Connected to PulseAudio!\n");
    printf("Playing 440 Hz sine wave...\n");

    /* Generate and play 2 seconds of 440 Hz sine */
    float buf[1024 * 2]; /* 1024 frames, stereo */
    int total_frames = 44100 * 2; /* 2 seconds */
    int frames_written = 0;

    while (frames_written < total_frames) {
        int chunk = 1024;
        if (frames_written + chunk > total_frames)
            chunk = total_frames - frames_written;

        for (int i = 0; i < chunk; i++) {
            float t = (float)(frames_written + i) / 44100.0f;
            float sample = 0.3f * sinf(2.0f * 3.14159265f * 440.0f * t);
            buf[i * 2]     = sample;
            buf[i * 2 + 1] = sample;
        }

        if (pa_simple_write(s, buf, chunk * 2 * sizeof(float), &error) < 0) {
            fprintf(stderr, "pa_simple_write failed: %s\n", pa_strerror(error));
            break;
        }

        frames_written += chunk;
        printf("\r  %d / %d frames (%.1f%%)", frames_written, total_frames,
               100.0f * frames_written / total_frames);
        fflush(stdout);
    }

    printf("\nDraining...\n");
    pa_simple_drain(s, &error);
    pa_simple_free(s);
    printf("Done!\n");
    return 0;
}
