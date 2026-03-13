/*
 * malformed_input_test.c — Test that project_load handles bad input gracefully.
 *
 * Writes various malformed .sqproj files and verifies no crashes,
 * correct clamping, and sensible error returns.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "engine/engine.h"
#include "formats/project.h"

#ifdef _WIN32
#define TEST_FILE "malformed_test.sqproj"
#else
#define TEST_FILE "/tmp/malformed_test.sqproj"
#endif

static int write_test_file(const char *path, const char *content) {
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    fputs(content, f);
    fclose(f);
    return 0;
}

/* ── Test 1: Out-of-range num_tracks (999) should clamp to SQ_MAX_TRACKS ── */
static int test_out_of_range_num_tracks(void) {
    sq_engine_t engine;
    sq_engine_init(&engine, 44100);

    write_test_file(TEST_FILE,
        "{\"version\":1,\"bpm\":120,\"patterns\":["
        "{\"name\":\"X\",\"num_tracks\":999,\"tracks\":[]}"
        "]}");

    int rc = project_load(&engine, TEST_FILE);
    /* Should succeed (clamps) or at least not crash */
    assert(rc == 0 && "load with num_tracks=999 should succeed");
    assert(engine.patterns[0].num_tracks <= SQ_MAX_TRACKS);
    printf("  num_tracks after clamp: %u (max %d)\n",
           engine.patterns[0].num_tracks, SQ_MAX_TRACKS);

    sq_engine_shutdown(&engine);
    printf("PASS: Out-of-range num_tracks clamped correctly\n");
    return 0;
}

/* ── Test 2: Negative values should clamp to minimum ─────────────────────── */
static int test_negative_values(void) {
    sq_engine_t engine;
    sq_engine_init(&engine, 44100);

    write_test_file(TEST_FILE,
        "{\"version\":1,\"bpm\":-50,\"master_volume\":-1.0,"
        "\"patterns\":[{\"name\":\"Neg\",\"num_tracks\":1,\"tracks\":["
        "{\"type\":0,\"length\":-5,\"sample_index\":-99,\"volume\":-0.5,"
        "\"pan\":-2.0,\"mute\":false,\"solo\":false,\"humanize\":-1.0,"
        "\"steps\":{\"0\":{\"vel\":-10}}}"
        "]}]}");

    int rc = project_load(&engine, TEST_FILE);
    assert(rc == 0 && "load with negative values should succeed");

    /* BPM should be clamped to [20, 300] */
    assert(engine.transport.bpm >= 20.0 && engine.transport.bpm <= 300.0);
    printf("  bpm after clamp: %.1f\n", engine.transport.bpm);

    /* Master volume clamped to [0, 2] */
    assert(engine.master_volume >= 0.0f && engine.master_volume <= 2.0f);

    /* Track length clamped to [1, SQ_MAX_STEPS] */
    assert(engine.patterns[0].tracks[0].length >= 1);
    assert(engine.patterns[0].tracks[0].length <= SQ_MAX_STEPS);

    /* Volume clamped to [0, 1] */
    assert(engine.patterns[0].tracks[0].volume >= 0.0f);

    /* Pan clamped to [-1, 1] */
    assert(engine.patterns[0].tracks[0].pan >= -1.0f);
    assert(engine.patterns[0].tracks[0].pan <= 1.0f);

    /* Humanize clamped to [0, 1] */
    assert(engine.patterns[0].tracks[0].humanize >= 0.0f);

    /* Velocity clamped to [0, 127] */
    assert(engine.patterns[0].tracks[0].steps[0].velocity <= 127);

    sq_engine_shutdown(&engine);
    printf("PASS: Negative values clamped correctly\n");
    return 0;
}

/* ── Test 3: Missing fields should use defaults ──────────────────────────── */
static int test_missing_fields(void) {
    sq_engine_t engine;
    sq_engine_init(&engine, 44100);

    /* Minimal valid JSON — almost everything missing */
    write_test_file(TEST_FILE,
        "{\"version\":1,\"patterns\":[{\"name\":\"Minimal\",\"num_tracks\":1,"
        "\"tracks\":[{\"type\":0,\"length\":16}]}]}");

    int rc = project_load(&engine, TEST_FILE);
    assert(rc == 0 && "load with missing fields should succeed");
    assert(engine.num_patterns == 1);
    assert(engine.patterns[0].num_tracks == 1);
    assert(engine.patterns[0].tracks[0].length == 16);

    /* Missing bpm should use engine default (120) */
    assert(engine.transport.bpm >= 20.0 && engine.transport.bpm <= 300.0);

    /* Missing master_volume should use default (1.0) */
    assert(engine.master_volume >= 0.0f && engine.master_volume <= 2.0f);

    sq_engine_shutdown(&engine);
    printf("PASS: Missing fields use defaults\n");
    return 0;
}

/* ── Test 4: Truncated JSON should fail gracefully ───────────────────────── */
static int test_truncated_json(void) {
    sq_engine_t engine;
    sq_engine_init(&engine, 44100);

    write_test_file(TEST_FILE,
        "{\"version\":1,\"bpm\":120,\"patterns\":[{\"name\":\"Tr");

    int rc = project_load(&engine, TEST_FILE);
    assert(rc == -1 && "truncated JSON should return error");

    sq_engine_shutdown(&engine);
    printf("PASS: Truncated JSON fails gracefully\n");
    return 0;
}

