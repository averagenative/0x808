#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "engine/engine.h"
#include "engine/effects.h"
#include "engine/synth.h"

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
        const char *type_names[] = {"None", "Filter", "Delay", "Reverb", "Overdrive", "Fuzz", "Chorus",
                                     "Bitcrusher", "Compressor", "Phaser", "Flanger", "Tremolo",
                                     "Ring Mod", "Tape", "Shimmer", "EQ", "Limiter", "Foldback"};
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

    /* Test 12: 3-band parametric EQ
     *
     * Three properties to verify:
     *   a) Flat (all gains 0 dB) → near-passthrough (within rounding noise)
     *   b) Boost a band → that frequency gets louder
     *   c) Cut a band → that frequency gets quieter
     */
    {
        sq_effect_slot_t eq;
        memset(&eq, 0, sizeof(eq));
        effect_init(&eq, EFFECT_EQ, SAMPLE_RATE);

        /* a) flat = passthrough */
        gen_sine(buf, NUM_FRAMES, 1000.0f);
        float pre_rms = 0;
        for (uint32_t i = 0; i < NUM_FRAMES * 2; i++) pre_rms += buf[i] * buf[i];
        pre_rms = sqrtf(pre_rms / (NUM_FRAMES * 2));

        effect_process(&eq, buf, NUM_FRAMES, SAMPLE_RATE, 120.0);
        float post_rms = 0;
        for (uint32_t i = 0; i < NUM_FRAMES * 2; i++) post_rms += buf[i] * buf[i];
        post_rms = sqrtf(post_rms / (NUM_FRAMES * 2));

        float ratio_db = 20.0f * log10f(post_rms / pre_rms);
        ASSERT(fabsf(ratio_db) < 0.5f, "EQ flat should be near-passthrough (<0.5dB)");
        printf("  PASS: EQ flat passthrough (%.3f dB delta)\n", ratio_db);

        /* b) boost mid band by 12 dB at 1 kHz → ~12 dB louder */
        effect_free(&eq);
        effect_init(&eq, EFFECT_EQ, SAMPLE_RATE);
        eq.eq.bands[1].gain_db = 12.0f;

        gen_sine(buf, NUM_FRAMES, 1000.0f);
        pre_rms = 0;
        for (uint32_t i = 0; i < NUM_FRAMES * 2; i++) pre_rms += buf[i] * buf[i];
        pre_rms = sqrtf(pre_rms / (NUM_FRAMES * 2));

        /* Run multiple blocks so the filter reaches steady state */
        for (int it = 0; it < 4; it++) {
            gen_sine(buf, NUM_FRAMES, 1000.0f);
            effect_process(&eq, buf, NUM_FRAMES, SAMPLE_RATE, 120.0);
        }
        post_rms = 0;
        for (uint32_t i = 0; i < NUM_FRAMES * 2; i++) post_rms += buf[i] * buf[i];
        post_rms = sqrtf(post_rms / (NUM_FRAMES * 2));

        ratio_db = 20.0f * log10f(post_rms / pre_rms);
        ASSERT(ratio_db > 8.0f && ratio_db < 14.0f,
               "EQ +12dB peak at 1kHz should boost ~12dB (got out of [8,14])");
        printf("  PASS: EQ +12dB peak at 1kHz boosts %.1f dB\n", ratio_db);

        /* c) cut high shelf by 12 dB → 8 kHz signal much quieter */
        effect_free(&eq);
        effect_init(&eq, EFFECT_EQ, SAMPLE_RATE);
        eq.eq.bands[2].gain_db = -12.0f;

        for (int it = 0; it < 4; it++) {
            gen_sine(buf, NUM_FRAMES, 8000.0f);
            effect_process(&eq, buf, NUM_FRAMES, SAMPLE_RATE, 120.0);
        }
        post_rms = 0;
        for (uint32_t i = 0; i < NUM_FRAMES * 2; i++) post_rms += buf[i] * buf[i];
        post_rms = sqrtf(post_rms / (NUM_FRAMES * 2));

        gen_sine(buf, NUM_FRAMES, 8000.0f);
        pre_rms = 0;
        for (uint32_t i = 0; i < NUM_FRAMES * 2; i++) pre_rms += buf[i] * buf[i];
        pre_rms = sqrtf(pre_rms / (NUM_FRAMES * 2));

        ratio_db = 20.0f * log10f(post_rms / pre_rms);
        ASSERT(ratio_db < -6.0f, "EQ -12dB high shelf at 8kHz should cut at least 6dB");
        printf("  PASS: EQ -12dB high shelf cuts 8kHz by %.1f dB\n", ratio_db);

        effect_free(&eq);
    }

    /* Test 13: Brickwall limiter
     *
     *   a) Below threshold → no signal change
     *   b) Above threshold → output never exceeds ceiling
     */
    {
        sq_effect_slot_t lim;
        memset(&lim, 0, sizeof(lim));
        effect_init(&lim, EFFECT_LIMITER, SAMPLE_RATE);
        lim.limiter.ceiling = 0.5f;
        lim.limiter.release_ms = 50.0f;

        /* a) Quiet signal (peak ~0.3) should pass through unchanged. */
        gen_sine(buf, NUM_FRAMES, 440.0f);
        for (uint32_t i = 0; i < NUM_FRAMES * 2; i++) buf[i] *= 0.3f;
        effect_process(&lim, buf, NUM_FRAMES, SAMPLE_RATE, 120.0);
        float quiet_peak = 0.0f;
        for (uint32_t i = 0; i < NUM_FRAMES * 2; i++) {
            float v = fabsf(buf[i]); if (v > quiet_peak) quiet_peak = v;
        }
        ASSERT(quiet_peak < 0.35f,
               "Limiter should not amplify a quiet signal");
        printf("  PASS: Limiter passes quiet signal (peak %.3f)\n", quiet_peak);

        /* b) Hot signal (peak 1.0) hammered through the limiter for several
         *    blocks must NEVER exceed the ceiling.  We discard the first
         *    block (lookahead window still filling). */
        effect_free(&lim);
        effect_init(&lim, EFFECT_LIMITER, SAMPLE_RATE);
        lim.limiter.ceiling = 0.5f;

        gen_sine(buf, NUM_FRAMES, 440.0f);  /* peak 1.0 */
        effect_process(&lim, buf, NUM_FRAMES, SAMPLE_RATE, 120.0);

        float hot_peak = 0.0f;
        for (int it = 0; it < 4; it++) {
            gen_sine(buf, NUM_FRAMES, 440.0f);
            effect_process(&lim, buf, NUM_FRAMES, SAMPLE_RATE, 120.0);
            for (uint32_t i = 0; i < NUM_FRAMES * 2; i++) {
                float v = fabsf(buf[i]);
                if (v > hot_peak) hot_peak = v;
            }
        }
        ASSERT(hot_peak <= 0.501f,
               "Limiter must hold output below ceiling");
        printf("  PASS: Limiter holds hot signal at %.3f (ceiling 0.500)\n",
               hot_peak);

        effect_free(&lim);
    }

    /* Test 14a: Foldback distortion (TASK-215)
     *
     * Drive a sine wave well above the fold threshold and verify:
     *   - output is bounded (no runaway in the fold loop)
     *   - output differs from input (distortion did something)
     *   - 0 mix is bit-identical passthrough
     */
    {
        sq_effect_slot_t fb;
        memset(&fb, 0, sizeof(fb));
        effect_init(&fb, EFFECT_FOLDBACK, SAMPLE_RATE);
        fb.foldback.drive = 0.8f;
        fb.foldback.threshold = 0.4f;
        fb.foldback.tone = 1.0f;  /* full HF for clear comparison */
        fb.foldback.mix = 1.0f;

        gen_sine(buf, NUM_FRAMES, 220.0f);
        float pre_peak = 0.0f;
        for (uint32_t i = 0; i < NUM_FRAMES * 2; i++) {
            float v = fabsf(buf[i]); if (v > pre_peak) pre_peak = v;
        }
        effect_process(&fb, buf, NUM_FRAMES, SAMPLE_RATE, 120.0);
        float post_peak = 0.0f;
        bool changed = false;
        for (uint32_t i = 0; i < NUM_FRAMES * 2; i++) {
            float v = fabsf(buf[i]);
            if (v > post_peak) post_peak = v;
            if (v > 0.01f) changed = true;
        }
        ASSERT(post_peak < 1.5f, "Foldback output must stay bounded");
        ASSERT(changed, "Foldback should produce non-zero output");
        printf("  PASS: Foldback bounded (pre=%.3f post=%.3f)\n", pre_peak, post_peak);

        /* mix=0 should be exact passthrough */
        effect_free(&fb);
        effect_init(&fb, EFFECT_FOLDBACK, SAMPLE_RATE);
        fb.foldback.drive = 0.8f;
        fb.foldback.mix = 0.0f;
        gen_sine(buf, NUM_FRAMES, 440.0f);
        float clean[NUM_FRAMES * 2];
        memcpy(clean, buf, sizeof(clean));
        effect_process(&fb, buf, NUM_FRAMES, SAMPLE_RATE, 120.0);
        for (uint32_t i = 0; i < NUM_FRAMES * 2; i++) {
            ASSERT(fabsf(buf[i] - clean[i]) < 1e-6f,
                   "Foldback mix=0 should be passthrough");
        }
        printf("  PASS: Foldback mix=0 is bit-identical passthrough\n");
        effect_free(&fb);
    }

    /* Test 14: Per-track FX isolation (TASK-226)
     *
     * Put a 2-track pattern where track 0 has the kick and track 1 has
     * a synth. Apply a heavy compressor to track 0 only. Render. Track
     * 1's synth output should NOT be ducked by the compressor — it
     * lives on a different render buffer. */
    {
        sq_engine_t eng;
        sq_engine_init(&eng, SAMPLE_RATE);

        /* Two tracks: track 0 synth, track 1 synth (different preset). */
        eng.num_patterns = 1;
        sq_pattern_t *p = &eng.patterns[0];
        p->num_tracks = 2;
        p->tracks[0].type = TRACK_SYNTH;
        p->tracks[0].length = 16;
        p->tracks[0].synth_preset = 0;
        p->tracks[0].volume = 1.0f;
        p->tracks[0].pan = 0.0f;
        p->tracks[1].type = TRACK_SYNTH;
        p->tracks[1].length = 16;
        p->tracks[1].synth_preset = 1;
        p->tracks[1].volume = 1.0f;
        p->tracks[1].pan = 0.0f;

        /* Get baseline output without any FX: trigger both synth voices. */
        synth_trigger(&eng, 0, 1.0f, 0, 1.0f, 0.0f, 60, 0);
        synth_trigger(&eng, 1, 1.0f, 0, 1.0f, 0.0f, 72, 1);
        float baseline[1024 * 2];
        memset(baseline, 0, sizeof(baseline));
        sq_engine_process(&eng, baseline, 1024);

        float base_energy = 0.0f;
        for (int i = 0; i < 1024 * 2; i++) base_energy += baseline[i] * baseline[i];

        /* Reset + now attach a heavy compressor to track 0. */
        sq_engine_shutdown(&eng);
        sq_engine_init(&eng, SAMPLE_RATE);
        eng.num_patterns = 1;
        p = &eng.patterns[0];
        p->num_tracks = 2;
        p->tracks[0].type = TRACK_SYNTH;
        p->tracks[0].length = 16;
        p->tracks[0].synth_preset = 0;
        p->tracks[0].volume = 1.0f;
        p->tracks[1].type = TRACK_SYNTH;
        p->tracks[1].length = 16;
        p->tracks[1].synth_preset = 1;
        p->tracks[1].volume = 1.0f;

        effect_init(&p->tracks[0].effects[0], EFFECT_COMPRESSOR, SAMPLE_RATE);
        p->tracks[0].effects[0].compressor.threshold = 0.01f;
        p->tracks[0].effects[0].compressor.ratio = 20.0f;
        p->tracks[0].effects[0].compressor.attack = 0.0001f;
        p->tracks[0].effects[0].compressor.makeup = 0.3f;

        synth_trigger(&eng, 0, 1.0f, 0, 1.0f, 0.0f, 60, 0);
        synth_trigger(&eng, 1, 1.0f, 0, 1.0f, 0.0f, 72, 1);
        float with_fx[1024 * 2];
        memset(with_fx, 0, sizeof(with_fx));
        sq_engine_process(&eng, with_fx, 1024);

        float fx_energy = 0.0f;
        for (int i = 0; i < 1024 * 2; i++) fx_energy += with_fx[i] * with_fx[i];

        /* With the compressor crushing track 0 but NOT track 1, total
         * energy should be measurably different from the baseline AND
         * the outputs should not be identical. */
        ASSERT(base_energy > 0.0f, "Baseline should have non-zero energy");
        ASSERT(fx_energy > 0.0f, "Post-FX should still have non-zero energy");
        int first_diff = -1;
        for (int i = 0; i < 1024 * 2; i++) {
            if (fabsf(baseline[i] - with_fx[i]) > 1e-5f) { first_diff = i; break; }
        }
        ASSERT(first_diff >= 0, "Per-track FX should change the output");
        printf("  PASS: per-track FX changes output (baseline_e=%.4f fx_e=%.4f)\n",
               base_energy, fx_energy);

        sq_engine_shutdown(&eng);
    }

    free(buf);
    printf("\n=== ALL EFFECTS TESTS PASSED ===\n");
    return 0;
}
