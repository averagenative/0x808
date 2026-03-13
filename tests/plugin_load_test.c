#include <stdio.h>
#include <string.h>
#include <math.h>
#include "engine/engine.h"
#include "engine/synth.h"
#include "engine/export.h"

#define SR 44100
#define ASSERT(cond, msg) do { if (!(cond)) { fprintf(stderr, "FAIL: %s\n", msg); return 1; } } while(0)

int main(void) {
    printf("=== Plugin Integration Test ===\n");
    sq_engine_t engine;

    /* Test 1: Simulate plugin lifecycle */
    sq_engine_init(&engine, SR);
    ASSERT(engine.sample_rate == SR, "Sample rate set");
    ASSERT(engine.num_synth_presets > 0, "Presets loaded");
    printf("  PASS: Plugin init\n");

    /* Test 2: Process audio (simulates plugin process callback) */
    float buf[512 * 2];
    memset(buf, 0, sizeof(buf));
    engine.transport.playing = true;
    engine.transport.current_pattern = 0;
    /* Add some notes */
    engine.patterns[0].tracks[0].steps[0].velocity = 100;
    sq_engine_process(&engine, buf, 512);
    printf("  PASS: Process callback\n");

    /* Test 3: Multiple rapid process calls (simulates DAW buffer sizes) */
    for (int i = 0; i < 100; i++) {
        sq_engine_process(&engine, buf, 64); /* small buffer like in a DAW */
    }
    printf("  PASS: 100 rapid small-buffer process calls\n");

    /* Test 4: Parameter changes between process calls */
    engine.transport.bpm = 90.0;
    sq_engine_process(&engine, buf, 256);
    engine.transport.bpm = 180.0;
    sq_engine_process(&engine, buf, 256);
    engine.master_volume = 0.5f;
    sq_engine_process(&engine, buf, 256);
    printf("  PASS: Parameter changes between process calls\n");

    /* Test 5: Start/stop rapidly */
    for (int i = 0; i < 50; i++) {
        engine.transport.playing = (i % 2 == 0);
        sq_engine_process(&engine, buf, 128);
    }
    printf("  PASS: Rapid start/stop\n");

    /* Test 6: Synth note on/off during processing */
    synth_trigger(&engine, 0, 0.8f, 0, 0.8f, 0.0f, 69);
    sq_engine_process(&engine, buf, 256);
    synth_release_all(&engine);
    sq_engine_process(&engine, buf, 256);
    printf("  PASS: Synth trigger during processing\n");

    /* Test 7: Shutdown and re-init (simulates plugin reload) */
    sq_engine_shutdown(&engine);
    sq_engine_init(&engine, 48000);
    ASSERT(engine.sample_rate == 48000, "Re-init with different sample rate");
    sq_engine_process(&engine, buf, 256);
    sq_engine_shutdown(&engine);
    printf("  PASS: Shutdown and re-init\n");

    printf("\n=== ALL PLUGIN INTEGRATION TESTS PASSED ===\n");
    return 0;
}
