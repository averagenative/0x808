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
#include "engine/effects.h"

#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <stdatomic.h>
#include <math.h>

#include "engine/sampler.h"
#include "engine/sq_midi.h"
#include "app/session.h"
#include "formats/project.h"
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

/* Forward declarations */
static int  audio_open_device(const char *device_name);
static void audio_shutdown(void);

static void gtk_audio_restart_callback(void *userdata)
{
    (void)userdata;
    LOG_INFO("GTK audio restart requested");

    /* Stop recording if active */
    if (g_engine.recorder.state == SQ_REC_ACTIVE) {
        sq_recorder_stop(&g_engine.recorder);
    }
    g_engine.transport.playing = false;

    audio_shutdown();

    const char *name = g_gtk.app.audio_config.device_name;
    if (audio_open_device(name) != 0) {
        LOG_ERROR("Failed to restart audio, trying default");
        audio_open_device(NULL);
    }
}

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
    return audio_open_device(NULL);
}

static int audio_open_device(const char *device_name)
{
    SDL_AudioSpec want = {0}, have;
    want.freq = 44100;
    want.format = AUDIO_F32SYS;
    want.channels = 2;
    want.samples = AUDIO_CHUNK_FRAMES;

    const char *dev = (device_name && device_name[0]) ? device_name : NULL;

    g_gtk.audio_dev = SDL_OpenAudioDevice(dev, 0, &want, &have, 0);
    if (g_gtk.audio_dev == 0) {
        LOG_ERROR("SDL_OpenAudioDevice failed: %s", SDL_GetError());
        return -1;
    }

    g_engine.sample_rate = have.freq;
    SDL_PauseAudioDevice(g_gtk.audio_dev, 0);

    /* Pre-fill queue */
    float prebuf[AUDIO_CHUNK_FRAMES * 2];
    for (int i = 0; i < 8; i++) {
        sq_engine_process(&g_engine, prebuf, AUDIO_CHUNK_FRAMES);
        SDL_QueueAudio(g_gtk.audio_dev, prebuf, sizeof(prebuf));
    }

    atomic_store(&g_audio_running, 1);
    g_gtk.audio_thread = (void *)SDL_CreateThread(audio_push_thread, "sq_audio",
                                                   &g_gtk.audio_dev);
    LOG_INFO("Audio initialized: device=%s, %d Hz, %d ch",
             dev ? dev : "(default)", have.freq, have.channels);
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

    /* ── Trap 808 drums: sparse, heavy, hi-hat rolls with dynamics ── */
    {
        static const uint8_t trap[6][16] = {
            {127,0,0,0,0,0,0,0,0,0,0,0,110,0,0,0},        /* kick: 1 and &-of-4 */
            {0,0,0,0,0,0,0,0,127,0,0,0,0,0,0,0},          /* snare: beat 3 */
            {100,50,70,50,100,50,70,90,100,50,70,50,100,70,90,100}, /* hats: velocity rolls */
            {0,0,0,0,0,0,0,0,110,0,0,0,0,0,0,0},          /* clap: doubles snare */
            {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,70},           /* open hat: bar end */
            {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}             /* (empty) */
        };
        int applied = 0;
        for (uint32_t t = 0; t < p->num_tracks && applied < 6; t++) {
            if (p->tracks[t].type != TRACK_SAMPLER) continue;
            for (int s = 0; s < 16; s++)
                p->tracks[t].steps[s].velocity = trap[applied][s];
            applied++;
        }
    }

    /* ── 808 sub bass: ONE long note per bar, deep as possible ──
     * Single C1 (MIDI 24) sustaining the entire bar.
     * This is what makes trap FEEL heavy — one massive sub note. */
    if (synth_bass < p->num_tracks) {
        const uint8_t notes[] = {24,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};  /* C1 */
        const uint8_t vels[]  = {127,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
        const float   lens[]  = {14,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};  /* sustains 14 steps */
        for (int s = 0; s < 16; s++) {
            p->tracks[synth_bass].steps[s].note = notes[s];
            p->tracks[synth_bass].steps[s].velocity = vels[s];
            p->tracks[synth_bass].steps[s].length = lens[s];
        }
        /* Tape saturation for analog warmth */
        p->tracks[synth_bass].effects[0].type = EFFECT_TAPE;
        p->tracks[synth_bass].effects[0].tape.drive = 0.4f;
        p->tracks[synth_bass].effects[0].tape.warmth = 0.7f;
        p->tracks[synth_bass].effects[0].tape.mix = 0.4f;
    }

    /* ── Dark pad: single sustained minor chord tone for atmosphere ──
     * One long Eb note (MIDI 39 = Eb2) — minor third over the C bass,
     * creates dark/ominous mood. Length 12 steps — leaves 4 steps for release. */
    if (synth_pad < p->num_tracks) {
        const uint8_t notes[] = {39,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};  /* Eb2 */
        const uint8_t vels[]  = {80,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
        const float   lens[]  = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};   /* ADSR handles duration — no note-off system yet */
        for (int s = 0; s < 16; s++) {
            p->tracks[synth_pad].steps[s].note = notes[s];
            p->tracks[synth_pad].steps[s].velocity = vels[s];
            p->tracks[synth_pad].steps[s].length = lens[s];
        }
        /* Shimmer reverb for atmosphere */
        p->tracks[synth_pad].effects[0].type = EFFECT_SHIMMER;
        p->tracks[synth_pad].effects[0].shimmer.decay = 0.7f;
        p->tracks[synth_pad].effects[0].shimmer.shimmer = 0.4f;
        p->tracks[synth_pad].effects[0].shimmer.mix = 0.5f;
        effect_init(&p->tracks[synth_pad].effects[0], EFFECT_SHIMMER,
                    g_engine.sample_rate);
    }

    /* Add delay to master bus for atmosphere */
    g_engine.master_effects[0].type = EFFECT_DELAY;
    g_engine.master_effects[0].delay.time = 0.375f;  /* dotted 8th at 145 */
    g_engine.master_effects[0].delay.feedback = 0.3f;
    g_engine.master_effects[0].delay.wet = 0.15f;
    g_engine.master_effects[0].delay.bpm_sync = true;
    g_engine.master_effects[0].delay.sync_division = 2;  /* 1/4 note */
    effect_init(&g_engine.master_effects[0], EFFECT_DELAY, g_engine.sample_rate);

    /* Add compressor to master for glue */
    g_engine.master_effects[1].type = EFFECT_COMPRESSOR;
    g_engine.master_effects[1].compressor.threshold = 0.6f;
    g_engine.master_effects[1].compressor.ratio = 3.0f;
    g_engine.master_effects[1].compressor.attack = 0.01f;
    g_engine.master_effects[1].compressor.release = 0.15f;
    g_engine.master_effects[1].compressor.makeup = 1.2f;

    g_engine.transport.bpm = 145.0;
    snprintf(p->name, SQ_PATTERN_NAME_LEN, "Pattern 1");
    g_engine.num_patterns = 5;
    g_engine.transport.current_pattern = 0;

    /* Debug: log demo pattern setup */
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
    signal(SIGTERM, SIG_DFL); /* Let GTK handle SIGTERM gracefully */
    signal(SIGINT,  SIG_DFL); /* Let GTK handle SIGINT gracefully */

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

    /* Load samples and set up demo pattern (fallback if no saved project) */
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

    /* Load session state + auto-load last project */
    {
        char last_project[512] = {0};
        sq_session_load(&g_gtk.app, &g_engine, NULL, NULL, NULL,
                        last_project, sizeof(last_project));
        if (last_project[0]) {
            if (project_load(&g_engine, last_project) == 0)
                LOG_INFO("Auto-loaded project: %s", last_project);
        }
    }

    /* Register audio restart callback for settings panel */
    g_gtk.app.audio_restart_fn = gtk_audio_restart_callback;
    g_gtk.app.audio_restart_userdata = NULL;

    /* Initialize MIDI input */
    {
        sq_midi_t *midi = sq_midi_init(&g_engine.cmd_queue, 0);
        if (midi) {
            int ports = sq_midi_get_port_count(midi);
            if (ports > 0) {
                sq_midi_open_port(midi, 0);
                LOG_WARN("MIDI: auto-opened port 0 (%s)", sq_midi_get_port_name(midi, 0));
            }
        }
        g_gtk.midi = midi;
    }

    /* Create GTK application */
    g_gtk.gtk_app = gtk_application_new("com.dcmichael.sequencer808",
                                        G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(g_gtk.gtk_app, "activate",
                     G_CALLBACK(gtk_window_setup), NULL);

    int status = g_application_run(G_APPLICATION(g_gtk.gtk_app), argc, argv);

    /* Auto-save current state */
    {
        const char *autosave = sq_session_autosave_path();
        project_save(&g_engine, autosave);
        sq_session_save(&g_gtk.app, &g_engine, 1280, 720, 0, autosave);
    }

    /* Cleanup */
    g_object_unref(g_gtk.gtk_app);
    sq_midi_shutdown((sq_midi_t *)g_gtk.midi);
    audio_shutdown();
    sq_engine_shutdown(&g_engine);

    LOG_INFO("0x808 GTK frontend exiting (status=%d)", status);
    return status;
}
