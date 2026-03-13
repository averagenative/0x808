/*
 * edge_case_test.c — Test edge cases that could cause division by zero,
 * underflow, or unexpected behavior in the engine.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>
#include "engine/engine.h"

/* ── Test 1: Render with 0 frames ─────────────────────────────────────────── */
static int test_zero_frame_render(void) {
    sq_engine_t engine;
    sq_engine_init(&engine, 44100);

    engine.transport.playing = true;
    float buf[2] = {0};
    sq_engine_process(&engine, buf, 0);

    sq_engine_shutdown(&engine);
    printf("PASS: Zero-frame render\n");
    return 0;
}

/* ── Test 2: Zero master volume produces silence ──────────────────────────── */
static int test_zero_master_volume(void) {
    sq_engine_t engine;
    sq_engine_init(&engine, 44100);

    /* Set up a pattern with active steps to produce audio */
    engine.patterns[0].num_tracks = 1;
    engine.patterns[0].tracks[0].type = TRACK_SYNTH;
    engine.patterns[0].tracks[0].length = 16;
    engine.patterns[0].tracks[0].synth_preset = 0;
    engine.patterns[0].tracks[0].volume = 1.0f;
    engine.patterns[0].tracks[0].steps[0].velocity = 127;
    engine.patterns[0].tracks[0].steps[0].note = 60;

    engine.master_volume = 0.0f;
    engine.transport.playing = true;

    float out[256 * 2];
    memset(out, 0, sizeof(out));
    sq_engine_process(&engine, out, 256);

    float peak = 0;
    for (int i = 0; i < 512; i++) {
        float v = fabsf(out[i]);
        if (v > peak) peak = v;
    }
    assert(peak < 0.001f && "Zero master volume should produce silence");

    sq_engine_shutdown(&engine);
    printf("PASS: Zero master volume produces silence\n");
    return 0;
}

/* ── Test 3: All tracks muted ─────────────────────────────────────────────── */
static int test_all_tracks_muted(void) {
    sq_engine_t engine;
    sq_engine_init(&engine, 44100);

    engine.transport.playing = true;
    for (uint32_t t = 0; t < engine.patterns[0].num_tracks; t++) {
        engine.patterns[0].tracks[t].mute = true;
    }

    float out[256 * 2];
    memset(out, 0, sizeof(out));
    sq_engine_process(&engine, out, 256);
    /* Should not crash */

    sq_engine_shutdown(&engine);
    printf("PASS: All tracks muted\n");
    return 0;
}

/* ── Test 4: Empty arrangement in song mode ───────────────────────────────── */
static int test_empty_arrangement_song_mode(void) {
    sq_engine_t engine;
    sq_engine_init(&engine, 44100);

    engine.transport.mode = MODE_SONG;
    engine.arrangement.num_sections = 0;
    engine.transport.playing = true;

    float out[256 * 2];
    memset(out, 0, sizeof(out));
    sq_engine_process(&engine, out, 256);
    /* Should not crash */

    sq_engine_shutdown(&engine);
    printf("PASS: Empty arrangement in song mode\n");
    return 0;
}

/* ── Test 5: Zero BPM (should be safe even if not clamped) ────────────────── */
static int test_zero_bpm(void) {
    sq_engine_t engine;
    sq_engine_init(&engine, 44100);

    engine.transport.bpm = 0.0;
    engine.transport.playing = true;

    float out[256 * 2];
    memset(out, 0, sizeof(out));
    sq_engine_process(&engine, out, 256);
    /* Should not crash or produce NaN/Inf */

    int has_nan = 0;
    for (int i = 0; i < 512; i++) {
        if (isnan(out[i]) || isinf(out[i])) {
            has_nan = 1;
            break;
        }
    }
    assert(!has_nan && "Zero BPM should not produce NaN/Inf");

    sq_engine_shutdown(&engine);
    printf("PASS: Zero BPM\n");
    return 0;
}

/* ── Test 6: Pattern with 0 num_tracks ────────────────────────────────────── */
static int test_zero_num_tracks(void) {
    sq_engine_t engine;
    sq_engine_init(&engine, 44100);

    engine.patterns[0].num_tracks = 0;
    engine.transport.playing = true;

    float out[256 * 2];
    memset(out, 0, sizeof(out));
    sq_engine_process(&engine, out, 256);
    /* Should not crash */

    sq_engine_shutdown(&engine);
    printf("PASS: Pattern with 0 tracks\n");
    return 0;
}

