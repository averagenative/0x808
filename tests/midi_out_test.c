/*
 * midi_out_test.c — Sequencer MIDI output, sample-accurate step timing,
 * and host-sync locate.
 *
 * Verifies:
 * 1. Notes come out on the right channel, note, and frame, and the result
 *    doesn't depend on the buffer size (including events that fall exactly
 *    on a buffer boundary)
 * 2. Swing, ratchets, and note lengths
 * 3. Stopping sends note-offs for notes still sounding
 * 4. sq_engine_locate() for pattern and song mode, without double hits
 * 5. Drum audio starts on the step's exact sample
 * 6. The channel / note map used for MIDI input
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "engine/engine.h"
#include "engine/kits.h"
#include "engine/midi_out.h"

#define SR    48000
#define STEP  6000        /* frames per 16th note at 120 BPM, 48 kHz */
#define BEAT  (4 * STEP)

#define FAIL(...) do { fprintf(stderr, "FAIL: " __VA_ARGS__); fprintf(stderr, "\n"); return 1; } while (0)

typedef struct {
    uint64_t frame;
    uint8_t  status, data1, data2;
} ev_t;

#define MAX_EV 4096
static ev_t g_ev[MAX_EV];
static int  g_nev;
static uint64_t g_pos;
static float g_buf[8192 * 2];

static sq_engine_t g_engine;

/* Render total frames in blocks of `block`, collecting MIDI with absolute
 * frame numbers. Fails on events outside the buffer or out of order. */
static int run(sq_engine_t *e, uint32_t total, uint32_t block)
{
    uint32_t done = 0;
    while (done < total) {
        uint32_t n = (total - done < block) ? total - done : block;
        e->midi_out_enabled = true;
        sq_engine_process(e, g_buf, n);
        for (uint32_t i = 0; i < e->midi_out_count; i++) {
            const sq_midi_event_t *m = &e->midi_out[i];
            if (m->frame >= n)
                FAIL("event frame %u outside a %u-frame buffer", m->frame, n);
            if (i > 0 && m->frame < e->midi_out[i - 1].frame)
                FAIL("events out of order at buffer pos %llu", (unsigned long long)g_pos);
            if (g_nev < MAX_EV)
                g_ev[g_nev++] = (ev_t){ g_pos + m->frame, m->status, m->data1, m->data2 };
        }
        g_pos += n;
        done += n;
    }
    return 0;
}

static void reset_capture(void) { g_nev = 0; g_pos = 0; }

static int find_ev(uint64_t frame, uint8_t status, uint8_t note)
{
    for (int i = 0; i < g_nev; i++)
        if (g_ev[i].frame == frame && g_ev[i].status == status && g_ev[i].data1 == note)
            return i;
    return -1;
}

static int count_ev(uint8_t status, uint8_t note, uint64_t from, uint64_t to)
{
    int n = 0;
    for (int i = 0; i < g_nev; i++)
        if (g_ev[i].status == status && g_ev[i].data1 == note &&
            g_ev[i].frame >= from && g_ev[i].frame < to)
            n++;
    return n;
}

/* Pattern 0: kick (kit slot 0), synth, snare (kit slot 1), all 16 steps */
static void setup(sq_engine_t *e)
{
    static bool initialized = false;
    if (initialized) sq_engine_shutdown(e);
    sq_engine_init(e, SR);
    initialized = true;
    e->transport.bpm = 120.0;
    sq_pattern_t *p = &e->patterns[0];
    memset(p, 0, sizeof(*p));
    p->num_tracks = 3;
    for (int t = 0; t < 3; t++) {
        p->tracks[t].length = 16;
        p->tracks[t].volume = 0.8f;
    }
    p->tracks[0].type = TRACK_SAMPLER;
    p->tracks[0].sample_index = 0;
    p->tracks[1].type = TRACK_SYNTH;
    p->tracks[1].synth_preset = 0;
    p->tracks[2].type = TRACK_SAMPLER;
    p->tracks[2].sample_index = 1;
    if (e->num_patterns < 2) e->num_patterns = 2;
    e->transport.current_pattern = 0;
}

static void play(sq_engine_t *e)
{
    e->transport.playing = true;
    e->transport.current_beat = 0.0;
    e->transport.current_step = 0;
    e->transport.step0_pending = true;
}

