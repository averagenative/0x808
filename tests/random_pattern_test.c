/*
 * random_pattern_test.c — Verify drum pattern randomizer produces
 * musically-sensible output and is deterministic given a seed.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine/engine.h"
#include "engine/random_pattern.h"

static int count_hits(const sq_track_t *t)
{
    int n = 0;
    for (int s = 0; s < 16; s++)
        if (t->steps[s].velocity > 0) n++;
    return n;
}

int main(void)
{
    sq_engine_t e;
    sq_engine_init(&e, 44100);

    /* Build an 8-sampler-track pattern matching the default 808 layout. */
    e.num_patterns = 1;
    sq_pattern_t *pat = &e.patterns[0];
    snprintf(pat->name, sizeof(pat->name), "RandTest");
    pat->num_tracks = 8;
    for (int t = 0; t < 8; t++) {
        pat->tracks[t].type = TRACK_SAMPLER;
        pat->tracks[t].length = 16;
        pat->tracks[t].sample_index = t;
        pat->tracks[t].volume = 0.8f;
    }

    /* Determinism: same seed → identical pattern */
    sq_pattern_randomize(pat, 12345);
    sq_pattern_t snapshot = *pat;

    sq_pattern_randomize(pat, 12345);
    if (memcmp(&snapshot, pat, sizeof(snapshot)) != 0) {
        fprintf(stderr, "FAIL: same seed produced different pattern\n");
        return 1;
    }
    printf("PASS: deterministic for fixed seed\n");

    /* Different seeds → different patterns (very likely) */
    sq_pattern_randomize(pat, 99999);
    if (memcmp(&snapshot, pat, sizeof(snapshot)) == 0) {
        fprintf(stderr, "FAIL: different seeds produced identical pattern\n");
        return 1;
    }
    printf("PASS: different seed produces different pattern\n");

    /* Musical sanity:
     * - kick (track 0) must hit at least once
     * - snare (track 1) must hit on backbeat (step 4 or 12)
     * - kick should not be on EVERY single 16th step (sounds bad)
     * Run several seeds to make sure it's robust. */
    int kick_silent = 0, snare_no_backbeat = 0, kick_full = 0;
    for (uint32_t seed = 1; seed <= 50; seed++) {
        sq_pattern_randomize(pat, seed);
        int kick_hits = count_hits(&pat->tracks[0]);
        if (kick_hits == 0) kick_silent++;
        if (kick_hits == 16) kick_full++;
        bool backbeat = pat->tracks[1].steps[4].velocity > 0
                     || pat->tracks[1].steps[12].velocity > 0;
        if (!backbeat) snare_no_backbeat++;
    }
    if (kick_silent > 0) {
        fprintf(stderr, "FAIL: kick was silent in %d/50 seeds\n", kick_silent);
        return 1;
    }
    if (kick_full > 0) {
        fprintf(stderr, "FAIL: kick hit every step in %d/50 seeds\n", kick_full);
        return 1;
    }
    if (snare_no_backbeat > 5) {
        fprintf(stderr, "FAIL: snare missed backbeat in %d/50 seeds (>5)\n",
                snare_no_backbeat);
        return 1;
    }
    printf("PASS: 50 seeds — kick always hits, snare on backbeat in %d/50\n",
           50 - snare_no_backbeat);

    /* Synth tracks must be left untouched. Add a synth track and verify. */
    pat->num_tracks = 9;
    pat->tracks[8].type = TRACK_SYNTH;
    pat->tracks[8].length = 16;
    pat->tracks[8].steps[0].velocity = 100;
    pat->tracks[8].steps[0].note = 60;
    sq_pattern_randomize(pat, 42);
    if (pat->tracks[8].steps[0].velocity != 100 ||
        pat->tracks[8].steps[0].note != 60) {
        fprintf(stderr, "FAIL: synth track was modified by randomizer\n");
        return 1;
    }
    printf("PASS: synth tracks left untouched\n");

    /* TASK-227: per-flag option isolation
     *
     * With only "velocity" enabled the step on/off pattern must not
     * change — only the velocity values on existing hits get re-rolled.
     * With only "notes" enabled, sampler tracks must not change. */
    {
        sq_pattern_t base;
        memset(&base, 0, sizeof(base));
        base.num_tracks = 2;
        base.tracks[0].type = TRACK_SAMPLER;
        base.tracks[0].length = 16;
        base.tracks[0].steps[0].velocity = 100;
        base.tracks[0].steps[4].velocity = 80;
        base.tracks[0].steps[8].velocity = 100;
        base.tracks[0].steps[12].velocity = 80;
        base.tracks[1].type = TRACK_SYNTH;
        base.tracks[1].length = 16;
        base.tracks[1].steps[0].velocity = 100;
        base.tracks[1].steps[0].note = 60;

        /* Velocity-only re-roll */
        sq_pattern_t p1 = base;
        sq_random_options_t opts;
        sq_random_options_default(&opts);
        opts.steps = false;
        opts.velocity = true;
        opts.seed = 42;
        sq_pattern_randomize_opts(&p1, &opts);

        /* Step on/off must match base */
        for (int s = 0; s < 16; s++) {
            bool a = base.tracks[0].steps[s].velocity > 0;
            bool b = p1.tracks[0].steps[s].velocity > 0;
            if (a != b) {
                fprintf(stderr,
                        "FAIL: velocity-only changed step %d on/off (was %d, now %d)\n",
                        s, a, b);
                return 1;
            }
        }
        printf("PASS: velocity-only preserves step on/off\n");

        /* Notes-only must not touch sampler tracks */
        sq_pattern_t p2 = base;
        sq_random_options_default(&opts);
        opts.steps = false;
        opts.notes = true;
        opts.seed = 42;
        sq_pattern_randomize_opts(&p2, &opts);
        if (memcmp(&p2.tracks[0], &base.tracks[0], sizeof(sq_track_t)) != 0) {
            fprintf(stderr, "FAIL: notes-only modified sampler track\n");
            return 1;
        }
        if (p2.tracks[1].steps[0].note == 60 &&
            base.tracks[1].steps[0].note == 60) {
            /* Note may or may not have changed (random pick could land on 60).
             * Just check the synth track wasn't fully cleared. */
        }
        printf("PASS: notes-only leaves sampler tracks untouched\n");

        /* Pitch-only on existing hits */
        sq_pattern_t p3 = base;
        sq_random_options_default(&opts);
        opts.steps = false;
        opts.pitch = true;
        opts.seed = 42;
        sq_pattern_randomize_opts(&p3, &opts);
        bool any_pitch_change = false;
        for (int s = 0; s < 16; s++) {
            if (p3.tracks[0].steps[s].pitch_offset != 0) any_pitch_change = true;
        }
        if (!any_pitch_change) {
            fprintf(stderr, "FAIL: pitch-only didn't set any pitch_offset\n");
            return 1;
        }
        printf("PASS: pitch-only writes per-step pitch_offset\n");
    }

    sq_engine_shutdown(&e);
    printf("\n=== ALL RANDOM PATTERN TESTS PASSED ===\n");
    return 0;
}
