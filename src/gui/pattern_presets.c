/*
 * pattern_presets.c — Drum pattern and bass line preset library.
 *
 * Contains common industry-standard drum patterns (four-on-floor, trap,
 * boom bap, DnB, reggaeton, etc.) and bass line generators that write
 * notes into synth tracks.
 *
 * Drum patterns assume 16-step resolution (4/4 time, 4 steps per beat).
 * Bass lines write MIDI notes into synth track steps.
 */

#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#include "nuklear.h"

#include "gui/pattern_presets.h"
#include "gui/gui.h"
#include "gui/undo.h"

#define LOG_TAG "presets"
#include "core/log.h"

#include <string.h>

/* ─── Drum Pattern Definitions ──────────────────────────────────────────── */

/*
 * Each drum preset has velocities for up to 6 sample tracks × 16 steps.
 * Tracks: 0=Kick, 1=Snare, 2=HiHat, 3=Clap, 4=OpenHH, 5=Perc
 */
typedef struct {
    const char *name;
    uint8_t tracks[6][16];  /* velocity per track per step */
    float   suggested_bpm;
} drum_preset_t;

#define V(x) (x)  /* velocity shorthand */

static const drum_preset_t s_drum_presets[] = {
    /* ── House / Four-on-floor ─────────────────────────────────────── */
    {
        .name = "House",
        .tracks = {
            /* Kick:  four-on-floor */
            {120, 0, 0, 0, 110, 0, 0, 0, 120, 0, 0, 0, 110, 0, 0, 0},
            /* Snare: backbeat */
            {0, 0, 0, 0, 127, 0, 0, 0, 0, 0, 0, 0, 127, 0, 0, 0},
            /* HH:    offbeat 8ths */
            {0, 0, 100, 0, 0, 0, 100, 0, 0, 0, 100, 0, 0, 0, 100, 0},
            /* Clap:  on the 2 and 4 */
            {0, 0, 0, 0, 100, 0, 0, 0, 0, 0, 0, 0, 100, 0, 0, 0},
            /* OpenHH: accent */
            {0, 0, 0, 0, 0, 0, 0, 80, 0, 0, 0, 0, 0, 0, 0, 80},
            /* Perc: shaker 16ths */
            {60, 40, 60, 40, 60, 40, 60, 40, 60, 40, 60, 40, 60, 40, 60, 40},
        },
        .suggested_bpm = 124.0f,
    },
    /* ── Boom Bap (Hip-Hop) ────────────────────────────────────────── */
    {
        .name = "Boom Bap",
        .tracks = {
            /* Kick:  boom bap classic */
            {120, 0, 0, 0, 0, 0, 0, 0, 0, 0, 110, 0, 120, 0, 0, 0},
            /* Snare: 2 and 4 with ghost */
            {0, 0, 0, 0, 127, 0, 0, 50, 0, 0, 0, 0, 127, 0, 0, 0},
            /* HH:    swing 8ths */
            {90, 0, 70, 0, 90, 0, 70, 0, 90, 0, 70, 0, 90, 0, 70, 0},
            /* Clap */
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            /* OpenHH */
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 70, 0, 0, 0, 0},
            /* Perc */
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
        },
        .suggested_bpm = 92.0f,
    },
    /* ── Trap ──────────────────────────────────────────────────────── */
    {
        .name = "Trap",
        .tracks = {
            /* Kick:  808 kicks with rolls */
            {120, 0, 0, 0, 0, 0, 100, 0, 0, 0, 0, 110, 0, 0, 100, 0},
            /* Snare: on the 3 (step 8) */
            {0, 0, 0, 0, 0, 0, 0, 0, 127, 0, 0, 0, 0, 0, 0, 0},
            /* HH:    rapid-fire 16ths with accents */
            {100, 60, 80, 60, 100, 60, 80, 60, 100, 60, 80, 60, 100, 60, 80, 90},
            /* Clap:  on the 3 with snare */
            {0, 0, 0, 0, 0, 0, 0, 0, 110, 0, 0, 0, 0, 0, 0, 0},
            /* OpenHH */
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 80},
            /* Perc */
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
        },
        .suggested_bpm = 140.0f,
    },
    /* ── Drum & Bass ───────────────────────────────────────────────── */
    {
        .name = "DnB",
        .tracks = {
            /* Kick:  syncopated */
            {120, 0, 0, 0, 0, 0, 0, 0, 0, 0, 110, 0, 0, 0, 0, 0},
            /* Snare: on 2 and 4 (fast) */
            {0, 0, 0, 0, 127, 0, 0, 0, 0, 0, 0, 0, 127, 0, 0, 0},
            /* HH:    fast 16ths */
            {90, 60, 70, 60, 90, 60, 70, 60, 90, 60, 70, 60, 90, 60, 70, 60},
            /* Clap */
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            /* OpenHH: accents */
            {0, 0, 0, 0, 0, 0, 0, 80, 0, 0, 0, 0, 0, 0, 0, 80},
            /* Perc: ride */
            {70, 0, 70, 0, 70, 0, 70, 0, 70, 0, 70, 0, 70, 0, 70, 0},
        },
        .suggested_bpm = 174.0f,
    },
    /* ── Reggaeton ─────────────────────────────────────────────────── */
    {
        .name = "Reggaeton",
        .tracks = {
            /* Kick:  dembow pattern */
            {120, 0, 0, 100, 0, 0, 0, 0, 120, 0, 0, 100, 0, 0, 0, 0},
            /* Snare: dembow on the AND of 2, AND of 4 */
            {0, 0, 0, 0, 0, 0, 120, 0, 0, 0, 0, 0, 0, 0, 120, 0},
            /* HH */
            {80, 0, 80, 0, 80, 0, 80, 0, 80, 0, 80, 0, 80, 0, 80, 0},
            /* Clap */
            {0, 0, 0, 0, 0, 0, 100, 0, 0, 0, 0, 0, 0, 0, 100, 0},
            /* OpenHH */
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            /* Perc */
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
        },
        .suggested_bpm = 95.0f,
    },
    /* ── Disco ─────────────────────────────────────────────────────── */
    {
        .name = "Disco",
        .tracks = {
            /* Kick:  four-on-floor */
            {120, 0, 0, 0, 120, 0, 0, 0, 120, 0, 0, 0, 120, 0, 0, 0},
            /* Snare: backbeat */
            {0, 0, 0, 0, 120, 0, 0, 0, 0, 0, 0, 0, 120, 0, 0, 0},
            /* HH:    16ths with open on upbeats */
            {100, 70, 80, 70, 100, 70, 80, 70, 100, 70, 80, 70, 100, 70, 80, 70},
            /* Clap */
            {0, 0, 0, 0, 110, 0, 0, 0, 0, 0, 0, 0, 110, 0, 0, 0},
            /* OpenHH: upbeat 8ths */
            {0, 0, 80, 0, 0, 0, 80, 0, 0, 0, 80, 0, 0, 0, 80, 0},
            /* Perc: cowbell */
            {60, 0, 60, 0, 60, 0, 60, 0, 60, 0, 60, 0, 60, 0, 60, 0},
        },
        .suggested_bpm = 120.0f,
    },
    /* ── Techno ────────────────────────────────────────────────────── */
    {
        .name = "Techno",
        .tracks = {
            /* Kick:  four-on-floor, hard */
            {127, 0, 0, 0, 120, 0, 0, 0, 127, 0, 0, 0, 120, 0, 0, 0},
            /* Snare: rimshot on offbeats */
            {0, 0, 0, 0, 0, 0, 80, 0, 0, 0, 0, 0, 0, 0, 80, 0},
            /* HH:    fast closed 16ths */
            {100, 70, 80, 70, 100, 70, 80, 70, 100, 70, 80, 70, 100, 70, 80, 70},
            /* Clap:  on the 2 and 4 */
            {0, 0, 0, 0, 110, 0, 0, 0, 0, 0, 0, 0, 110, 0, 0, 0},
            /* OpenHH */
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            /* Perc */
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
        },
        .suggested_bpm = 130.0f,
    },
    /* ── Breakbeat ─────────────────────────────────────────────────── */
    {
        .name = "Breakbeat",
        .tracks = {
            /* Kick:  broken pattern */
            {120, 0, 0, 0, 0, 0, 0, 100, 0, 0, 110, 0, 0, 0, 0, 0},
            /* Snare: displaced */
            {0, 0, 0, 0, 127, 0, 0, 0, 0, 0, 0, 0, 0, 0, 127, 0},
            /* HH:    8ths */
            {90, 0, 70, 0, 90, 0, 70, 0, 90, 0, 70, 0, 90, 0, 70, 0},
            /* Clap */
            {0, 0, 0, 0, 100, 0, 0, 0, 0, 0, 0, 0, 0, 0, 100, 0},
            /* OpenHH */
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 80, 0, 0, 0, 0},
            /* Perc */
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
        },
        .suggested_bpm = 130.0f,
    },
    /* ── Lo-fi Hip-Hop ─────────────────────────────────────────────── */
    {
        .name = "Lo-fi",
        .tracks = {
            /* Kick:  lazy boom bap */
            {100, 0, 0, 0, 0, 0, 90, 0, 0, 0, 0, 80, 100, 0, 0, 0},
            /* Snare: 2 and 4 (soft) */
            {0, 0, 0, 0, 100, 0, 0, 40, 0, 0, 0, 0, 100, 0, 0, 40},
            /* HH:    swing feel */
            {70, 0, 50, 0, 70, 0, 50, 0, 70, 0, 50, 0, 70, 0, 50, 0},
            /* Clap */
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            /* OpenHH */
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 60, 0, 0, 0, 0},
            /* Perc */
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
        },
        .suggested_bpm = 80.0f,
    },
    /* ── Rock / Pop ────────────────────────────────────────────────── */
    {
        .name = "Rock",
        .tracks = {
            /* Kick */
            {120, 0, 0, 0, 0, 0, 0, 0, 120, 0, 100, 0, 0, 0, 0, 0},
            /* Snare */
            {0, 0, 0, 0, 127, 0, 0, 0, 0, 0, 0, 0, 127, 0, 0, 0},
            /* HH:    8ths */
            {100, 0, 80, 0, 100, 0, 80, 0, 100, 0, 80, 0, 100, 0, 80, 0},
            /* Clap */
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            /* OpenHH */
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 80, 0},
            /* Perc: ride */
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
        },
        .suggested_bpm = 120.0f,
    },
    /* ── Afrobeat ──────────────────────────────────────────────────── */
    {
        .name = "Afrobeat",
        .tracks = {
            /* Kick */
            {120, 0, 0, 0, 0, 0, 100, 0, 0, 0, 0, 0, 120, 0, 0, 0},
            /* Snare: ghost + backbeat */
            {0, 0, 0, 40, 110, 0, 0, 0, 0, 0, 40, 0, 110, 0, 0, 0},
            /* HH:    busy pattern */
            {90, 60, 80, 60, 90, 60, 80, 60, 90, 60, 80, 60, 90, 60, 80, 60},
            /* Clap */
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            /* OpenHH */
            {0, 0, 0, 0, 0, 0, 0, 80, 0, 0, 0, 0, 0, 0, 0, 80},
            /* Perc: shaker */
            {70, 50, 70, 50, 70, 50, 70, 50, 70, 50, 70, 50, 70, 50, 70, 50},
        },
        .suggested_bpm = 108.0f,
    },
    /* ── Bossa Nova ────────────────────────────────────────────────── */
    {
        .name = "Bossa Nova",
        .tracks = {
            /* Kick:  bossa pattern */
            {100, 0, 0, 90, 0, 0, 100, 0, 0, 0, 90, 0, 0, 0, 0, 0},
            /* Snare: cross-stick */
            {0, 0, 80, 0, 0, 0, 80, 0, 0, 0, 80, 0, 0, 0, 80, 0},
            /* HH:    brush pattern */
            {70, 0, 70, 0, 70, 0, 70, 0, 70, 0, 70, 0, 70, 0, 70, 0},
            /* Clap */
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            /* OpenHH */
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            /* Perc: shaker */
            {50, 40, 50, 40, 50, 40, 50, 40, 50, 40, 50, 40, 50, 40, 50, 40},
        },
        .suggested_bpm = 140.0f,
    },
};
#define NUM_DRUM_PRESETS ((int)(sizeof(s_drum_presets) / sizeof(s_drum_presets[0])))

