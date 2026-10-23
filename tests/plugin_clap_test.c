/*
 * plugin_clap_test.c — Load the built 0x808.clap in a minimal CLAP host and
 * check MIDI output, host transport sync, MIDI input, and saved state.
 *
 * Verifies:
 * 1. The plugin has one MIDI input and one MIDI output note port
 * 2. Nothing plays while the host is stopped
 * 3. Host play from beat 0 plays the demo pattern at the host tempo, with
 *    every note on its exact frame and every note ended after a stop
 * 4. A host loop wrap plays the loop's first step exactly once
 * 5. Incoming drum notes on channel 10 play the matching drum track
 * 6. The state holds the whole project: an edited pattern survives a save
 *    and load, and a re-activation at another sample rate
 * 7. Host sync is saved with the state, and INT mode ignores the host
 * 8. Pre-1.5 state (parameters only) still loads
 * 9. Loading state while another thread processes audio is safe
 * 10. Two instances play at once, each with its own pattern
 * 11. Kits come from the samples built into the plugin: a project using the
 *     909 kit plays its drums, and so does a v1.5.0 project
 *
 * usage: plugin_clap_test [path/to/0x808.clap]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <dlfcn.h>
#include <pthread.h>
#include <stdatomic.h>
#include <clap/clap.h>
#include "cJSON.h"

#ifndef PLUGIN_CLAP_PATH
#define PLUGIN_CLAP_PATH "build/0x808.clap"
#endif

#define SR     48000
#define BLOCK  512
#define TEMPO  120.0
#define STEP   6000                 /* frames per 16th at 120 BPM, 48 kHz */
#define BEAT   (4 * STEP)

#define FAIL(...) do { fprintf(stderr, "FAIL: " __VA_ARGS__); fprintf(stderr, "\n"); exit(1); } while (0)

/* ─── Minimal host ───────────────────────────────────────────────────────── */

static const void *host_get_extension(const clap_host_t *h, const char *id) { (void)h; (void)id; return NULL; }
static void host_noop(const clap_host_t *h) { (void)h; }
static const clap_host_t s_host = {
    CLAP_VERSION_INIT, NULL, "plugin_clap_test", "0x808", "", "1.0",
    host_get_extension, host_noop, host_noop, host_noop,
};

/* MIDI the plugin sends, with absolute frame numbers */
typedef struct { uint64_t frame; uint8_t status, data1, data2; } ev_t;
static ev_t     g_ev[8192];
static int      g_nev;
static uint64_t g_block_pos;      /* absolute frame of the current block */
static uint32_t g_last_time;

static bool out_try_push(const clap_output_events_t *list, const clap_event_header_t *hdr)
{
    (void)list;
    if (hdr->space_id != CLAP_CORE_EVENT_SPACE_ID || hdr->type != CLAP_EVENT_MIDI)
        return true;
    const clap_event_midi_t *m = (const clap_event_midi_t *)hdr;
    if (hdr->time >= BLOCK) FAIL("MIDI event at time %u in a %d-frame block", hdr->time, BLOCK);
    if (hdr->time < g_last_time) FAIL("MIDI events out of order");
    if (m->port_index != 0) FAIL("MIDI on port %u", m->port_index);
    g_last_time = hdr->time;
    if (g_nev < 8192)
        g_ev[g_nev++] = (ev_t){ g_block_pos + hdr->time, m->data[0], m->data[1], m->data[2] };
    return true;
}
static const clap_output_events_t s_out_events = { NULL, out_try_push };

static bool null_try_push(const clap_output_events_t *l, const clap_event_header_t *h) { (void)l; (void)h; return true; }
static const clap_output_events_t s_null_out_events = { NULL, null_try_push };

/* MIDI the host sends this block */
static clap_event_midi_t g_in[16];
static uint32_t g_nin;
static uint32_t in_size(const clap_input_events_t *l) { (void)l; return g_nin; }
static const clap_event_header_t *in_get(const clap_input_events_t *l, uint32_t i) { (void)l; return &g_in[i].header; }
static const clap_input_events_t s_in_events = { NULL, in_size, in_get };

static const clap_plugin_t *g_plugin;
static float g_left[BLOCK], g_right[BLOCK];

/* Process one block. beats < 0 means the host is stopped. Returns the
 * block's peak level above the mixer's tiny keep-alive DC offset. */
