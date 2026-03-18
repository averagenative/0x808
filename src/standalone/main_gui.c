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
#include "gui/theme.h"
#include "engine/sq_midi.h"
#include "app/session.h"
#include "formats/project.h"
#include "engine/kits.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <signal.h>
#include <stdatomic.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

/* ─── Graceful shutdown flag (for SIGTERM/SIGINT) ──────────────────────────── */

static volatile int g_shutdown_requested = 0;

static void shutdown_handler(int sig)
{
    (void)sig;
    g_shutdown_requested = 1;
}

/* ─── Crash handler — logs signal and flushes before dying ─────────────────── */

static void crash_handler(int sig)
{
    const char *name = "UNKNOWN";
    switch (sig) {
    case SIGSEGV: name = "SIGSEGV (segmentation fault)"; break;
    case SIGABRT: name = "SIGABRT (abort)"; break;
    case SIGFPE:  name = "SIGFPE (floating point exception)"; break;
#ifndef _WIN32
    case SIGBUS:  name = "SIGBUS (bus error)"; break;
#endif
    }
    fprintf(stderr, "\n*** CRASH: signal %d (%s) ***\n", sig, name);
#ifdef _WIN32
    /* Walk the stack using CaptureStackBackTrace (no extra deps needed) */
    {
        void *frames[32];
        USHORT n = CaptureStackBackTrace(0, 32, frames, NULL);
        fprintf(stderr, "Stack frames (%d):\n", (int)n);
        for (USHORT i = 0; i < n; i++)
            fprintf(stderr, "  [%d] %p\n", i, frames[i]);
    }
#endif
    fflush(stderr);
    fflush(stdout);
    /* Re-raise to get default behavior (core dump / WER report) */
    signal(sig, SIG_DFL);
    raise(sig);
}

/* ─── Global engine state ─────────────────────────────────────────────────── */

static sq_engine_t g_engine;

/* ─── Audio push thread ──────────────────────────────────────────────────── */

#define AUDIO_CHUNK_FRAMES 512   /* frames per push (~11.6ms at 44100) */
#define AUDIO_TARGET_QUEUED 4096 /* keep this many frames queued */

static atomic_int g_audio_running = 0;
static atomic_uint_fast64_t g_audio_push_count = 0;
static SDL_AudioDeviceID g_audio_dev = 0;
static SDL_Thread *g_audio_thread = NULL;

/* Forward declarations for audio restart */
static void audio_stop(void);
static int  audio_start(const char *device_name);
static void audio_restart_callback(void *userdata);

