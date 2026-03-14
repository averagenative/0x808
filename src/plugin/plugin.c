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
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "engine/engine.h"
#include "engine/sampler.h"
#include "engine/synth.h"
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
#include "core/log.h"
#define LOG_TAG "plugin"
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
} SqPlugin;

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

/* ─── Demo beat pattern (mirrors standalone setup) ────────────────────────── */

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

    /* Synth tracks at the end */
    uint32_t synth_bass  = num_sample_tracks;
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

    /* Kick: four-on-the-floor */
    p->tracks[0].steps[0].velocity  = 120;
    p->tracks[0].steps[4].velocity  = 110;
    p->tracks[0].steps[8].velocity  = 120;
    p->tracks[0].steps[12].velocity = 110;

    /* Snare: backbeat */
    if (num_sample_tracks > 1) {
        p->tracks[1].steps[4].velocity  = 127;
        p->tracks[1].steps[12].velocity = 127;
    }

    /* Closed hihat: 8th notes */
    if (num_sample_tracks > 2) {
        for (int s = 0; s < 16; s += 2)
            p->tracks[2].steps[s].velocity = (s % 4 == 0) ? 100 : 70;
    }

    /* Clap: offbeats */
    if (num_sample_tracks > 3) {
        p->tracks[3].steps[7].velocity  = 90;
        p->tracks[3].steps[15].velocity = 80;
    }

    /* Open hihat: sparse hits */
    if (num_sample_tracks > 4) {
        p->tracks[4].steps[3].velocity  = 80;
        p->tracks[4].steps[11].velocity = 80;
        p->tracks[4].volume = 0.5f;
    }

    /* Cowbell: disco pattern */
    if (num_sample_tracks > 5) {
        for (int s = 0; s < 16; s += 2)
            p->tracks[5].steps[s].velocity = 60;
        p->tracks[5].volume = 0.35f;
    }

    /* Synth bass line */
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

    /* Synth pluck melody */
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

void cplug_setSampleRateAndBlockSize(void *ptr, double sampleRate, uint32_t maxBlockSize)
{
    SqPlugin *p = (SqPlugin *)ptr;
    p->sample_rate    = sampleRate;
    p->max_block_size = maxBlockSize;

    /* Re-init engine at new sample rate */
    sq_engine_shutdown(&p->engine);
    sq_engine_init(&p->engine, (uint32_t)sampleRate);

    /* Reload embedded samples and demo pattern after re-init */
    load_embedded_samples(&p->engine);
    setup_plugin_demo_pattern(&p->engine);

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

void cplug_process(void *ptr, CplugProcessContext *ctx)
{
    SqPlugin *p = (SqPlugin *)ptr;
    sq_engine_t *engine = &p->engine;

    /* ── Internal transport (independent from DAW host) ─────────── */
    /* The plugin runs its own clock like a hardware groove box.
     * GUI controls play/stop/BPM; the DAW just captures audio output.
     * Host transport state (ctx->flags) is intentionally ignored.
     * TODO: Add an "Internal / Host Sync" toggle if host-sync is needed. */

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
        {
            /* Forward MIDI note events to the engine's synth voices */
            uint8_t status = event.midi.status & 0xF0;
            uint8_t note   = event.midi.data1;
            uint8_t vel    = event.midi.data2;

            if (status == 0x90 && vel > 0) {
                /* Note On — use synth_trigger() for proper voice init */
                int pat = engine->transport.current_pattern;
                for (uint32_t t = 0; t < engine->patterns[pat].num_tracks; t++) {
                    if (engine->patterns[pat].tracks[t].type == TRACK_SYNTH) {
                        synth_trigger(engine,
                                      engine->patterns[pat].tracks[t].synth_preset,
                                      (float)vel / 127.0f,
                                      0, /* pitch_offset */
                                      engine->patterns[pat].tracks[t].volume,
                                      engine->patterns[pat].tracks[t].pan,
                                      note);
                        break; /* only trigger on first synth track */
                    }
                }
            }
            else if (status == 0x80 || (status == 0x90 && vel == 0)) {
                /* Note Off — release matching synth voices */
                float freq = 440.0f * powf(2.0f, ((float)note - 69.0f) / 12.0f);
                for (int v = 0; v < SQ_MAX_SYNTH_VOICES; v++) {
                    if (engine->synth_voices[v].active &&
                        fabsf(engine->synth_voices[v].frequency - freq) < 0.1f) {
                        engine->synth_voices[v].amp_env.stage = ENV_RELEASE;
                    }
                }
            }
            break;
        }

        case CPLUG_EVENT_PROCESS_AUDIO:
        {
            uint32_t num_frames = event.processAudio.endFrame - frame;

            /* Engine renders into interleaved buffer */
            if (num_frames <= p->interleave_buf_frames) {
                sq_engine_process(engine, p->interleave_buf, num_frames);

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

/* Simple binary state: save all param values */
struct sq_param_state {
    uint32_t id;
    double   value;
};

void cplug_saveState(void *userPlugin, const void *stateCtx, cplug_writeProc writeProc)
{
    SqPlugin *p = (SqPlugin *)userPlugin;
    struct sq_param_state state[NUM_PARAMS];

    for (int i = 0; i < NUM_PARAMS; i++) {
        state[i].id    = PARAM_DEFS[i].id;
        state[i].value = read_param_from_engine(p, PARAM_DEFS[i].id);
    }
    writeProc(stateCtx, state, sizeof(state));
}

void cplug_loadState(void *userPlugin, const void *stateCtx, cplug_readProc readProc)
{
    SqPlugin *p = (SqPlugin *)userPlugin;
    struct sq_param_state state[NUM_PARAMS * 2]; /* extra space for forward compat */

    int64_t bytes = readProc(stateCtx, state, sizeof(state));
    if (bytes <= 0) return;

    size_t count = (size_t)bytes / sizeof(struct sq_param_state);
    for (size_t i = 0; i < count && i < NUM_PARAMS * 2; i++) {
        int idx = param_index_from_id(state[i].id);
        if (idx >= 0)
            apply_param_to_engine(p, state[i].id, state[i].value);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * GUI — Embedded Nuklear+SDL2+OpenGL inside host's native window (TASK-110)
 * ═══════════════════════════════════════════════════════════════════════════ */

#ifndef SQ_PLUGIN_NO_GUI

void *cplug_createGUI(void *userPlugin)
{
    PLOG_INFO("cplug_createGUI called: userPlugin=%p", userPlugin);
    SqPlugin *p = (SqPlugin *)userPlugin;
    p->gui = plugin_gui_create(&p->engine);
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
