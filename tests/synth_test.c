#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "engine/engine.h"
#include "engine/synth.h"
#include "engine/export.h"

#define SR 44100
#define ASSERT(cond, msg) do { if (!(cond)) { fprintf(stderr, "FAIL: %s\n", msg); return 1; } } while(0)

int main(void) {
    sq_engine_t engine;
    sq_engine_init(&engine, SR);

    printf("=== Subtractive Synth Test ===\n");

    /* Test 1: Each subtractive preset produces audio */
    for (int p = 0; p < 5 && (uint32_t)p < engine.num_synth_presets; p++) {
        if (engine.synth_presets[p].synth_mode != SYNTH_SUBTRACTIVE) continue;

        /* Clear voices and trigger a note (MIDI 69 = A4) */
        memset(engine.synth_voices, 0, sizeof(engine.synth_voices));
        synth_trigger(&engine, p, 0.8f, 0, 0.8f, 0.0f, 69);

        /* Render some audio */
        float buf[512 * 2];
        memset(buf, 0, sizeof(buf));
        synth_render(&engine, buf, 512);

        /* Check for non-zero output */
        float peak = 0;
        for (int i = 0; i < 512*2; i++) { float v = fabsf(buf[i]); if (v > peak) peak = v; }
        printf("  Preset %d '%s': peak=%.4f\n", p, engine.synth_presets[p].name, peak);
        ASSERT(peak > 0.001f, "Subtractive preset should produce audio");
    }
    printf("  PASS: All subtractive presets produce audio\n");

    /* Test 2: Different waveforms produce different output */
    float peaks[4] = {0};
    for (int w = 0; w < 4; w++) {
        engine.synth_presets[0].osc1_wave = (sq_waveform_t)w;
        engine.synth_presets[0].osc_mix = 0.0f; /* only osc1 */
        memset(engine.synth_voices, 0, sizeof(engine.synth_voices));
        synth_trigger(&engine, 0, 1.0f, 0, 0.8f, 0.0f, 69);
        float buf[256 * 2];
        memset(buf, 0, sizeof(buf));
        synth_render(&engine, buf, 256);
        for (int i = 0; i < 256*2; i++) { float v = fabsf(buf[i]); if (v > peaks[w]) peaks[w] = v; }
    }
    printf("  Waveform peaks: saw=%.3f sq=%.3f tri=%.3f sin=%.3f\n", peaks[0], peaks[1], peaks[2], peaks[3]);
    ASSERT(peaks[0] > 0.01f && peaks[1] > 0.01f && peaks[2] > 0.01f && peaks[3] > 0.01f,
           "All waveforms should produce output");
    printf("  PASS: Different waveforms produce audio\n");

    /* Test 3: Export with synth tracks produces audio */
    sq_export_config_t cfg = { .sample_rate = SR, .num_bars = 1, .pattern_index = 0 };
    sq_export_result_t res;
    /* Ensure a synth track has notes */
    sq_pattern_t *pat = &engine.patterns[0];
    for (uint32_t t = 0; t < pat->num_tracks; t++) {
        if (pat->tracks[t].type == TRACK_SYNTH) {
            pat->tracks[t].steps[0].velocity = 100;
            pat->tracks[t].steps[0].note = 60;
            break;
        }
    }
    int rc = sq_export_render(&engine, &cfg, &res);
    ASSERT(rc == 0, "Export render should succeed");
    printf("  Export: %u frames, peak=%.4f\n", res.num_frames, res.peak_level);
    ASSERT(res.peak_level > 0.01f, "Exported audio should have signal");
    free(res.data);
    printf("  PASS: Export with synth tracks produces audio\n");

    sq_engine_shutdown(&engine);
    printf("\n=== ALL SUBTRACTIVE SYNTH TESTS PASSED ===\n");
    return 0;
}
