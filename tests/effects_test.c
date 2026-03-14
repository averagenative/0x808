#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "engine/engine.h"
#include "engine/effects.h"

#define SAMPLE_RATE 44100
#define NUM_FRAMES 4096
#define ASSERT(cond, msg) do { if (!(cond)) { fprintf(stderr, "FAIL: %s\n", msg); return 1; } } while(0)

/* Generate a test impulse (1.0 at frame 0, 0.0 elsewhere) */
static void gen_impulse(float *buf, uint32_t frames) {
    memset(buf, 0, frames * 2 * sizeof(float));
    buf[0] = 1.0f; buf[1] = 1.0f;
}

/* Generate a sine wave */
static void gen_sine(float *buf, uint32_t frames, float freq) {
    for (uint32_t i = 0; i < frames; i++) {
        float v = sinf(2.0f * 3.14159f * freq * i / SAMPLE_RATE);
        buf[i*2] = v; buf[i*2+1] = v;
    }
}

/* Check if buffer has any non-zero samples */
static int has_signal(float *buf, uint32_t frames) {
    for (uint32_t i = 0; i < frames * 2; i++)
        if (fabsf(buf[i]) > 1e-6f) return 1;
    return 0;
}

int main(void) {
    float *buf = calloc(NUM_FRAMES * 2, sizeof(float));
    sq_effect_slot_t slot;

    printf("=== Effects DSP Test ===\n");

    /* Test 1: Filter LP — should attenuate high frequencies */
    effect_init(&slot, EFFECT_FILTER, SAMPLE_RATE);
    slot.filter.cutoff = 200.0f;
    slot.filter.resonance = 1.0f;
    slot.filter.wet = 1.0f;
    gen_sine(buf, NUM_FRAMES, 10000.0f); /* 10kHz sine */
    float pre_peak = 0;
    for (uint32_t i = 0; i < NUM_FRAMES*2; i++) { float v = fabsf(buf[i]); if (v > pre_peak) pre_peak = v; }
    effect_process(&slot, buf, NUM_FRAMES, SAMPLE_RATE, 120.0);
    float post_peak = 0;
    for (uint32_t i = 0; i < NUM_FRAMES*2; i++) { float v = fabsf(buf[i]); if (v > post_peak) post_peak = v; }
    printf("  LP filter: 10kHz pre=%.4f post=%.4f (attenuation=%.1fdB)\n", pre_peak, post_peak, 20*log10f(post_peak/pre_peak));
    ASSERT(post_peak < pre_peak * 0.5f, "LP filter should attenuate 10kHz by at least 6dB");
    effect_free(&slot);
    printf("  PASS: LP filter attenuates high frequencies\n");

    /* Test 2: Filter bypass */
    effect_init(&slot, EFFECT_FILTER, SAMPLE_RATE);
    slot.bypass = true;
    gen_sine(buf, NUM_FRAMES, 440.0f);
    float *ref = calloc(NUM_FRAMES * 2, sizeof(float));
    gen_sine(ref, NUM_FRAMES, 440.0f);
    effect_process(&slot, buf, NUM_FRAMES, SAMPLE_RATE, 120.0);
    int identical = 1;
    for (uint32_t i = 0; i < NUM_FRAMES*2; i++) { if (fabsf(buf[i] - ref[i]) > 1e-6f) { identical = 0; break; } }
    ASSERT(identical, "Bypassed filter should not modify signal");
    effect_free(&slot);
    free(ref);
    printf("  PASS: Bypass mode passes signal unchanged\n");

    /* Test 3: Delay — impulse should produce echo */
    effect_init(&slot, EFFECT_DELAY, SAMPLE_RATE);
    slot.delay.time = 0.05f; /* 50ms = 2205 samples, fits in NUM_FRAMES */
    slot.delay.feedback = 0.0f;
    slot.delay.wet = 1.0f;
    gen_impulse(buf, NUM_FRAMES);
    effect_process(&slot, buf, NUM_FRAMES, SAMPLE_RATE, 120.0);
    int delay_samples = (int)(0.05f * SAMPLE_RATE);
    ASSERT(fabsf(buf[0]) > 0.5f, "Delay: original impulse should be present");
    ASSERT(fabsf(buf[delay_samples * 2]) > 0.1f, "Delay: echo should appear at delay time");
    effect_free(&slot);
    printf("  PASS: Delay produces echo at correct time\n");

    /* Test 4: Reverb — impulse should produce tail */
    effect_init(&slot, EFFECT_REVERB, SAMPLE_RATE);
    slot.reverb.room_size = 0.8f;
    slot.reverb.damping = 0.5f;
    slot.reverb.wet = 1.0f;
    gen_impulse(buf, NUM_FRAMES);
    effect_process(&slot, buf, NUM_FRAMES, SAMPLE_RATE, 120.0);
    /* Check that reverb tail exists (non-zero signal well after impulse) */
    int has_tail = 0;
    for (uint32_t i = NUM_FRAMES / 2; i < NUM_FRAMES; i++) {
        if (fabsf(buf[i*2]) > 1e-6f) { has_tail = 1; break; }
    }
    ASSERT(has_tail, "Reverb should produce a tail in second half of buffer");
    effect_free(&slot);
    printf("  PASS: Reverb produces tail\n");

    /* Test 5: Effect chain */
    sq_effect_slot_t chain[3];
    effect_init(&chain[0], EFFECT_FILTER, SAMPLE_RATE);
    chain[0].filter.cutoff = 5000.0f;
    chain[0].filter.wet = 1.0f;
    effect_init(&chain[1], EFFECT_DELAY, SAMPLE_RATE);
    chain[1].delay.time = 0.05f;
    chain[1].delay.wet = 0.3f;
    effect_init(&chain[2], EFFECT_NONE, SAMPLE_RATE);
    gen_sine(buf, NUM_FRAMES, 440.0f);
    effects_chain_process(chain, 3, buf, NUM_FRAMES, SAMPLE_RATE, 120.0);
    ASSERT(has_signal(buf, NUM_FRAMES), "Effect chain should produce output");
    for (int i = 0; i < 3; i++) effect_free(&chain[i]);
    printf("  PASS: Effect chain processes correctly\n");

    /* Test 6: EFFECT_NONE does nothing */
    effect_init(&slot, EFFECT_NONE, SAMPLE_RATE);
    gen_sine(buf, NUM_FRAMES, 440.0f);
    float first = buf[0];
    effect_process(&slot, buf, NUM_FRAMES, SAMPLE_RATE, 120.0);
    ASSERT(fabsf(buf[0] - first) < 1e-6f, "EFFECT_NONE should not modify signal");
    printf("  PASS: EFFECT_NONE is a no-op\n");

    /* Test 7: Overdrive — should clip/saturate signal */
    effect_init(&slot, EFFECT_OVERDRIVE, SAMPLE_RATE);
    slot.overdrive.drive = 0.8f;
    slot.overdrive.tone = 0.5f;
    slot.overdrive.mix = 1.0f;
    gen_sine(buf, NUM_FRAMES, 440.0f);
    effect_process(&slot, buf, NUM_FRAMES, SAMPLE_RATE, 120.0);
    ASSERT(has_signal(buf, NUM_FRAMES), "Overdrive should produce output");
    /* Overdrive soft-clips: output should be bounded */
    float od_peak = 0;
    for (uint32_t i = 0; i < NUM_FRAMES*2; i++) { float v = fabsf(buf[i]); if (v > od_peak) od_peak = v; }
    ASSERT(od_peak <= 1.01f, "Overdrive output should be bounded near [-1,1]");
    effect_free(&slot);
    printf("  PASS: Overdrive produces bounded output\n");

    /* Test 8: Fuzz — should hard-clip signal */
    effect_init(&slot, EFFECT_FUZZ, SAMPLE_RATE);
    slot.fuzz.gain = 0.8f;
    slot.fuzz.tone = 0.5f;
    slot.fuzz.mix = 1.0f;
    gen_sine(buf, NUM_FRAMES, 440.0f);
    effect_process(&slot, buf, NUM_FRAMES, SAMPLE_RATE, 120.0);
    ASSERT(has_signal(buf, NUM_FRAMES), "Fuzz should produce output");
    float fz_peak = 0;
    for (uint32_t i = 0; i < NUM_FRAMES*2; i++) { float v = fabsf(buf[i]); if (v > fz_peak) fz_peak = v; }
    ASSERT(fz_peak <= 1.01f, "Fuzz output should be bounded near [-1,1]");
    effect_free(&slot);
    printf("  PASS: Fuzz produces bounded output\n");

    /* Test 9: Chorus — should produce modulated signal */
    effect_init(&slot, EFFECT_CHORUS, SAMPLE_RATE);
    slot.chorus.rate = 2.0f;
    slot.chorus.depth = 0.5f;
    slot.chorus.mix = 0.5f;
    gen_sine(buf, NUM_FRAMES, 440.0f);
    effect_process(&slot, buf, NUM_FRAMES, SAMPLE_RATE, 120.0);
    ASSERT(has_signal(buf, NUM_FRAMES), "Chorus should produce output");
    effect_free(&slot);
    printf("  PASS: Chorus produces output\n");

    /* Test 10: New effects in chain */
    effect_init(&chain[0], EFFECT_OVERDRIVE, SAMPLE_RATE);
    chain[0].overdrive.drive = 0.5f;
    chain[0].overdrive.mix = 1.0f;
    effect_init(&chain[1], EFFECT_CHORUS, SAMPLE_RATE);
    chain[1].chorus.mix = 0.3f;
    effect_init(&chain[2], EFFECT_REVERB, SAMPLE_RATE);
    chain[2].reverb.wet = 0.2f;
    gen_sine(buf, NUM_FRAMES, 440.0f);
    effects_chain_process(chain, 3, buf, NUM_FRAMES, SAMPLE_RATE, 120.0);
    ASSERT(has_signal(buf, NUM_FRAMES), "Chain with new effects should produce output");
    for (int i = 0; i < 3; i++) effect_free(&chain[i]);
    printf("  PASS: Chain with overdrive+chorus+reverb works\n");

    /* Test 11: Every effect type in the enum can be init/process/free without crash */
    {
        const char *type_names[] = {"None", "Filter", "Delay", "Reverb", "Overdrive", "Fuzz", "Chorus"};
        int name_count = (int)(sizeof(type_names) / sizeof(type_names[0]));
        ASSERT(name_count == EFFECT_TYPE_COUNT,
               "Effect type names array must match EFFECT_TYPE_COUNT — did you add a new effect?");
        printf("  PASS: Effect names array matches enum count (%d)\n", EFFECT_TYPE_COUNT);

        for (int i = 0; i < EFFECT_TYPE_COUNT; i++) {
            sq_effect_slot_t s;
            memset(&s, 0, sizeof(s));
            effect_init(&s, (sq_effect_type_t)i, SAMPLE_RATE);
            ASSERT((int)s.type == i, "Effect type should match after init");

            gen_sine(buf, NUM_FRAMES, 440.0f);
            effect_process(&s, buf, NUM_FRAMES, SAMPLE_RATE, 120.0);

            effect_free(&s);
            ASSERT(s.type == EFFECT_NONE, "Effect should be NONE after free");
            printf("  PASS: Effect type %d (%s) init/process/free OK\n", i, type_names[i]);
        }
    }

    free(buf);
    printf("\n=== ALL EFFECTS TESTS PASSED ===\n");
    return 0;
}
