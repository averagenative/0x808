/*
 * main_gui.c — Standalone GUI application entry point.
 *
 * Architecture:
 *   Main thread:    SDL2 event loop + Nuklear GUI rendering
 *   Audio thread:   Dedicated thread pushes audio via SDL_QueueAudio()
 *
 * Why push-based audio?
 *   WSLg's PulseAudio RDP sink doesn't drive pull-based callbacks at the
 *   correct rate (~1/sec instead of ~43/sec). Push-based audio lets us
 *   control the timing ourselves — we generate audio in chunks and queue
 *   it faster than it's consumed.
 */

#define LOG_TAG "main"
#include "core/log.h"
#include "engine/engine.h"
#include "engine/sampler.h"
#include "formats/sample_io.h"
#include "gui/gui.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdatomic.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

/* ─── Global engine state ─────────────────────────────────────────────────── */

static sq_engine_t g_engine;

/* ─── Audio push thread ──────────────────────────────────────────────────── */

#define AUDIO_CHUNK_FRAMES 512   /* frames per push (~11.6ms at 44100) */
#define AUDIO_TARGET_QUEUED 4096 /* keep this many frames queued */

static atomic_int g_audio_running = 0;
static atomic_uint_fast64_t g_audio_push_count = 0;

static int audio_push_thread(void *userdata)
{
    SDL_AudioDeviceID dev = *(SDL_AudioDeviceID *)userdata;
    float buf[AUDIO_CHUNK_FRAMES * 2]; /* stereo */

    LOG_INFO("Audio push thread started (chunk=%d frames, target=%d frames queued)",
             AUDIO_CHUNK_FRAMES, AUDIO_TARGET_QUEUED);

    while (atomic_load(&g_audio_running)) {
        /* Check how much is already queued */
        Uint32 queued = SDL_GetQueuedAudioSize(dev);
        Uint32 queued_frames = queued / (2 * sizeof(float));

        if (queued_frames < AUDIO_TARGET_QUEUED) {
            /* Generate and push a chunk */
            sq_engine_process(&g_engine, buf, AUDIO_CHUNK_FRAMES);

            if (SDL_QueueAudio(dev, buf, sizeof(buf)) != 0) {
                /* Don't spam logs on error */
                static int err_count = 0;
                if (err_count++ < 5)
                    LOG_ERROR("SDL_QueueAudio failed: %s", SDL_GetError());
            }
            atomic_fetch_add(&g_audio_push_count, 1);
        } else {
            /* Queue is full enough — sleep a bit to avoid busy-waiting */
            SDL_Delay(2);
        }
    }

    LOG_INFO("Audio push thread exiting");
    return 0;
}

/* ─── Audio test: trigger kick sample to verify output works ─────────────── */

static void audio_test_beep(void)
{
    if (g_engine.num_samples > 0) {
        for (int v = 0; v < SQ_MAX_VOICES; v++) {
            if (!g_engine.voices[v].active) {
                g_engine.voices[v].active       = true;
                g_engine.voices[v].sample_index  = 0;
                g_engine.voices[v].position      = 0.0;
                g_engine.voices[v].rate          = 1.0;
                g_engine.voices[v].velocity      = 0.8f;
                g_engine.voices[v].volume        = 0.8f;
                g_engine.voices[v].pan           = 0.0f;
                g_engine.voices[v].start_time    = 0;
                LOG_INFO("Audio test: triggered kick on voice %d", v);
                break;
            }
        }
    }
}

/* ─── Load a sample ───────────────────────────────────────────────────────── */

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

/* ─── Set up demo pattern ─────────────────────────────────────────────────── */

