/*
 * plugin.c — CPLUG plugin implementation for 0x808.
 *
 * Implements all CPLUG callbacks: lifecycle, audio processing, parameters,
 * host transport sync, state persistence, and GUI stubs.
 *
 * The engine (Layer 1) does all DSP work. This file adapts between
 * CPLUG's non-interleaved buffer format and the engine's interleaved format,
 * maps host transport to sq_transport_t, and exposes key parameters.
 */

#include <cplug.h>
#include <math.h>
#include <stdatomic.h>
#include <time.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "engine/engine.h"
#include "engine/sampler.h"
#include "engine/synth.h"
#include "engine/envelope.h"
#include "engine/midi_out.h"
#include "formats/sf2.h"
#include "formats/project.h"
#include "cJSON.h"
#include "formats/sample_io.h"
#include "plugin/embedded_samples.h"

/* dr_wav is already implemented in sample_io.c — just need declarations */
#include "dr_wav.h"

#ifndef SQ_PLUGIN_NO_GUI
#include "plugin/plugin_gui.h"
#endif

/* ─── Plugin file logger (Windows: file, Linux: stderr via core/log.h) ──── */

#ifdef _WIN32
#include <stdarg.h>
#include <windows.h>

const char *sq_plugin_log_path(void)
{
    return "C:\\Users\\Public\\0x808.log";
}

static void sq_plugin_log(const char *level, const char *tag, const char *fmt, ...)
{
    FILE *f = fopen(sq_plugin_log_path(), "a");
    if (!f) return;

    /* Local timestamp HH:MM:SS */
    SYSTEMTIME st;
    GetLocalTime(&st);
    fprintf(f, "%02d:%02d:%02d.%03d %s %s: ",
            st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
            level, tag);

    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);

    fprintf(f, "\n");
    fflush(f);
    fclose(f);
}

#define PLOG_DEBUG(fmt, ...) sq_plugin_log("DEBUG", "plugin", fmt, ##__VA_ARGS__)
#define PLOG_INFO(fmt, ...)  sq_plugin_log("INFO ", "plugin", fmt, ##__VA_ARGS__)
#define PLOG_WARN(fmt, ...)  sq_plugin_log("WARN ", "plugin", fmt, ##__VA_ARGS__)
#define PLOG_ERROR(fmt, ...) sq_plugin_log("ERROR", "plugin", fmt, ##__VA_ARGS__)
#else
#define LOG_TAG "plugin"
#include "core/log.h"
#define PLOG_DEBUG(fmt, ...) LOG_DEBUG(fmt, ##__VA_ARGS__)
#define PLOG_INFO(fmt, ...)  LOG_INFO(fmt, ##__VA_ARGS__)
#define PLOG_WARN(fmt, ...)  LOG_WARN(fmt, ##__VA_ARGS__)
#define PLOG_ERROR(fmt, ...) LOG_ERROR(fmt, ##__VA_ARGS__)
#endif

/* ─── Parameter IDs ──────────────────────────────────────────────────────── */

enum {
    PARAM_BPM            = 0x0001,
    PARAM_MASTER_VOLUME  = 0x0002,
    PARAM_PATTERN_SELECT = 0x0003,
    PARAM_SWING          = 0x0004,
    /* Per-track volumes (tracks 0-15) */
    PARAM_TRACK_VOL_BASE = 0x0100,  /* 0x0100 .. 0x010F */
    /* Synth parameters (applied to first synth track found) */
    PARAM_FILTER_CUTOFF  = 0x0200,
    PARAM_FILTER_RESO    = 0x0201,
    PARAM_AMP_ATTACK     = 0x0202,
    PARAM_AMP_DECAY      = 0x0203,
    PARAM_AMP_SUSTAIN    = 0x0204,
    PARAM_AMP_RELEASE    = 0x0205,
};

/* Flat parameter table: ID, name, min, max, default, flags */
typedef struct {
    uint32_t id;
    const char *name;
    double min, max, def;
    uint32_t flags;
} sq_param_def_t;

#define NUM_TRACK_VOL_PARAMS 8  /* expose first 8 track volumes */

/* Total parameters: 4 global + 8 track vols + 6 synth = 18 */
#define NUM_PARAMS (4 + NUM_TRACK_VOL_PARAMS + 6)