/* ─── Bass Line Definitions ─────────────────────────────────────────────── */

typedef struct {
    uint8_t note;       /* MIDI note (0 = rest) */
    uint8_t velocity;   /* 0 = rest */
    float   length;     /* note length in steps */
} bass_step_t;

typedef struct {
    const char *name;
    bass_step_t steps[16];
    int synth_preset;       /* recommended synth preset index */
    float suggested_bpm;
} bass_preset_t;

/* Common bass note references:
 * C2=36, D2=38, E2=40, F2=41, G2=43, A2=45, Bb2=46, B2=47
 * C3=48, D3=50, E3=52, F3=53, G3=55
 */

static const bass_preset_t s_bass_presets[] = {
    /* ── Octave Bass (house/disco) ─────────────────────────────────── */
    {
        .name = "Octave Bass",
        .steps = {
            {36, 110, 1}, {48, 80, 1}, {0,0,0}, {48, 70, 1},
            {36, 110, 1}, {48, 80, 1}, {0,0,0}, {48, 70, 1},
            {36, 110, 1}, {48, 80, 1}, {0,0,0}, {48, 70, 1},
            {36, 110, 1}, {48, 80, 1}, {0,0,0}, {48, 70, 1},
        },
        .synth_preset = 0,  /* Bass */
        .suggested_bpm = 124.0f,
    },
    /* ── Root Fifth (pop/rock) ─────────────────────────────────────── */
    {
        .name = "Root-Fifth",
        .steps = {
            {36, 110, 2}, {0,0,0}, {43, 90, 2}, {0,0,0},
            {36, 100, 2}, {0,0,0}, {43, 80, 2}, {0,0,0},
            {36, 110, 2}, {0,0,0}, {43, 90, 2}, {0,0,0},
            {36, 100, 1}, {0,0,0}, {43, 90, 1}, {41, 70, 1},
        },
        .synth_preset = 0,
        .suggested_bpm = 120.0f,
    },
    /* ── Walking Bass (jazz/funk) ──────────────────────────────────── */
    {
        .name = "Walking Bass",
        .steps = {
            {36, 100, 1}, {0,0,0}, {38, 90, 1}, {0,0,0},
            {40, 100, 1}, {0,0,0}, {41, 90, 1}, {0,0,0},
            {43, 100, 1}, {0,0,0}, {41, 90, 1}, {0,0,0},
            {40, 100, 1}, {0,0,0}, {38, 90, 1}, {0,0,0},
        },
        .synth_preset = 14, /* Moog Bass */
        .suggested_bpm = 100.0f,
    },
    /* ── Synth Sub (EDM) ───────────────────────────────────────────── */
    {
        .name = "Sub Bass",
        .steps = {
            {36, 120, 4}, {0,0,0}, {0,0,0}, {0,0,0},
            {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0},
            {36, 110, 2}, {0,0,0}, {39, 100, 2}, {0,0,0},
            {43, 100, 2}, {0,0,0}, {41, 90, 2}, {0,0,0},
        },
        .synth_preset = 46, /* Sub Bass (sine) */
        .suggested_bpm = 128.0f,
    },
    /* ── 808 Slide (trap) ──────────────────────────────────────────── */
    {
        .name = "808 Trap",
        .steps = {
            {36, 120, 3}, {0,0,0}, {0,0,0}, {0,0,0},
            {0,0,0}, {0,0,0}, {36, 100, 2}, {0,0,0},
            {0,0,0}, {0,0,0}, {0,0,0}, {39, 110, 2},
            {0,0,0}, {0,0,0}, {36, 100, 2}, {0,0,0},
        },
        .synth_preset = 46, /* Sub Bass */
        .suggested_bpm = 140.0f,
    },
    /* ── Reese (DnB) ───────────────────────────────────────────────── */
    {
        .name = "Reese DnB",
        .steps = {
            {36, 110, 2}, {0,0,0}, {0,0,0}, {0,0,0},
            {0,0,0}, {43, 90, 2}, {0,0,0}, {0,0,0},
            {41, 100, 2}, {0,0,0}, {0,0,0}, {0,0,0},
            {0,0,0}, {38, 90, 2}, {0,0,0}, {36, 80, 1},
        },
        .synth_preset = 21, /* Reese Bass */
        .suggested_bpm = 174.0f,
    },
    /* ── Acid (303-style) ──────────────────────────────────────────── */
    {
        .name = "Acid 303",
        .steps = {
            {36, 120, 1}, {0,0,0}, {36, 80, 1}, {48, 90, 1},
            {36, 110, 1}, {0,0,0}, {39, 100, 1}, {0,0,0},
            {36, 120, 1}, {41, 70, 1}, {0,0,0}, {43, 90, 1},
            {36, 110, 1}, {0,0,0}, {48, 80, 1}, {36, 90, 1},
        },
        .synth_preset = 15, /* 303 Acid */
        .suggested_bpm = 130.0f,
    },
    /* ── Disco Funk ────────────────────────────────────────────────── */
    {
        .name = "Disco Funk",
        .steps = {
            {36, 110, 1}, {0,0,0}, {36, 70, 1}, {43, 80, 1},
            {0,0,0}, {36, 100, 1}, {0,0,0}, {43, 80, 1},
            {41, 100, 1}, {0,0,0}, {41, 70, 1}, {43, 80, 1},
            {0,0,0}, {41, 100, 1}, {0,0,0}, {36, 80, 1},
        },
        .synth_preset = 14, /* Moog Bass */
        .suggested_bpm = 120.0f,
    },
    /* ── Reggaeton Bass ────────────────────────────────────────────── */
    {
        .name = "Dembow Bass",
        .steps = {
            {36, 120, 2}, {0,0,0}, {0,0,0}, {36, 80, 1},
            {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0},
            {36, 120, 2}, {0,0,0}, {0,0,0}, {36, 80, 1},
            {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0},
        },
        .synth_preset = 46,
        .suggested_bpm = 95.0f,
    },
    /* ── Minimal Pulse ─────────────────────────────────────────────── */
    {
        .name = "Minimal Pulse",
        .steps = {
            {36, 100, 1}, {0,0,0}, {0,0,0}, {0,0,0},
            {36, 90, 1}, {0,0,0}, {0,0,0}, {0,0,0},
            {36, 100, 1}, {0,0,0}, {0,0,0}, {0,0,0},
            {36, 90, 1}, {0,0,0}, {0,0,0}, {38, 70, 1},
        },
        .synth_preset = 0,
        .suggested_bpm = 120.0f,
    },
};
#define NUM_BASS_PRESETS ((int)(sizeof(s_bass_presets) / sizeof(s_bass_presets[0])))

