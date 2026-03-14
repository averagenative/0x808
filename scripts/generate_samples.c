/*
 * generate_samples.c — Synthetic drum sample generator for 0x808
 *
 * Generates 808-style kicks, claps, snaps, toms, and percussion samples
 * as mono 16-bit 44100Hz WAV files using dr_wav.
 *
 * Build:  gcc -O2 -o build/generate_samples scripts/generate_samples.c -I deps -lm
 * Run:    ./build/generate_samples
 */

#define DR_WAV_IMPLEMENTATION
#include "dr_wav.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#define SAMPLE_RATE 44100
#define PI 3.14159265358979323846

/* ── helpers ───────────────────────────────────────────────────────── */

static void ensure_dir(const char *path) {
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    mkdir(tmp, 0755);
}

static void ensure_parent_dir(const char *filepath) {
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", filepath);
    char *last_slash = strrchr(tmp, '/');
    if (last_slash) {
        *last_slash = '\0';
        ensure_dir(tmp);
    }
}

/* Random float in [-1, 1] */
static float noise(void) {
    return ((float)rand() / (float)RAND_MAX) * 2.0f - 1.0f;
}

/* One-pole lowpass filter state */
typedef struct { float y; } LP1;

static float lp1_tick(LP1 *f, float x, float coeff) {
    f->y += coeff * (x - f->y);
    return f->y;
}

/* One-pole highpass: y[n] = x[n] - lp(x[n]) */
typedef struct { float lp_y; } HP1;

static float hp1_tick(HP1 *f, float x, float coeff) {
    f->lp_y += coeff * (x - f->lp_y);
    return x - f->lp_y;
}

/* Simple bandpass = HP then LP */
typedef struct { HP1 hp; LP1 lp; } BP;

static float bp_tick(BP *f, float x, float hp_coeff, float lp_coeff) {
    float h = hp1_tick(&f->hp, x, hp_coeff);
    return lp1_tick(&f->lp, h, lp_coeff);
}

/* Convert cutoff freq to one-pole coefficient */
static float freq_to_coeff(float freq_hz) {
    float c = 2.0f * (float)PI * freq_hz / (float)SAMPLE_RATE;
    if (c > 1.0f) c = 1.0f;
    return c;
}

/* Write a mono 16-bit WAV from float buffer */
static int write_wav(const char *path, const float *buf, int num_samples) {
    ensure_parent_dir(path);

    drwav wav;
    drwav_data_format fmt;
    fmt.container     = drwav_container_riff;
    fmt.format        = DR_WAVE_FORMAT_PCM;
    fmt.channels      = 1;
    fmt.sampleRate    = SAMPLE_RATE;
    fmt.bitsPerSample = 16;

    if (!drwav_init_file_write(&wav, path, &fmt, NULL)) {
        fprintf(stderr, "ERROR: cannot open %s for writing\n", path);
        return -1;
    }

    /* Convert float -> int16 */
    drwav_int16 *i16 = (drwav_int16 *)malloc(num_samples * sizeof(drwav_int16));
    for (int i = 0; i < num_samples; i++) {
        float s = buf[i];
        if (s >  1.0f) s =  1.0f;
        if (s < -1.0f) s = -1.0f;
        i16[i] = (drwav_int16)(s * 32767.0f);
    }

    drwav_write_pcm_frames(&wav, num_samples, i16);
    drwav_uninit(&wav);
    free(i16);

    printf("  wrote %s (%d samples, %.3f s)\n", path, num_samples,
           (float)num_samples / SAMPLE_RATE);
    return 0;
}

/* ── sample generators ─────────────────────────────────────────────── */

/*
 * 808 sub kick — sine with pitch envelope + click transient
 *   start_hz:   initial pitch
 *   end_hz:     final pitch
 *   sweep_ms:   pitch sweep duration
 *   decay_ms:   amplitude decay time
 *   duration_s: total duration
 */
static void gen_808_kick(const char *path,
                         float start_hz, float end_hz,
                         float sweep_ms, float decay_ms,
                         float duration_s)
{
    int n = (int)(duration_s * SAMPLE_RATE);
    float *buf = (float *)calloc(n, sizeof(float));
    float phase = 0.0f;
    float sweep_samples = sweep_ms * SAMPLE_RATE / 1000.0f;
    float decay_samples = decay_ms * SAMPLE_RATE / 1000.0f;
    float click_samples = 2.0f * SAMPLE_RATE / 1000.0f; /* 2ms click */

    for (int i = 0; i < n; i++) {
        float t = (float)i;

        /* pitch envelope: exponential sweep */
        float pitch_t = t / sweep_samples;
        if (pitch_t > 1.0f) pitch_t = 1.0f;
        float freq = start_hz + (end_hz - start_hz) * pitch_t * pitch_t;

        /* sine oscillator */
        phase += 2.0f * (float)PI * freq / SAMPLE_RATE;
        float sample = sinf(phase) * 0.9f;

        /* click transient */
        if (t < click_samples) {
            float click_env = 1.0f - t / click_samples;
            sample += noise() * click_env * 0.6f;
        }

        /* amplitude envelope: exponential decay */
        float amp = expf(-t / decay_samples * 5.0f);
        buf[i] = sample * amp;
    }

    write_wav(path, buf, n);
    free(buf);
}

