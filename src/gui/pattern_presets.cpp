/*
 * pattern_presets.cpp — Drum pattern and bass line preset library (Dear ImGui port).
 *
 * Contains common industry-standard drum patterns (four-on-floor, trap,
 * boom bap, DnB, reggaeton, etc.) and bass line generators that write
 * notes into synth tracks.
 *
 * Drum patterns assume 16-step resolution (4/4 time, 4 steps per beat).
 * Bass lines write MIDI notes into synth track steps.
 */

#include "imgui.h"

extern "C" {
#include "engine/engine.h"
#include "gui/undo.h"
}

extern "C" {
#define LOG_TAG "presets"
#include "core/log.h"
}

#include <cstring>
#include <cstdio>

/* --- Drum Pattern Definitions --- */

struct drum_preset_t {
    const char *name;
    uint8_t tracks[6][16];  /* velocity per track per step */
    float   suggested_bpm;
};

static const drum_preset_t s_drum_presets[] = {
    /* House / Four-on-floor — solid kick every beat, snare 2&4,
       closed hats on all 8ths, open hat on offbeats */
    {
        "House",
        {
            {120, 0, 0, 0, 115, 0, 0, 0, 120, 0, 0, 0, 115, 0, 0, 0},  /* kick: four-on-floor */
            {0, 0, 0, 0, 127, 0, 0, 0, 0, 0, 0, 0, 127, 0, 0, 0},      /* snare: 2&4 */
            {100, 0, 80, 0, 100, 0, 80, 0, 100, 0, 80, 0, 100, 0, 80, 0}, /* closed hat: all 8ths */
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},          /* clap: none */
            {0, 0, 90, 0, 0, 0, 90, 0, 0, 0, 90, 0, 0, 0, 90, 0},      /* open hat: offbeats */
            {50, 0, 50, 0, 50, 0, 50, 0, 50, 0, 50, 0, 50, 0, 50, 0},  /* shaker: subtle 8ths */
        },
        124.0f,
    },
    /* Boom Bap (Hip-Hop) — lazy kick, snare 2&4, swing hats */
    {
        "Boom Bap",
        {
            {120, 0, 0, 0, 0, 0, 0, 100, 0, 0, 0, 0, 110, 0, 0, 0},   /* kick: 1, &-of-2, &-of-3 */
            {0, 0, 0, 0, 127, 0, 0, 0, 0, 0, 0, 0, 127, 0, 0, 0},      /* snare: 2&4 */
            {90, 0, 55, 0, 90, 0, 50, 0, 90, 0, 55, 0, 90, 0, 60, 0},  /* hat: swing feel, accents on beats */
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},          /* clap: none */
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 70, 0, 0, 0, 0},         /* open hat: one accent */
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
        },
        92.0f,
    },
    /* Trap */
    {
        "Trap",
        {
            {120, 0, 0, 0, 0, 0, 100, 0, 0, 0, 0, 110, 0, 0, 100, 0},
            {0, 0, 0, 0, 0, 0, 0, 0, 127, 0, 0, 0, 0, 0, 0, 0},
            {100, 60, 80, 60, 100, 60, 80, 60, 100, 60, 80, 60, 100, 60, 80, 90},
            {0, 0, 0, 0, 0, 0, 0, 0, 110, 0, 0, 0, 0, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 80},
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
        },
        140.0f,
    },
    /* Drum & Bass — Amen break style: kick on 1 & &-of-3,
       snare on 2&4, frantic 16th hats/ride */
    {
        "DnB",
        {
            {127, 0, 0, 0, 0, 0, 0, 0, 0, 0, 115, 0, 0, 0, 0, 0},   /* kick: 1, &-of-3 */
            {0, 0, 0, 0, 127, 0, 0, 0, 0, 0, 0, 0, 127, 0, 0, 0},    /* snare: 2&4 */
            {100, 70, 80, 70, 100, 70, 80, 70, 100, 70, 80, 70, 100, 70, 80, 70}, /* hat: every 16th */
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 80},       /* open hat: end fill */
            {80, 60, 80, 60, 80, 60, 80, 60, 80, 60, 80, 60, 80, 60, 80, 60}, /* ride: every 16th */
        },
        174.0f,
    },
    /* Reggaeton — Dembow rhythm: kick 1&3, snare/rim on offbeats */
    {
        "Reggaeton",
        {
            {120, 0, 0, 0, 0, 0, 0, 0, 120, 0, 0, 0, 0, 0, 0, 0},   /* kick: 1&3 */
            {0, 0, 0, 110, 0, 0, 0, 110, 0, 0, 0, 110, 0, 0, 0, 110},/* snare: dembow offbeats */
            {80, 0, 80, 0, 80, 0, 80, 0, 80, 0, 80, 0, 80, 0, 80, 0},/* hat: 8ths */
            {0, 0, 0, 100, 0, 0, 0, 100, 0, 0, 0, 100, 0, 0, 0, 100},/* rim: doubling snare */
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
        },
        95.0f,
    },
    /* Disco — four-on-floor, open hat on every offbeat, snare 2&4, shaker */
    {
        "Disco",
        {
            {120, 0, 0, 0, 115, 0, 0, 0, 120, 0, 0, 0, 115, 0, 0, 0},/* kick: four-on-floor */
            {0, 0, 0, 0, 120, 0, 0, 0, 0, 0, 0, 0, 120, 0, 0, 0},    /* snare: 2&4 */
            {90, 0, 60, 0, 90, 0, 60, 0, 90, 0, 60, 0, 90, 0, 60, 0},/* closed hat: 8ths, ghost offbeats */
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {0, 0, 100, 0, 0, 0, 100, 0, 0, 0, 100, 0, 0, 0, 100, 0},/* open hat: offbeats driving */
            {60, 50, 60, 50, 60, 50, 60, 50, 60, 50, 60, 50, 60, 50, 60, 50}, /* shaker: 16ths */
        },
        120.0f,
    },
    /* Techno — hard four-on-floor, sparse hats, minimal clap */
    {
        "Techno",
        {
            {127, 0, 0, 0, 127, 0, 0, 0, 127, 0, 0, 0, 127, 0, 0, 0},/* kick: hard four-on-floor */
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},        /* snare: none */
            {0, 0, 0, 0, 80, 0, 0, 0, 0, 0, 0, 0, 80, 0, 0, 0},      /* hat: sparse, on 2&4 */
            {0, 0, 0, 0, 120, 0, 0, 0, 0, 0, 0, 0, 120, 0, 0, 0},    /* clap: on 2&4 */
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 80},       /* open hat: end of bar */
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
        },
        130.0f,
    },
    /* Breakbeat — broken funky kick, snare with ghost, ride groove */
    {
        "Breakbeat",
        {
            {120, 0, 0, 0, 0, 0, 0, 110, 0, 0, 100, 0, 0, 0, 0, 100},/* kick: broken funk pattern */
            {0, 0, 0, 0, 120, 0, 0, 0, 0, 50, 0, 0, 0, 0, 120, 0},   /* snare: 2&4 with ghost */
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},        /* hat: off (ride drives) */
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 80, 0, 0, 0, 0, 0, 0, 0, 0, 0},       /* open hat: one accent */
            {90, 60, 80, 60, 90, 60, 80, 60, 90, 60, 80, 60, 90, 60, 80, 60}, /* ride: 16th groove */
        },
        130.0f,
    },
    /* Lo-fi Hip-Hop — lazy, ghost notes, imperfect velocity */
    {
        "Lo-fi",
        {
            {95, 0, 0, 0, 0, 0, 0, 85, 0, 0, 0, 75, 90, 0, 0, 0},   /* kick: lazy, soft */
            {0, 0, 0, 0, 95, 0, 0, 0, 0, 0, 0, 0, 95, 0, 0, 40},    /* snare: 2&4, ghost end */
            {65, 40, 50, 35, 65, 45, 50, 40, 60, 35, 55, 40, 65, 45, 50, 35}, /* hat: uneven velocity */
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 55, 0, 0, 0, 0},       /* open hat: one ghost accent */
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
        },
        80.0f,
    },
    /* Rock — kick 1&3, snare 2&4, steady 8th hats, simple and driving */
    {
        "Rock",
        {
            {120, 0, 0, 0, 0, 0, 0, 0, 120, 0, 0, 0, 0, 0, 0, 0},   /* kick: 1&3 */
            {0, 0, 0, 0, 127, 0, 0, 0, 0, 0, 0, 0, 127, 0, 0, 0},    /* snare: 2&4 */
            {100, 0, 75, 0, 100, 0, 75, 0, 100, 0, 75, 0, 100, 0, 75, 0}, /* hat: steady 8ths */
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 80},       /* open hat: bar end */
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
        },
        120.0f,
    },
    /* Afrobeat — polyrhythmic kick, shaker 8ths, bell pattern, ghost snare */
    {
        "Afrobeat",
        {
            {110, 0, 0, 100, 0, 0, 110, 0, 0, 0, 100, 0, 0, 110, 0, 0}, /* kick: cross-bar polyrhythm */
            {0, 0, 0, 45, 110, 0, 0, 0, 0, 0, 45, 0, 110, 0, 0, 0},    /* snare: 2&4 with ghosts */
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},          /* hat: off */
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 0, 80, 0, 0, 0, 0, 0, 0, 0, 80},        /* open hat: accents */
            {80, 55, 80, 55, 80, 55, 80, 55, 80, 55, 80, 55, 80, 55, 80, 55}, /* shaker: driving 16ths */
        },
        108.0f,
    },
    /* Bossa Nova — cross-stick, syncopated kick, brushed hat, soft */
    {
        "Bossa Nova",
        {
            {90, 0, 0, 85, 0, 0, 90, 0, 0, 0, 0, 85, 0, 0, 0, 0},   /* kick: syncopated */
            {0, 0, 70, 0, 0, 70, 0, 0, 0, 0, 70, 0, 0, 70, 0, 0},    /* cross-stick: bossa clave */
            {55, 40, 55, 40, 55, 40, 55, 40, 55, 40, 55, 40, 55, 40, 55, 40}, /* hat: brushed 16ths, low vel */
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {45, 35, 45, 35, 45, 35, 45, 35, 45, 35, 45, 35, 45, 35, 45, 35}, /* shaker: light 16ths */
        },
        140.0f,
    },
    /* Punk — fast driving kick-snare, steady hats, crash accents */
    {
        "Punk",
        {
            {127, 0, 127, 0, 127, 0, 127, 0, 127, 0, 127, 0, 127, 0, 127, 0},
            {0, 0, 0, 0, 127, 0, 0, 0, 0, 0, 0, 0, 127, 0, 0, 0},
            {110, 0, 110, 0, 110, 0, 110, 0, 110, 0, 110, 0, 110, 0, 110, 0},
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {120, 0, 0, 0, 0, 0, 0, 0, 120, 0, 0, 0, 0, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
        },
        180.0f,
    },
    /* Surf Rock — tom-heavy, driving rhythm, snare on 2&4 */
    {
        "Surf",
        {
            {110, 0, 0, 0, 100, 0, 0, 0, 110, 0, 0, 0, 100, 0, 0, 0},
            {0, 0, 0, 0, 120, 0, 0, 0, 0, 0, 0, 0, 120, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {100, 0, 90, 0, 0, 0, 100, 0, 90, 0, 0, 0, 100, 0, 90, 80},
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {80, 0, 80, 0, 80, 0, 80, 0, 80, 0, 80, 0, 80, 0, 80, 0},
        },
        150.0f,
    },
    /* Metal — double kick, heavy snare, fast hi-hats, china accents */
    {
        "Metal",
        {
            {127, 0, 120, 0, 127, 0, 120, 0, 127, 0, 120, 0, 127, 0, 120, 0},
            {0, 0, 0, 0, 127, 0, 0, 0, 0, 0, 0, 0, 127, 0, 0, 0},
            {100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100},
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 110, 0, 0, 0, 0, 0, 0, 0, 110, 0},
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
        },
        160.0f,
    },
    /* Indie — syncopated, ghost notes, space */
    {
        "Indie",
        {
            {100, 0, 0, 0, 0, 0, 0, 0, 90, 0, 0, 80, 0, 0, 0, 0},
            {0, 0, 0, 0, 100, 0, 0, 0, 0, 0, 0, 0, 100, 0, 0, 40},
            {60, 0, 0, 50, 0, 0, 60, 0, 0, 50, 0, 0, 60, 0, 50, 0},
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 70, 0, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
        },
        115.0f,
    },
    /* Trap 808 — booming kick, snare on 3rd beat, rapid hi-hat rolls */
    {
        "Trap 808",
        {
            {127, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 120, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 0, 0, 127, 0, 0, 0, 0, 0, 0, 0},
            {90, 50, 70, 50, 90, 60, 80, 50, 90, 50, 70, 60, 100, 70, 90, 80},
            {0, 0, 0, 0, 0, 0, 0, 0, 120, 0, 0, 0, 0, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 100},
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
        },
        145.0f,
    },
    /* Darkwave — four-on-floor kick, steady 8th hi-hats, sharp snare */
    {
        "Darkwave",
        {
            {120, 0, 0, 0, 120, 0, 0, 0, 120, 0, 0, 0, 120, 0, 0, 0},
            {0, 0, 0, 0, 110, 0, 0, 0, 0, 0, 0, 0, 110, 0, 0, 0},
            {80, 0, 80, 0, 80, 0, 80, 0, 80, 0, 80, 0, 80, 0, 80, 0},
            {0, 0, 0, 0, 100, 0, 0, 0, 0, 0, 0, 0, 100, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 80, 0},
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
        },
        118.0f,
    },
    /* Synthwave — punchy kick, gated snare, open hi-hats, tom fills */
    {
        "Synthwave",
        {
            {120, 0, 0, 0, 120, 0, 0, 0, 120, 0, 0, 0, 120, 0, 0, 0},
            {0, 0, 0, 0, 127, 0, 0, 0, 0, 0, 0, 0, 127, 0, 0, 0},
            {80, 0, 0, 80, 80, 0, 0, 80, 80, 0, 0, 80, 80, 0, 0, 80},
            {0, 0, 0, 0, 110, 0, 0, 0, 0, 0, 0, 0, 110, 0, 0, 0},
            {0, 0, 80, 0, 0, 0, 0, 0, 0, 0, 80, 0, 0, 0, 0, 0},
            {70, 0, 0, 0, 70, 0, 0, 0, 70, 0, 0, 0, 70, 0, 0, 0},
        },
        105.0f,
    },
};
#define NUM_DRUM_PRESETS ((int)(sizeof(s_drum_presets) / sizeof(s_drum_presets[0])))

