/*
 * audio_test2.c — Test different sample rates and buffer configs.
 * gcc -o audio_test2 tests/audio_test2.c -lpulse-simple -lpulse -lm
 */
#include <pulse/simple.h>
#include <pulse/error.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

static int test_rate(int rate, int duration_sec)
{
    printf("\n=== Testing %d Hz, %d second(s) ===\n", rate, duration_sec);

    pa_sample_spec ss = {
        .format   = PA_SAMPLE_FLOAT32LE,
        .rate     = rate,
        .channels = 2
    };

    /* Use very small buffer attributes */
    pa_buffer_attr attr = {
        .maxlength = (uint32_t)-1,
        .tlength   = rate * 2 * sizeof(float) / 20, /* ~50ms */
        .prebuf    = 0,                              /* start immediately */
        .minreq    = (uint32_t)-1,
        .fragsize  = (uint32_t)-1
    };

    int error;
    pa_simple *s = pa_simple_new(NULL, "Test", PA_STREAM_PLAYBACK,
                                  NULL, "test", &ss, NULL, &attr, &error);
    if (!s) {
        printf("  FAILED: %s\n", pa_strerror(error));
        return -1;
    }

    int total_frames = rate * duration_sec;
    int frames_written = 0;
    float buf[256 * 2];
    double start = now_ms();

    while (frames_written < total_frames) {
        int chunk = 256;
        if (frames_written + chunk > total_frames)
            chunk = total_frames - frames_written;

        for (int i = 0; i < chunk; i++) {
            float t = (float)(frames_written + i) / (float)rate;
            float sample = 0.3f * sinf(2.0f * 3.14159265f * 440.0f * t);
            buf[i * 2] = buf[i * 2 + 1] = sample;
        }

        if (pa_simple_write(s, buf, chunk * 2 * sizeof(float), &error) < 0) {
            printf("  Write error: %s\n", pa_strerror(error));
            break;
        }
        frames_written += chunk;

        /* Timeout after 10 seconds real time */
        if (now_ms() - start > 10000.0) {
            printf("  TIMEOUT after 10s real time\n");
            break;
        }
    }

    double elapsed = (now_ms() - start) / 1000.0;
    printf("  Wrote %d/%d frames in %.1f sec (%.0f frames/sec, expected %d)\n",
           frames_written, total_frames, elapsed,
           frames_written / elapsed, rate);

    pa_simple_free(s);
    return frames_written;
}

int main(void)
{
    printf("PulseAudio throughput test\n");
    test_rate(8000,  1);
    test_rate(22050, 1);
    test_rate(44100, 1);
    printf("\nDone.\n");
    return 0;
}