static const sq_param_def_t PARAM_DEFS[NUM_PARAMS] = {
    /* Global */
    { PARAM_BPM,            "BPM",              20.0, 300.0, 120.0,
      CPLUG_FLAG_PARAMETER_IS_AUTOMATABLE },
    { PARAM_MASTER_VOLUME,  "Master Volume",     0.0,   1.0,   1.0,
      CPLUG_FLAG_PARAMETER_IS_AUTOMATABLE },
    { PARAM_PATTERN_SELECT, "Pattern",           0.0,  63.0,   0.0,
      CPLUG_FLAG_PARAMETER_IS_AUTOMATABLE | CPLUG_FLAG_PARAMETER_IS_INTEGER },
    { PARAM_SWING,          "Swing",             0.0,   1.0,   0.0,
      CPLUG_FLAG_PARAMETER_IS_AUTOMATABLE },
    /* Track volumes */
    { PARAM_TRACK_VOL_BASE + 0, "Track 1 Vol", 0.0, 1.0, 0.8,
      CPLUG_FLAG_PARAMETER_IS_AUTOMATABLE },
    { PARAM_TRACK_VOL_BASE + 1, "Track 2 Vol", 0.0, 1.0, 0.8,
      CPLUG_FLAG_PARAMETER_IS_AUTOMATABLE },
    { PARAM_TRACK_VOL_BASE + 2, "Track 3 Vol", 0.0, 1.0, 0.8,
      CPLUG_FLAG_PARAMETER_IS_AUTOMATABLE },
    { PARAM_TRACK_VOL_BASE + 3, "Track 4 Vol", 0.0, 1.0, 0.8,
      CPLUG_FLAG_PARAMETER_IS_AUTOMATABLE },
    { PARAM_TRACK_VOL_BASE + 4, "Track 5 Vol", 0.0, 1.0, 0.8,
      CPLUG_FLAG_PARAMETER_IS_AUTOMATABLE },
    { PARAM_TRACK_VOL_BASE + 5, "Track 6 Vol", 0.0, 1.0, 0.8,
      CPLUG_FLAG_PARAMETER_IS_AUTOMATABLE },
    { PARAM_TRACK_VOL_BASE + 6, "Track 7 Vol", 0.0, 1.0, 0.8,
      CPLUG_FLAG_PARAMETER_IS_AUTOMATABLE },
    { PARAM_TRACK_VOL_BASE + 7, "Track 8 Vol", 0.0, 1.0, 0.8,
      CPLUG_FLAG_PARAMETER_IS_AUTOMATABLE },
    /* Synth parameters */
    { PARAM_FILTER_CUTOFF,  "Filter Cutoff",    20.0, 20000.0, 8000.0,
      CPLUG_FLAG_PARAMETER_IS_AUTOMATABLE },
    { PARAM_FILTER_RESO,    "Filter Resonance",  0.5,    20.0,    1.0,
      CPLUG_FLAG_PARAMETER_IS_AUTOMATABLE },
    { PARAM_AMP_ATTACK,     "Amp Attack",       0.001,  10.0,   0.01,
      CPLUG_FLAG_PARAMETER_IS_AUTOMATABLE },
    { PARAM_AMP_DECAY,      "Amp Decay",        0.001,  10.0,    0.3,
      CPLUG_FLAG_PARAMETER_IS_AUTOMATABLE },
    { PARAM_AMP_SUSTAIN,    "Amp Sustain",       0.0,    1.0,    0.7,
      CPLUG_FLAG_PARAMETER_IS_AUTOMATABLE },
    { PARAM_AMP_RELEASE,    "Amp Release",      0.001,  10.0,    0.3,
      CPLUG_FLAG_PARAMETER_IS_AUTOMATABLE },
};

/* ─── Plugin instance ────────────────────────────────────────────────────── */

typedef struct {
    CplugHostContext *host;
    sq_engine_t       engine;
    double            sample_rate;
    uint32_t          max_block_size;

    /* Interleave conversion buffer (allocated at setSampleRateAndBlockSize) */
    float            *interleave_buf;
    uint32_t          interleave_buf_frames;

    /* GUI handle (NULL if no GUI open) */
#ifndef SQ_PLUGIN_NO_GUI
    sq_plugin_gui_t  *gui;
#else
    void             *gui;
#endif

    /* Host sync: when on, the host's transport drives the sequencer.
     * host_sync is the setting (GUI thread writes it); the rest is
     * audio-thread state. */
    bool              host_sync;
    bool              host_sync_active;     /* host_sync as of the last block */
    bool              host_was_playing;
    double            host_expected_beats;  /* where the host should be next */

    /* Rebuilding the engine (state load, sample rate change) pauses the
     * audio thread and the GUI; see engine_pause() */
    atomic_int        engine_paused;
    atomic_int        in_process;           /* audio thread is in cplug_process */
} SqPlugin;


/* ─── Pausing the engine while it's rebuilt ─────────────────────────────── */

static void sleep_ms(int ms)
{
#ifdef _WIN32
    Sleep((DWORD)ms);
#else
    struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
#endif
}

/*
 * Hosts load state on their main thread while audio keeps running (REAPER
 * does it on every undo). Loading re-initializes the engine and frees its
 * samples and effect buffers, so first stop the audio thread from using it
 * (cplug_process outputs silence while engine_paused is set) and hold off
 * the GUI. The two flags work as a handshake: the loader sets engine_paused
 * and then waits out in_process; the audio thread sets in_process and then
 * checks engine_paused. Main thread only.
 */
static void engine_pause(SqPlugin *p)
{
    atomic_store(&p->engine_paused, 1);
    while (atomic_load(&p->in_process))
        sleep_ms(1);
#ifndef SQ_PLUGIN_NO_GUI
    plugin_gui_lock(p->gui);
#endif
}

static void engine_resume(SqPlugin *p)
{
#ifndef SQ_PLUGIN_NO_GUI
    plugin_gui_unlock(p->gui);
#endif
    atomic_store(&p->engine_paused, 0);
}

/* ─── Helper: find parameter def index by ID ─────────────────────────────── */

static int param_index_from_id(uint32_t id)
{
    for (int i = 0; i < NUM_PARAMS; i++)
        if (PARAM_DEFS[i].id == id) return i;
    return -1;
}

/* ─── Helper: apply a parameter value to the engine ──────────────────────── */