static void basic_pattern(sq_engine_t *e)
{
    sq_track_t *kick = &e->patterns[0].tracks[0];
    sq_track_t *synth = &e->patterns[0].tracks[1];
    sq_track_t *snare = &e->patterns[0].tracks[2];
    for (int s = 0; s < 16; s += 4) kick->steps[s].velocity = 100;
    synth->steps[2].velocity = 100;
    synth->steps[2].note = 64;
    synth->steps[2].length = 2.0f;
    snare->steps[4].velocity = 127;
    snare->steps[13].velocity = 64;
}

/* ─── Test 1: notes, channels, frames; independent of buffer size ─────── */

static int test_basic_and_block_sizes(void)
{
    static const uint32_t blocks[] = { 4096, 1, 64, 500, 3000, 6000, 7 };
    static ev_t ref[MAX_EV];
    int ref_n = 0;

    for (size_t b = 0; b < sizeof(blocks) / sizeof(blocks[0]); b++) {
        sq_engine_t *e = &g_engine;
        setup(e);
        basic_pattern(e);
        play(e);
        reset_capture();
        if (run(e, 16 * STEP + 100, blocks[b])) return 1;

        if (b == 0) {
            if (find_ev(0, 0x99, 36) < 0)            FAIL("kick note-on at 0 (ch 10, note 36)");
            if (find_ev(STEP / 2, 0x89, 36) < 0)     FAIL("kick note-off half a step later");
            if (find_ev(2 * STEP, 0x90, 64) < 0)     FAIL("synth note-on at step 2 (ch 1, note 64)");
            if (find_ev(4 * STEP, 0x80, 64) < 0)     FAIL("synth note-off after its 2-step length");
            if (find_ev(4 * STEP, 0x99, 38) < 0)     FAIL("snare at step 4");
            if (find_ev(13 * STEP, 0x99, 38) < 0)    FAIL("snare at step 13");
            if (find_ev(16 * STEP, 0x99, 36) < 0)    FAIL("kick when the pattern loops");
            int i = find_ev(13 * STEP, 0x99, 38);
            if (g_ev[i].data2 != 64)                  FAIL("velocity 64 came out as %d", g_ev[i].data2);
            /* Same-frame order: the synth's note-off before step 4's note-ons */
            if (find_ev(4 * STEP, 0x80, 64) > find_ev(4 * STEP, 0x99, 36))
                FAIL("note-off should come before note-ons on the same frame");
            memcpy(ref, g_ev, sizeof(ev_t) * (size_t)g_nev);
            ref_n = g_nev;
            printf("  PASS: notes, channels, and frames (%d events)\n", ref_n);
        } else {
            if (g_nev != ref_n)
                FAIL("block %u: %d events, expected %d", blocks[b], g_nev, ref_n);
            for (int i = 0; i < g_nev; i++) {
                if (memcmp(&g_ev[i], &ref[i], sizeof(ev_t)) != 0)
                    FAIL("block %u: event %d differs (frame %llu vs %llu)", blocks[b], i,
                         (unsigned long long)g_ev[i].frame, (unsigned long long)ref[i].frame);
            }
        }
    }
    printf("  PASS: identical output for buffer sizes 1 to 6000\n");
    return 0;
}

/* ─── Test 2: swing and ratchets ──────────────────────────────────────── */

static int test_swing_and_ratchet(void)
{
    sq_engine_t *e = &g_engine;
    setup(e);
    basic_pattern(e);
    e->transport.swing = 1.0f;
    play(e);
    reset_capture();
    if (run(e, 16 * STEP, 512)) return 1;
    /* Odd step 13 starts at beat 3.25 + 1/6 = frame 82000 at full swing */
    if (find_ev(82000, 0x99, 38) < 0) FAIL("swung step 13 should start at frame 82000");
    if (find_ev(4 * STEP, 0x99, 38) < 0) FAIL("even step 4 shouldn't move with swing");
    printf("  PASS: swing moves odd steps to the exact frame\n");

    setup(e);
    e->patterns[0].tracks[0].steps[0].velocity = 100;
    e->patterns[0].tracks[0].steps[0].retrigger = 2;
    play(e);
    reset_capture();
    if (run(e, STEP, 256)) return 1;
    if (find_ev(0, 0x99, 36) < 0)            FAIL("ratchet first hit at 0");
    if (find_ev(STEP / 4, 0x89, 36) < 0)     FAIL("ratchet hits last half their interval");
    int i = find_ev(STEP / 2, 0x99, 36);
    if (i < 0)                               FAIL("ratchet second hit at half a step");
    if (g_ev[i].data2 != 75)                 FAIL("ratchet decays velocity 100 -> 75, got %d", g_ev[i].data2);
    if (find_ev(3 * STEP / 4, 0x89, 36) < 0) FAIL("ratchet second note-off");
    printf("  PASS: ratchets\n");
    return 0;
}

