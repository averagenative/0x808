/*
 * random_pattern.c — Heuristic drum pattern generator.
 *
 * The 808 default kit slot order (from kits.c) is:
 *   0=kick, 1=snare, 2=closed hat, 3=clap, 4=open hat,
 *   5=cowbell, 6=rimshot, 7=tom
 *
 * We treat the first 8 sampler tracks as those drum roles. Tracks
 * beyond slot 7 fall through to the generic "percussion" profile.
 */

#include "engine/random_pattern.h"

#include <string.h>
#include <time.h>
#include <stdlib.h>

#define LOG_TAG "random"
#include "core/log.h"

/* Local LCG so we don't disturb the global rand() state used elsewhere. */
static uint32_t s_rng = 1;
static uint32_t rng_next(void) {
    s_rng = s_rng * 1664525u + 1013904223u;
    return s_rng;
}
static int rng_chance(int percent) {
    return (int)(rng_next() % 100u) < percent;
}
static int rng_range(int lo, int hi) {
    if (hi <= lo) return lo;
    return lo + (int)(rng_next() % (uint32_t)(hi - lo + 1));
}

/* Per-track-role probability profile for a 16-step pattern. */
typedef struct {
    int step_prob[16];   /* per-step probability of a hit, 0-100 */
    int vel_lo, vel_hi;  /* velocity range when a step hits */
} role_profile_t;

/* Style picker — different musical feels rolled up-front so the whole
 * pattern is internally consistent rather than role-by-role chaos. */
typedef enum {
    STYLE_FOUR_ON_FLOOR = 0,  /* house / techno */
    STYLE_BOOM_BAP,           /* hip-hop classic */
    STYLE_TRAP,               /* sparse kick + skittery hats */
    STYLE_BREAKBEAT,          /* syncopated snare */
    NUM_STYLES
} pattern_style_t;

static const role_profile_t kick_profiles[NUM_STYLES] = {
    /* four-on-floor: every quarter */
    {{100,0,0,0, 100,0,0,0, 100,0,0,0, 100,0,0,0}, 105, 127},
    /* boom bap: 1, 9 + occasional ghost */
    {{100,0,0,15, 0,0,30,0, 100,0,0,20, 0,0,0,0}, 100, 127},
    /* trap: 1, syncopated */
    {{100,0,0,0, 0,0,40,0, 0,0,80,0, 0,0,30,0}, 110, 127},
    /* breakbeat: 1, 11 + offset */
    {{100,0,0,0, 0,0,0,0, 0,0,100,0, 30,0,0,0}, 100, 127},
};

static const role_profile_t snare_profiles[NUM_STYLES] = {
    /* four-on-floor: 5, 13 (back beat) */
    {{0,0,0,0, 100,0,0,0, 0,0,0,0, 100,0,0,0}, 100, 125},
    /* boom bap: 5, 13 + ghost */
    {{0,0,15,0, 100,0,20,0, 0,0,15,0, 100,0,25,0}, 95, 125},
    /* trap: 5, 13 */
    {{0,0,0,0, 100,0,0,0, 0,0,0,0, 100,0,0,0}, 100, 125},
    /* breakbeat: 5, 11, 13 with rolls */
    {{0,0,0,0, 100,0,0,30, 0,0,100,0, 100,0,40,0}, 95, 125},
};

static const role_profile_t chat_profiles[NUM_STYLES] = {
    /* four-on-floor: 8th notes */
    {{50,0,80,0, 50,0,80,0, 50,0,80,0, 50,0,80,0}, 60, 100},
    /* boom bap: 8th notes with accents */
    {{40,0,90,0, 50,0,80,0, 40,0,90,0, 50,0,70,0}, 55, 105},
    /* trap: 16ths with rolls */
    {{60,40,80,40, 50,40,80,40, 50,40,80,60, 50,40,90,70}, 50, 110},
    /* breakbeat: dense 16ths */
    {{70,40,90,40, 70,40,90,40, 70,40,90,40, 70,40,90,40}, 60, 110},
};

static const role_profile_t clap_profiles[NUM_STYLES] = {
    /* four-on-floor: backs up snare on 5, 13 */
    {{0,0,0,0, 80,0,0,0, 0,0,0,0, 80,0,0,0}, 90, 115},
    /* boom bap: layer with snare */
    {{0,0,0,0, 70,0,0,0, 0,0,0,0, 70,0,0,0}, 85, 110},
    /* trap: alongside snare */
    {{0,0,0,0, 90,0,0,0, 0,0,0,0, 90,0,0,15}, 95, 120},
    /* breakbeat: sparse */
    {{0,0,0,0, 50,0,0,0, 0,0,0,0, 50,0,0,0}, 85, 110},
};

static const role_profile_t ohat_profiles[NUM_STYLES] = {
    {{0,0,0,30, 0,0,0,30, 0,0,0,30, 0,0,0,30}, 70, 100},
    {{0,0,0,30, 0,0,0,40, 0,0,0,30, 0,0,0,40}, 70, 100},
    {{0,0,0,20, 0,0,0,20, 0,0,0,40, 0,0,0,30}, 70, 100},
    {{0,0,0,30, 0,0,0,40, 0,0,0,30, 0,0,0,50}, 70, 110},
};

/* Sparse percussion profile (cowbell, rim, tom, etc.) — used for slots 5-7
 * and any sampler tracks beyond. */
static const role_profile_t perc_profiles[NUM_STYLES] = {
    {{0,15,0,15, 0,15,0,15, 0,15,0,15, 0,15,0,15}, 50, 90},
    {{0,10,0,20, 0,10,0,15, 0,10,0,25, 0,10,0,15}, 50, 95},
    {{15,0,0,10, 0,15,0,0, 10,0,15,0, 0,10,0,15}, 60, 100},
    {{0,20,0,10, 0,20,0,15, 0,20,0,10, 0,25,0,15}, 50, 95},
};

static const role_profile_t *profile_for_slot(int sampler_slot,
                                               pattern_style_t style)
{
    switch (sampler_slot) {
        case 0: return &kick_profiles[style];
        case 1: return &snare_profiles[style];
        case 2: return &chat_profiles[style];
        case 3: return &clap_profiles[style];
        case 4: return &ohat_profiles[style];
        default: return &perc_profiles[style];
    }
}

void sq_pattern_randomize(sq_pattern_t *pat, uint32_t seed)
{
    if (!pat) return;

    s_rng = seed ? seed : (uint32_t)time(NULL);
    /* Stir a few times so consecutive calls with similar time seeds diverge */
    rng_next(); rng_next(); rng_next();

    pattern_style_t style = (pattern_style_t)(rng_next() % NUM_STYLES);
    LOG_INFO("Randomizing pattern '%s' (style=%d, %u tracks)",
             pat->name, (int)style, pat->num_tracks);

    int sampler_slot = 0;
    for (uint32_t t = 0; t < pat->num_tracks; t++) {
        sq_track_t *track = &pat->tracks[t];
        if (track->type != TRACK_SAMPLER) continue;

        const role_profile_t *prof = profile_for_slot(sampler_slot, style);

        /* Wipe step data; keep the rest of the track config. */
        memset(track->steps, 0, sizeof(track->steps));

        uint32_t len = track->length ? track->length : 16;
        if (len > 16) len = 16;  /* heuristic only covers 16 steps */

        for (uint32_t s = 0; s < len; s++) {
            if (rng_chance(prof->step_prob[s])) {
                track->steps[s].velocity =
                    (uint8_t)rng_range(prof->vel_lo, prof->vel_hi);
                track->steps[s].probability = 100;
            }
        }
        sampler_slot++;
    }
}