static float process_block(double beats)
{
    clap_event_transport_t tr;
    memset(&tr, 0, sizeof(tr));
    tr.header.size = sizeof(tr);
    tr.header.type = CLAP_EVENT_TRANSPORT;
    tr.flags = CLAP_TRANSPORT_HAS_TEMPO | CLAP_TRANSPORT_HAS_BEATS_TIMELINE;
    tr.tempo = TEMPO;
    if (beats >= 0.0) {
        tr.flags |= CLAP_TRANSPORT_IS_PLAYING;
        tr.song_pos_beats = (clap_beattime)llround(beats * (double)CLAP_BEATTIME_FACTOR);
    }

    float *chans[2] = { g_left, g_right };
    clap_audio_buffer_t out = { 0 };
    out.data32 = chans;
    out.channel_count = 2;

    clap_process_t proc;
    memset(&proc, 0, sizeof(proc));
    proc.steady_time = (int64_t)g_block_pos;
    proc.frames_count = BLOCK;
    proc.transport = &tr;
    proc.audio_outputs = &out;
    proc.audio_outputs_count = 1;
    proc.in_events = &s_in_events;
    proc.out_events = &s_out_events;

    g_last_time = 0;
    g_plugin->process(g_plugin, &proc);
    g_nin = 0;
    g_block_pos += BLOCK;

    float peak = 0.0f;
    for (int i = 0; i < BLOCK; i++) {
        float a = fabsf(g_left[i] - 1e-4f), b = fabsf(g_right[i] - 1e-4f);
        if (a > peak) peak = a;
        if (b > peak) peak = b;
    }
    return peak;
}

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

/* ─── State streams ──────────────────────────────────────────────────────── */

static unsigned char g_state[1 << 20];
static size_t g_state_len, g_state_pos;

/* Like real hosts, these move at most 1000 bytes per call */
static int64_t ostream_write(const clap_ostream_t *s, const void *buf, uint64_t size)
{
    (void)s;
    if (size > 1000) size = 1000;
    if (g_state_len + size > sizeof(g_state)) return -1;
    memcpy(g_state + g_state_len, buf, size);
    g_state_len += size;
    return (int64_t)size;
}
static int64_t istream_read(const clap_istream_t *s, void *buf, uint64_t size)
{
    (void)s;
    uint64_t left = g_state_len - g_state_pos;
    if (size > left) size = left;
    if (size > 1000) size = 1000;
    memcpy(buf, g_state + g_state_pos, size);
    g_state_pos += size;
    return (int64_t)size;
}

static const clap_plugin_state_t *g_st;
static void load_state_bytes(void);
#define STATE_MAGIC "0x808 plugin state 1\n"

/* Save the plugin state and parse its JSON (caller deletes) */
static cJSON *save_state(void)
{
    clap_ostream_t os = { NULL, ostream_write };
    g_state_len = 0;
    if (!g_st->save(g_plugin, &os)) FAIL("state save");
    size_t ml = strlen(STATE_MAGIC);
    if (g_state_len < ml || memcmp(g_state, STATE_MAGIC, ml) != 0)
        FAIL("saved state should start with the state header");
    g_state[g_state_len] = '\0';
    cJSON *j = cJSON_Parse((const char *)g_state + ml);
    if (!j) FAIL("saved state JSON doesn't parse");
    return j;
}

static void load_state_json(const cJSON *j)
{
    char *txt = cJSON_PrintUnformatted(j);
    size_t ml = strlen(STATE_MAGIC);
    g_state_len = ml + strlen(txt);
    memcpy(g_state, STATE_MAGIC, ml);
    memcpy(g_state + ml, txt, strlen(txt));
    cJSON_free(txt);
    load_state_bytes();
}

static void load_state_bytes(void)
{
    clap_istream_t is = { NULL, istream_read };
    g_state_pos = 0;
    if (!g_st->load(g_plugin, &is)) FAIL("state load");
}

/* Play `beats` beats from beat 0 with the host transport running */
static void play_host(double beats_total)
{
    double beats = 0.0;
    int blocks = (int)(beats_total * BEAT / BLOCK);
    for (int b = 0; b < blocks; b++) {
        process_block(beats);
        beats += (double)BLOCK / BEAT;
    }
    process_block(-1.0);
}

static void wait_quiet(void)
{
    int q = 0;
    for (int b = 0; b < 2000 && q < 10; b++)
        q = process_block(-1.0) < 1e-3f ? q + 1 : 0;
}

