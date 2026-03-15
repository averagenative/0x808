/*
 * recorder_test.c — Test the streaming recorder API.
 *
 * Tests:
 *   1. Start/write/stop round-trip at 16-bit, 24-bit, and 32-bit
 *   2. Auto-incrementing filenames (empty dir, sequential, gaps)
 *   3. Write failure handling (read-only path)
 *   4. Integration: record engine output and verify file size
 */

#define LOG_TAG "recorder_test"
#include "core/log.h"
#include "engine/engine.h"

#include "dr_wav.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/stat.h>
#include <unistd.h>

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, msg) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; LOG_ERROR("FAIL: %s", msg); } \
} while(0)

/* Generate a simple sine wave buffer */
static void generate_sine(float *buf, uint32_t num_frames, float freq, float sr)
{
    for (uint32_t i = 0; i < num_frames; i++) {
        float v = 0.5f * sinf(2.0f * 3.14159265f * freq * (float)i / sr);
        buf[i * 2]     = v; /* L */
        buf[i * 2 + 1] = v; /* R */
    }
}

/* ─── Test 1: Round-trip at each bit depth ────────────────────────────────── */

static void test_roundtrip(int bit_depth)
{
    char label[64];
    snprintf(label, sizeof(label), "round-trip %d-bit", bit_depth);

    char path[256];
    snprintf(path, sizeof(path), "/tmp/0x808_test_rec_%d.wav", bit_depth);

    sq_recorder_t rec;
    memset(&rec, 0, sizeof(rec));

    /* Start */
    int ret = sq_recorder_start(&rec, path, 44100, (uint32_t)bit_depth);
    CHECK(ret == 0, label);
    CHECK(rec.state == SQ_REC_ACTIVE, label);

    /* Write 1 second of audio in chunks */
    float buf[1024]; /* 512 stereo frames */
    generate_sine(buf, 512, 440.0f, 44100.0f);

    uint32_t total = 0;
    while (total < 44100) {
        uint32_t chunk = 512;
        if (total + chunk > 44100) chunk = 44100 - total;
        sq_recorder_write(&rec, buf, chunk);
        CHECK(rec.state == SQ_REC_ACTIVE, label);
        total += chunk;
    }

    CHECK(rec.frames_written == 44100, label);

    /* Stop */
    sq_recorder_stop(&rec);
    CHECK(rec.state == SQ_REC_IDLE, label);

    /* Verify the file is valid WAV */
    drwav wav;
    if (drwav_init_file(&wav, path, NULL)) {
        CHECK(wav.channels == 2, label);
        CHECK(wav.sampleRate == 44100, label);
        CHECK(wav.totalPCMFrameCount == 44100, label);

        if (bit_depth == 32) {
            CHECK(wav.bitsPerSample == 32, label);
        } else {
            CHECK(wav.bitsPerSample == (unsigned)bit_depth, label);
        }

        drwav_uninit(&wav);
        LOG_INFO("  %s: OK (frames=%llu, bps=%u)",
                 label, (unsigned long long)wav.totalPCMFrameCount,
                 wav.bitsPerSample);
    } else {
        g_fail++;
        LOG_ERROR("FAIL: %s — could not open output WAV", label);
    }

    unlink(path);
}

/* ─── Test 2: Auto-incrementing filenames ─────────────────────────────────── */

static void test_auto_filenames(void)
{
    const char *dir = "/tmp/0x808_test_rec_dir";
    char cmd[512];

    /* Clean and create test directory */
    snprintf(cmd, sizeof(cmd), "rm -rf %s && mkdir -p %s", dir, dir);
    system(cmd);

    char out[512];
    int num;

    /* Test 2a: Empty directory → should get _001 */
    num = sq_recorder_next_filename(dir, "recording", 0, out, sizeof(out));
    CHECK(num == 1, "empty dir → 001");
    CHECK(strstr(out, "recording_001.wav") != NULL, "filename contains recording_001.wav");
    LOG_INFO("  empty dir: %s (num=%d)", out, num);

    /* Create some files to simulate existing recordings */
    snprintf(cmd, sizeof(cmd), "touch %s/recording_001.wav %s/recording_002.wav %s/recording_003.wav",
             dir, dir, dir);
    system(cmd);

    /* Test 2b: Sequential files → should get _004 */
    num = sq_recorder_next_filename(dir, "recording", 0, out, sizeof(out));
    CHECK(num == 4, "sequential → 004");
    LOG_INFO("  sequential: %s (num=%d)", out, num);

    /* Test 2c: Gap in numbering → should get max+1 */
    snprintf(cmd, sizeof(cmd), "rm %s/recording_002.wav && touch %s/recording_010.wav",
             dir, dir);
    system(cmd);

    num = sq_recorder_next_filename(dir, "recording", 0, out, sizeof(out));
    CHECK(num == 11, "gap → 011 (max+1)");
    LOG_INFO("  with gap: %s (num=%d)", out, num);

    /* Test 2d: last_known skips scan for higher values */
    num = sq_recorder_next_filename(dir, "recording", 20, out, sizeof(out));
    CHECK(num == 21, "last_known=20 → 021");
    LOG_INFO("  last_known: %s (num=%d)", out, num);

    /* Cleanup */
    snprintf(cmd, sizeof(cmd), "rm -rf %s", dir);
    system(cmd);
}