/* ─── Application Logic ─────────────────────────────────────────────────── */

static void apply_drum_preset(sq_engine_t *engine, int preset_idx)
{
    int pat_idx = engine->transport.current_pattern;
    if (pat_idx < 0 || (uint32_t)pat_idx >= engine->num_patterns) return;
    sq_pattern_t *p = &engine->patterns[pat_idx];

    const drum_preset_t *preset = &s_drum_presets[preset_idx];

    /* Save for undo */
    undo_push(engine);

    /* Apply to existing sampler tracks (up to 6) */
    int applied = 0;
    for (uint32_t t = 0; t < p->num_tracks && applied < 6; t++) {
        if (p->tracks[t].type != TRACK_SAMPLER) continue;

        for (int s = 0; s < 16 && (uint32_t)s < p->tracks[t].length; s++) {
            p->tracks[t].steps[s].velocity = preset->tracks[applied][s];
        }
        applied++;
    }

    LOG_INFO("Applied drum preset: %s (BPM suggestion: %.0f)",
             preset->name, preset->suggested_bpm);
}

static void apply_bass_preset(sq_engine_t *engine, int preset_idx)
{
    int pat_idx = engine->transport.current_pattern;
    if (pat_idx < 0 || (uint32_t)pat_idx >= engine->num_patterns) return;
    sq_pattern_t *p = &engine->patterns[pat_idx];

    const bass_preset_t *preset = &s_bass_presets[preset_idx];

    /* Save for undo */
    undo_push(engine);

    /* Find first synth track, or create one */
    int target = -1;
    for (uint32_t t = 0; t < p->num_tracks; t++) {
        if (p->tracks[t].type == TRACK_SYNTH) {
            target = (int)t;
            break;
        }
    }

    if (target < 0 && p->num_tracks < SQ_MAX_TRACKS) {
        /* Add a new synth track */
        target = (int)p->num_tracks;
        p->num_tracks++;
        p->tracks[target].type = TRACK_SYNTH;
        p->tracks[target].length = 16;
        p->tracks[target].volume = 0.7f;
        p->tracks[target].pan = 0.0f;
        p->tracks[target].mute = false;
        p->tracks[target].solo = false;
    }

    if (target < 0) return;

    /* Set the synth preset */
    p->tracks[target].synth_preset = preset->synth_preset;

    /* Write the bass notes */
    for (int s = 0; s < 16 && (uint32_t)s < p->tracks[target].length; s++) {
        p->tracks[target].steps[s].velocity = preset->steps[s].velocity;
        p->tracks[target].steps[s].note     = preset->steps[s].note;
        p->tracks[target].steps[s].length   = preset->steps[s].length;
        p->tracks[target].steps[s].pitch_offset = 0;
    }

    LOG_INFO("Applied bass preset: %s (synth preset %d, BPM suggestion: %.0f)",
             preset->name, preset->synth_preset, preset->suggested_bpm);
}

