/*
 * main_gtk.c — GTK 4.0 frontend entry point.
 *
 * Architecture:
 *   Main thread:  GTK main loop (UI rendering + event handling)
 *   Audio thread:  Push-based audio via SDL2 (audio-only init)
 *
 * The GTK frontend uses sq_app for shared app logic (shortcuts, panel state,
 * playhead) and sq_engine for audio/sequencing. SDL2 is used only for audio
 * output — no SDL video, no OpenGL.
 */

#include "gtk_gui.h"

#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <stdatomic.h>
#include <math.h>

#include "engine/sampler.h"
#include "formats/sample_io.h"

#define LOG_TAG "gtk_main"
#include "core/log.h"

/* ─── Global state ────────────────────────────────────────────────────────── */

sq_gtk_state_t g_gtk;
static sq_engine_t g_engine;

/* ─── Audio push thread (same approach as standalone) ─────────────────────── */

#define AUDIO_CHUNK_FRAMES 512
#define AUDIO_TARGET_QUEUED 4096

static atomic_int g_audio_running = 0;

static int audio_push_thread(void *userdata)
{
    SDL_AudioDeviceID dev = *(uint32_t *)userdata;
    float buf[AUDIO_CHUNK_FRAMES * 2];

    LOG_INFO("Audio push thread started");

    while (atomic_load(&g_audio_running)) {
        Uint32 queued = SDL_GetQueuedAudioSize(dev);
        Uint32 queued_frames = queued / (2 * sizeof(float));

        if (queued_frames < AUDIO_TARGET_QUEUED) {
            sq_engine_process(&g_engine, buf, AUDIO_CHUNK_FRAMES);
            SDL_QueueAudio(dev, buf, sizeof(buf));
        } else {
            SDL_Delay(2);
        }
    }

    LOG_INFO("Audio push thread exiting");
    return 0;
}

static int audio_init(void)
{
    if (SDL_Init(SDL_INIT_AUDIO) != 0) {
        LOG_ERROR("SDL_Init(AUDIO) failed: %s", SDL_GetError());
        return -1;
    }

    SDL_AudioSpec want = {0}, have;
    want.freq = 44100;
    want.format = AUDIO_F32SYS;
    want.channels = 2;
    want.samples = AUDIO_CHUNK_FRAMES;

    g_gtk.audio_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (g_gtk.audio_dev == 0) {
        LOG_ERROR("SDL_OpenAudioDevice failed: %s", SDL_GetError());
        return -1;
    }

    g_engine.sample_rate = have.freq;
    SDL_PauseAudioDevice(g_gtk.audio_dev, 0);

    atomic_store(&g_audio_running, 1);
    g_gtk.audio_thread = (void *)SDL_CreateThread(audio_push_thread, "sq_audio",
                                                   &g_gtk.audio_dev);
    LOG_INFO("Audio initialized: %d Hz, %d ch", have.freq, have.channels);
    return 0;
}

static void audio_shutdown(void)
{
    atomic_store(&g_audio_running, 0);
    if (g_gtk.audio_thread) {
        SDL_WaitThread((SDL_Thread *)g_gtk.audio_thread, NULL);
        g_gtk.audio_thread = NULL;
    }
    if (g_gtk.audio_dev) {
        SDL_CloseAudioDevice(g_gtk.audio_dev);
        g_gtk.audio_dev = 0;
    }
    SDL_Quit();
}

/* ─── Sample loading ──────────────────────────────────────────────────────── */

static int load_sample(const char *filepath)
{
    if (g_engine.num_samples >= SQ_MAX_SAMPLES) return -1;
    int idx = (int)g_engine.num_samples;
    if (sample_io_load(filepath, &g_engine.samples[idx]) != 0) {
        LOG_ERROR("Failed to load: %s", filepath);
        return -1;
    }
    g_engine.num_samples++;
    LOG_INFO("Loaded [%d]: %s", idx, g_engine.samples[idx].name);
    return idx;
}