/*
 * 808 clap — multiple noise bursts + reverb tail
 */
static void gen_808_clap(const char *path) {
    float duration_s = 0.4f;
    int n = (int)(duration_s * SAMPLE_RATE);
    float *buf = (float *)calloc(n, sizeof(float));
    BP bp = {0};

    float hp_coeff = freq_to_coeff(1000.0f);
    float lp_coeff = freq_to_coeff(3000.0f);

    /* Generate 4 noise bursts */
    int burst_count = 4;
    float burst_len_ms = 8.0f;
    float burst_gap_ms = 15.0f;

    for (int b = 0; b < burst_count; b++) {
        int start = (int)((b * burst_gap_ms) * SAMPLE_RATE / 1000.0f);
        int len   = (int)(burst_len_ms * SAMPLE_RATE / 1000.0f);
        for (int i = start; i < start + len && i < n; i++) {
            float env = 1.0f - (float)(i - start) / (float)len;
            buf[i] += noise() * env * 0.8f;
        }
    }

    /* Reverb tail: filtered noise decay starting after bursts */
    int tail_start = (int)((burst_count * burst_gap_ms + burst_len_ms) * SAMPLE_RATE / 1000.0f);
    float tail_decay = 200.0f * SAMPLE_RATE / 1000.0f;
    for (int i = tail_start; i < n; i++) {
        float env = expf(-(float)(i - tail_start) / tail_decay * 5.0f);
        buf[i] += noise() * env * 0.4f;
    }

    /* Apply bandpass filter to entire buffer */
    float *filtered = (float *)calloc(n, sizeof(float));
    for (int i = 0; i < n; i++) {
        filtered[i] = bp_tick(&bp, buf[i], hp_coeff, lp_coeff);
    }

    write_wav(path, filtered, n);
    free(buf);
    free(filtered);
}

/*
 * 808 snap — short bandpassed noise burst
 */
static void gen_808_snap(const char *path) {
    float duration_s = 0.2f;
    int n = (int)(duration_s * SAMPLE_RATE);
    float *buf = (float *)calloc(n, sizeof(float));
    BP bp = {0};

    float hp_coeff = freq_to_coeff(2000.0f);
    float lp_coeff = freq_to_coeff(5000.0f);

    float burst_ms = 3.0f;
    float decay_ms = 100.0f;
    int burst_samples = (int)(burst_ms * SAMPLE_RATE / 1000.0f);
    float decay_samples = decay_ms * SAMPLE_RATE / 1000.0f;

    for (int i = 0; i < n; i++) {
        float raw = noise();
        float env;
        if (i < burst_samples) {
            env = 1.0f;
        } else {
            env = expf(-(float)(i - burst_samples) / decay_samples * 5.0f);
        }
        buf[i] = bp_tick(&bp, raw * env * 0.9f, hp_coeff, lp_coeff);
    }

    write_wav(path, buf, n);
    free(buf);
}

/*
 * Shaker — highpass filtered noise with envelope
 */
static void gen_shaker(const char *path, float decay_ms, float duration_s) {
    int n = (int)(duration_s * SAMPLE_RATE);
    float *buf = (float *)calloc(n, sizeof(float));
    HP1 hp = {0};

    float hp_coeff = freq_to_coeff(3000.0f);
    float attack_samples = 5.0f * SAMPLE_RATE / 1000.0f;
    float decay_samples = decay_ms * SAMPLE_RATE / 1000.0f;

    for (int i = 0; i < n; i++) {
        float env;
        if ((float)i < attack_samples) {
            env = (float)i / attack_samples;
        } else {
            env = expf(-((float)i - attack_samples) / decay_samples * 5.0f);
        }
        float raw = noise() * env * 0.7f;
        buf[i] = hp1_tick(&hp, raw, hp_coeff);
    }

    write_wav(path, buf, n);
    free(buf);
}

/*
 * Tambourine — noise burst + metallic ring (6k, 8k, 12k sines)
 */
static void gen_tambourine(const char *path) {
    float duration_s = 0.4f;
    int n = (int)(duration_s * SAMPLE_RATE);
    float *buf = (float *)calloc(n, sizeof(float));
    HP1 hp = {0};

    float hp_coeff = freq_to_coeff(4000.0f);
    float attack_samples = 2.0f * SAMPLE_RATE / 1000.0f;
    float decay_samples = 300.0f * SAMPLE_RATE / 1000.0f;

    float phase1 = 0, phase2 = 0, phase3 = 0;

    for (int i = 0; i < n; i++) {
        float env;
        if ((float)i < attack_samples) {
            env = (float)i / attack_samples;
        } else {
            env = expf(-((float)i - attack_samples) / decay_samples * 5.0f);
        }

        /* Noise component (highpass filtered) */
        float n_part = hp1_tick(&hp, noise(), hp_coeff) * 0.4f;

        /* Metallic ring: three inharmonic sines */
        phase1 += 2.0f * (float)PI * 6000.0f / SAMPLE_RATE;
        phase2 += 2.0f * (float)PI * 8000.0f / SAMPLE_RATE;
        phase3 += 2.0f * (float)PI * 12000.0f / SAMPLE_RATE;
        float ring = (sinf(phase1) + sinf(phase2) * 0.7f + sinf(phase3) * 0.5f) * 0.25f;

        buf[i] = (n_part + ring) * env;
    }

    write_wav(path, buf, n);
    free(buf);
}