/* --- Bass Line Definitions --- */

struct bass_step_t {
    uint8_t note;
    uint8_t velocity;
    float   length;
};

struct bass_preset_t {
    const char *name;
    bass_step_t steps[16];
    int synth_preset;
    float suggested_bpm;
};

static const bass_preset_t s_bass_presets[] = {
    /* Octave Bass — house style, C1/C2 bounce with longer sustain */
    {"Octave Bass", {{24,110,2},{0,0,0},{0,0,0},{36,80,2},{0,0,0},{0,0,0},{24,100,2},{0,0,0},{0,0,0},{36,75,2},{0,0,0},{0,0,0},{24,110,2},{0,0,0},{36,80,2},{0,0,0}}, 14, 124.0f},
    /* Root-Fifth — C1 root, G1 fifth, long sustains */
    {"Root-Fifth", {{24,110,4},{0,0,0},{0,0,0},{0,0,0},{31,90,3},{0,0,0},{0,0,0},{0,0,0},{24,100,4},{0,0,0},{0,0,0},{0,0,0},{31,80,3},{0,0,0},{0,0,0},{0,0,0}}, 14, 120.0f},
    /* Walking Bass — C2 range, smoother feel with length 2 */
    {"Walking Bass", {{36,100,2},{0,0,0},{38,85,2},{0,0,0},{40,95,2},{0,0,0},{41,85,2},{0,0,0},{43,100,2},{0,0,0},{41,85,2},{0,0,0},{40,95,2},{0,0,0},{38,85,2},{36,75,2}}, 14, 100.0f},
    /* Sub Bass — deep C1, half-bar sustain, 2 notes max */
    {"Sub Bass", {{24,120,8},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{22,110,8},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0}}, 46, 128.0f},
    /* 808 Trap — massive C0 sub, one note per bar */
    {"808 Trap", {{12,127,14},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0}}, 50, 140.0f},
    /* Reese DnB — dark C1 reese, longer notes */
    {"Reese DnB", {{24,110,3},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{31,90,3},{0,0,0},{0,0,0},{29,100,3},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{27,90,3},{0,0,0},{24,80,2}}, 21, 174.0f},
    /* Acid 303 — busy pattern dropped to C1 range */
    {"Acid 303", {{24,120,1},{0,0,0},{24,75,1},{36,95,1},{24,110,1},{0,0,0},{27,100,1},{0,0,0},{24,120,1},{29,70,1},{0,0,0},{31,95,1},{24,110,1},{0,0,0},{36,80,1},{24,90,1}}, 15, 130.0f},
    /* Disco Funk — funky C1 base, octave to C2 */
    {"Disco Funk", {{24,110,1},{0,0,0},{24,65,1},{31,85,1},{0,0,0},{24,100,1},{0,0,0},{36,80,1},{29,100,1},{0,0,0},{29,65,1},{31,85,1},{0,0,0},{29,100,1},{0,0,0},{24,80,1}}, 14, 120.0f},
    /* Dembow Bass — massive C0 sub, long sustains */
    {"Dembow Bass", {{12,120,4},{0,0,0},{0,0,0},{0,0,0},{15,80,4},{0,0,0},{0,0,0},{0,0,0},{12,120,4},{0,0,0},{0,0,0},{0,0,0},{15,80,4},{0,0,0},{0,0,0},{0,0,0}}, 46, 95.0f},
    /* Minimal Pulse — sparse C1, 4 notes max */
    {"Minimal Pulse", {{24,100,2},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{24,85,2},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{22,75,2}}, 30, 120.0f},
    /* Punk Drive — every-step C1 root bass */
    {"Punk Drive", {{24,120,1},{24,110,1},{24,120,1},{24,110,1},{24,120,1},{24,110,1},{24,120,1},{24,110,1},{24,120,1},{24,110,1},{24,120,1},{24,110,1},{24,120,1},{24,110,1},{24,120,1},{24,110,1}}, 0, 180.0f},
    /* Surf Walk — chromatic walk at C1 range */
    {"Surf Walk", {{24,100,1},{25,90,1},{26,100,1},{0,0,0},{28,100,1},{29,90,1},{31,100,1},{0,0,0},{33,100,1},{31,90,1},{29,100,1},{0,0,0},{28,100,1},{26,90,1},{25,100,1},{0,0,0}}, 14, 150.0f},
    /* Metal Chug — staccato C1 palm-mute */
    {"Metal Chug", {{24,127,0.5},{0,0,0},{24,120,0.5},{0,0,0},{24,127,0.5},{24,110,0.5},{0,0,0},{24,120,0.5},{0,0,0},{24,127,0.5},{0,0,0},{19,120,0.5},{0,0,0},{24,127,0.5},{19,110,0.5},{0,0,0}}, 0, 160.0f},
    /* Indie Groove — C1 base with octave jumps to C2 */
    {"Indie Groove", {{24,100,2},{0,0,0},{36,80,1},{0,0,0},{28,90,1},{0,0,0},{0,0,0},{31,70,1},{24,100,2},{26,80,1},{0,0,0},{36,70,1},{28,90,1},{0,0,0},{31,80,1},{0,0,0}}, 14, 115.0f},
    /* Trap 808 Slide — maximum sub, one C0 note, length 14 */
    {"Trap 808 Slide", {{12,127,14},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0}}, 50, 145.0f},
    /* Darkwave Drive — C1 base, minor key (Eb1, Bb0) */
    {"Darkwave Drive", {{24,110,1},{27,100,1},{24,110,1},{27,100,1},{24,110,1},{27,100,1},{24,110,1},{27,100,1},{22,110,1},{24,100,1},{22,110,1},{24,100,1},{19,110,1},{22,100,1},{19,110,1},{22,100,1}}, 54, 118.0f},
    /* Synthwave Groove — melodic C1 base, deeper fifth and octave */
    {"Synthwave Groove", {{24,110,2},{0,0,0},{31,90,1},{36,80,1},{24,100,2},{0,0,0},{31,90,1},{29,80,1},{24,110,2},{0,0,0},{31,90,1},{36,80,1},{24,100,2},{0,0,0},{29,90,1},{31,80,1}}, 57, 105.0f},
};
#define NUM_BASS_PRESETS ((int)(sizeof(s_bass_presets) / sizeof(s_bass_presets[0])))