/* ─── Test 3: Write failure handling ──────────────────────────────────────── */

static void test_write_failure(void)
{
    sq_recorder_t rec;
    memset(&rec, 0, sizeof(rec));

    /* Try to write to a path that doesn't exist / is invalid */
    int ret = sq_recorder_start(&rec, "/dev/null/impossible/path.wav",
                                44100, 16);
    CHECK(ret == -1, "write to invalid path returns -1");
    CHECK(rec.state != SQ_REC_ACTIVE, "state not active after failure");
    LOG_INFO("  invalid path: correctly rejected");

    /* Try writing to a read-only directory */
    system("mkdir -p /tmp/0x808_test_readonly && chmod 444 /tmp/0x808_test_readonly");
    ret = sq_recorder_start(&rec, "/tmp/0x808_test_readonly/test.wav", 44100, 16);
    CHECK(ret == -1, "write to read-only dir returns -1");
    LOG_INFO("  read-only dir: correctly rejected (ret=%d)", ret);
    system("chmod 755 /tmp/0x808_test_readonly && rm -rf /tmp/0x808_test_readonly");

    /* Verify stop is safe on a recorder that never started */
    sq_recorder_stop(&rec);
    CHECK(rec.state == SQ_REC_IDLE, "stop on idle recorder is safe");
    LOG_INFO("  idle stop: OK");
}

/* ─── Test 4: Integration — record engine output ──────────────────────────── */

static void test_engine_integration(void)
{
    const char *path = "/tmp/0x808_test_engine_rec.wav";
    sq_engine_t engine;
    sq_engine_init(&engine, 48000);

    /* Set up a simple pattern so the engine produces non-silence */
    sq_pattern_t *p = &engine.patterns[0];
    p->tracks[4].steps[0].velocity = 100;  /* synth note */
    p->tracks[4].steps[4].velocity = 100;
    engine.transport.bpm = 120.0;
    engine.transport.playing = true;

    /* Start recording */
    int ret = sq_recorder_start(&engine.recorder, path, 48000, 16);
    CHECK(ret == 0, "engine integration: start OK");

    /* Render 0.5 seconds of audio */
    float buf[1024]; /* 512 stereo frames */
    uint32_t total = 0;
    uint32_t target = 24000; /* 0.5s at 48kHz */
    while (total < target) {
        uint32_t chunk = 512;
        if (total + chunk > target) chunk = target - total;
        sq_engine_process(&engine, buf, chunk);
        total += chunk;
    }

    CHECK(engine.recorder.state == SQ_REC_ACTIVE, "still recording after 2s");
    CHECK(engine.recorder.frames_written == target, "correct frame count");

    /* Stop and verify */
    sq_recorder_stop(&engine.recorder);

    /* Check file */
    drwav wav;
    if (drwav_init_file(&wav, path, NULL)) {
        CHECK(wav.channels == 2, "engine rec: stereo");
        CHECK(wav.sampleRate == 48000, "engine rec: 48kHz");
        CHECK(wav.totalPCMFrameCount == target, "engine rec: frame count matches");

        /* Verify file size: frames * channels * bytes_per_sample + WAV header */
        drwav_uninit(&wav);

        struct stat st;
        if (stat(path, &st) == 0) {
            long expected_data = (long)target * 2 * 2; /* frames * channels * 2 bytes */
            long actual = (long)st.st_size;
            /* Allow for WAV header overhead (44-80 bytes) */
            CHECK(actual >= expected_data && actual < expected_data + 200,
                  "engine rec: file size within expected range");
            LOG_INFO("  engine rec: %ld bytes (expected ~%ld + header)",
                     actual, expected_data);
        }
    } else {
        g_fail++;
        LOG_ERROR("FAIL: engine integration — could not read output file");
    }

    unlink(path);
    sq_engine_shutdown(&engine);
}

/* ─── Main ────────────────────────────────────────────────────────────────── */

int main(void)
{
    sq_log_init();
    sq_log_set_level(SQ_LOG_INFO);
    LOG_INFO("=== Recorder Test Suite ===");

    LOG_INFO("--- Test 1: Round-trip at each bit depth ---");
    test_roundtrip(16);
    test_roundtrip(24);
    test_roundtrip(32);

    LOG_INFO("--- Test 2: Auto-incrementing filenames ---");
    test_auto_filenames();

    LOG_INFO("--- Test 3: Write failure handling ---");
    test_write_failure();

    LOG_INFO("--- Test 4: Engine integration ---");
    test_engine_integration();

    LOG_INFO("=== Results: %d passed, %d failed ===", g_pass, g_fail);

    if (g_fail > 0) {
        LOG_ERROR("SOME TESTS FAILED");
        return 1;
    }

    LOG_INFO("ALL RECORDER TESTS PASSED");
    return 0;
}