/*
 * Rimshot — short click + sine at 800Hz with fast decay
 */
static void gen_rimshot(const char *path) {
    float duration_s = 0.1f;
    int n = (int)(duration_s * SAMPLE_RATE);
    float *buf = (float *)calloc(n, sizeof(float));

    float click_samples = 1.0f * SAMPLE_RATE / 1000.0f;
    float decay_samples = 50.0f * SAMPLE_RATE / 1000.0f;
    float phase = 0;

    for (int i = 0; i < n; i++) {
        float sample = 0;

        /* Click transient */
        if ((float)i < click_samples) {
            sample += noise() * 0.9f;
        }

        /* Tone */
        phase += 2.0f * (float)PI * 800.0f / SAMPLE_RATE;
        float env = expf(-(float)i / decay_samples * 5.0f);
        sample += sinf(phase) * env * 0.8f;

        buf[i] = sample;
    }

    write_wav(path, buf, n);
    free(buf);
}

/*
 * Clave — pure sine at 2500Hz, very short
 */
static void gen_clave(const char *path) {
    float duration_s = 0.05f;
    int n = (int)(duration_s * SAMPLE_RATE);
    float *buf = (float *)calloc(n, sizeof(float));

    float decay_samples = 30.0f * SAMPLE_RATE / 1000.0f;
    float phase = 0;

    for (int i = 0; i < n; i++) {
        phase += 2.0f * (float)PI * 2500.0f / SAMPLE_RATE;
        float env = expf(-(float)i / decay_samples * 5.0f);
        buf[i] = sinf(phase) * env * 0.9f;
    }

    write_wav(path, buf, n);
    free(buf);
}

/*
 * 808 tom — sine with pitch drop
 */
static void gen_808_tom(const char *path,
                        float start_hz, float end_hz, float decay_ms)
{
    float duration_s = (decay_ms + 100.0f) / 1000.0f;
    int n = (int)(duration_s * SAMPLE_RATE);
    float *buf = (float *)calloc(n, sizeof(float));

    float sweep_samples = decay_ms * 0.5f * SAMPLE_RATE / 1000.0f;
    float decay_s = decay_ms * SAMPLE_RATE / 1000.0f;
    float phase = 0;

    for (int i = 0; i < n; i++) {
        float t = (float)i;

        /* Pitch envelope */
        float pt = t / sweep_samples;
        if (pt > 1.0f) pt = 1.0f;
        float freq = start_hz + (end_hz - start_hz) * pt;

        phase += 2.0f * (float)PI * freq / SAMPLE_RATE;
        float env = expf(-t / decay_s * 5.0f);

        /* Add a slight click at the start */
        float click = 0;
        if (t < SAMPLE_RATE * 0.001f) {
            click = noise() * 0.3f * (1.0f - t / (SAMPLE_RATE * 0.001f));
        }

        buf[i] = (sinf(phase) * 0.85f + click) * env;
    }

    write_wav(path, buf, n);
    free(buf);
}

/* ── main ──────────────────────────────────────────────────────────── */

int main(void) {
    srand((unsigned)time(NULL));

    printf("Generating synthetic drum samples...\n\n");

    /* 808 kicks */
    printf("[808-synth kicks]\n");
    gen_808_kick("samples/808-synth/808-sub-kick.wav",
                 150, 40, 200, 800, 1.0f);
    gen_808_kick("samples/808-synth/808-sub-kick-long.wav",
                 150, 30, 200, 1500, 2.0f);
    gen_808_kick("samples/808-synth/808-sub-kick-short.wav",
                 120, 50, 100, 300, 0.5f);

    /* 808 clap & snap */
    printf("\n[808-synth clap & snap]\n");
    gen_808_clap("samples/808-synth/808-clap.wav");
    gen_808_snap("samples/808-synth/808-snap.wav");

    /* 808 toms */
    printf("\n[808-synth toms]\n");
    gen_808_tom("samples/808-synth/808-tom-low.wav",  120, 80,  200);
    gen_808_tom("samples/808-synth/808-tom-mid.wav",  180, 120, 180);
    gen_808_tom("samples/808-synth/808-tom-high.wav", 250, 180, 150);

    /* Percussion */
    printf("\n[percussion]\n");
    gen_shaker("samples/percussion/shaker.wav",      80,  0.15f);
    gen_shaker("samples/percussion/shaker-long.wav", 200, 0.3f);
    gen_tambourine("samples/percussion/tambourine.wav");
    gen_rimshot("samples/percussion/rimshot.wav");
    gen_clave("samples/percussion/clave.wav");

    printf("\nDone! Generated 13 samples.\n");
    return 0;
}