/* Audio thread for the concurrency test: process as a host would */
static atomic_int g_stop_thread;
static void *audio_thread(void *arg)
{
    (void)arg;
    static float l[BLOCK], r[BLOCK];
    float *chans[2] = { l, r };
    double beats = 0.0;
    while (!atomic_load(&g_stop_thread)) {
        clap_event_transport_t tr;
        memset(&tr, 0, sizeof(tr));
        tr.header.size = sizeof(tr);
        tr.header.type = CLAP_EVENT_TRANSPORT;
        tr.flags = CLAP_TRANSPORT_HAS_TEMPO | CLAP_TRANSPORT_HAS_BEATS_TIMELINE |
                   CLAP_TRANSPORT_IS_PLAYING;
        tr.tempo = TEMPO;
        tr.song_pos_beats = (clap_beattime)llround(beats * (double)CLAP_BEATTIME_FACTOR);
        clap_audio_buffer_t out = { 0 };
        out.data32 = chans;
        out.channel_count = 2;
        clap_process_t proc;
        memset(&proc, 0, sizeof(proc));
        proc.frames_count = BLOCK;
        proc.transport = &tr;
        proc.audio_outputs = &out;
        proc.audio_outputs_count = 1;
        proc.in_events = &s_in_events;
        proc.out_events = &s_null_out_events;
        g_plugin->process(g_plugin, &proc);
        for (int i = 0; i < BLOCK; i++)
            if (!isfinite(l[i]) || !isfinite(r[i])) {
                fprintf(stderr, "FAIL: non-finite audio during state load\n");
                exit(1);
            }
        beats += (double)BLOCK / BEAT;
    }
    return NULL;
}