static int audio_push_thread(void *userdata)
{
    SDL_AudioDeviceID dev = *(SDL_AudioDeviceID *)userdata;
    float buf[AUDIO_CHUNK_FRAMES * 2]; /* stereo */

    LOG_INFO("Audio push thread started (chunk=%d frames, target=%d frames queued)",
             AUDIO_CHUNK_FRAMES, AUDIO_TARGET_QUEUED);

    bool was_playing = false;
    while (atomic_load(&g_audio_running)) {
        /* Detect stop: clear SDL audio queue immediately for instant silence */
        bool now_playing = g_engine.transport.playing;
        if (was_playing && !now_playing) {
            SDL_ClearQueuedAudio(dev);
        }
        was_playing = now_playing;

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

/* ─── Audio device management ────────────────────────────────────────────── */

static void audio_stop(void)
{
    if (g_audio_thread) {
        atomic_store(&g_audio_running, 0);
        SDL_WaitThread(g_audio_thread, NULL);
        g_audio_thread = NULL;
    }
    if (g_audio_dev) {
        SDL_CloseAudioDevice(g_audio_dev);
        g_audio_dev = 0;
    }
}

static int audio_start(const char *device_name)
{
    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq     = 44100;
    want.format   = AUDIO_F32SYS;
    want.channels = 2;
    want.samples  = 1024;
    want.callback = NULL;

    /* NULL or empty string = default device */
    const char *dev = (device_name && device_name[0]) ? device_name : NULL;

    g_audio_dev = SDL_OpenAudioDevice(
        dev, 0, &want, &have, SDL_AUDIO_ALLOW_SAMPLES_CHANGE);
    if (g_audio_dev == 0) {
        LOG_ERROR("SDL_OpenAudioDevice failed: %s", SDL_GetError());
        return -1;
    }

    LOG_INFO("SDL2 Audio: device=%s, freq=%d, channels=%d, samples=%d",
             dev ? dev : "(default)", have.freq, have.channels, have.samples);

    /* Update engine sample rate if device reports different */
    if ((uint32_t)have.freq != g_engine.sample_rate) {
        LOG_INFO("Updating engine sample rate: %u -> %d",
                 g_engine.sample_rate, have.freq);
        g_engine.sample_rate = (uint32_t)have.freq;
    }

    SDL_PauseAudioDevice(g_audio_dev, 0);

    /* Start push thread (it will fill the queue) */
    atomic_store(&g_audio_running, 1);
    g_audio_thread = SDL_CreateThread(
        audio_push_thread, "audio_push", &g_audio_dev);
    if (!g_audio_thread) {
        LOG_ERROR("Failed to create audio thread: %s", SDL_GetError());
        return -1;
    }

    return 0;
}

static void audio_restart_callback(void *userdata)
{
    (void)userdata;
    LOG_INFO("Audio restart requested");
    audio_stop();

    /* Read device name from g_app via gui — but we don't have direct access.
     * The sq_app audio_config is set by the settings panel before calling this.
     * We need to get it. Use a simple extern or pass it through userdata. */

    /* We'll use the extern g_engine — the settings panel stores config in sq_app
     * which is in gui.cpp. We can access it via a gui function. For simplicity,
     * check SDL device list for the device_index. */

    /* For now, use the device name stored in the callback userdata
     * (which is a pointer to the app's audio_config) */

    /* Actually, let's just enumerate and pick by index */
    extern int gui_get_audio_device_index(void);
    extern const char *gui_get_audio_device_name(void);

    const char *name = gui_get_audio_device_name();
    if (audio_start(name) != 0) {
        LOG_ERROR("Failed to restart audio, trying default");
        audio_start(NULL);
    }
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

    /* Two synth tracks: 808 sub bass + dark pad for atmosphere */
    uint32_t synth_bass = num_sample_tracks;
    uint32_t synth_pad = num_sample_tracks + 1;
    if (synth_bass < p->num_tracks) {
        p->tracks[synth_bass].type = TRACK_SYNTH;
        p->tracks[synth_bass].synth_preset = 50;  /* Trap 808 sub */
        p->tracks[synth_bass].length = 16;
        p->tracks[synth_bass].volume = 0.85f;
    }
    if (synth_pad < p->num_tracks) {
        p->tracks[synth_pad].type = TRACK_SYNTH;
        p->tracks[synth_pad].synth_preset = 52;  /* Dark Pad */
        p->tracks[synth_pad].length = 16;
        p->tracks[synth_pad].volume = 0.35f;
    }

    /* ── Trap 808 drums ── */
    {
        static const uint8_t trap[6][16] = {
            {127,0,0,0,0,0,0,0,0,0,0,0,110,0,0,0},
            {0,0,0,0,0,0,0,0,127,0,0,0,0,0,0,0},
            {100,50,70,50,100,50,70,90,100,50,70,50,100,70,90,100},
            {0,0,0,0,0,0,0,0,110,0,0,0,0,0,0,0},
            {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,70},
            {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}
        };
        int applied = 0;
        for (uint32_t t = 0; t < p->num_tracks && applied < 6; t++) {
            if (p->tracks[t].type != TRACK_SAMPLER) continue;
            for (int s = 0; s < 16; s++)
                p->tracks[t].steps[s].velocity = trap[applied][s];
            applied++;
        }
    }

    /* ── 808 sub bass: ONE deep note sustaining the whole bar ── */
    if (synth_bass < p->num_tracks) {
        p->tracks[synth_bass].steps[0].note = 24;      /* C1 — audible on consumer headphones */
        p->tracks[synth_bass].steps[0].velocity = 127;
        p->tracks[synth_bass].steps[0].length = 14.0f;  /* sustains 14 steps */
    }

    /* ── Dark pad: Eb2 hit for ominous atmosphere ── */
    if (synth_pad < p->num_tracks) {
        p->tracks[synth_pad].steps[0].note = 39;       /* Eb2 */
        p->tracks[synth_pad].steps[0].velocity = 80;
        p->tracks[synth_pad].steps[0].length = 0.0f;   /* ADSR handles duration — no note-off system yet */
    }

    g_engine.transport.bpm = 145.0;
    snprintf(p->name, SQ_PATTERN_NAME_LEN, "Pattern 1");
    g_engine.num_patterns = 5;
    g_engine.transport.current_pattern = 0;

    /* Initialize patterns 2-5 with tracks but no steps */
    for (int i = 1; i < 5; i++) {
        sq_pattern_t *pp = &g_engine.patterns[i];
        pp->num_tracks = p->num_tracks;
        for (uint32_t t = 0; t < pp->num_tracks; t++) {
            pp->tracks[t].type = p->tracks[t].type;
            pp->tracks[t].sample_index = p->tracks[t].sample_index;
            pp->tracks[t].synth_preset = p->tracks[t].synth_preset;
            pp->tracks[t].length = 16;
            pp->tracks[t].volume = p->tracks[t].volume;
        }
        snprintf(pp->name, SQ_PATTERN_NAME_LEN, "Pattern %d", i + 1);
    }

    /* Debug: log what we set up */
    LOG_WARN("Demo pattern: %u tracks, BPM=%.0f", p->num_tracks, g_engine.transport.bpm);
    for (uint32_t t = 0; t < p->num_tracks; t++) {
        sq_track_t *trk = &p->tracks[t];
        if (trk->type == TRACK_SYNTH) {
            const char *pname = (trk->synth_preset >= 0 &&
                (uint32_t)trk->synth_preset < g_engine.num_synth_presets)
                ? g_engine.synth_presets[trk->synth_preset].name : "?";
            for (int s = 0; s < 16; s++) {
                if (trk->steps[s].velocity > 0) {
                    LOG_WARN("  Track %u: Synth preset=%d (%s), step[%d] note=%d vel=%d len=%.1f",
                             t, trk->synth_preset, pname, s,
                             trk->steps[s].note, trk->steps[s].velocity,
                             trk->steps[s].length);
                }
            }
        }
    }
}

/* ─── Main ────────────────────────────────────────────────────────────────── */

int main(int argc, char *argv[])
{
#ifdef _WIN32
    /* Redirect stderr to a log file so we can read it after a crash */
    freopen("0x808_log.txt", "w", stderr);
#endif

    /* Install crash handlers so we get stack info in the log */
    signal(SIGSEGV, crash_handler);
    signal(SIGABRT, crash_handler);
    signal(SIGFPE,  crash_handler);
#ifndef _WIN32
    signal(SIGBUS, crash_handler);
#endif

    /* Graceful shutdown on SIGTERM/SIGINT (Linux/macOS kill, Ctrl+C) */
    signal(SIGTERM, shutdown_handler);
    signal(SIGINT,  shutdown_handler);

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
#elif defined(__APPLE__)
        /* On macOS, use _NSGetExecutablePath to find the binary */
        char exe_path[512];
        uint32_t path_size = sizeof(exe_path);
        if (_NSGetExecutablePath(exe_path, &path_size) == 0) {
            /* Resolve symlinks */
            char real_path[512];
            if (realpath(exe_path, real_path))
                strncpy(exe_path, real_path, sizeof(exe_path) - 1);
            char *last_sep = strrchr(exe_path, '/');
            if (last_sep) {
                last_sep[1] = '\0';
                snprintf(base_dir, sizeof(base_dir), "%s", exe_path);
            }
        }
#else
        /* On Linux, use /proc/self/exe */
        ssize_t rlen = readlink("/proc/self/exe", base_dir, sizeof(base_dir) - 1);
        if (rlen > 0) {
            base_dir[rlen] = '\0';
            char *last_sep = strrchr(base_dir, '/');
            if (last_sep) last_sep[1] = '\0';
        }
#endif
        LOG_INFO("Base directory: %s", base_dir[0] ? base_dir : "(CWD)");
        snprintf(g_engine.base_dir, sizeof(g_engine.base_dir), "%s", base_dir);
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
#ifdef __APPLE__
        /* macOS .app bundle: samples are in Contents/Resources/.
         * Resolve with realpath() so stored filepaths have no ".."
         * components — otherwise path_is_safe() rejects them on reload. */
        if (g_engine.num_samples == 0 && base_dir[0]) {
            LOG_INFO("Trying macOS .app bundle Resources directory...");
            for (int i = 0; kit_808[i]; i++) {
                char full_path[1024];
                snprintf(full_path, sizeof(full_path), "%s../Resources/%s",
                         base_dir, kit_808[i]);
                char resolved[1024];
                if (realpath(full_path, resolved))
                    load_sample(resolved);
                else
                    load_sample(full_path);
            }
        }
#endif
    }

    if (g_engine.num_samples == 0) {
        LOG_WARN("No samples loaded — running with synth tracks only");
    }

    /* Always set up demo pattern as a fallback.
     * If a saved project exists, it will be loaded after gui_init
     * and overwrite the demo pattern. */
    int session_win_w = 1280, session_win_h = 720, session_theme = 0;
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
    if (gui_init(session_win_w, session_win_h, "0x808") != 0) {
        LOG_ERROR("Failed to initialize GUI");
        sq_engine_shutdown(&g_engine);
        return 1;
    }

    /* Scan for user JSON themes in {base_dir}/themes/ */
    {
        char themes_dir[600];
        snprintf(themes_dir, sizeof(themes_dir), "%sthemes", base_dir);
        theme_scan_user_themes(themes_dir);
#ifdef __APPLE__
        /* Also check .app bundle Resources/themes/ */
        snprintf(themes_dir, sizeof(themes_dir), "%s../Resources/themes", base_dir);
        theme_scan_user_themes(themes_dir);
#endif
    }

    /* Load session state into gui's app */
    {
        sq_app_t *app = (sq_app_t *)gui_get_app();
        char last_project[512] = {0};
        if (app) {
            sq_session_load(app, &g_engine,
                            NULL, NULL, &session_theme,
                            last_project, sizeof(last_project));
            /* Sync tooltip global from session */
            g_tooltips_enabled = app->show_tooltips ? 1 : 0;
        }

        /* Auto-load: autosave first (most recent state), then explicit project as fallback */
        bool loaded = false;

        /* 1. Try autosave.sqproj (always has the latest state) */
        {
            const char *autosave = sq_session_autosave_path();
            FILE *check = fopen(autosave, "rb");
            if (check) {
                fclose(check);
                LOG_INFO("Loading autosave: %s", autosave);
                if (project_load(&g_engine, autosave) == 0) {
                    LOG_INFO("Autosave loaded successfully");
                    loaded = true;
                    /* Remember the explicit project path for Ctrl+S */
                    if (last_project[0])
                        gui_set_project_path(last_project);
                } else {
                    LOG_WARN("Failed to parse autosave");
                }
            }
        }

        /* 2. Fallback: try explicit project file if no autosave */
        if (!loaded && last_project[0]) {
            FILE *check = fopen(last_project, "rb");
            if (check) {
                fclose(check);
                LOG_INFO("Loading project (no autosave): %s", last_project);
                if (project_load(&g_engine, last_project) == 0) {
                    gui_set_project_path(last_project);
                    LOG_INFO("Project loaded successfully");
                    loaded = true;
                }
            }
        }

        if (!loaded)
            LOG_INFO("No saved state found — keeping demo pattern");
    }

    /* Restore kit from session — reload samples to ensure they're valid */
    if (sq_current_kit >= 0 && sq_current_kit < SQ_NUM_KITS) {
        /* Check if any samples are missing data (placeholder from failed load) */
        bool need_reload = false;
        for (uint32_t i = 0; i < g_engine.num_samples; i++) {
            if (!g_engine.samples[i].data) { need_reload = true; break; }
        }
        if (need_reload || sq_current_kit > 0) {
            LOG_INFO("Restoring kit %d from session", sq_current_kit);
            sq_kit_load(&g_engine, sq_current_kit, base_dir);
        }
    }

    /* Apply session theme */
    if (session_theme > 0)
        theme_apply(session_theme);

    /* Open SDL2 audio device */
    if (audio_start(NULL) != 0) {
        LOG_ERROR("Failed to initialize audio");
        gui_shutdown();
        sq_engine_shutdown(&g_engine);
        return 1;
    }

    /* Trigger test beep */
    audio_test_beep();

    /* Register audio restart callback for settings panel */
    gui_set_audio_restart(audio_restart_callback, NULL);

    /* Initialize MIDI input */
    sq_midi_t *midi = sq_midi_init(&g_engine.cmd_queue, 0);
    if (midi) {
        sq_app_t *app = (sq_app_t *)gui_get_app();

        /* Try to reconnect to session-saved MIDI device */
        bool connected = false;
        if (app && app->midi_device_name[0]) {
            int ports = sq_midi_get_port_count(midi);
            for (int i = 0; i < ports; i++) {
                const char *name = sq_midi_get_port_name(midi, i);
                if (name && strstr(name, app->midi_device_name)) {
                    sq_midi_open_port(midi, i);
                    app->midi_port_index = i;
                    LOG_WARN("MIDI: reconnected to saved device: %s", name);
                    connected = true;
                    break;
                }
            }
        }

        /* Fallback: auto-open first available port */
        if (!connected) {
            int ports = sq_midi_get_port_count(midi);
            if (ports > 0) {
                sq_midi_open_port(midi, 0);
                const char *name = sq_midi_get_port_name(midi, 0);
                LOG_WARN("MIDI: auto-opened port 0 (%s)", name);
                if (app) {
                    app->midi_port_index = 0;
                    snprintf(app->midi_device_name, SQ_DEVICE_NAME_LEN, "%s", name);
                }
            }
        }
        gui_set_midi(midi);
    }

    /* ── Main loop ────────────────────────────────────────────────────── */
    LOG_INFO("Entering main loop");
    uint64_t last_diag_count = 0;
    double   last_diag_ms    = sq_log_elapsed_ms();
    double   last_autosave_ms = last_diag_ms;

    while (!gui_frame(&g_engine) && !g_shutdown_requested) {
        /* Periodic autosave (every 5 minutes) */
        double now_ms = sq_log_elapsed_ms();
        if (now_ms - last_autosave_ms > 300000.0) { /* 5 min = 300000 ms */
            last_autosave_ms = now_ms;
            const char *autosave = sq_session_autosave_path();
            if (project_save(&g_engine, autosave) == 0) {
                LOG_INFO("Periodic autosave to %s", autosave);
                gui_trigger_autosave_indicator();
            }
        }
        if (now_ms - last_diag_ms > 3000.0) {
            uint64_t count = atomic_load(&g_audio_push_count);
            uint64_t delta = count - last_diag_count;
            Uint32 queued = SDL_GetQueuedAudioSize(g_audio_dev);
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

    /* Stop audio and MIDI */
    /* Auto-save current state before shutdown */
    {
        sq_app_t *app = (sq_app_t *)gui_get_app();
        int cur_w = g_win_width, cur_h = g_win_height;
        int cur_theme = theme_get_current();

        /* Always auto-save the project to session dir so work isn't lost */
        const char *autosave = sq_session_autosave_path();
        if (project_save(&g_engine, autosave) == 0)
            LOG_INFO("Auto-saved project to %s", autosave);

        /* Save session with autosave as fallback project path */
        const char *proj_path = gui_get_project_path();
        if (!proj_path || !proj_path[0])
            proj_path = autosave;
        sq_session_save(app, &g_engine, cur_w, cur_h, cur_theme, proj_path);
    }

    audio_stop();
    sq_midi_shutdown(midi);

    /* Clean up */
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