static void apply_param_to_engine(SqPlugin *p, uint32_t id, double value)
{
    sq_engine_t *e = &p->engine;
    int pat = e->transport.current_pattern;

    switch (id) {
    case PARAM_BPM:
        e->transport.bpm = value;
        break;
    case PARAM_MASTER_VOLUME:
        e->master_volume = (float)value;
        break;
    case PARAM_PATTERN_SELECT:
        if ((int)value < (int)e->num_patterns)
            e->transport.current_pattern = (int)value;
        break;
    case PARAM_SWING:
        e->transport.swing = (float)value;
        break;
    case PARAM_FILTER_CUTOFF:
    case PARAM_FILTER_RESO:
    case PARAM_AMP_ATTACK:
    case PARAM_AMP_DECAY:
    case PARAM_AMP_SUSTAIN:
    case PARAM_AMP_RELEASE:
    {
        /* Apply to all synth presets currently in use */
        for (uint32_t i = 0; i < e->num_synth_presets; i++) {
            sq_synth_preset_t *sp = &e->synth_presets[i];
            switch (id) {
            case PARAM_FILTER_CUTOFF:  sp->filter_cutoff    = (float)value; break;
            case PARAM_FILTER_RESO:    sp->filter_resonance = (float)value; break;
            case PARAM_AMP_ATTACK:     sp->amp_env.attack   = (float)value; break;
            case PARAM_AMP_DECAY:      sp->amp_env.decay    = (float)value; break;
            case PARAM_AMP_SUSTAIN:    sp->amp_env.sustain  = (float)value; break;
            case PARAM_AMP_RELEASE:    sp->amp_env.release  = (float)value; break;
            }
        }
        break;
    }
    default:
        /* Track volumes */
        if (id >= PARAM_TRACK_VOL_BASE && id < PARAM_TRACK_VOL_BASE + NUM_TRACK_VOL_PARAMS) {
            int track = (int)(id - PARAM_TRACK_VOL_BASE);
            if (pat < (int)e->num_patterns && track < (int)e->patterns[pat].num_tracks)
                e->patterns[pat].tracks[track].volume = (float)value;
        }
        break;
    }
}

/* ─── Helper: read a parameter value from the engine ─────────────────────── */

static double read_param_from_engine(const SqPlugin *p, uint32_t id)
{
    const sq_engine_t *e = &p->engine;
    int pat = e->transport.current_pattern;

    switch (id) {
    case PARAM_BPM:            return e->transport.bpm;
    case PARAM_MASTER_VOLUME:  return e->master_volume;
    case PARAM_PATTERN_SELECT: return (double)e->transport.current_pattern;
    case PARAM_SWING:          return e->transport.swing;
    case PARAM_FILTER_CUTOFF:
        if (e->num_synth_presets > 0) return e->synth_presets[0].filter_cutoff;
        return 8000.0;
    case PARAM_FILTER_RESO:
        if (e->num_synth_presets > 0) return e->synth_presets[0].filter_resonance;
        return 1.0;
    case PARAM_AMP_ATTACK:
        if (e->num_synth_presets > 0) return e->synth_presets[0].amp_env.attack;
        return 0.01;
    case PARAM_AMP_DECAY:
        if (e->num_synth_presets > 0) return e->synth_presets[0].amp_env.decay;
        return 0.3;
    case PARAM_AMP_SUSTAIN:
        if (e->num_synth_presets > 0) return e->synth_presets[0].amp_env.sustain;
        return 0.7;
    case PARAM_AMP_RELEASE:
        if (e->num_synth_presets > 0) return e->synth_presets[0].amp_env.release;
        return 0.3;
    default:
        if (id >= PARAM_TRACK_VOL_BASE && id < PARAM_TRACK_VOL_BASE + NUM_TRACK_VOL_PARAMS) {
            int track = (int)(id - PARAM_TRACK_VOL_BASE);
            if (pat < (int)e->num_patterns && track < (int)e->patterns[pat].num_tracks)
                return e->patterns[pat].tracks[track].volume;
            return 0.8;
        }
        return 0.0;
    }
}

/* ─── Embedded sample loading ─────────────────────────────────────────────── */

static void load_embedded_samples(sq_engine_t *engine)
{
    for (int i = 0; i < SQ_NUM_EMBEDDED_SAMPLES && engine->num_samples < SQ_MAX_SAMPLES; i++) {
        unsigned int channels = 0, sample_rate = 0;
        drwav_uint64 total_frames = 0;
        float *decoded = drwav_open_memory_and_read_pcm_frames_f32(
            SQ_EMBEDDED_SAMPLES[i].data,
            SQ_EMBEDDED_SAMPLES[i].size,
            &channels, &sample_rate, &total_frames, NULL);
        if (!decoded) continue;

        int idx = (int)engine->num_samples;
        engine->samples[idx].data         = decoded;
        engine->samples[idx].num_frames   = (uint32_t)total_frames;
        engine->samples[idx].num_channels = channels;
        engine->samples[idx].sample_rate  = sample_rate;
        strncpy(engine->samples[idx].name, SQ_EMBEDDED_SAMPLES[i].name,
                SQ_SAMPLE_NAME_LEN - 1);
        engine->samples[idx].name[SQ_SAMPLE_NAME_LEN - 1] = '\0';
        engine->num_samples++;
    }
}

/* ─── Demo beat pattern (mirrors standalone setup — Trap 808) ─────────────── */