static void load_default_samples(void)
{
    const char *kit[] = {
        "samples/808/01.BD.808.wav",   /* kick */
        "samples/808/01.SD5.808.wav",  /* snare */
        "samples/808/01.CH.808.wav",   /* closed hihat */
        "samples/808/01.CP.808.wav",   /* clap */
        "samples/808/01.OH.808.wav",   /* open hihat */
        "samples/808/01.LT.808.wav",   /* lo tom */
        "samples/808/01.RS.808.wav",   /* rimshot */
        "samples/808/01.HT.808.wav",   /* hi tom */
        NULL
    };
    /* Try relative to CWD first */
    for (int i = 0; kit[i]; i++)
        load_sample(kit[i]);

    /* Fall back to exe directory */
    if (g_engine.num_samples == 0 && g_engine.base_dir[0]) {
        char path[1024];
        for (int i = 0; kit[i]; i++) {
            snprintf(path, sizeof(path), "%s%s", g_engine.base_dir, kit[i]);
            load_sample(path);
        }
    }
}

static void setup_demo_pattern(void)
{
    sq_pattern_t *p = &g_engine.patterns[0];
    uint32_t num_sample_tracks = g_engine.num_samples;
    if (num_sample_tracks > 8) num_sample_tracks = 8;

    p->num_tracks = num_sample_tracks + 2;
    if (p->num_tracks > SQ_MAX_TRACKS) p->num_tracks = SQ_MAX_TRACKS;

    for (uint32_t t = 0; t < num_sample_tracks; t++) {
        p->tracks[t].type = TRACK_SAMPLER;
        p->tracks[t].sample_index = (int)t;
        p->tracks[t].length = 16;
        p->tracks[t].volume = 0.8f;
    }

    uint32_t synth_bass = num_sample_tracks;
    uint32_t synth_pluck = num_sample_tracks + 1;
    if (synth_bass < p->num_tracks) {
        p->tracks[synth_bass].type = TRACK_SYNTH;
        p->tracks[synth_bass].synth_preset = 0;
        p->tracks[synth_bass].length = 16;
        p->tracks[synth_bass].volume = 0.6f;
    }
    if (synth_pluck < p->num_tracks) {
        p->tracks[synth_pluck].type = TRACK_SYNTH;
        p->tracks[synth_pluck].synth_preset = 3;
        p->tracks[synth_pluck].length = 16;
        p->tracks[synth_pluck].volume = 0.5f;
    }

    /* Prefill with Trap 808 drum preset */
    {
        static const uint8_t trap[6][16] = {
            {127,0,0,0,0,0,0,0,0,0,0,0,110,0,0,0},       /* kick: sparse, heavy */
            {0,0,0,0,0,0,0,0,120,0,0,0,0,0,0,0},         /* snare: on 3 */
            {100,60,80,60,100,60,80,100,100,60,80,60,100,80,100,80}, /* hihat: rolls */
            {0,0,0,0,0,0,0,0,110,0,0,0,0,0,0,0},         /* clap: on 3 */
            {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,80},          /* open hat */
            {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}            /* (empty) */
        };
        int applied = 0;
        for (uint32_t t = 0; t < p->num_tracks && applied < 6; t++) {
            if (p->tracks[t].type != TRACK_SAMPLER) continue;
            for (int s = 0; s < 16; s++)
                p->tracks[t].steps[s].velocity = trap[applied][s];
            applied++;
        }
    }

    /* Prefill Trap 808 bass on first synth track (preset 50) */
    if (synth_bass < p->num_tracks) {
        p->tracks[synth_bass].synth_preset = 50;  /* Trap 808 */
        const uint8_t notes[] = {36,0,0,0,0,0,0,0,31,0,0,33,0,0,36,0};
        const uint8_t vels[]  = {120,0,0,0,0,0,0,0,110,0,0,100,0,0,120,0};
        const float   lens[]  = {2,0,0,0,0,0,0,0,2,0,0,1,0,0,2,0};
        for (int s = 0; s < 16; s++) {
            p->tracks[synth_bass].steps[s].note = notes[s];
            p->tracks[synth_bass].steps[s].velocity = vels[s];
            p->tracks[synth_bass].steps[s].length = lens[s];
        }
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
}

/* ─── Crash handler ───────────────────────────────────────────────────────── */

static void crash_handler(int sig)
{
    const char *name = "UNKNOWN";
    switch (sig) {
    case SIGSEGV: name = "SIGSEGV"; break;
    case SIGABRT: name = "SIGABRT"; break;
    case SIGFPE:  name = "SIGFPE";  break;
    case SIGBUS:  name = "SIGBUS";  break;
    }
    fprintf(stderr, "\n*** CRASH: signal %d (%s) ***\n", sig, name);
    fflush(stderr);
    signal(sig, SIG_DFL);
    raise(sig);
}

/* ─── Main ────────────────────────────────────────────────────────────────── */

int main(int argc, char *argv[])
{
    signal(SIGSEGV, crash_handler);
    signal(SIGABRT, crash_handler);
    signal(SIGFPE, crash_handler);
    signal(SIGBUS, crash_handler);

    LOG_INFO("0x808 GTK frontend starting");

    /* Initialize engine */
    sq_engine_init(&g_engine, 44100);
    g_engine.transport.bpm = 120.0;

    /* Determine base directory */
    char *exe_path = SDL_GetBasePath();
    if (exe_path) {
        snprintf(g_engine.base_dir, sizeof(g_engine.base_dir), "%s", exe_path);
        SDL_free(exe_path);
    }

    /* Scan for user themes in {base_dir}/themes/ and ./themes/ */
    if (g_engine.base_dir[0]) {
        char themes_dir[600];
        snprintf(themes_dir, sizeof(themes_dir), "%sthemes", g_engine.base_dir);
        gtk_theme_scan_user_themes(themes_dir);
    }
    if (gtk_theme_num_user_themes() == 0) {
        /* Fallback: try ./themes/ relative to CWD (development builds) */
        gtk_theme_scan_user_themes("themes");
    }

    /* Initialize app state */
    memset(&g_gtk, 0, sizeof(g_gtk));
    g_gtk.engine = &g_engine;
    sq_app_init(&g_gtk.app);

    /* Default: select first synth track (matches ImGui standalone behavior) */

    /* Load samples and set up demo pattern */
    load_default_samples();
    setup_demo_pattern();

    /* Select first synth track by default and show piano roll */
    {
        int pi = g_engine.transport.current_pattern;
        if (pi >= 0 && (uint32_t)pi < g_engine.num_patterns) {
            sq_pattern_t *pat = &g_engine.patterns[pi];
            for (uint32_t t = 0; t < pat->num_tracks; t++) {
                if (pat->tracks[t].type == TRACK_SYNTH) {
                    g_gtk.app.selected_track = (int)t;
                    g_gtk.app.panels[SQ_PANEL_PIANO_ROLL] = true;
                    break;
                }
            }
        }
    }

    /* Initialize audio */
    if (audio_init() != 0) {
        LOG_ERROR("Audio init failed — continuing without audio");
    }

    /* Create GTK application */
    g_gtk.gtk_app = gtk_application_new("com.dcmichael.sequencer808",
                                        G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(g_gtk.gtk_app, "activate",
                     G_CALLBACK(gtk_window_setup), NULL);

    int status = g_application_run(G_APPLICATION(g_gtk.gtk_app), argc, argv);

    /* Cleanup */
    g_object_unref(g_gtk.gtk_app);
    audio_shutdown();
    sq_engine_shutdown(&g_engine);

    LOG_INFO("0x808 GTK frontend exiting (status=%d)", status);
    return status;
}
