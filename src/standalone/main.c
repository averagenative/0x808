/*
 * main.c — Standalone application entry point.
 *
 * Phase 2: Now has a step sequencer! Press SPACE to play/stop a drum loop.
 *
 * Controls:
 *   SPACE  = play / stop the sequencer
 *   1-4    = trigger samples manually
 *   - / =  = pitch sample 0 down/up an octave
 *   [ / ]  = decrease / increase BPM by 10
 *   q      = quit
 *
 * Threading model:
 *   Main thread:   reads keypresses, modifies engine state
 *   Audio thread:   miniaudio calls audio_callback() → sq_engine_process()
 */

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include "engine/engine.h"
#include "engine/sampler.h"
#include "formats/sample_io.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ─── Global engine state ─────────────────────────────────────────────────── */

static sq_engine_t g_engine;

/* ─── Audio callback (runs on miniaudio's thread) ─────────────────────────── */

static void audio_callback(ma_device *device, void *output, const void *input,
                            ma_uint32 frame_count)
{
    (void)device;
    (void)input;
    sq_engine_process(&g_engine, (float *)output, frame_count);
}

/* ─── Load a sample into the engine ───────────────────────────────────────── */

static int load_sample(const char *filepath)
{
    if (g_engine.num_samples >= SQ_MAX_SAMPLES) {
        fprintf(stderr, "Error: max samples reached (%d)\n", SQ_MAX_SAMPLES);
        return -1;
    }

    int idx = (int)g_engine.num_samples;
    if (sample_io_load(filepath, &g_engine.samples[idx]) != 0) {
        fprintf(stderr, "Error: failed to load '%s'\n", filepath);
        return -1;
    }

    g_engine.num_samples++;
    printf("Loaded sample %d: '%s' (%u frames, %u ch, %u Hz)\n",
           idx, g_engine.samples[idx].name,
           g_engine.samples[idx].num_frames,
           g_engine.samples[idx].num_channels,
           g_engine.samples[idx].sample_rate);
    return idx;
}

/* ─── Set up a demo drum pattern ──────────────────────────────────────────── *
 *
 * Creates a classic 16-step pattern:
 *   Kick:   X . . . X . . . X . . . X . . .   (beats 1,2,3,4)
 *   Snare:  . . . . X . . . . . . . X . . .   (beats 2,4)
 *   HiHat:  X . X . X . X . X . X . X . X .   (every 8th note)
 *   Clap:   . . . . . . . X . . . . . . . X   (off-beat accents)
 */
static void setup_demo_pattern(void)
{
    sq_pattern_t *p = &g_engine.patterns[0];

    /* Assign loaded samples to tracks (0=kick, 1=snare, 2=hihat, 3=clap) */
    for (uint32_t t = 0; t < p->num_tracks && t < g_engine.num_samples; t++) {
        p->tracks[t].sample_index = (int)t;
    }

    /* Track 0: Kick — steps 0, 4, 8, 12 (four-on-the-floor) */
    p->tracks[0].steps[0].velocity  = 120;
    p->tracks[0].steps[4].velocity  = 110;
    p->tracks[0].steps[8].velocity  = 120;
    p->tracks[0].steps[12].velocity = 110;

    /* Track 1: Snare — steps 4, 12 (backbeat on 2 and 4) */
    if (p->num_tracks > 1) {
        p->tracks[1].steps[4].velocity  = 127;
        p->tracks[1].steps[12].velocity = 127;
    }

    /* Track 2: HiHat — every other step (8th notes) */
    if (p->num_tracks > 2) {
        for (int s = 0; s < 16; s += 2) {
            p->tracks[2].steps[s].velocity = (s % 4 == 0) ? 100 : 70;
        }
    }

    /* Track 3: Clap — steps 7, 15 (off-beat accents) */
    if (p->num_tracks > 3) {
        p->tracks[3].steps[7].velocity  = 90;
        p->tracks[3].steps[15].velocity = 80;
    }

    printf("\nDemo pattern loaded:\n");
    printf("  Kick:   X . . . X . . . X . . . X . . .\n");
    printf("  Snare:  . . . . X . . . . . . . X . . .\n");
    printf("  HiHat:  X . X . X . X . X . X . X . X .\n");
    printf("  Clap:   . . . . . . . X . . . . . . . X\n");
}

/* ─── Main ────────────────────────────────────────────────────────────────── */