/* --- Application Logic --- */

static void apply_drum_preset(sq_engine_t *engine, int preset_idx)
{
    int pat_idx = engine->transport.current_pattern;
    if (pat_idx < 0 || (uint32_t)pat_idx >= engine->num_patterns) return;
    sq_pattern_t *p = &engine->patterns[pat_idx];

    const drum_preset_t *preset = &s_drum_presets[preset_idx];

    undo_push(engine);

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

    p->tracks[target].synth_preset = preset->synth_preset;

    for (int s = 0; s < 16 && (uint32_t)s < p->tracks[target].length; s++) {
        p->tracks[target].steps[s].velocity = preset->steps[s].velocity;
        p->tracks[target].steps[s].note     = preset->steps[s].note;
        p->tracks[target].steps[s].length   = preset->steps[s].length;
        p->tracks[target].steps[s].pitch_offset = 0;
    }

    LOG_INFO("Applied bass preset: %s (synth preset %d, BPM suggestion: %.0f)",
             preset->name, preset->synth_preset, preset->suggested_bpm);
}

/* --- GUI State --- */

static int s_drum_preset_idx = 0;
static int s_bass_preset_idx = 0;
static int s_show_presets = 0;

/* --- Draw --- */

extern "C" void pattern_presets_draw(sq_engine_t *engine)
{
    if (!s_show_presets) return;

    bool open = true;
    ImGui::SetNextWindowSize(ImVec2(450, 380), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(200, 100), ImGuiCond_FirstUseEver);

    if (!ImGui::Begin("Pattern Presets", &open)) {
        ImGui::End();
        if (!open) s_show_presets = 0;
        return;
    }

    if (!open) {
        s_show_presets = 0;
        ImGui::End();
        return;
    }

    /* --- Drum Presets --- */
    ImGui::Text("Drum Patterns:");

    /* Build drum preset name list */
    const char *drum_names[NUM_DRUM_PRESETS];
    for (int i = 0; i < NUM_DRUM_PRESETS; i++)
        drum_names[i] = s_drum_presets[i].name;

    ImGui::SetNextItemWidth(180.0f);
    ImGui::Combo("##DrumPreset", &s_drum_preset_idx, drum_names, NUM_DRUM_PRESETS);
    ImGui::SameLine();

    if (ImGui::Button("Apply Drums")) {
        apply_drum_preset(engine, s_drum_preset_idx);
    }
    ImGui::SameLine();

    {
        char bpm_label[32];
        snprintf(bpm_label, sizeof(bpm_label), "BPM: %.0f",
                 s_drum_presets[s_drum_preset_idx].suggested_bpm);
        if (ImGui::Button(bpm_label)) {
            engine->transport.bpm =
                (double)s_drum_presets[s_drum_preset_idx].suggested_bpm;
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    /* --- Bass Presets --- */
    ImGui::Text("Bass Lines:");

    const char *bass_names[NUM_BASS_PRESETS];
    for (int i = 0; i < NUM_BASS_PRESETS; i++)
        bass_names[i] = s_bass_presets[i].name;

    ImGui::SetNextItemWidth(180.0f);
    ImGui::Combo("##BassPreset", &s_bass_preset_idx, bass_names, NUM_BASS_PRESETS);
    ImGui::SameLine();

    if (ImGui::Button("Apply Bass")) {
        apply_bass_preset(engine, s_bass_preset_idx);
    }
    ImGui::SameLine();

    {
        char bpm_label[32];
        snprintf(bpm_label, sizeof(bpm_label), "BPM: %.0f",
                 s_bass_presets[s_bass_preset_idx].suggested_bpm);
        if (ImGui::Button(bpm_label)) {
            engine->transport.bpm =
                (double)s_bass_presets[s_bass_preset_idx].suggested_bpm;
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    /* --- Combined (apply both drum + bass) --- */
    if (ImGui::Button("Apply Both", ImVec2(130, 30))) {
        apply_drum_preset(engine, s_drum_preset_idx);
        apply_bass_preset(engine, s_bass_preset_idx);
        engine->transport.bpm =
            (double)s_drum_presets[s_drum_preset_idx].suggested_bpm;
    }
    ImGui::SameLine();

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.2f, 0.2f, 1.0f));
    if (ImGui::Button("Clear Pattern", ImVec2(130, 30))) {
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
    ImGui::PopStyleColor();

    ImGui::Spacing();

    /* Info tips */
    ImGui::TextColored(ImVec4(0.55f, 0.55f, 0.55f, 1.0f),
                       "Tip: Select a synth track to see the piano roll below");
    ImGui::TextColored(ImVec4(0.55f, 0.55f, 0.55f, 1.0f),
                       "Click notes in piano roll to edit. Right-click to delete.");

    ImGui::End();
}

extern "C" int *pattern_presets_visible_ptr(void)
{
    return &s_show_presets;
}