/* ─── GUI ───────────────────────────────────────────────────────────────── */

static int s_drum_preset_idx = 0;
static int s_bass_preset_idx = 0;
static int s_show_presets = 0;

void pattern_presets_draw(struct nk_context *ctx, sq_engine_t *engine)
{
    if (!s_show_presets) return;

    if (nk_begin(ctx, "Pattern Presets",
                 nk_rect(200, 100, 450, 380),
                 NK_WINDOW_BORDER | NK_WINDOW_TITLE |
                 NK_WINDOW_MOVABLE | NK_WINDOW_CLOSABLE))
    {
        /* ── Drum Presets ──────────────────────────────────────────── */
        nk_layout_row_dynamic(ctx, 20, 1);
        nk_label(ctx, "Drum Patterns:", NK_TEXT_LEFT);

        nk_layout_row_dynamic(ctx, 25, 3);

        /* Preset selector */
        {
            const char *names[NUM_DRUM_PRESETS];
            for (int i = 0; i < NUM_DRUM_PRESETS; i++)
                names[i] = s_drum_presets[i].name;
            nk_combobox(ctx, names, NUM_DRUM_PRESETS, &s_drum_preset_idx,
                        20, nk_vec2(180, 250));
        }

        /* Apply button */
        if (nk_button_label(ctx, "Apply Drums")) {
            apply_drum_preset(engine, s_drum_preset_idx);
        }

        /* Set BPM button */
        {
            char bpm_label[32];
            snprintf(bpm_label, sizeof(bpm_label), "BPM: %.0f",
                     s_drum_presets[s_drum_preset_idx].suggested_bpm);
            if (nk_button_label(ctx, bpm_label)) {
                engine->transport.bpm =
                    (double)s_drum_presets[s_drum_preset_idx].suggested_bpm;
            }
        }

        /* Separator */
        nk_layout_row_dynamic(ctx, 10, 1);
        nk_spacing(ctx, 1);

        /* ── Bass Presets ──────────────────────────────────────────── */
        nk_layout_row_dynamic(ctx, 20, 1);
        nk_label(ctx, "Bass Lines:", NK_TEXT_LEFT);

        nk_layout_row_dynamic(ctx, 25, 3);

        /* Preset selector */
        {
            const char *names[NUM_BASS_PRESETS];
            for (int i = 0; i < NUM_BASS_PRESETS; i++)
                names[i] = s_bass_presets[i].name;
            nk_combobox(ctx, names, NUM_BASS_PRESETS, &s_bass_preset_idx,
                        20, nk_vec2(180, 250));
        }

        /* Apply button */
        if (nk_button_label(ctx, "Apply Bass")) {
            apply_bass_preset(engine, s_bass_preset_idx);
        }

        /* Set BPM button */
        {
            char bpm_label[32];
            snprintf(bpm_label, sizeof(bpm_label), "BPM: %.0f",
                     s_bass_presets[s_bass_preset_idx].suggested_bpm);
            if (nk_button_label(ctx, bpm_label)) {
                engine->transport.bpm =
                    (double)s_bass_presets[s_bass_preset_idx].suggested_bpm;
            }
        }

        nk_layout_row_dynamic(ctx, 10, 1);
        nk_spacing(ctx, 1);

        /* ── Combined (apply both drum + bass) ─────────────────────── */
        nk_layout_row_dynamic(ctx, 30, 2);
        if (nk_button_label(ctx, "Apply Both")) {
            apply_drum_preset(engine, s_drum_preset_idx);
            apply_bass_preset(engine, s_bass_preset_idx);
            engine->transport.bpm =
                (double)s_drum_presets[s_drum_preset_idx].suggested_bpm;
        }
        if (nk_button_label(ctx, "Clear Pattern")) {
            int pat_idx = engine->transport.current_pattern;
            if (pat_idx >= 0 && (uint32_t)pat_idx < engine->num_patterns) {
                undo_push(engine);
                sq_pattern_t *p = &engine->patterns[pat_idx];
                for (uint32_t t = 0; t < p->num_tracks; t++) {
                    for (uint32_t s = 0; s < p->tracks[t].length; s++) {
                        memset(&p->tracks[t].steps[s], 0, sizeof(sq_step_t));
                    }
                }
                LOG_INFO("Cleared pattern %d", pat_idx);
            }
        }

        /* Info */
        nk_layout_row_dynamic(ctx, 14, 1);
        nk_label_colored(ctx,
            "Tip: Select a synth track to see the piano roll below",
            NK_TEXT_LEFT, nk_rgb(140, 140, 140));
        nk_label_colored(ctx,
            "Click notes in piano roll to edit. Right-click to delete.",
            NK_TEXT_LEFT, nk_rgb(140, 140, 140));
    } else {
        s_show_presets = 0;
    }
    nk_end(ctx);
}

/* Toggle function — called from toolbar */
int *pattern_presets_visible_ptr(void)
{
    return &s_show_presets;
}
