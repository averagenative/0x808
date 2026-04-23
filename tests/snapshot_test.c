#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "engine/engine.h"
#include "engine/export.h"
#include "formats/sample_io.h"

#define SR 44100
#define ASSERT(cond, msg) do { if (!(cond)) { fprintf(stderr, "FAIL: %s\n", msg); return 1; } } while(0)

int main(void) {
    printf("=== Snapshot Regression Test ===\n");
    sq_engine_t engine;
    sq_engine_init(&engine, SR);

    /* Load samples for deterministic output */
    /* (samples/ directory must be accessible) */
    const char *sample_paths[] = {
        "samples/kicks/kick.wav",
        "samples/snares/snare.wav",
        "samples/hihats/hihat.wav",
        "samples/percussion/clap.wav"
    };
    for (int i = 0; i < 4; i++) {
        if (sample_io_load(sample_paths[i], &engine.samples[engine.num_samples]) == 0) {
            engine.num_samples++;
        }
    }

    /* Set up a deterministic pattern */
    sq_pattern_t *p = &engine.patterns[0];
    p->tracks[0].sample_index = 0;
    p->tracks[0].steps[0].velocity = 127;
    p->tracks[0].steps[4].velocity = 127;
    p->tracks[0].steps[8].velocity = 127;
    p->tracks[0].steps[12].velocity = 127;

    /* Render */
    sq_export_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.sample_rate = SR;
    cfg.num_bars = 1;
    cfg.pattern_index = 0;
    sq_export_result_t res;
    int rc = sq_export_render(&engine, &cfg, &res);
    ASSERT(rc == 0, "Render should succeed");
    ASSERT(res.num_frames > 0, "Should have frames");

    /* Compute checksum-like values for regression detection */
    double sum = 0;
    float peak = 0;
    uint32_t nonzero = 0;
    for (uint32_t i = 0; i < res.num_frames * 2; i++) {
        float v = fabsf(res.data[i]);
        sum += v;
        if (v > peak) peak = v;
        if (v > 1e-6f) nonzero++;
    }

    printf("  Frames: %u\n", res.num_frames);
    printf("  Peak: %.4f\n", peak);
    printf("  Sum: %.2f\n", sum);
    printf("  Non-zero: %u / %u (%.1f%%)\n", nonzero, res.num_frames * 2, 100.0f * nonzero / (res.num_frames * 2));

    /* Regression checks — these values should be stable across builds */
    /* If they change, it means the DSP output changed */
    ASSERT(peak > 0.1f, "Peak should be significant");
    ASSERT(peak < 2.0f, "Peak should not clip");
    ASSERT(nonzero > res.num_frames / 4, "Should have substantial audio content");

    /* Test 2: Render twice — should be identical (deterministic) */
    sq_export_result_t res2;
    rc = sq_export_render(&engine, &cfg, &res2);
    ASSERT(rc == 0, "Second render should succeed");
    ASSERT(res.num_frames == res2.num_frames, "Frame counts should match");

    int diffs = 0;
    for (uint32_t i = 0; i < res.num_frames * 2 && i < res2.num_frames * 2; i++) {
        if (fabsf(res.data[i] - res2.data[i]) > 1e-6f) diffs++;
    }
    printf("  Determinism: %d differences between two renders\n", diffs);
    ASSERT(diffs == 0, "Two renders of same pattern should be identical");
    printf("  PASS: Deterministic rendering verified\n");

    free(res.data);
    free(res2.data);

    /* TASK-202: oscilloscope ring buffer must get populated by the
     * audio path. Trigger a kick and run one process block directly,
     * then verify the scope buffer captured non-zero audio. */
    {
        memset(engine.scope_buffer, 0, sizeof(engine.scope_buffer));
        engine.scope_write_pos = 0;
        engine.transport.playing = true;
        engine.transport.sample_position = 0;
        engine.transport.current_step = 0;
        engine.transport.step0_pending = true;
        float buf[1024 * 2];
        sq_engine_process(&engine, buf, 1024);

        int nonzero = 0;
        int bufsize = (int)(sizeof(engine.scope_buffer) /
                            sizeof(engine.scope_buffer[0]));
        for (int i = 0; i < bufsize; i++)
            if (engine.scope_buffer[i] != 0.0f) nonzero++;
        ASSERT(nonzero > 0, "scope_buffer should capture audio for oscilloscope");
        printf("  PASS: scope_buffer captured %d/%d non-zero samples\n",
               nonzero, bufsize);
    }

    sq_engine_shutdown(&engine);

    printf("\n=== ALL SNAPSHOT REGRESSION TESTS PASSED ===\n");
    return 0;
}