static void setup_demo_pattern(void)
{
    sq_pattern_t *p = &g_engine.patterns[0];

    /* Set up sampler tracks from loaded samples */
    uint32_t num_sample_tracks = g_engine.num_samples;
    if (num_sample_tracks > 8) num_sample_tracks = 8;

    /* Total tracks: sample tracks + 2 synth tracks */
    p->num_tracks = num_sample_tracks + 2;
    if (p->num_tracks > SQ_MAX_TRACKS) p->num_tracks = SQ_MAX_TRACKS;

    for (uint32_t t = 0; t < num_sample_tracks; t++) {
        p->tracks[t].type = TRACK_SAMPLER;
        p->tracks[t].sample_index = (int)t;
        p->tracks[t].length = 16;
        p->tracks[t].volume = 0.8f;
    }

    /* Synth tracks at the end */
    uint32_t synth_bass = num_sample_tracks;
    uint32_t synth_pluck = num_sample_tracks + 1;
    if (synth_bass < p->num_tracks) {
        p->tracks[synth_bass].type = TRACK_SYNTH;
        p->tracks[synth_bass].synth_preset = 0; /* Bass */
        p->tracks[synth_bass].length = 16;
        p->tracks[synth_bass].volume = 0.6f;
    }
    if (synth_pluck < p->num_tracks) {
        p->tracks[synth_pluck].type = TRACK_SYNTH;
        p->tracks[synth_pluck].synth_preset = 3; /* Pluck */
        p->tracks[synth_pluck].length = 16;
        p->tracks[synth_pluck].volume = 0.5f;
    }

    /* --- Demo beat pattern --- */

    /* Track 0 (Kick): four-on-the-floor */
    p->tracks[0].steps[0].velocity  = 120;
    p->tracks[0].steps[4].velocity  = 110;
    p->tracks[0].steps[8].velocity  = 120;
    p->tracks[0].steps[12].velocity = 110;

    /* Track 1 (Snare): backbeat */
    if (num_sample_tracks > 1) {
        p->tracks[1].steps[4].velocity  = 127;
        p->tracks[1].steps[12].velocity = 127;
    }

    /* Track 2 (Closed HiHat): 8th notes */
    if (num_sample_tracks > 2) {
        for (int s = 0; s < 16; s += 2)
            p->tracks[2].steps[s].velocity = (s % 4 == 0) ? 100 : 70;
    }

    /* Track 3 (Clap): off-beat accents */
    if (num_sample_tracks > 3) {
        p->tracks[3].steps[7].velocity  = 90;
        p->tracks[3].steps[15].velocity = 80;
    }

    /* Track 4 (Open HiHat): sparse accents */
    if (num_sample_tracks > 4) {
        p->tracks[4].steps[3].velocity  = 80;
        p->tracks[4].steps[11].velocity = 80;
        p->tracks[4].volume = 0.5f;
    }

    /* Track 5 (Cowbell): disco pattern */
    if (num_sample_tracks > 5) {
        p->tracks[5].steps[0].velocity  = 60;
        p->tracks[5].steps[2].velocity  = 60;
        p->tracks[5].steps[4].velocity  = 60;
        p->tracks[5].steps[6].velocity  = 60;
        p->tracks[5].steps[8].velocity  = 60;
        p->tracks[5].steps[10].velocity = 60;
        p->tracks[5].steps[12].velocity = 60;
        p->tracks[5].steps[14].velocity = 60;
        p->tracks[5].volume = 0.35f;
    }

    /* Synth Bass: root notes */
    if (synth_bass < p->num_tracks) {
        p->tracks[synth_bass].steps[0].velocity = 100;
        p->tracks[synth_bass].steps[0].note = 36;  /* C2 */
        p->tracks[synth_bass].steps[4].velocity = 80;
        p->tracks[synth_bass].steps[4].note = 36;
        p->tracks[synth_bass].steps[8].velocity = 100;
        p->tracks[synth_bass].steps[8].note = 39;  /* D#2 */
        p->tracks[synth_bass].steps[12].velocity = 80;
        p->tracks[synth_bass].steps[12].note = 43; /* G2 */
    }

    /* Synth Pluck: melodic accents */
    if (synth_pluck < p->num_tracks) {
        p->tracks[synth_pluck].steps[2].velocity = 90;
        p->tracks[synth_pluck].steps[2].note = 60;  /* C4 */
        p->tracks[synth_pluck].steps[6].velocity = 70;
        p->tracks[synth_pluck].steps[6].note = 63;  /* D#4 */
        p->tracks[synth_pluck].steps[10].velocity = 90;
        p->tracks[synth_pluck].steps[10].note = 67; /* G4 */
        p->tracks[synth_pluck].steps[14].velocity = 70;
        p->tracks[synth_pluck].steps[14].note = 65; /* F4 */
    }
}