/* ── Test 5: Empty file should fail gracefully ───────────────────────────── */
static int test_empty_file(void) {
    sq_engine_t engine;
    sq_engine_init(&engine, 44100);

    write_test_file(TEST_FILE, "");

    int rc = project_load(&engine, TEST_FILE);
    assert(rc == -1 && "empty file should return error");

    sq_engine_shutdown(&engine);
    printf("PASS: Empty file fails gracefully\n");
    return 0;
}

/* ── Test 6: Completely invalid JSON should return error ──────────────────── */
static int test_invalid_json(void) {
    sq_engine_t engine;
    sq_engine_init(&engine, 44100);

    write_test_file(TEST_FILE, "this is not json at all!!! ###");

    int rc = project_load(&engine, TEST_FILE);
    assert(rc == -1 && "invalid JSON should return error");

    sq_engine_shutdown(&engine);
    printf("PASS: Invalid JSON returns error\n");
    return 0;
}

/* ── Test 7: Nonexistent file should fail gracefully ─────────────────────── */
static int test_nonexistent_file(void) {
    sq_engine_t engine;
    sq_engine_init(&engine, 44100);

    int rc = project_load(&engine, "/tmp/does_not_exist_12345.sqproj");
    assert(rc == -1 && "nonexistent file should return error");

    sq_engine_shutdown(&engine);
    printf("PASS: Nonexistent file fails gracefully\n");
    return 0;
}

/* ── Test 8: Extreme BPM and volume values ───────────────────────────────── */
static int test_extreme_values(void) {
    sq_engine_t engine;
    sq_engine_init(&engine, 44100);

    write_test_file(TEST_FILE,
        "{\"version\":1,\"bpm\":99999,\"master_volume\":999.0,"
        "\"swing\":5.0,\"patterns\":[{\"name\":\"Extreme\",\"num_tracks\":1,"
        "\"tracks\":[{\"type\":0,\"length\":9999,\"volume\":100.0,\"pan\":50.0,"
        "\"humanize\":10.0,\"steps\":{\"0\":{\"vel\":9999,\"pitch\":100}}}]}]}");

    int rc = project_load(&engine, TEST_FILE);
    assert(rc == 0 && "load with extreme values should succeed");

    /* BPM clamped to [20, 300] */
    assert(engine.transport.bpm <= 300.0);

    /* Master volume clamped to [0, 2] */
    assert(engine.master_volume <= 2.0f);

    /* Swing clamped to [0, 1] */
    assert(engine.transport.swing >= 0.0f && engine.transport.swing <= 1.0f);

    /* Track length clamped to [1, SQ_MAX_STEPS] */
    assert(engine.patterns[0].tracks[0].length <= SQ_MAX_STEPS);

    /* Track volume clamped to [0, 1] */
    assert(engine.patterns[0].tracks[0].volume <= 1.0f);

    /* Pan clamped to [-1, 1] */
    assert(engine.patterns[0].tracks[0].pan <= 1.0f);

    /* Velocity clamped to [0, 127] */
    assert(engine.patterns[0].tracks[0].steps[0].velocity <= 127);

    /* Pitch clamped to [-24, 24] */
    assert(engine.patterns[0].tracks[0].steps[0].pitch_offset >= -24);
    assert(engine.patterns[0].tracks[0].steps[0].pitch_offset <= 24);

    sq_engine_shutdown(&engine);
    printf("PASS: Extreme values clamped correctly\n");
    return 0;
}

/* ── Test 9: Extra/unknown fields should be ignored ──────────────────────── */
static int test_extra_fields(void) {
    sq_engine_t engine;
    sq_engine_init(&engine, 44100);

    write_test_file(TEST_FILE,
        "{\"version\":1,\"bpm\":130,\"unknown_field\":\"hello\","
        "\"extra_number\":42,\"nested\":{\"a\":1},"
        "\"patterns\":[{\"name\":\"Extra\",\"num_tracks\":1,"
        "\"bogus\":true,\"tracks\":[{\"type\":0,\"length\":16,"
        "\"volume\":0.7,\"mystery\":99}]}]}");

    int rc = project_load(&engine, TEST_FILE);
    assert(rc == 0 && "load with extra fields should succeed");
    assert(engine.num_patterns == 1);
    assert(engine.patterns[0].tracks[0].length == 16);

    sq_engine_shutdown(&engine);
    printf("PASS: Extra/unknown fields ignored\n");
    return 0;
}

int main(void) {
    printf("=== Malformed Input Tests ===\n\n");

    test_out_of_range_num_tracks();
    test_negative_values();
    test_missing_fields();
    test_truncated_json();
    test_empty_file();
    test_invalid_json();
    test_nonexistent_file();
    test_extreme_values();
    test_extra_fields();

    /* Cleanup */
    remove(TEST_FILE);

    printf("\n=== ALL MALFORMED INPUT TESTS PASSED ===\n");
    return 0;
}
