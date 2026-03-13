/*
 * fm_synth_test.c — Test FM synthesis produces audio output.
 *
 * Triggers each FM preset, renders audio, and verifies non-silent output.
 * Exports a WAV file for manual listening.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "engine/engine.h"
#include "engine/synth.h"
#include "engine/export.h"

#define SR 44100
#define NUM_FRAMES (SR * 2) /* 2 seconds */

int main(void)
{
    sq_engine_t engine;
    sq_engine_init(&engine, SR);

    printf("FM presets loaded: %u total\n", engine.num_synth_presets);

    /* Test each FM preset */
    int fm_presets[] = {5, 6, 7, 8, 9}; /* Bell, EPiano, Metal, Bass, Pad */
    int num_fm = sizeof(fm_presets) / sizeof(fm_presets[0]);

    for (int t = 0; t < num_fm; t++) {
        int pi = fm_presets[t];
        sq_synth_preset_t *p = &engine.synth_presets[pi];

        printf("Testing preset %d: '%s' (mode=%d, alg=%d)...\n",
               pi, p->name, p->synth_mode, p->fm_algorithm);

        /* Clear voices */
        memset(engine.synth_voices, 0, sizeof(engine.synth_voices));

        /* Trigger a note (C4 = MIDI 60) */
        synth_trigger(&engine, pi, 0.8f, 0, 1.0f, 0.0f, 60);

        /* Render audio */
        float *buf = calloc(NUM_FRAMES * 2, sizeof(float));
        synth_render(&engine, buf, NUM_FRAMES);

        /* Check for non-silent output */
        float peak = 0.0f;
        for (uint32_t i = 0; i < NUM_FRAMES * 2; i++) {
            float a = fabsf(buf[i]);
            if (a > peak) peak = a;
        }

        printf("  Peak level: %.4f\n", peak);
        if (peak < 0.001f) {
            fprintf(stderr, "FAIL: Preset '%s' produced silent output!\n", p->name);
            free(buf);
            return 1;
        }
        if (peak > 10.0f) {
            fprintf(stderr, "FAIL: Preset '%s' output too loud (%.2f)!\n",
                    p->name, peak);
            free(buf);
            return 1;
        }

        free(buf);
        printf("  OK\n");
    }

    /* Test wavetable presets */
    printf("\n--- Wavetable Synthesis Tests ---\n");
    printf("Wavetable banks loaded: %u\n", engine.num_wt_banks);

    int wt_presets[] = {10, 11, 12, 13}; /* Sweep, Harmonic, PWM, Vocal */
    int num_wt = sizeof(wt_presets) / sizeof(wt_presets[0]);

    for (int t = 0; t < num_wt; t++) {
        int pi = wt_presets[t];
        sq_synth_preset_t *p = &engine.synth_presets[pi];

        printf("Testing preset %d: '%s' (mode=%d, bank=%d)...\n",
               pi, p->name, p->synth_mode, p->wt_bank_index);

        memset(engine.synth_voices, 0, sizeof(engine.synth_voices));
        synth_trigger(&engine, pi, 0.8f, 0, 1.0f, 0.0f, 60);

        float *buf = calloc(NUM_FRAMES * 2, sizeof(float));
        synth_render(&engine, buf, NUM_FRAMES);

        float peak = 0.0f;
        for (uint32_t i = 0; i < NUM_FRAMES * 2; i++) {
            float a = fabsf(buf[i]);
            if (a > peak) peak = a;
        }

        printf("  Peak level: %.4f\n", peak);
        if (peak < 0.001f) {
            fprintf(stderr, "FAIL: Preset '%s' produced silent output!\n", p->name);
            free(buf);
            return 1;
        }
        if (peak > 10.0f) {
            fprintf(stderr, "FAIL: Preset '%s' output too loud (%.2f)!\n",
                    p->name, peak);
            free(buf);
            return 1;
        }

        free(buf);
        printf("  OK\n");
    }

    /* Test classic synth presets (14-25) */
    printf("\n--- Classic Synth Preset Tests ---\n");
    for (int pi = 14; pi < (int)engine.num_synth_presets; pi++) {
        sq_synth_preset_t *p = &engine.synth_presets[pi];
        printf("Testing preset %d: '%s' (mode=%d)...\n",
               pi, p->name, p->synth_mode);

        memset(engine.synth_voices, 0, sizeof(engine.synth_voices));
        synth_trigger(&engine, pi, 0.8f, 0, 1.0f, 0.0f, 60);

        float *buf = calloc(NUM_FRAMES * 2, sizeof(float));
        synth_render(&engine, buf, NUM_FRAMES);

        float peak = 0.0f;
        for (uint32_t i = 0; i < NUM_FRAMES * 2; i++) {
            float a = fabsf(buf[i]);
            if (a > peak) peak = a;
        }

        printf("  Peak level: %.4f\n", peak);
        if (peak < 0.001f) {
            fprintf(stderr, "FAIL: Preset '%s' produced silent output!\n", p->name);
            free(buf);
            return 1;
        }
        free(buf);
        printf("  OK\n");
    }

    /* Export a test WAV with the FM Bell preset playing a melody */
    printf("\nExporting FM demo WAV...\n");
    memset(engine.synth_voices, 0, sizeof(engine.synth_voices));

    /* Simple melody: C4, E4, G4, C5 — each 0.5 seconds */
    uint8_t notes[] = {60, 64, 67, 72};
    int note_frames = SR / 2; /* 0.5 sec per note */
    int total_frames = note_frames * 4;
    float *out = calloc(total_frames * 2, sizeof(float));

    for (int n = 0; n < 4; n++) {
        synth_trigger(&engine, 5, 0.7f, 0, 0.8f, 0.0f, notes[n]);
        synth_render(&engine, out + n * note_frames * 2, note_frames);
    }

    /* Write WAV */
    sq_export_result_t res;
    res.data = out;
    res.num_frames = total_frames;
    res.sample_rate = SR;

    float peak = 0.0f;
    for (int i = 0; i < total_frames * 2; i++) {
        float a = fabsf(out[i]);
        if (a > peak) peak = a;
    }
    res.peak_level = peak;

    int rc = sq_export_write_wav("test_fm_synth.wav", &res, 32);
    if (rc != 0) {
        fprintf(stderr, "FAIL: WAV export failed\n");
        free(out);
        return 1;
    }
    printf("Exported test_fm_synth.wav (peak=%.4f)\n", peak);
    free(out);

    printf("\n=== ALL FM + WAVETABLE SYNTHESIS TESTS PASSED ===\n");
    return 0;
}
