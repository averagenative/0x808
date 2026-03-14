/*
 * export_test.c — Test the export API: render + write WAV at multiple bit depths.
 */

#define LOG_TAG "export_test"
#include "core/log.h"
#include "engine/engine.h"
#include "engine/export.h"
#include "formats/sample_io.h"

#include <stdio.h>
#include <stdlib.h>

static sq_engine_t g_engine;

static void load_sample(const char *path)
{
    if (g_engine.num_samples >= SQ_MAX_SAMPLES) return;
    int idx = (int)g_engine.num_samples;
    if (sample_io_load(path, &g_engine.samples[idx]) == 0) {
        g_engine.num_samples++;
        LOG_INFO("Loaded [%d]: %s", idx, g_engine.samples[idx].name);
    }
}

int main(void)
{
    sq_log_init();
    LOG_INFO("Export API test");

    sq_engine_init(&g_engine, 44100);

    /* Load samples */
    load_sample("samples/kicks/kick.wav");
    load_sample("samples/snares/snare.wav");
    load_sample("samples/hihats/hihat.wav");
    load_sample("samples/percussion/clap.wav");

    /* Set up demo pattern */
    sq_pattern_t *p = &g_engine.patterns[0];
    for (uint32_t t = 0; t < p->num_tracks && t < g_engine.num_samples; t++)
        p->tracks[t].sample_index = (int)t;

    p->tracks[0].steps[0].velocity  = 120;
    p->tracks[0].steps[4].velocity  = 110;
    p->tracks[0].steps[8].velocity  = 120;
    p->tracks[0].steps[12].velocity = 110;
    p->tracks[1].steps[4].velocity  = 127;
    p->tracks[1].steps[12].velocity = 127;
    for (int s = 0; s < 16; s += 2)
        p->tracks[2].steps[s].velocity = (s % 4 == 0) ? 100 : 70;

    g_engine.transport.bpm = 120.0;

    /* Test 1: 32-bit float export */
    {
        sq_export_config_t cfg = {.sample_rate = 44100, .num_bars = 2, .pattern_index = 0};
        sq_export_result_t result = {0};

        if (sq_export_render(&g_engine, &cfg, &result) == 0) {
            LOG_INFO("Test 1 OK: %u frames, peak=%.4f", result.num_frames, result.peak_level);
            sq_export_write_wav("test_export_32f.wav", &result, 32);
            free(result.data);
        } else {
            LOG_ERROR("Test 1 FAILED: render error");
            return 1;
        }
    }

    /* Test 2: 16-bit export */
    {
        sq_export_config_t cfg = {.sample_rate = 44100, .num_bars = 1, .pattern_index = 0};
        sq_export_result_t result = {0};

        if (sq_export_render(&g_engine, &cfg, &result) == 0) {
            LOG_INFO("Test 2 OK: %u frames, peak=%.4f", result.num_frames, result.peak_level);
            sq_export_write_wav("test_export_16.wav", &result, 16);
            free(result.data);
        } else {
            LOG_ERROR("Test 2 FAILED: render error");
            return 1;
        }
    }

    /* Test 3: 24-bit export */
    {
        sq_export_config_t cfg = {.sample_rate = 48000, .num_bars = 1, .pattern_index = 0};
        sq_export_result_t result = {0};

        if (sq_export_render(&g_engine, &cfg, &result) == 0) {
            LOG_INFO("Test 3 OK: %u frames at 48kHz, peak=%.4f",
                     result.num_frames, result.peak_level);
            sq_export_write_wav("test_export_24.wav", &result, 24);
            free(result.data);
        } else {
            LOG_ERROR("Test 3 FAILED: render error");
            return 1;
        }
    }

    /* Test 4: MP3 export at 128 kbps */
    {
        sq_export_config_t cfg = {.sample_rate = 44100, .num_bars = 2, .pattern_index = 0};
        sq_export_result_t result = {0};

        if (sq_export_render(&g_engine, &cfg, &result) == 0) {
            if (sq_export_write_mp3("test_export_128.mp3", &result, 128) == 0) {
                LOG_INFO("Test 4 OK: MP3 128k export");
            } else {
                LOG_ERROR("Test 4 FAILED: MP3 write error");
                free(result.data);
                return 1;
            }
            free(result.data);
        } else {
            LOG_ERROR("Test 4 FAILED: render error");
            return 1;
        }
    }

    /* Test 5: MP3 export at 320 kbps */
    {
        sq_export_config_t cfg = {.sample_rate = 44100, .num_bars = 1, .pattern_index = 0};
        sq_export_result_t result = {0};

        if (sq_export_render(&g_engine, &cfg, &result) == 0) {
            if (sq_export_write_mp3("test_export_320.mp3", &result, 320) == 0) {
                LOG_INFO("Test 5 OK: MP3 320k export");
            } else {
                LOG_ERROR("Test 5 FAILED: MP3 write error");
                free(result.data);
                return 1;
            }
            free(result.data);
        } else {
            LOG_ERROR("Test 5 FAILED: render error");
            return 1;
        }
    }

    /* Test 6: FLAC 16-bit export */
    {
        sq_export_config_t cfg = {.sample_rate = 44100, .num_bars = 2, .pattern_index = 0};
        sq_export_result_t result = {0};

        if (sq_export_render(&g_engine, &cfg, &result) == 0) {
            if (sq_export_write_flac("test_export_16.flac", &result, 16) == 0) {
                LOG_INFO("Test 6 OK: FLAC 16-bit export");
            } else {
                LOG_ERROR("Test 6 FAILED: FLAC write error");
                free(result.data);
                return 1;
            }
            free(result.data);
        } else {
            LOG_ERROR("Test 6 FAILED: render error");
            return 1;
        }
    }

    /* Test 7: FLAC 24-bit export */
    {
        sq_export_config_t cfg = {.sample_rate = 48000, .num_bars = 1, .pattern_index = 0};
        sq_export_result_t result = {0};

        if (sq_export_render(&g_engine, &cfg, &result) == 0) {
            if (sq_export_write_flac("test_export_24.flac", &result, 24) == 0) {
                LOG_INFO("Test 7 OK: FLAC 24-bit export at 48kHz");
            } else {
                LOG_ERROR("Test 7 FAILED: FLAC write error");
                free(result.data);
                return 1;
            }
            free(result.data);
        } else {
            LOG_ERROR("Test 7 FAILED: render error");
            return 1;
        }
    }

    sq_engine_shutdown(&g_engine);
    LOG_INFO("All export tests passed!");
    return 0;
}