/* ─── Test 3: stopping sends note-offs ────────────────────────────────── */

static int test_stop_flush(void)
{
    sq_engine_t *e = &g_engine;
    for (int how = 0; how < 2; how++) {
        setup(e);
        e->patterns[0].tracks[1].steps[0].velocity = 100;
        e->patterns[0].tracks[1].steps[0].note = 60;
        e->patterns[0].tracks[1].steps[0].length = 8.0f;
        play(e);
        reset_capture();
        if (run(e, 10000, 512)) return 1;
        if (how == 0)
            sq_engine_stop(e);              /* host stop */
        else
            e->transport.playing = false;   /* GUI stop button */
        if (run(e, 2000, 512)) return 1;
        if (find_ev(10000, 0x80, 60) < 0)
            FAIL("%s stop: note-off at the stop", how == 0 ? "host" : "GUI");
        if (count_ev(0x80, 60, 0, 100000) != 1)
            FAIL("exactly one note-off after stopping");
    }
    printf("  PASS: stopping ends sounding notes\n");
    return 0;
}

/* ─── Test 4: locate (host sync) ──────────────────────────────────────── */

static int test_locate(void)
{
    sq_engine_t *e = &g_engine;
    const double window = 512.0 / BEAT;

    /* Start exactly on a step: it plays */
    setup(e);
    basic_pattern(e);
    sq_engine_locate(e, 1.0, window);
    reset_capture();
    if (run(e, 512, 512)) return 1;
    if (find_ev(0, 0x99, 36) < 0 || find_ev(0, 0x99, 38) < 0)
        FAIL("locate onto step 4 should play it right away");

    /* Start mid-step: no late hit; the next step lands on its frame */
    setup(e);
    basic_pattern(e);
    sq_engine_locate(e, 1.1, window);
    reset_capture();
    if (run(e, BEAT, 512)) return 1;
    if (count_ev(0x99, 36, 0, 1000) != 0) FAIL("locate mid-step shouldn't play that step");
    if (find_ev((uint64_t)(0.9 * BEAT), 0x99, 36) < 0) FAIL("next kick at beat 2.0");

    /* Host loop wraps onto the step the engine just played: one hit */
    setup(e);
    basic_pattern(e);
    play(e);
    reset_capture();
    if (run(e, 16 * STEP + 256, 512)) return 1;
    sq_engine_locate(e, 256.0 / BEAT, window);
    if (run(e, 1024, 512)) return 1;
    if (count_ev(0x99, 36, 16 * STEP, 16 * STEP + 2048) != 1)
        FAIL("loop wrap onto a step the engine already played hit it twice");
    printf("  PASS: locate in pattern mode\n");

    /* Song mode: section 0 = pattern 0 once, section 1 = pattern 1 twice */
    setup(e);
    e->patterns[1] = e->patterns[0];
    e->transport.mode = MODE_SONG;
    e->arrangement.num_sections = 2;
    e->arrangement.sections[0] = (sq_section_t){ 0, 1 };
    e->arrangement.sections[1] = (sq_section_t){ 1, 2 };
    sq_engine_locate(e, 6.0, window);
    if (!e->transport.playing || e->transport.current_section != 1 ||
        e->transport.section_repeat != 0 || e->transport.current_pattern != 1 ||
        fabs(e->transport.current_beat - 2.0) > 1e-9 || e->transport.current_step != 8)
        FAIL("song locate to beat 6: section %d repeat %d pattern %d beat %.3f step %d",
             e->transport.current_section, e->transport.section_repeat,
             e->transport.current_pattern, e->transport.current_beat,
             e->transport.current_step);
    sq_engine_locate(e, 9.0, window);
    if (e->transport.section_repeat != 1 || e->transport.current_step != 4)
        FAIL("song locate to beat 9 should be section 1's second pass, step 4");
    sq_engine_locate(e, 13.0, window);
    if (e->transport.playing) FAIL("locate past the end of the song should stop");
    printf("  PASS: locate in song mode\n");
    return 0;
}