/* ─── Tests ──────────────────────────────────────────────────────────────── */

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : PLUGIN_CLAP_PATH;
    printf("=== CLAP Plugin Test (%s) ===\n", path);

    void *lib = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!lib) FAIL("dlopen: %s", dlerror());
    const clap_plugin_entry_t *entry = (const clap_plugin_entry_t *)dlsym(lib, "clap_entry");
    if (!entry || !entry->init(path)) FAIL("clap_entry");
    const clap_plugin_factory_t *fac =
        (const clap_plugin_factory_t *)entry->get_factory(CLAP_PLUGIN_FACTORY_ID);
    const clap_plugin_descriptor_t *desc = fac->get_plugin_descriptor(fac, 0);
    g_plugin = fac->create_plugin(fac, &s_host, desc->id);
    if (!g_plugin || !g_plugin->init(g_plugin)) FAIL("plugin init");

    /* 1. Note ports */
    const clap_plugin_note_ports_t *np =
        (const clap_plugin_note_ports_t *)g_plugin->get_extension(g_plugin, CLAP_EXT_NOTE_PORTS);
    if (!np) FAIL("no note ports extension");
    if (np->count(g_plugin, true) != 1 || np->count(g_plugin, false) != 1)
        FAIL("want 1 MIDI input and 1 MIDI output port, got %u and %u",
             np->count(g_plugin, true), np->count(g_plugin, false));
    clap_note_port_info_t info;
    if (!np->get(g_plugin, 0, false, &info) || strcmp(info.name, "MIDI Output") != 0 ||
        !(info.supported_dialects & CLAP_NOTE_DIALECT_MIDI))
        FAIL("MIDI output port info");
    printf("  PASS: MIDI input and output note ports\n");

    if (!g_plugin->activate(g_plugin, SR, 1, BLOCK)) FAIL("activate");
    if (!g_plugin->start_processing(g_plugin)) FAIL("start_processing");

    /* 2. Host stopped: silence, no MIDI */
    for (int b = 0; b < 20; b++)
        if (process_block(-1.0) > 1e-3f) FAIL("audio while the host is stopped");
    if (g_nev != 0) FAIL("MIDI while the host is stopped");
    printf("  PASS: nothing plays while the host is stopped\n");

    /* 3. Host plays from beat 0 for one bar at 120 BPM (plugin default is 145) */
    uint64_t start = g_block_pos;
    double beats = 0.0;
    float peak = 0.0f;
    for (int b = 0; b < 4 * BEAT / BLOCK; b++) {
        float pk = process_block(beats);
        if (pk > peak) peak = pk;
        beats += (double)BLOCK / BEAT;
    }
    if (peak < 0.01f) FAIL("no audio while the host plays");
    if (getenv("DUMP"))
        for (int i = 0; i < g_nev; i++)
            fprintf(stderr, "%8lld %02x %3u %3u\n", (long long)(g_ev[i].frame - start),
                    g_ev[i].status, g_ev[i].data1, g_ev[i].data2);
    for (int s = 0; s < 16; s++)
        if (find_ev(start + (uint64_t)s * STEP, 0x99, 42) < 0)
            FAIL("closed hat (ch 10, note 42) at step %d, frame %llu", s,
                 (unsigned long long)(start + (uint64_t)s * STEP));
    if (find_ev(start, 0x99, 36) < 0 || find_ev(start + 12 * STEP, 0x99, 36) < 0)
        FAIL("kick at steps 0 and 12");
    if (find_ev(start + 8 * STEP, 0x99, 38) < 0) FAIL("snare at step 8");
    if (find_ev(start, 0x90, 24) < 0) FAIL("808 bass (ch 1, note 24) at step 0");
    if (find_ev(start + 14 * STEP, 0x80, 24) < 0) FAIL("808 bass note-off after 14 steps");
    printf("  PASS: host play follows the host tempo (%d MIDI events in one bar)\n", g_nev);

    /* 4. Loop the first bar: the block that crosses beat 4 wraps back */
    uint64_t wrap_frame = 0;
    for (int b = 0; b < 2 * 4 * BEAT / BLOCK; b++) {
        double next = beats + (double)BLOCK / BEAT;
        if (beats < 4.0 && next > 4.0 && !wrap_frame)
            wrap_frame = g_block_pos + (uint64_t)llround((4.0 - beats) * BEAT);
        process_block(beats);
        beats = fmod(next, 4.0);
    }
    if (!wrap_frame) FAIL("loop never wrapped");
    if (count_ev(0x99, 36, wrap_frame - STEP / 2, wrap_frame + STEP / 2) != 1)
        FAIL("loop wrap should play the kick on step 0 exactly once, got %d",
             count_ev(0x99, 36, wrap_frame - STEP / 2, wrap_frame + STEP / 2));
    if (find_ev(wrap_frame, 0x99, 36) < 0)
        FAIL("kick at the loop wrap should land on frame %llu", (unsigned long long)wrap_frame);
    printf("  PASS: loop wrap plays the first step once, on time\n");

    /* Stop: every note that started since the host started has ended */
    process_block(-1.0);
    int ons = 0, offs = 0;
    for (int i = 0; i < g_nev; i++) {
        if ((g_ev[i].status & 0xF0) == 0x90) ons++;
        if ((g_ev[i].status & 0xF0) == 0x80) offs++;
    }
    if (ons != offs) FAIL("%d note-ons but %d note-offs after the host stopped", ons, offs);
    printf("  PASS: host stop ends every note (%d notes)\n", ons);

    /* 5. MIDI input: snare (ch 10, note 38) plays; an unmapped drum note doesn't */
    int quiet = 0;
    for (int b = 0; b < 2000 && quiet < 10; b++)        /* let effect tails die */
        quiet = process_block(-1.0) < 1e-3f ? quiet + 1 : 0;
    if (quiet < 10) FAIL("audio never went quiet after stopping");
    memset(&g_in[0], 0, sizeof(g_in[0]));
    g_in[0].header.size = sizeof(clap_event_midi_t);
    g_in[0].header.type = CLAP_EVENT_MIDI;
    g_in[0].data[0] = 0x99; g_in[0].data[1] = 49; g_in[0].data[2] = 100;
    g_nin = 1;
    if (process_block(-1.0) > 1e-3f) FAIL("unmapped drum note 49 made sound");
    g_in[0].data[1] = 38;
    g_nin = 1;
    if (process_block(-1.0) < 0.01f) FAIL("snare note 38 on ch 10 made no sound");
    printf("  PASS: MIDI input plays drums on channel 10\n");

    /* 6. State holds the project: silence the hats, save, load, re-activate */
    g_st = (const clap_plugin_state_t *)g_plugin->get_extension(g_plugin, CLAP_EXT_STATE);
    if (!g_st) FAIL("no state extension");
    cJSON *state = save_state();
    cJSON *plugin = cJSON_GetObjectItem(state, "plugin");
    cJSON *project = cJSON_GetObjectItem(state, "project");
    if (!cJSON_IsTrue(cJSON_GetObjectItem(plugin, "host_sync")))
        FAIL("saved state should have host sync on");
    cJSON *tracks = cJSON_GetObjectItem(cJSON_GetArrayItem(
        cJSON_GetObjectItem(project, "patterns"), 0), "tracks");
    cJSON *hats = cJSON_GetArrayItem(tracks, 2);
    cJSON *steps = cJSON_GetObjectItem(hats, "steps");
    /* Steps are saved sparsely, keyed by step number; the hats use all 16 */
    if (!cJSON_IsObject(steps) || cJSON_GetArraySize(steps) != 16)
        FAIL("saved state should hold the hat track's 16 steps");
    cJSON *step;
    cJSON_ArrayForEach(step, steps)
        cJSON_ReplaceItemInObject(step, "vel", cJSON_CreateNumber(0));
    load_state_json(state);

    wait_quiet();
    g_nev = 0;
    play_host(4.0);
    if (count_ev(0x99, 42, 0, UINT64_MAX) != 0) FAIL("loaded pattern should have no hats");
    if (count_ev(0x99, 36, 0, UINT64_MAX) != 2) FAIL("loaded pattern should keep its 2 kicks");

    /* The host re-activates at 44.1 kHz (sample rate change): pattern stays */
    g_plugin->stop_processing(g_plugin);
    g_plugin->deactivate(g_plugin);
    if (!g_plugin->activate(g_plugin, 44100, 1, BLOCK)) FAIL("re-activate");
    if (!g_plugin->start_processing(g_plugin)) FAIL("start_processing");
    g_nev = 0;
    play_host(4.0);
    if (count_ev(0x99, 42, 0, UINT64_MAX) != 0) FAIL("re-activation brought the hats back");
    if (count_ev(0x99, 36, 0, UINT64_MAX) != 2) FAIL("re-activation lost the kicks");
    printf("  PASS: edited pattern survives state save/load and re-activation\n");

    /* 7. Host sync off in the state: INT mode ignores the host transport */
    cJSON_ReplaceItemInObject(plugin, "host_sync", cJSON_CreateBool(0));
    load_state_json(state);
    cJSON *again = save_state();
    if (!cJSON_IsFalse(cJSON_GetObjectItem(cJSON_GetObjectItem(again, "plugin"), "host_sync")))
        FAIL("host sync off should round-trip through the state");
    cJSON_Delete(again);
    g_nev = 0;
    play_host(1.0);
    if (g_nev != 0) FAIL("INT mode should ignore the host transport, got %d events", g_nev);
    printf("  PASS: host sync saved with state; INT mode ignores the host\n");

    /* 8. Pre-1.5 state: { id, value } pairs. BPM (id 1) = 99 */
    struct { uint32_t id; double value; } legacy[2] = { { 1, 99.0 }, { 2, 0.5 } };
    memcpy(g_state, legacy, sizeof(legacy));
    g_state_len = sizeof(legacy);
    load_state_bytes();
    const clap_plugin_params_t *params =
        (const clap_plugin_params_t *)g_plugin->get_extension(g_plugin, CLAP_EXT_PARAMS);
    double bpm = 0.0;
    if (!params || !params->get_value(g_plugin, 1, &bpm) || fabs(bpm - 99.0) > 1e-6)
        FAIL("pre-1.5 state should set BPM to 99, got %.2f", bpm);
    printf("  PASS: pre-1.5 parameter state still loads\n");

    /* 9. Load state 50 times while another thread processes with the host playing */
    cJSON_ReplaceItemInObject(plugin, "host_sync", cJSON_CreateBool(1));
    pthread_t audio;
    atomic_store(&g_stop_thread, 0);
    pthread_create(&audio, NULL, audio_thread, NULL);
    for (int i = 0; i < 50; i++) {
        load_state_json(state);
        struct timespec ts = { 0, 2 * 1000000L };
        nanosleep(&ts, NULL);
    }
    atomic_store(&g_stop_thread, 1);
    pthread_join(audio, NULL);
    printf("  PASS: 50 state loads while audio runs\n");
    cJSON_Delete(state);

    /* 10. A second instance (demo pattern, with hats) next to this one (no hats) */
    const clap_plugin_t *a = g_plugin;
    const clap_plugin_t *b = fac->create_plugin(fac, &s_host, desc->id);
    if (!b || !b->init(b) || !b->activate(b, SR, 1, BLOCK) || !b->start_processing(b))
        FAIL("second instance");
    wait_quiet();
    int hats_a = 0, hats_b = 0;
    beats = 0.0;
    for (int blk = 0; blk < 4 * BEAT / BLOCK; blk++) {
        int before = count_ev(0x99, 42, 0, UINT64_MAX);
        g_plugin = a;
        process_block(beats);
        g_block_pos -= BLOCK;   /* both instances render the same frames */
        int mid = count_ev(0x99, 42, 0, UINT64_MAX);
        g_plugin = b;
        process_block(beats);
        hats_a += mid - before;
        hats_b += count_ev(0x99, 42, 0, UINT64_MAX) - mid;
        beats += (double)BLOCK / BEAT;
    }
    g_plugin = b;
    process_block(-1.0);
    b->stop_processing(b);
    b->deactivate(b);
    b->destroy(b);
    g_plugin = a;
    if (hats_a != 0 || hats_b != 16)
        FAIL("instances should be independent: %d hats from the edited one, %d from the demo (want 0 and 16)",
             hats_a, hats_b);
    printf("  PASS: two instances play at once with their own patterns\n");

    /* 11. Drum-only audio (synths muted) from the 909 kit, then from the
     * names v1.5.0 saved the built-in 808 kit under */
    static const char *const kit909[8] = {
        "samples/909/kick.wav", "samples/909/snare.wav", "samples/909/hihat-closed-1.wav",
        "samples/909/clap1.wav", "samples/909/hihat-open-1.wav", "samples/909/ride.wav",
        "samples/909/rim.wav", "samples/909/tom-h.wav",
    };
    static const char *const v150[8] = {
        "808 Kick", "808 Snare", "808 HiHat", "808 Clap",
        "808 OpenHH", "808 Cowbell", "808 Rimshot", "808 HiTom",
    };
    const char *const *sets[2] = { kit909, v150 };
    const char *want_first[2] = { "samples/909/kick.wav", "samples/808/01.BD.808.wav" };
    for (int k = 0; k < 2; k++) {
        cJSON *st11 = save_state();
        cJSON *proj = cJSON_GetObjectItem(st11, "project");
        cJSON *trks = cJSON_GetObjectItem(cJSON_GetArrayItem(
            cJSON_GetObjectItem(proj, "patterns"), 0), "tracks");
        cJSON *trk;
        cJSON_ArrayForEach(trk, trks) {
            cJSON *type = cJSON_GetObjectItem(trk, "type");
            if (cJSON_IsNumber(type) && type->valueint != 0) {   /* not a sampler */
                cJSON_DeleteItemFromObject(trk, "mute");
                cJSON_AddBoolToObject(trk, "mute", 1);
            }
        }
        cJSON_ReplaceItemInObject(proj, "sample_paths",
                                  cJSON_CreateStringArray(sets[k], 8));
        load_state_json(st11);
        cJSON_Delete(st11);

        wait_quiet();
        float drum_peak = 0.0f;
        beats = 0.0;
        for (int blk = 0; blk < 4 * BEAT / BLOCK; blk++) {
            float pk = process_block(beats);
            if (pk > drum_peak) drum_peak = pk;
            beats += (double)BLOCK / BEAT;
        }
        process_block(-1.0);
        if (drum_peak < 0.01f)
            FAIL("%s: the drums are silent (peak %.4f)", k ? "v1.5.0 names" : "909 kit", drum_peak);

        cJSON *again11 = save_state();
        cJSON *paths = cJSON_GetObjectItem(cJSON_GetObjectItem(again11, "project"), "sample_paths");
        const char *first = cJSON_GetStringValue(cJSON_GetArrayItem(paths, 0));
        if (!first || strcmp(first, want_first[k]) != 0)
            FAIL("%s: saved sample path is \"%s\", want \"%s\"",
                 k ? "v1.5.0 names" : "909 kit", first ? first : "(none)", want_first[k]);
        cJSON_Delete(again11);
    }
    printf("  PASS: 909 kit and v1.5.0 projects play their drums from built-in samples\n");

    g_plugin->stop_processing(g_plugin);
    g_plugin->deactivate(g_plugin);
    g_plugin->destroy(g_plugin);
    entry->deinit();
    printf("All CLAP plugin tests passed.\n");
    return 0;
}