static void setup_plugin_demo_pattern(sq_engine_t *engine)
{
    sq_pattern_t *p = &engine->patterns[0];

    uint32_t num_sample_tracks = engine->num_samples;
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
    uint32_t synth_pad  = num_sample_tracks + 1;
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

    /* ── 808 sub bass: ONE deep note sustaining the whole bar ── */
    if (synth_bass < p->num_tracks) {
        p->tracks[synth_bass].steps[0].note = 24;      /* C1 */
        p->tracks[synth_bass].steps[0].velocity = 127;
        p->tracks[synth_bass].steps[0].length = 14.0f;  /* sustains 14 steps */
        /* Tape saturation for analog warmth */
        p->tracks[synth_bass].effects[0].type = EFFECT_TAPE;
        p->tracks[synth_bass].effects[0].tape.drive = 0.4f;
        p->tracks[synth_bass].effects[0].tape.warmth = 0.7f;
        p->tracks[synth_bass].effects[0].tape.mix = 0.4f;
    }

    /* ── Dark pad: Eb2 hit for ominous atmosphere ── */
    if (synth_pad < p->num_tracks) {
        p->tracks[synth_pad].steps[0].note = 39;       /* Eb2 */
        p->tracks[synth_pad].steps[0].velocity = 80;
        p->tracks[synth_pad].steps[0].length = 0.0f;   /* ADSR handles duration */
        /* Shimmer reverb for atmosphere */
        p->tracks[synth_pad].effects[0].type = EFFECT_SHIMMER;
        p->tracks[synth_pad].effects[0].shimmer.decay = 0.7f;
        p->tracks[synth_pad].effects[0].shimmer.shimmer = 0.4f;
        p->tracks[synth_pad].effects[0].shimmer.mix = 0.5f;
        effect_init(&p->tracks[synth_pad].effects[0], EFFECT_SHIMMER,
                    engine->sample_rate);
    }

    /* Master bus effects */
    engine->master_effects[0].type = EFFECT_DELAY;
    engine->master_effects[0].delay.time = 0.375f;  /* dotted 8th at 145 */
    engine->master_effects[0].delay.feedback = 0.3f;
    engine->master_effects[0].delay.wet = 0.15f;
    engine->master_effects[0].delay.bpm_sync = true;
    engine->master_effects[0].delay.sync_division = 2;  /* 1/4 note */
    effect_init(&engine->master_effects[0], EFFECT_DELAY, engine->sample_rate);

    engine->master_effects[1].type = EFFECT_COMPRESSOR;
    engine->master_effects[1].compressor.threshold = 0.6f;
    engine->master_effects[1].compressor.ratio = 3.0f;
    engine->master_effects[1].compressor.attack = 0.01f;
    engine->master_effects[1].compressor.release = 0.15f;
    engine->master_effects[1].compressor.makeup = 1.2f;

    engine->transport.bpm = 145.0;
    snprintf(p->name, SQ_PATTERN_NAME_LEN, "Pattern 1");
    engine->num_patterns = 5;
    engine->transport.current_pattern = 0;

    /* Initialize patterns 2-5 with tracks but no steps */
    for (int i = 1; i < 5; i++) {
        sq_pattern_t *pp = &engine->patterns[i];
        pp->num_tracks = p->num_tracks;
        for (uint32_t t = 0; t < pp->num_tracks; t++) {
            pp->tracks[t].type = p->tracks[t].type;
            pp->tracks[t].sample_index = p->tracks[t].sample_index;
            pp->tracks[t].synth_preset = p->tracks[t].synth_preset;
            pp->tracks[t].length = 16;
            pp->tracks[t].volume = p->tracks[t].volume;
        }
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * CPLUG Lifecycle
 * ═══════════════════════════════════════════════════════════════════════════ */

void cplug_libraryLoad(void)
{
    PLOG_INFO("libraryLoad: CPLUG_WANT_GUI=%d", CPLUG_WANT_GUI);
#ifdef SQ_PLUGIN_NO_GUI
    PLOG_WARN("SQ_PLUGIN_NO_GUI is defined — GUI stubs active");
#else
    PLOG_INFO("GUI code is linked (not SQ_PLUGIN_NO_GUI)");
#endif
}
void cplug_libraryUnload(void) {}

void *cplug_createPlugin(CplugHostContext *ctx)
{
    PLOG_INFO("cplug_createPlugin called");
    SqPlugin *p = (SqPlugin *)calloc(1, sizeof(SqPlugin));
    if (!p) return NULL;

    p->host = ctx;
    p->sample_rate = 44100.0;
    p->host_sync = true;

    /* Initialize engine with default sample rate (host will call setSampleRate before processing) */
    sq_engine_init(&p->engine, 44100);

    /* Load embedded 808 samples and set up demo pattern */
    load_embedded_samples(&p->engine);
    setup_plugin_demo_pattern(&p->engine);

    return p;
}

void cplug_destroyPlugin(void *ptr)
{
    SqPlugin *p = (SqPlugin *)ptr;
    if (!p) return;

    sq_engine_shutdown(&p->engine);
    free(p->interleave_buf);
    free(p);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Busses
 * ═══════════════════════════════════════════════════════════════════════════ */

uint32_t cplug_getNumInputBusses(void *ptr)  { (void)ptr; return 0; }
uint32_t cplug_getNumOutputBusses(void *ptr) { (void)ptr; return 1; }
uint32_t cplug_getInputBusChannelCount(void *ptr, uint32_t idx)  { (void)ptr; (void)idx; return 0; }
uint32_t cplug_getOutputBusChannelCount(void *ptr, uint32_t idx) { (void)ptr; (void)idx; return 2; }

void cplug_getInputBusName(void *ptr, uint32_t idx, char *buf, size_t buflen)
{
    (void)ptr; (void)idx;
    snprintf(buf, buflen, "None");
}

void cplug_getOutputBusName(void *ptr, uint32_t idx, char *buf, size_t buflen)
{
    (void)ptr; (void)idx;
    snprintf(buf, buflen, "Stereo Output");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Audio Processing
 * ═══════════════════════════════════════════════════════════════════════════ */

uint32_t cplug_getLatencyInSamples(void *ptr) { (void)ptr; return 0; }
uint32_t cplug_getTailInSamples(void *ptr)    { (void)ptr; return 0; }

/* Rebuild the engine from a project JSON tree at the given sample rate,
 * with the plugin's built-in samples. Call with the engine paused. */
static int apply_project(SqPlugin *p, cJSON *project, uint32_t sample_rate)
{
    cJSON *sr = cJSON_CreateNumber((double)sample_rate);
    if (cJSON_GetObjectItem(project, "sample_rate"))
        cJSON_ReplaceItemInObject(project, "sample_rate", sr);
    else
        cJSON_AddItemToObject(project, "sample_rate", sr);
    return project_from_json(&p->engine, project, load_embedded_samples);
}

static void set_current_pattern(SqPlugin *p, int pattern)
{
    if (pattern >= 0 && (uint32_t)pattern < p->engine.num_patterns)
        p->engine.transport.current_pattern = pattern;
}

void cplug_setSampleRateAndBlockSize(void *ptr, double sampleRate, uint32_t maxBlockSize)
{
    SqPlugin *p = (SqPlugin *)ptr;
    p->sample_rate    = sampleRate;
    p->max_block_size = maxBlockSize;

    /* Re-init the engine at the new sample rate, keeping the project */
    engine_pause(p);
    int pattern = p->engine.transport.current_pattern;
    cJSON *project = project_to_json(&p->engine);
    if (project && apply_project(p, project, (uint32_t)sampleRate) == 0) {
        set_current_pattern(p, pattern);
    } else {
        sq_engine_shutdown(&p->engine);
        sq_engine_init(&p->engine, (uint32_t)sampleRate);
        load_embedded_samples(&p->engine);
        setup_plugin_demo_pattern(&p->engine);
    }
    cJSON_Delete(project);

    /* The engine stopped: host sync re-locates on the next playing block */
    p->host_was_playing = false;
    engine_resume(p);

    /* Allocate interleave conversion buffer */
    free(p->interleave_buf);
    p->interleave_buf = (float *)calloc(maxBlockSize * 2, sizeof(float));
    if (!p->interleave_buf) {
        PLOG_ERROR("Failed to allocate interleave buffer (%u frames)", maxBlockSize);
        p->interleave_buf_frames = 0;
    } else {
        p->interleave_buf_frames = maxBlockSize;
    }
}

/* ─── Host transport sync ────────────────────────────────────────────────── */

/* Playhead moves bigger than this (about a fifth of a step) are seeks or
 * loop wraps; smaller ones are drift from tempo changes inside a block */
#define HOST_JUMP_BEATS 0.05

/*
 * With host sync on, the host's play/stop, tempo, and playhead drive the
 * sequencer. While the host plays straight through, the engine runs at the
 * host tempo and is nudged onto the host's position each block. A play
 * start, seek, or loop wrap re-locates it. With host sync off, the plugin
 * runs its own clock like a hardware groove box and ignores the host.
 */
static void host_sync_update(SqPlugin *p, const CplugProcessContext *ctx)
{
    sq_engine_t *e = &p->engine;
    bool sync = p->host_sync;

    /* Switching modes stops whatever the other clock was playing */
    if (sync != p->host_sync_active) {
        if (e->transport.playing) sq_engine_stop(e);
        p->host_sync_active = sync;
        p->host_was_playing = false;
    }
    if (!sync) return;

    if ((ctx->flags & CPLUG_FLAG_TRANSPORT_HAS_BPM) && ctx->bpm > 0.0)
        e->transport.bpm = ctx->bpm;

    bool host_playing = (ctx->flags & CPLUG_FLAG_TRANSPORT_IS_PLAYING) != 0;
    double block_beats = (double)ctx->numFrames * e->transport.bpm / 60.0
                       / (double)e->sample_rate;
    double beats;
    if (ctx->flags & CPLUG_FLAG_TRANSPORT_HAS_PLAYHEAD_BEATS)
        beats = ctx->playheadBeats;
    else
        beats = p->host_was_playing ? p->host_expected_beats : 0.0;

    /* Stopped, or in a count-in before the project start */
    if (!host_playing || beats < 0.0) {
        if (e->transport.playing) sq_engine_stop(e);
        p->host_was_playing = false;
        return;
    }

    if (!p->host_was_playing || fabs(beats - p->host_expected_beats) > HOST_JUMP_BEATS)
        sq_engine_locate(e, beats, block_beats);
    else
        e->transport.current_beat += beats - p->host_expected_beats;

    p->host_was_playing = true;
    p->host_expected_beats = beats + block_beats;
}

/* Play an incoming note on the track it maps to (see engine/midi_out.h) */
static void handle_midi_input(sq_engine_t *engine, const CplugEvent *event)
{
    uint8_t status  = event->midi.status & 0xF0;
    int     channel = event->midi.status & 0x0F;
    uint8_t note    = event->midi.data1;
    uint8_t vel     = event->midi.data2;
    bool note_on  = status == 0x90 && vel > 0;
    bool note_off = status == 0x80 || (status == 0x90 && vel == 0);
    if (!note_on && !note_off) return;

    int pat = engine->transport.current_pattern;
    if (pat < 0 || (uint32_t)pat >= engine->num_patterns) return;
    sq_pattern_t *pattern = &engine->patterns[pat];
    int t = sq_midi_find_track(pattern, channel, note);
    if (t < 0) return;
    sq_track_t *track = &pattern->tracks[t];

    if (note_on) {
        float v = (float)vel / 127.0f;
        if (track->type == TRACK_SAMPLER && track->sample_index >= 0)
            sampler_trigger(engine, track->sample_index, v, 0,
                            track->volume, track->pan, t);
        else if (track->type == TRACK_SYNTH && track->synth_preset >= 0)
            synth_trigger(engine, track->synth_preset, v, 0,
                          track->volume, track->pan, note, t);
        else if (track->type == TRACK_SF2 && track->sf2_preset >= 0)
            sf2_note_on(engine, track->sf2_preset, note, v * track->volume);
        return;
    }

    /* Note off: drum samples play out; release this track's synth note */
    if (track->type == TRACK_SYNTH) {
        float freq = 440.0f * powf(2.0f, ((float)note - 69.0f) / 12.0f);
        for (int v = 0; v < SQ_MAX_SYNTH_VOICES; v++) {
            sq_synth_voice_t *voice = &engine->synth_voices[v];
            int pi = voice->preset_index;
            if (voice->active && voice->track_index == t &&
                fabsf(voice->frequency - freq) < 0.1f &&
                pi >= 0 && (uint32_t)pi < engine->num_synth_presets)
                envelope_release(&voice->amp_env, &engine->synth_presets[pi].amp_env,
                                 engine->sample_rate);
        }
    } else if (track->type == TRACK_SF2 && track->sf2_preset >= 0) {
        sf2_note_off(engine, track->sf2_preset, note);
    }
}

void cplug_process(void *ptr, CplugProcessContext *ctx)
{
    SqPlugin *p = (SqPlugin *)ptr;
    sq_engine_t *engine = &p->engine;

    /* The engine is being rebuilt (state load): output silence */
    atomic_store(&p->in_process, 1);
    if (atomic_load(&p->engine_paused)) {
        float **output = ctx->getAudioOutput(ctx, 0);
        if (output && output[0] && output[1]) {
            memset(output[0], 0, ctx->numFrames * sizeof(float));
            memset(output[1], 0, ctx->numFrames * sizeof(float));
        }
        atomic_store(&p->in_process, 0);
        return;
    }

    engine->midi_out_enabled = true;
    host_sync_update(p, ctx);

    /* ── Event-driven process loop ──────────────────────────────── */
    CplugEvent event;
    uint32_t frame = 0;

    while (ctx->dequeueEvent(ctx, &event, frame)) {
        switch (event.type) {
        case CPLUG_EVENT_UNHANDLED_EVENT:
            break;

        case CPLUG_EVENT_PARAM_CHANGE_UPDATE:
            cplug_setParameterValue(ptr, event.parameter.id, event.parameter.value);
            break;

        case CPLUG_EVENT_MIDI:
            handle_midi_input(engine, &event);
            break;

        case CPLUG_EVENT_PROCESS_AUDIO:
        {
            uint32_t num_frames = event.processAudio.endFrame - frame;

            /* Engine renders into interleaved buffer */
            if (num_frames <= p->interleave_buf_frames) {
                sq_engine_process(engine, p->interleave_buf, num_frames);

                /* The sequencer's notes go out on the MIDI output port */
                for (uint32_t i = 0; i < engine->midi_out_count; i++) {
                    const sq_midi_event_t *m = &engine->midi_out[i];
                    CplugEvent out;
                    memset(&out, 0, sizeof(out));
                    out.midi.type   = CPLUG_EVENT_MIDI;
                    out.midi.status = m->status;
                    out.midi.data1  = m->data1;
                    out.midi.data2  = m->data2;
                    ctx->enqueueEvent(ctx, &out, frame + m->frame);
                }

                /* De-interleave into CPLUG's non-interleaved output */
                float **output = ctx->getAudioOutput(ctx, 0);
                if (output && output[0] && output[1]) {
                    for (uint32_t i = 0; i < num_frames; i++) {
                        output[0][frame + i] = p->interleave_buf[i * 2];
                        output[1][frame + i] = p->interleave_buf[i * 2 + 1];
                    }
                }
            }

            frame = event.processAudio.endFrame;
            break;
        }

        default:
            break;
        }
    }

    atomic_store(&p->in_process, 0);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Parameters
 * ═══════════════════════════════════════════════════════════════════════════ */

uint32_t cplug_getNumParameters(void *ptr) { (void)ptr; return NUM_PARAMS; }

uint32_t cplug_getParameterID(void *ptr, uint32_t paramIndex)
{
    (void)ptr;
    if (paramIndex < NUM_PARAMS) return PARAM_DEFS[paramIndex].id;
    return 0;
}

uint32_t cplug_getParameterFlags(void *ptr, uint32_t paramId)
{
    (void)ptr;
    int idx = param_index_from_id(paramId);
    if (idx >= 0) return PARAM_DEFS[idx].flags;
    return 0;
}

void cplug_getParameterRange(void *ptr, uint32_t paramId, double *min, double *max)
{
    (void)ptr;
    int idx = param_index_from_id(paramId);
    if (idx >= 0) {
        *min = PARAM_DEFS[idx].min;
        *max = PARAM_DEFS[idx].max;
    } else {
        *min = 0.0;
        *max = 1.0;
    }
}

void cplug_getParameterName(void *ptr, uint32_t paramId, char *buf, size_t buflen)
{
    (void)ptr;
    int idx = param_index_from_id(paramId);
    if (idx >= 0)
        snprintf(buf, buflen, "%s", PARAM_DEFS[idx].name);
    else
        snprintf(buf, buflen, "Unknown");
}

double cplug_getParameterValue(void *ptr, uint32_t paramId)
{
    SqPlugin *p = (SqPlugin *)ptr;
    return read_param_from_engine(p, paramId);
}

double cplug_getDefaultParameterValue(void *ptr, uint32_t paramId)
{
    (void)ptr;
    int idx = param_index_from_id(paramId);
    if (idx >= 0) return PARAM_DEFS[idx].def;
    return 0.0;
}

void cplug_setParameterValue(void *ptr, uint32_t paramId, double value)
{
    SqPlugin *p = (SqPlugin *)ptr;
    int idx = param_index_from_id(paramId);
    if (idx < 0) return;

    /* Clamp */
    if (value < PARAM_DEFS[idx].min) value = PARAM_DEFS[idx].min;
    if (value > PARAM_DEFS[idx].max) value = PARAM_DEFS[idx].max;

    apply_param_to_engine(p, paramId, value);
}

double cplug_denormaliseParameterValue(void *ptr, uint32_t paramId, double normalised)
{
    (void)ptr;
    int idx = param_index_from_id(paramId);
    if (idx < 0) return normalised;

    double min = PARAM_DEFS[idx].min;
    double max = PARAM_DEFS[idx].max;
    double val = normalised * (max - min) + min;

    if (val < min) val = min;
    if (val > max) val = max;
    return val;
}

double cplug_normaliseParameterValue(void *ptr, uint32_t paramId, double denormalised)
{
    (void)ptr;
    int idx = param_index_from_id(paramId);
    if (idx < 0) return denormalised;

    double min = PARAM_DEFS[idx].min;
    double max = PARAM_DEFS[idx].max;
    double range = max - min;
    if (range <= 0.0) return 0.0;

    double val = (denormalised - min) / range;
    if (val < 0.0) val = 0.0;
    if (val > 1.0) val = 1.0;
    return val;
}

double cplug_parameterStringToValue(void *ptr, uint32_t paramId, const char *str)
{
    (void)ptr;
    int idx = param_index_from_id(paramId);
    if (idx < 0) return 0.0;

    if (PARAM_DEFS[idx].flags & CPLUG_FLAG_PARAMETER_IS_INTEGER)
        return (double)atoi(str);
    return atof(str);
}

void cplug_parameterValueToString(void *ptr, uint32_t paramId, char *buf, size_t bufsize, double value)
{
    (void)ptr;
    int idx = param_index_from_id(paramId);
    if (idx < 0) {
        snprintf(buf, bufsize, "%.2f", value);
        return;
    }

    if (PARAM_DEFS[idx].flags & CPLUG_FLAG_PARAMETER_IS_INTEGER)
        snprintf(buf, bufsize, "%d", (int)value);
    else if (paramId == PARAM_BPM)
        snprintf(buf, bufsize, "%.1f BPM", value);
    else if (paramId == PARAM_FILTER_CUTOFF)
        snprintf(buf, bufsize, "%.0f Hz", value);
    else
        snprintf(buf, bufsize, "%.2f", value);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * State Persistence
 * ═══════════════════════════════════════════════════════════════════════════ */

/*
 * Saved state: STATE_MAGIC, then a JSON object
 *   { "plugin":  { "host_sync": true, "current_pattern": 0 },
 *     "project": { the same JSON as a .sqproj project file } }
 * Before 1.5 the state was only an array of { id, value } parameter pairs;
 * loading one of those still applies its parameters.
 */
#define STATE_MAGIC "0x808 plugin state 1\n"
#define STATE_MAX_BYTES (64u * 1024u * 1024u)

/* Pre-1.5 state entry */
struct sq_param_state {
    uint32_t id;
    double   value;
};

/* Hosts' streams may take or return fewer bytes than asked for */
static bool write_all(const void *stateCtx, cplug_writeProc writeProc,
                      const char *data, size_t len)
{
    while (len > 0) {
        int64_t n = writeProc(stateCtx, (void *)data, len);
        if (n <= 0) return false;
        data += n;
        len  -= (size_t)n;
    }
    return true;
}

/* Read the whole stream into a NUL-terminated buffer (caller frees) */
static char *read_all(const void *stateCtx, cplug_readProc readProc, size_t *out_len)
{
    size_t cap = 4096, len = 0;
    char *buf = (char *)malloc(cap + 1);
    if (!buf) return NULL;
    for (;;) {
        if (len == cap) {
            if (cap >= STATE_MAX_BYTES) break;
            char *grown = (char *)realloc(buf, cap * 2 + 1);
            if (!grown) break;
            buf = grown;
            cap *= 2;
        }
        int64_t n = readProc(stateCtx, buf + len, cap - len);
        if (n <= 0) break;
        len += (size_t)n;
    }
    buf[len] = '\0';
    *out_len = len;
    return buf;
}

void cplug_saveState(void *userPlugin, const void *stateCtx, cplug_writeProc writeProc)
{
    SqPlugin *p = (SqPlugin *)userPlugin;

    /* Read while audio and GUI keep running: at worst an edit made during
     * the save is half in it, which the host's next save picks up */
    cJSON *state = cJSON_CreateObject();
    cJSON *plugin = cJSON_AddObjectToObject(state, "plugin");
    cJSON_AddBoolToObject(plugin, "host_sync", p->host_sync);
    cJSON_AddNumberToObject(plugin, "current_pattern", p->engine.transport.current_pattern);
    cJSON *project = project_to_json(&p->engine);
    if (project)
        cJSON_AddItemToObject(state, "project", project);
    char *json = cJSON_PrintUnformatted(state);
    cJSON_Delete(state);
    if (!json) {
        PLOG_ERROR("Failed to serialize plugin state");
        return;
    }

    if (!write_all(stateCtx, writeProc, STATE_MAGIC, strlen(STATE_MAGIC)) ||
        !write_all(stateCtx, writeProc, json, strlen(json)))
        PLOG_ERROR("Host stream refused the plugin state");
    cJSON_free(json);
}

static void load_project_state(SqPlugin *p, const char *json)
{
    cJSON *state = cJSON_Parse(json);
    if (!state) {
        PLOG_ERROR("Plugin state isn't valid JSON; keeping the current project");
        return;
    }
    cJSON *plugin  = cJSON_GetObjectItem(state, "plugin");
    cJSON *project = cJSON_GetObjectItem(state, "project");

    engine_pause(p);
    if (cJSON_IsObject(project) &&
        apply_project(p, project, p->engine.sample_rate) != 0)
        PLOG_ERROR("Failed to restore the project from plugin state");
    if (cJSON_IsObject(plugin)) {
        cJSON *v = cJSON_GetObjectItem(plugin, "host_sync");
        if (cJSON_IsBool(v)) p->host_sync = cJSON_IsTrue(v);
        v = cJSON_GetObjectItem(plugin, "current_pattern");
        if (cJSON_IsNumber(v)) set_current_pattern(p, v->valueint);
    }
    /* The engine stopped: host sync re-locates on the next playing block */
    p->host_was_playing = false;
    engine_resume(p);

    cJSON_Delete(state);
}

static void load_legacy_params(SqPlugin *p, const char *data, size_t len)
{
    size_t count = len / sizeof(struct sq_param_state);
    for (size_t i = 0; i < count; i++) {
        struct sq_param_state entry;
        memcpy(&entry, data + i * sizeof(entry), sizeof(entry));
        if (param_index_from_id(entry.id) >= 0)
            cplug_setParameterValue(p, entry.id, entry.value);
    }
}

void cplug_loadState(void *userPlugin, const void *stateCtx, cplug_readProc readProc)
{
    SqPlugin *p = (SqPlugin *)userPlugin;
    size_t len = 0;
    char *data = read_all(stateCtx, readProc, &len);
    if (!data) return;

    size_t magic_len = strlen(STATE_MAGIC);
    if (len >= magic_len && memcmp(data, STATE_MAGIC, magic_len) == 0)
        load_project_state(p, data + magic_len);
    else
        load_legacy_params(p, data, len);
    free(data);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * GUI — Embedded Nuklear+SDL2+OpenGL inside host's native window (TASK-110)
 * ═══════════════════════════════════════════════════════════════════════════ */

#ifndef SQ_PLUGIN_NO_GUI

void *cplug_createGUI(void *userPlugin)
{
    PLOG_INFO("cplug_createGUI called: userPlugin=%p", userPlugin);
    SqPlugin *p = (SqPlugin *)userPlugin;
    p->gui = plugin_gui_create(&p->engine, &p->host_sync);
    PLOG_INFO("cplug_createGUI: gui=%p", (void*)p->gui);
    return p->gui;
}

void cplug_destroyGUI(void *userGUI)
{
    sq_plugin_gui_t *gui = (sq_plugin_gui_t *)userGUI;
    plugin_gui_destroy(gui);
}

void cplug_setParent(void *userGUI, void *hwnd_or_nsview)
{
    PLOG_INFO("cplug_setParent: userGUI=%p hwnd=%p", userGUI, hwnd_or_nsview);
    sq_plugin_gui_t *gui = (sq_plugin_gui_t *)userGUI;
    if (hwnd_or_nsview) {
        int rc = plugin_gui_attach(gui, hwnd_or_nsview);
        if (rc == 0)
            PLOG_INFO("plugin_gui_attach succeeded");
        else
            PLOG_ERROR("plugin_gui_attach failed (rc=%d)", rc);
    } else {
        plugin_gui_detach(gui);
    }
}

void cplug_setVisible(void *userGUI, bool visible)
{
    (void)userGUI;
    (void)visible;
}

void cplug_setScaleFactor(void *userGUI, float scale)
{
    sq_plugin_gui_t *gui = (sq_plugin_gui_t *)userGUI;
    plugin_gui_set_scale(gui, scale);
}

void cplug_getSize(void *userGUI, uint32_t *width, uint32_t *height)
{
    sq_plugin_gui_t *gui = (sq_plugin_gui_t *)userGUI;
    plugin_gui_get_size(gui, width, height);
}

void cplug_checkSize(void *userGUI, uint32_t *width, uint32_t *height)
{
    (void)userGUI;
    if (*width  < 640)  *width  = 640;
    if (*height < 360)  *height = 360;
}

bool cplug_setSize(void *userGUI, uint32_t width, uint32_t height)
{
    sq_plugin_gui_t *gui = (sq_plugin_gui_t *)userGUI;
    plugin_gui_set_size(gui, width, height);
    return true;
}

#else /* SQ_PLUGIN_NO_GUI — stub GUI for headless/cross-compiled builds */

void *cplug_createGUI(void *userPlugin)
{
    SqPlugin *p = (SqPlugin *)userPlugin;
    return (void *)p; /* non-NULL placeholder */
}

void cplug_destroyGUI(void *userGUI) { (void)userGUI; }
void cplug_setParent(void *userGUI, void *hwnd) { (void)userGUI; (void)hwnd; }
void cplug_setVisible(void *userGUI, bool v) { (void)userGUI; (void)v; }
void cplug_setScaleFactor(void *userGUI, float s) { (void)userGUI; (void)s; }

void cplug_getSize(void *userGUI, uint32_t *w, uint32_t *h)
{
    (void)userGUI; *w = 1280; *h = 720;
}

void cplug_checkSize(void *userGUI, uint32_t *w, uint32_t *h)
{
    (void)userGUI;
    if (*w < 640) *w = 640;
    if (*h < 360) *h = 360;
}

bool cplug_setSize(void *userGUI, uint32_t w, uint32_t h)
{
    (void)userGUI; (void)w; (void)h; return true;
}

#endif /* SQ_PLUGIN_NO_GUI */