/* ─── Test 5: drum audio starts on the exact sample ───────────────────── */

static int test_audio_sample_accurate(void)
{
    sq_engine_t *e = &g_engine;
    setup(e);
    if (sq_kit_load(e, 0, ".") != 0 || e->num_samples == 0 || !e->samples[0].data) {
        printf("  SKIP: audio onset (run from the project root to load samples/)\n");
        return 0;
    }
    e->patterns[0].tracks[0].steps[4].velocity = 127;
    play(e);

    /* With 4096-frame buffers the old code started the kick at 20480 */
    static float out[4096 * 2];
    int64_t onset = -1;
    for (uint32_t pos = 0; pos < 2 * BEAT && onset < 0; pos += 4096) {
        e->midi_out_enabled = true;
        sq_engine_process(e, out, 4096);
        for (int i = 0; i < 4096; i++) {
            /* Above the mixer's 1e-4 keep-alive DC offset */
            if (fabsf(out[i * 2]) > 1e-3f || fabsf(out[i * 2 + 1]) > 1e-3f) {
                onset = (int64_t)pos + i;
                break;
            }
        }
    }
    if (onset < 4 * STEP || onset > 4 * STEP + 16)
        FAIL("kick at step 4 should start at frame %d, started at %lld", 4 * STEP, (long long)onset);
    printf("  PASS: drum audio starts at frame %lld (step 4 = %d)\n", (long long)onset, 4 * STEP);
    return 0;
}

/* ─── Test 6: channel / note map ──────────────────────────────────────── */

static int test_map(void)
{
    static sq_pattern_t p;
    memset(&p, 0, sizeof(p));
    const sq_track_type_t types[] = { TRACK_SAMPLER, TRACK_SYNTH, TRACK_SAMPLER,
                                      TRACK_SYNTH, TRACK_SF2 };
    p.num_tracks = 5;
    for (int t = 0; t < 5; t++) {
        p.tracks[t].type = types[t];
        p.tracks[t].sample_index = t == 0 ? 0 : (t == 2 ? 2 : -1);
    }
    const int want[] = { 9, 0, 9, 1, 2 };
    for (int t = 0; t < 5; t++)
        if (sq_midi_track_channel(&p, t) != want[t])
            FAIL("track %d channel %d, want %d", t, sq_midi_track_channel(&p, t), want[t]);
    if (sq_midi_find_track(&p, 9, 42) != 2) FAIL("closed hat (42) on ch 10 -> track 2");
    if (sq_midi_find_track(&p, 9, 36) != 0) FAIL("kick (36) on ch 10 -> track 0");
    if (sq_midi_find_track(&p, 9, 49) != -1) FAIL("unmapped drum note -> no track");
    if (sq_midi_find_track(&p, 1, 60) != 3) FAIL("ch 2 -> second synth track");
    if (sq_midi_find_track(&p, 7, 60) != 1) FAIL("unused channel -> first melodic track");

    /* Ten melodic tracks: the tenth skips channel 10 */
    memset(&p, 0, sizeof(p));
    p.num_tracks = 10;
    for (int t = 0; t < 10; t++) p.tracks[t].type = TRACK_SYNTH;
    if (sq_midi_track_channel(&p, 8) != 8 || sq_midi_track_channel(&p, 9) != 10)
        FAIL("melodic channels should skip channel 10");
    printf("  PASS: channel and note map\n");
    return 0;
}

int main(void)
{
    printf("=== MIDI Output Test ===\n");
    if (test_basic_and_block_sizes()) return 1;
    if (test_swing_and_ratchet()) return 1;
    if (test_stop_flush()) return 1;
    if (test_locate()) return 1;
    if (test_audio_sample_accurate()) return 1;
    if (test_map()) return 1;
    sq_engine_shutdown(&g_engine);
    printf("All MIDI output tests passed.\n");
    return 0;
}
