/*
 * engine_render_test.c — Offline render: play 4 bars of the demo pattern
 * and write to a WAV file. Verifies the audio engine works without needing
 * a working audio device.
 *
 * Build: (handled by CMake)
 * Output: test_output.wav (open in any media player to verify)
 */

#define LOG_TAG "test"
#include "core/log.h"
#include "engine/engine.h"
#include "engine/synth.h"
#include "engine/sampler.h"
#include "formats/sample_io.h"

#include "dr_wav.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static sq_engine_t g_engine;

static int load_sample(const char *filepath)
{
    if (g_engine.num_samples >= SQ_MAX_SAMPLES) return -1;
    int idx = (int)g_engine.num_samples;
    if (sample_io_load(filepath, &g_engine.samples[idx]) != 0) {
        LOG_ERROR("Failed to load: %s", filepath);
        return -1;
    }
    g_engine.num_samples++;
    LOG_INFO("Loaded [%d]: %s (%u frames, %u ch, %u Hz)",
             idx, g_engine.samples[idx].name,
             g_engine.samples[idx].num_frames,
             g_engine.samples[idx].num_channels,
             g_engine.samples[idx].sample_rate);
    return idx;
}

int main(void)
{
    sq_log_init();
    LOG_INFO("Engine render test — offline WAV export");

    /* Init engine */
    sq_engine_init(&g_engine, 44100);

    /* Load samples */
    const char *defaults[] = {
        "samples/kicks/kick.wav",
        "samples/snares/snare.wav",
        "samples/hihats/hihat.wav",
        "samples/percussion/clap.wav",
        NULL
    };
    for (int i = 0; defaults[i]; i++)
        load_sample(defaults[i]);

    if (g_engine.num_samples == 0) {
        LOG_ERROR("No samples loaded!");
        return 1;
    }

    /* Set up demo pattern (same as main_gui.c) */
    sq_pattern_t *p = &g_engine.patterns[0];
    for (uint32_t t = 0; t < p->num_tracks && t < g_engine.num_samples; t++)
        p->tracks[t].sample_index = (int)t;

    /* Kick */
    p->tracks[0].steps[0].velocity  = 120;
    p->tracks[0].steps[4].velocity  = 110;
    p->tracks[0].steps[8].velocity  = 120;
    p->tracks[0].steps[12].velocity = 110;
    /* Snare */
    p->tracks[1].steps[4].velocity  = 127;
    p->tracks[1].steps[12].velocity = 127;
    /* HiHat */
    for (int s = 0; s < 16; s += 2)
        p->tracks[2].steps[s].velocity = (s % 4 == 0) ? 100 : 70;
    /* Clap */
    p->tracks[3].steps[7].velocity  = 90;
    p->tracks[3].steps[15].velocity = 80;
    /* Synth Bass */
    p->tracks[4].steps[0].velocity  = 100; p->tracks[4].steps[0].note  = 36;
    p->tracks[4].steps[4].velocity  = 80;  p->tracks[4].steps[4].note  = 36;
    p->tracks[4].steps[8].velocity  = 100; p->tracks[4].steps[8].note  = 39;
    p->tracks[4].steps[12].velocity = 80;  p->tracks[4].steps[12].note = 43;
    /* Synth Pluck */
    p->tracks[5].steps[2].velocity  = 90;  p->tracks[5].steps[2].note  = 60;
    p->tracks[5].steps[6].velocity  = 70;  p->tracks[5].steps[6].note  = 63;
    p->tracks[5].steps[10].velocity = 90;  p->tracks[5].steps[10].note = 67;
    p->tracks[5].steps[14].velocity = 70;  p->tracks[5].steps[14].note = 65;

    /* Start transport */
    g_engine.transport.playing = true;
    g_engine.transport.bpm = 120.0;

    /* Render 4 bars = 16 steps * 4 = 64 steps = 16 beats at 120 BPM = 8 seconds */
    uint32_t total_frames = 44100 * 8; /* 8 seconds */
    uint32_t chunk_size = 256;
    float *output = malloc(total_frames * 2 * sizeof(float));
    if (!output) {
        LOG_ERROR("malloc failed");
        return 1;
    }

    LOG_INFO("Rendering %u frames (%.1f seconds) at 120 BPM...",
             total_frames, (double)total_frames / 44100.0);

    float *ptr = output;
    uint32_t rendered = 0;
    while (rendered < total_frames) {
        uint32_t chunk = chunk_size;
        if (rendered + chunk > total_frames)
            chunk = total_frames - rendered;

        sq_engine_process(&g_engine, ptr, chunk);
        ptr += chunk * 2;
        rendered += chunk;
    }

    /* Check if we produced any non-zero audio */
    float peak = 0.0f;
    int nonzero_frames = 0;
    for (uint32_t i = 0; i < total_frames * 2; i++) {
        float abs_val = fabsf(output[i]);
        if (abs_val > peak) peak = abs_val;
        if (abs_val > 0.001f) nonzero_frames++;
    }

    LOG_INFO("Render complete: peak=%.4f, nonzero_samples=%d/%u (%.1f%%)",
             peak, nonzero_frames, total_frames * 2,
             100.0f * nonzero_frames / (total_frames * 2));

    if (peak < 0.001f) {
        LOG_ERROR("NO AUDIO PRODUCED! Engine may have a bug.");
    } else {
        LOG_INFO("Audio looks good!");
    }

    /* Write to WAV file */
    const char *outfile = "test_output.wav";
    drwav wav;
    drwav_data_format format;
    format.container     = drwav_container_riff;
    format.format        = DR_WAVE_FORMAT_IEEE_FLOAT;
    format.channels      = 2;
    format.sampleRate    = 44100;
    format.bitsPerSample = 32;

    if (drwav_init_file_write(&wav, outfile, &format, NULL)) {
        drwav_uint64 written = drwav_write_pcm_frames(&wav, total_frames, output);
        drwav_uninit(&wav);
        LOG_INFO("Wrote %llu frames to %s", (unsigned long long)written, outfile);
    } else {
        LOG_ERROR("Failed to open %s for writing", outfile);
    }

    free(output);

    /* ════════════════════════════════════════════════════════════════════════
     * Test: Synth trigger produces audio
     * ════════════════════════════════════════════════════════════════════════ */
    {
        LOG_INFO("=== Test: Synth trigger produces audio ===");
        sq_engine_t test_eng;
        sq_engine_init(&test_eng, 44100);

        /* Trigger a 440Hz note on preset 0 (subtractive Bass) */
        synth_trigger(&test_eng, 0, 0.8f, 0, 0.7f, 0.0f, 69, -1); /* A4 = MIDI 69 = 440Hz */

        uint32_t test_frames = 4096;
        float *buf = calloc(test_frames * 2, sizeof(float));
        synth_render(&test_eng, buf, test_frames);

        float test_peak = 0.0f;
        for (uint32_t i = 0; i < test_frames * 2; i++) {
            float a = fabsf(buf[i]);
            if (a > test_peak) test_peak = a;
        }

        if (test_peak > 0.001f) {
            LOG_INFO("PASS: synth trigger produced audio (peak=%.4f)", test_peak);
        } else {
            LOG_ERROR("FAIL: synth trigger produced no audio (peak=%.4f)", test_peak);
            free(buf);
            sq_engine_shutdown(&test_eng);
            return 1;
        }
        free(buf);
        sq_engine_shutdown(&test_eng);
    }

    /* ════════════════════════════════════════════════════════════════════════
     * Test: Synth release silences voice
     * ════════════════════════════════════════════════════════════════════════ */
    {
        LOG_INFO("=== Test: Synth release silences voice ===");
        sq_engine_t test_eng;
        sq_engine_init(&test_eng, 44100);

        /* Trigger and render a short chunk so envelope is active */
        synth_trigger(&test_eng, 0, 0.8f, 0, 0.7f, 0.0f, 69, -1);
        uint32_t warmup = 1024;
        float *buf = calloc(warmup * 2, sizeof(float));
        synth_render(&test_eng, buf, warmup);
        free(buf);

        /* Release all voices */
        synth_release_all(&test_eng);

        /* Render enough frames for the release tail to complete.
         * Preset 0 (Bass) has release=0.05s -> ~2205 frames at 44100Hz.
         * Render 22050 frames (~0.5s) to be safe. */
        uint32_t release_frames = 22050;
        buf = calloc(release_frames * 2, sizeof(float));
        synth_render(&test_eng, buf, release_frames);

        /* Check the last 1024 frames — should be near-zero */
        float tail_peak = 0.0f;
        uint32_t tail_start = (release_frames - 1024) * 2;
        for (uint32_t i = tail_start; i < release_frames * 2; i++) {
            float a = fabsf(buf[i]);
            if (a > tail_peak) tail_peak = a;
        }

        if (tail_peak < 0.01f) {
            LOG_INFO("PASS: release silenced voice (tail peak=%.6f)", tail_peak);
        } else {
            LOG_ERROR("FAIL: voice still audible after release (tail peak=%.4f)", tail_peak);
            free(buf);
            sq_engine_shutdown(&test_eng);
            return 1;
        }
        free(buf);
        sq_engine_shutdown(&test_eng);
    }

    /* ════════════════════════════════════════════════════════════════════════
     * Test: Multiple synth modes produce audio (FM and Wavetable)
     * ════════════════════════════════════════════════════════════════════════ */
    {
        LOG_INFO("=== Test: Multiple synth modes produce audio ===");

        struct { int preset; const char *name; } modes[] = {
            { 5,  "FM Bell" },
            { 10, "WT Sweep" },
        };
        int num_modes = (int)(sizeof(modes) / sizeof(modes[0]));

        for (int m = 0; m < num_modes; m++) {
            sq_engine_t test_eng;
            sq_engine_init(&test_eng, 44100);

            synth_trigger(&test_eng, modes[m].preset, 0.8f, 0, 0.7f, 0.0f, 60, -1); /* C4 */

            uint32_t test_frames = 4096;
            float *buf = calloc(test_frames * 2, sizeof(float));
            synth_render(&test_eng, buf, test_frames);

            float mode_peak = 0.0f;
            for (uint32_t i = 0; i < test_frames * 2; i++) {
                float a = fabsf(buf[i]);
                if (a > mode_peak) mode_peak = a;
            }

            if (mode_peak > 0.001f) {
                LOG_INFO("PASS: %s (preset %d) produced audio (peak=%.4f)",
                         modes[m].name, modes[m].preset, mode_peak);
            } else {
                LOG_ERROR("FAIL: %s (preset %d) produced no audio (peak=%.4f)",
                          modes[m].name, modes[m].preset, mode_peak);
                free(buf);
                sq_engine_shutdown(&test_eng);
                return 1;
            }
            free(buf);
            sq_engine_shutdown(&test_eng);
        }
    }

    /* ════════════════════════════════════════════════════════════════════════
     * Test: Virtual keyboard simulation (trigger, render, release)
     * ════════════════════════════════════════════════════════════════════════ */
    {
        LOG_INFO("=== Test: Virtual keyboard simulation ===");
        sq_engine_t test_eng;
        sq_engine_init(&test_eng, 44100);

        int preset = 1; /* "Lead" — subtractive preset with sustain */
        uint8_t midi_note = 64; /* E4 */

        /* Simulate note-on (what virtual_keyboard.c does) */
        synth_trigger(&test_eng, preset, 0.8f, 0, 0.7f, 0.0f, midi_note, -1);

        /* Render while note is held */
        uint32_t hold_frames = 4096;
        float *buf = calloc(hold_frames * 2, sizeof(float));
        synth_render(&test_eng, buf, hold_frames);

        float hold_peak = 0.0f;
        for (uint32_t i = 0; i < hold_frames * 2; i++) {
            float a = fabsf(buf[i]);
            if (a > hold_peak) hold_peak = a;
        }
        free(buf);

        if (hold_peak < 0.001f) {
            LOG_ERROR("FAIL: virtual keyboard note-on produced no audio");
            sq_engine_shutdown(&test_eng);
            return 1;
        }
        LOG_INFO("  note-on peak=%.4f", hold_peak);

        /* Simulate note-off (set envelope to release, like virtual_keyboard.c) */
        float freq = 440.0f * powf(2.0f, ((float)midi_note - 69.0f) / 12.0f);
        for (int v = 0; v < SQ_MAX_SYNTH_VOICES; v++) {
            if (test_eng.synth_voices[v].active &&
                fabsf(test_eng.synth_voices[v].frequency - freq) < 0.1f) {
                test_eng.synth_voices[v].amp_env.stage = ENV_RELEASE;
            }
        }

        /* Render release tail */
        uint32_t release_frames = 22050;
        buf = calloc(release_frames * 2, sizeof(float));
        synth_render(&test_eng, buf, release_frames);

        /* Check tail is silent */
        float tail_peak = 0.0f;
        uint32_t tail_start = (release_frames - 1024) * 2;
        for (uint32_t i = tail_start; i < release_frames * 2; i++) {
            float a = fabsf(buf[i]);
            if (a > tail_peak) tail_peak = a;
        }

        if (hold_peak > 0.001f && tail_peak < 0.01f) {
            LOG_INFO("PASS: virtual keyboard note-on/off cycle works (hold=%.4f, tail=%.6f)",
                     hold_peak, tail_peak);
        } else {
            LOG_ERROR("FAIL: virtual keyboard test (hold=%.4f, tail=%.4f)",
                      hold_peak, tail_peak);
            free(buf);
            sq_engine_shutdown(&test_eng);
            return 1;
        }
        free(buf);
        sq_engine_shutdown(&test_eng);
    }

    sq_engine_shutdown(&g_engine);
    LOG_INFO("Done. All tests passed. Open %s to verify audio.", outfile);
    return 0;
}
