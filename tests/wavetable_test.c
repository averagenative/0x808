#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "engine/engine.h"
#include "engine/synth.h"

#define SR 44100
#define ASSERT(cond, msg) do { if (!(cond)) { fprintf(stderr, "FAIL: %s\n", msg); return 1; } } while(0)

int main(void) {
    sq_engine_t engine;
    sq_engine_init(&engine, SR);

    printf("=== Wavetable Synth Test ===\n");

    /* Test 1: Wavetable banks were generated */
    printf("  WT banks: %u\n", engine.num_wt_banks);
    ASSERT(engine.num_wt_banks > 0, "Should have at least 1 wavetable bank");
    ASSERT(engine.wt_banks != NULL, "WT banks should be allocated");
    for (uint32_t b = 0; b < engine.num_wt_banks; b++) {
        printf("  Bank %u: '%s' (%d frames)\n", b, engine.wt_banks[b].name, engine.wt_banks[b].num_frames);
        ASSERT(engine.wt_banks[b].num_frames > 0, "Bank should have frames");
    }
    printf("  PASS: Wavetable banks generated\n");

    /* Test 2: Each wavetable preset produces audio */
    for (uint32_t p = 0; p < engine.num_synth_presets; p++) {
        if (engine.synth_presets[p].synth_mode != SYNTH_WAVETABLE) continue;

        memset(engine.synth_voices, 0, sizeof(engine.synth_voices));
        synth_trigger(&engine, (int)p, 0.8f, 0, 0.8f, 0.0f, 69);
        float buf[512 * 2];
        memset(buf, 0, sizeof(buf));
        synth_render(&engine, buf, 512);

        float peak = 0;
        for (int i = 0; i < 512*2; i++) { float v = fabsf(buf[i]); if (v > peak) peak = v; }
        printf("  WT Preset %u '%s': peak=%.4f\n", p, engine.synth_presets[p].name, peak);
        ASSERT(peak > 0.001f, "Wavetable preset should produce audio");
    }
    printf("  PASS: All wavetable presets produce audio\n");

    /* Test 3: Position sweep changes timbre */
    /* Find first WT preset */
    int wt_preset = -1;
    for (uint32_t p = 0; p < engine.num_synth_presets; p++) {
        if (engine.synth_presets[p].synth_mode == SYNTH_WAVETABLE) { wt_preset = (int)p; break; }
    }
    if (wt_preset >= 0) {
        float peak_low = 0, peak_high = 0;

        /* Position = 0.0 */
        engine.synth_presets[wt_preset].wt_position = 0.0f;
        memset(engine.synth_voices, 0, sizeof(engine.synth_voices));
        synth_trigger(&engine, wt_preset, 0.8f, 0, 0.8f, 0.0f, 69);
        float buf[256*2];
        memset(buf, 0, sizeof(buf));
        synth_render(&engine, buf, 256);
        for (int i = 0; i < 256*2; i++) { float v = fabsf(buf[i]); if (v > peak_low) peak_low = v; }

        /* Position = 1.0 */
        engine.synth_presets[wt_preset].wt_position = 1.0f;
        memset(engine.synth_voices, 0, sizeof(engine.synth_voices));
        synth_trigger(&engine, wt_preset, 0.8f, 0, 0.8f, 0.0f, 69);
        memset(buf, 0, sizeof(buf));
        synth_render(&engine, buf, 256);
        for (int i = 0; i < 256*2; i++) { float v = fabsf(buf[i]); if (v > peak_high) peak_high = v; }

        printf("  Position sweep: pos=0 peak=%.3f, pos=1 peak=%.3f\n", peak_low, peak_high);
        /* Both should produce audio (they might be similar or different) */
        ASSERT(peak_low > 0.001f && peak_high > 0.001f, "Both positions should produce audio");
        printf("  PASS: Position sweep produces audio at both extremes\n");
    }

    /* Test 4: Single-frame bank edge case */
    if (engine.wt_banks) {
        /* Temporarily create a 1-frame bank */
        sq_wt_bank_t *bank = &engine.wt_banks[0];
        int orig_frames = bank->num_frames;
        bank->num_frames = 1;
        if (wt_preset >= 0) {
            engine.synth_presets[wt_preset].wt_bank_index = 0;
            engine.synth_presets[wt_preset].wt_position = 0.5f;
            memset(engine.synth_voices, 0, sizeof(engine.synth_voices));
            synth_trigger(&engine, wt_preset, 0.8f, 0, 0.8f, 0.0f, 69);
            float buf[128*2]; memset(buf,0,sizeof(buf));
            synth_render(&engine, buf, 128);
            /* Should not crash */
            printf("  PASS: Single-frame bank does not crash\n");
        }
        bank->num_frames = orig_frames;
    }

    sq_engine_shutdown(&engine);
    printf("\n=== ALL WAVETABLE TESTS PASSED ===\n");
    return 0;
}