/* ─── Main ────────────────────────────────────────────────────────────────── */

int main(int argc, char *argv[])
{
#ifdef _WIN32
    /* Redirect stderr to a log file so we can read it after a crash */
    freopen("0x808_log.txt", "w", stderr);
#endif

    sq_log_init();

    LOG_INFO("0x808 v0.9");
    LOG_INFO("================================");

    /* Initialize engine */
    LOG_INFO("Initializing engine...");
    sq_engine_init(&g_engine, 44100);

    /* Determine base directory (where the exe lives) for resolving sample paths */
    char base_dir[512] = "";
    {
#ifdef _WIN32
        /* On Windows, find directory of exe from argv[0] or GetModuleFileName */
        char exe_path[512];
        DWORD len = GetModuleFileNameA(NULL, exe_path, sizeof(exe_path));
        if (len > 0 && len < sizeof(exe_path)) {
            /* Strip filename to get directory */
            char *last_sep = strrchr(exe_path, '\\');
            if (!last_sep) last_sep = strrchr(exe_path, '/');
            if (last_sep) {
                size_t dir_len = (size_t)(last_sep - exe_path + 1);
                if (dir_len < sizeof(base_dir)) {
                    memcpy(base_dir, exe_path, dir_len);
                    base_dir[dir_len] = '\0';
                }
            }
        }
#else
        /* On Unix, try /proc/self/exe or just use CWD */
        ssize_t rlen = readlink("/proc/self/exe", base_dir, sizeof(base_dir) - 1);
        if (rlen > 0) {
            base_dir[rlen] = '\0';
            char *last_sep = strrchr(base_dir, '/');
            if (last_sep) last_sep[1] = '\0';
        }
#endif
        LOG_INFO("Base directory: %s", base_dir[0] ? base_dir : "(CWD)");
    }

    /* Load samples */
    LOG_INFO("Loading samples...");
    if (argc > 1) {
        for (int i = 1; i < argc; i++)
            load_sample(argv[i]);
    } else {
        /* Try 808 kit first, fall back to legacy paths */
        const char *kit_808[] = {
            "samples/808/01.BD.808.wav",   /* kick */
            "samples/808/01.SD5.808.wav",  /* snare */
            "samples/808/01.CH.808.wav",   /* closed hihat */
            "samples/808/01.CP.808.wav",   /* clap */
            "samples/808/01.OH.808.wav",   /* open hihat */
            "samples/808/01.CB.808.wav",   /* cowbell */
            "samples/808/01.RS.808.wav",   /* rimshot */
            "samples/808/01.HT.808.wav",   /* hi tom */
            NULL
        };
        /* Try relative to CWD first, then relative to exe directory */
        for (int i = 0; kit_808[i]; i++)
            load_sample(kit_808[i]);

        if (g_engine.num_samples == 0 && base_dir[0]) {
            LOG_INFO("Trying samples relative to exe directory...");
            for (int i = 0; kit_808[i]; i++) {
                char full_path[1024];
                snprintf(full_path, sizeof(full_path), "%s%s", base_dir, kit_808[i]);
                load_sample(full_path);
            }
        }
    }

    if (g_engine.num_samples == 0) {
        LOG_WARN("No samples loaded — running with synth tracks only");
    }

    setup_demo_pattern();
    LOG_INFO("Demo pattern loaded");

    /* Auto-select first synth track so keyboard/editor work immediately */
    {
        sq_pattern_t *p = &g_engine.patterns[0];
        for (uint32_t t = 0; t < p->num_tracks; t++) {
            if (p->tracks[t].type == TRACK_SYNTH) {
                g_selected_track = (int)t;
                LOG_INFO("Auto-selected synth track %u", t);
                break;
            }
        }
    }

    /* Initialize GUI (also initializes SDL2 with VIDEO + AUDIO) */
    LOG_INFO("Initializing GUI...");
    if (gui_init(1280, 720, "0x808") != 0) {
        LOG_ERROR("Failed to initialize GUI");
        sq_engine_shutdown(&g_engine);
        return 1;
    }

    /* Open SDL2 audio device in queue mode (no callback) */
    LOG_INFO("Opening SDL2 audio device (push mode)...");
    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq     = 44100;
    want.format   = AUDIO_F32SYS;
    want.channels = 2;
    want.samples  = 1024;
    want.callback = NULL;  /* NULL = queue/push mode */

    SDL_AudioDeviceID audio_dev = SDL_OpenAudioDevice(
        NULL, 0, &want, &have, SDL_AUDIO_ALLOW_SAMPLES_CHANGE);
    if (audio_dev == 0) {
        LOG_ERROR("SDL_OpenAudioDevice failed: %s", SDL_GetError());
        gui_shutdown();
        sq_engine_shutdown(&g_engine);
        return 1;
    }

    LOG_INFO("SDL2 Audio: freq=%d, channels=%d, samples=%d, format=0x%04X",
             have.freq, have.channels, have.samples, have.format);

    /* Unpause audio (SDL2 starts paused) */
    SDL_PauseAudioDevice(audio_dev, 0);

    /* Pre-fill the audio queue to avoid initial silence */
    {
        float prebuf[AUDIO_CHUNK_FRAMES * 2];
        for (int i = 0; i < 8; i++) {
            sq_engine_process(&g_engine, prebuf, AUDIO_CHUNK_FRAMES);
            SDL_QueueAudio(audio_dev, prebuf, sizeof(prebuf));
        }
        LOG_INFO("Pre-filled audio queue with %d frames",
                 AUDIO_CHUNK_FRAMES * 8);
    }

    /* Trigger test beep */
    audio_test_beep();

    /* Start audio push thread */
    atomic_store(&g_audio_running, 1);
    SDL_Thread *audio_thread = SDL_CreateThread(
        audio_push_thread, "audio_push", &audio_dev);
    if (!audio_thread) {
        LOG_ERROR("Failed to create audio thread: %s", SDL_GetError());
    }

    /* ── Main loop ────────────────────────────────────────────────────── */
    LOG_INFO("Entering main loop");
    uint64_t last_diag_count = 0;
    double   last_diag_ms    = sq_log_elapsed_ms();

    while (!gui_frame(&g_engine)) {
        /* Periodic audio diagnostics (every ~3 seconds) */
        double now_ms = sq_log_elapsed_ms();
        if (now_ms - last_diag_ms > 3000.0) {
            uint64_t count = atomic_load(&g_audio_push_count);
            uint64_t delta = count - last_diag_count;
            Uint32 queued = SDL_GetQueuedAudioSize(audio_dev);
            Uint32 queued_frames = queued / (2 * sizeof(float));

            int active_voices = 0;
            for (int i = 0; i < SQ_MAX_VOICES; i++)
                if (g_engine.voices[i].active) active_voices++;
            int active_synth = 0;
            for (int i = 0; i < SQ_MAX_SYNTH_VOICES; i++)
                if (g_engine.synth_voices[i].active) active_synth++;

            LOG_DEBUG("Audio diag: %lu pushes (%.0f/sec), queued=%u frames, "
                      "sampler=%d/%d, synth=%d/%d, playing=%s",
                      (unsigned long)delta,
                      (double)delta / ((now_ms - last_diag_ms) / 1000.0),
                      queued_frames,
                      active_voices, SQ_MAX_VOICES,
                      active_synth, SQ_MAX_SYNTH_VOICES,
                      g_engine.transport.playing ? "YES" : "no");
            last_diag_count = count;
            last_diag_ms    = now_ms;
        }
    }

    /* Stop audio thread */
    atomic_store(&g_audio_running, 0);
    if (audio_thread) {
        SDL_WaitThread(audio_thread, NULL);
    }

    /* Clean up */
    SDL_CloseAudioDevice(audio_dev);
    gui_shutdown();
    sq_engine_shutdown(&g_engine);

    LOG_INFO("Done.");
    return 0;
}

/* ─── Windows entry point ────────────────────────────────────────────────── */

#ifdef _WIN32
#include <windows.h>
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                   LPSTR lpCmdLine, int nCmdShow)
{
    (void)hInstance; (void)hPrevInstance; (void)lpCmdLine; (void)nCmdShow;
    return main(__argc, __argv);
}
#endif