/* ── Test 7: Track with length=1 (minimum) ────────────────────────────────── */
static int test_minimum_track_length(void) {
    sq_engine_t engine;
    sq_engine_init(&engine, 44100);

    engine.patterns[0].num_tracks = 1;
    engine.patterns[0].tracks[0].length = 1;
    engine.patterns[0].tracks[0].type = TRACK_SYNTH;
    engine.patterns[0].tracks[0].synth_preset = 0;
    engine.patterns[0].tracks[0].volume = 1.0f;
    engine.patterns[0].tracks[0].steps[0].velocity = 100;
    engine.patterns[0].tracks[0].steps[0].note = 60;
    engine.transport.playing = true;

    float out[512 * 2];
    memset(out, 0, sizeof(out));
    sq_engine_process(&engine, out, 512);
    /* Should not crash — rapid re-triggering on every step */

    sq_engine_shutdown(&engine);
    printf("PASS: Track with length=1\n");
    return 0;
}

/* ── Test 8: Very high BPM (300) ──────────────────────────────────────────── */
static int test_high_bpm(void) {
    sq_engine_t engine;
    sq_engine_init(&engine, 44100);

    engine.transport.bpm = 300.0;
    engine.transport.playing = true;
    engine.patterns[0].num_tracks = 1;
    engine.patterns[0].tracks[0].length = 16;
    engine.patterns[0].tracks[0].type = TRACK_SYNTH;
    engine.patterns[0].tracks[0].synth_preset = 0;
    engine.patterns[0].tracks[0].volume = 1.0f;
    engine.patterns[0].tracks[0].steps[0].velocity = 100;
    engine.patterns[0].tracks[0].steps[0].note = 60;

    /* Process multiple buffers at high BPM */
    float out[256 * 2];
    for (int i = 0; i < 10; i++) {
        memset(out, 0, sizeof(out));
        sq_engine_process(&engine, out, 256);
    }
    /* Should not crash */

    sq_engine_shutdown(&engine);
    printf("PASS: Very high BPM (300)\n");
    return 0;
}

/* ── Test 9: Perform mode with queued section ─────────────────────────────── */
static int test_perform_mode(void) {
    sq_engine_t engine;
    sq_engine_init(&engine, 44100);

    engine.transport.mode = MODE_PERFORM;
    engine.arrangement.num_sections = 2;
    engine.arrangement.sections[0].pattern_index = 0;
    engine.arrangement.sections[0].repeat_count = 1;
    engine.arrangement.sections[1].pattern_index = 0;
    engine.arrangement.sections[1].repeat_count = 1;
    engine.transport.queued_section = 1;
    engine.transport.playing = true;

    float out[256 * 2];
    memset(out, 0, sizeof(out));
    sq_engine_process(&engine, out, 256);
    /* Should not crash */

    sq_engine_shutdown(&engine);
    printf("PASS: Perform mode with queued section\n");
    return 0;
}

/* ── Test 10: Multiple init/shutdown cycles ───────────────────────────────── */
static int test_init_shutdown_cycle(void) {
    sq_engine_t engine;

    for (int i = 0; i < 5; i++) {
        sq_engine_init(&engine, 44100);

        float out[64 * 2];
        memset(out, 0, sizeof(out));
        engine.transport.playing = true;
        sq_engine_process(&engine, out, 64);

        sq_engine_shutdown(&engine);
    }

    printf("PASS: Multiple init/shutdown cycles\n");
    return 0;
}

/* ── Test 11: Large buffer render ─────────────────────────────────────────── */
static int test_large_buffer(void) {
    sq_engine_t engine;
    sq_engine_init(&engine, 44100);

    engine.transport.playing = true;
    engine.patterns[0].num_tracks = 1;
    engine.patterns[0].tracks[0].length = 16;
    engine.patterns[0].tracks[0].type = TRACK_SYNTH;
    engine.patterns[0].tracks[0].synth_preset = 0;
    engine.patterns[0].tracks[0].volume = 1.0f;
    engine.patterns[0].tracks[0].steps[0].velocity = 100;
    engine.patterns[0].tracks[0].steps[0].note = 60;

    /* 4096 frames = ~93ms at 44100 Hz — larger than typical audio callback */
    float *out = calloc(4096 * 2, sizeof(float));
    assert(out != NULL);
    sq_engine_process(&engine, out, 4096);
    /* Should not crash */

    /* Check no NaN/Inf */
    int has_nan = 0;
    for (int i = 0; i < 4096 * 2; i++) {
        if (isnan(out[i]) || isinf(out[i])) {
            has_nan = 1;
            break;
        }
    }
    assert(!has_nan && "Large buffer should not produce NaN/Inf");

    free(out);
    sq_engine_shutdown(&engine);
    printf("PASS: Large buffer render (4096 frames)\n");
    return 0;
}

int main(void) {
    printf("=== Edge Case Tests ===\n\n");

    test_zero_frame_render();
    test_zero_master_volume();
    test_all_tracks_muted();
    test_empty_arrangement_song_mode();
    test_zero_bpm();
    test_zero_num_tracks();
    test_minimum_track_length();
    test_high_bpm();
    test_perform_mode();
    test_init_shutdown_cycle();
    test_large_buffer();

    printf("\n=== ALL EDGE CASE TESTS PASSED ===\n");
    return 0;
}
