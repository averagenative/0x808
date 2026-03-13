/*
 * generate_samples.c — Generate simple synthesized drum samples as WAV files.
 *
 * Creates basic kick, snare, and hi-hat sounds using simple synthesis:
 * - Kick: sine wave with pitch envelope (high to low)
 * - Snare: noise burst + sine with fast decay
 * - Hi-hat: filtered noise with very fast decay
 * - Clap: layered noise bursts
 *
 * Usage: ./generate_samples
 * Writes to: samples/kicks/kick.wav, samples/snares/snare.wav, etc.
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <stdint.h>

#define SAMPLE_RATE 44100

/* Simple pseudo-random number generator (no need for stdlib rand) */
static uint32_t rng_state = 12345;
static float noise(void)
{
    rng_state = rng_state * 1103515245 + 12345;
    return ((float)(rng_state >> 16) / 32768.0f) - 1.0f;
}

/* Write a mono WAV file */
static void write_wav(const char *path, const float *data, uint32_t num_samples)
{
    FILE *f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "Cannot open %s\n", path); return; }

    uint32_t data_size = num_samples * 2; /* 16-bit = 2 bytes per sample */
    uint32_t file_size = 36 + data_size;

    /* RIFF header */
    fwrite("RIFF", 1, 4, f);
    fwrite(&file_size, 4, 1, f);
    fwrite("WAVE", 1, 4, f);

    /* fmt chunk */
    fwrite("fmt ", 1, 4, f);
    uint32_t fmt_size = 16;
    uint16_t audio_format = 1; /* PCM */
    uint16_t channels = 1;
    uint32_t sample_rate = SAMPLE_RATE;
    uint32_t byte_rate = SAMPLE_RATE * 2;
    uint16_t block_align = 2;
    uint16_t bits_per_sample = 16;
    fwrite(&fmt_size, 4, 1, f);
    fwrite(&audio_format, 2, 1, f);
    fwrite(&channels, 2, 1, f);
    fwrite(&sample_rate, 4, 1, f);
    fwrite(&byte_rate, 4, 1, f);
    fwrite(&block_align, 2, 1, f);
    fwrite(&bits_per_sample, 2, 1, f);

    /* data chunk */
    fwrite("data", 1, 4, f);
    fwrite(&data_size, 4, 1, f);

    /* Convert float to 16-bit and write */
    for (uint32_t i = 0; i < num_samples; i++) {
        float s = data[i];
        if (s > 1.0f) s = 1.0f;
        if (s < -1.0f) s = -1.0f;
        int16_t sample = (int16_t)(s * 32767.0f);
        fwrite(&sample, 2, 1, f);
    }

    fclose(f);
    printf("  Wrote: %s (%u samples, %.2f sec)\n", path, num_samples,
           (float)num_samples / SAMPLE_RATE);
}

/* ─── Drum synthesis ──────────────────────────────────────────────────────── */

static void generate_kick(void)
{
    uint32_t len = SAMPLE_RATE / 3; /* ~333ms */
    float *buf = calloc(len, sizeof(float));
    double phase = 0.0;

    for (uint32_t i = 0; i < len; i++) {
        double t = (double)i / SAMPLE_RATE;

        /* Pitch envelope: starts at 150Hz, drops to 40Hz quickly */
        double freq = 40.0 + 110.0 * exp(-t * 30.0);

        /* Amplitude envelope: fast attack, medium decay */
        double amp = exp(-t * 7.0);

        /* Sine oscillator */
        phase += freq / SAMPLE_RATE;
        buf[i] = (float)(sin(phase * 2.0 * M_PI) * amp * 0.9);
    }

    write_wav("samples/kicks/kick.wav", buf, len);
    free(buf);
}

static void generate_snare(void)
{
    uint32_t len = SAMPLE_RATE / 4; /* ~250ms */
    float *buf = calloc(len, sizeof(float));
    double phase = 0.0;

    for (uint32_t i = 0; i < len; i++) {
        double t = (double)i / SAMPLE_RATE;

        /* Tone component: sine at ~200Hz with fast decay */
        double tone_amp = exp(-t * 20.0);
        phase += 200.0 / SAMPLE_RATE;
        double tone = sin(phase * 2.0 * M_PI) * tone_amp;

        /* Noise component: white noise with medium decay */
        double noise_amp = exp(-t * 12.0);
        double n = noise() * noise_amp;

        buf[i] = (float)((tone * 0.5 + n * 0.5) * 0.8);
    }

    write_wav("samples/snares/snare.wav", buf, len);
    free(buf);
}

static void generate_hihat(void)
{
    uint32_t len = SAMPLE_RATE / 8; /* ~125ms */
    float *buf = calloc(len, sizeof(float));

    for (uint32_t i = 0; i < len; i++) {
        double t = (double)i / SAMPLE_RATE;

        /* High-passed noise with very fast decay */
        double amp = exp(-t * 40.0);
        double n = noise();

        /* Simple highpass: difference of consecutive noise samples */
        static float prev = 0.0f;
        float hp = (float)n - prev;
        prev = (float)n;

        buf[i] = hp * (float)amp * 0.7f;
    }

    write_wav("samples/hihats/hihat.wav", buf, len);
    free(buf);
}

static void generate_clap(void)
{
    uint32_t len = SAMPLE_RATE / 4; /* ~250ms */
    float *buf = calloc(len, sizeof(float));

    for (uint32_t i = 0; i < len; i++) {
        double t = (double)i / SAMPLE_RATE;

        /* Multiple noise bursts layered for "clap" texture */
        double amp = exp(-t * 15.0);

        /* Add short re-triggers at the start for the "clap" attack */
        if (t < 0.02) {
            double burst = sin(t * 400.0 * M_PI);
            amp *= (burst > 0) ? 1.0 : 0.3;
        }

        buf[i] = noise() * (float)amp * 0.6f;
    }

    write_wav("samples/percussion/clap.wav", buf, len);
    free(buf);
}

int main(void)
{
    printf("Generating drum samples...\n");
    generate_kick();
    generate_snare();
    generate_hihat();
    generate_clap();
    printf("Done!\n");
    return 0;
}