int main(int argc, char *argv[])
{
    printf("0x808 v0.9\n");
    printf("==========================================\n\n");

    /* Initialize engine at 44100 Hz */
    sq_engine_init(&g_engine, 44100);

    /* Load samples */
    if (argc > 1) {
        for (int i = 1; i < argc; i++) {
            load_sample(argv[i]);
        }
    } else {
        const char *default_samples[] = {
            "samples/kicks/kick.wav",
            "samples/snares/snare.wav",
            "samples/hihats/hihat.wav",
            "samples/percussion/clap.wav",
            NULL
        };
        printf("Loading default samples...\n");
        for (int i = 0; default_samples[i] != NULL; i++) {
            load_sample(default_samples[i]);
        }
    }

    if (g_engine.num_samples == 0) {
        fprintf(stderr, "\nNo samples loaded! Provide WAV files as arguments:\n");
        fprintf(stderr, "  ./sequencer_standalone path/to/kick.wav path/to/snare.wav\n");
        sq_engine_shutdown(&g_engine);
        return 1;
    }

    /* Set up the demo pattern */
    setup_demo_pattern();

    /* Set up miniaudio device */
    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format   = ma_format_f32;
    config.playback.channels = 2;
    config.sampleRate        = 44100;
    config.dataCallback      = audio_callback;
    config.pUserData         = NULL;

    ma_device device;
    if (ma_device_init(NULL, &config, &device) != MA_SUCCESS) {
        fprintf(stderr, "Error: failed to initialize audio device\n");
        sq_engine_shutdown(&g_engine);
        return 1;
    }

    if (ma_device_start(&device) != MA_SUCCESS) {
        fprintf(stderr, "Error: failed to start audio device\n");
        ma_device_uninit(&device);
        sq_engine_shutdown(&g_engine);
        return 1;
    }

    printf("\nAudio device: %s (f32, stereo, 44100 Hz)\n", device.playback.name);
    printf("BPM: %.0f\n\n", g_engine.transport.bpm);
    printf("Controls:\n");
    printf("  SPACE = play / stop sequencer\n");
    printf("  1-4   = trigger samples manually\n");
    printf("  - / = = pitch kick down/up one octave\n");
    printf("  [ / ] = BPM -10 / +10\n");
    printf("  q     = quit\n\n");

    /* Disable line buffering for immediate keypress response */
    #ifdef __unix__
    system("stty raw -echo");
    #endif

    int running = 1;
    while (running) {
        int ch = getchar();

        switch (ch) {
        case 'q': case 'Q': case 3: /* Ctrl+C */
            running = 0;
            break;

        case ' ': /* SPACE — toggle play/stop */
            g_engine.transport.playing = !g_engine.transport.playing;
            if (!g_engine.transport.playing) {
                /* Reset position when stopping */
                g_engine.transport.current_beat = 0.0;
                g_engine.transport.sample_position = 0;
                g_engine.transport.current_step = 0;
            }
            /* Can't use printf in raw mode cleanly, but it works for status */
            break;

        case '1': case '2': case '3': case '4':
        case '5': case '6': case '7': case '8': case '9': {
            int idx = ch - '1';
            if ((uint32_t)idx < g_engine.num_samples) {
                sampler_trigger(&g_engine, idx, 1.0f, 0, 0.8f, 0.0f, -1);
            }
            break;
        }

        case '-': /* Pitch kick down one octave */
            if (g_engine.num_samples > 0)
                sampler_trigger(&g_engine, 0, 1.0f, -12, 0.8f, 0.0f, -1);
            break;

        case '=': /* Pitch kick up one octave */
            if (g_engine.num_samples > 0)
                sampler_trigger(&g_engine, 0, 1.0f, 12, 0.8f, 0.0f, -1);
            break;

        case '[': /* BPM down */
            g_engine.transport.bpm -= 10.0;
            if (g_engine.transport.bpm < 20.0) g_engine.transport.bpm = 20.0;
            break;

        case ']': /* BPM up */
            g_engine.transport.bpm += 10.0;
            if (g_engine.transport.bpm > 300.0) g_engine.transport.bpm = 300.0;
            break;
        }
    }

    /* Restore terminal settings */
    #ifdef __unix__
    system("stty sane");
    #endif

    printf("\nShutting down...\n");
    ma_device_uninit(&device);
    sq_engine_shutdown(&g_engine);
    printf("Done.\n");
    return 0;
}
