/*
 * project_test.c — Test project save/load round-trip.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "engine/engine.h"
#include "formats/project.h"

#ifdef _WIN32
#define TEST_FILE "test_project.sqproj"
#else
#define TEST_FILE "/tmp/test_project.sqproj"
#endif

static void setup_engine(sq_engine_t *e) {
    sq_engine_init(e, 44100);

    /* Transport */
    e->transport.bpm = 140.0;
    e->transport.mode = MODE_SONG;

    /* Pattern 0: 2 tracks */
    e->num_patterns = 2;
    sq_pattern_t *p0 = &e->patterns[0];
    snprintf(p0->name, sizeof(p0->name), "Verse");
    p0->num_tracks = 2;

    /* Track 0: sampler */
    sq_track_t *t0 = &p0->tracks[0];
    t0->type = TRACK_SAMPLER;
    t0->length = 16;
    t0->sample_index = 0;
    t0->volume = 0.8f;
    t0->pan = -0.2f;
    t0->steps[0].velocity = 127;
    t0->steps[4].velocity = 100;
    t0->steps[4].pitch_offset = 3;
    t0->steps[8].velocity = 90;
    t0->steps[12].velocity = 110;

    /* Track 1: synth */
    sq_track_t *t1 = &p0->tracks[1];
    t1->type = TRACK_SYNTH;
    t1->length = 16;
    t1->synth_preset = 1;
    t1->volume = 0.6f;
    t1->pan = 0.3f;
    t1->steps[0].velocity = 100;
    t1->steps[0].note = 60;
    t1->steps[0].length = 2.0f;
    t1->steps[4].velocity = 80;
    t1->steps[4].note = 64;
    t1->steps[4].length = 1.5f;

    /* Pattern 1: 1 track */
    sq_pattern_t *p1 = &e->patterns[1];
    snprintf(p1->name, sizeof(p1->name), "Chorus");
    p1->num_tracks = 1;
    p1->tracks[0].type = TRACK_SAMPLER;
    p1->tracks[0].length = 16;
    p1->tracks[0].volume = 1.0f;
    p1->tracks[0].steps[0].velocity = 127;

    /* Synth preset 1 */
    sq_synth_preset_t *sp = &e->synth_presets[1];
    sp->osc1_wave = WAVE_SAW;
    sp->filter_cutoff = 2000.0f;
    sp->filter_resonance = 0.7f;
    sp->amp_env.attack = 0.01f;
    sp->amp_env.decay = 0.2f;
    sp->amp_env.sustain = 0.5f;
    sp->amp_env.release = 0.3f;

    /* Arrangement */
    e->arrangement.num_sections = 3;
    e->arrangement.sections[0].pattern_index = 0;
    e->arrangement.sections[0].repeat_count = 2;
    e->arrangement.sections[1].pattern_index = 1;
    e->arrangement.sections[1].repeat_count = 4;
    e->arrangement.sections[2].pattern_index = 0;
    e->arrangement.sections[2].repeat_count = 1;

    /* Master effects: slot 0 = reverb */
    e->master_effects[0].type = EFFECT_REVERB;
    e->master_effects[0].bypass = false;
    e->master_effects[0].reverb.room_size = 0.8f;
    e->master_effects[0].reverb.damping = 0.5f;
    e->master_effects[0].reverb.wet = 0.3f;
}

#define ASSERT_EQ_INT(a, b, msg) do { \
    if ((a) != (b)) { \
        fprintf(stderr, "FAIL: %s: expected %d, got %d\n", msg, (int)(b), (int)(a)); \
        return 1; \
    } \
} while(0)

#define ASSERT_EQ_FLOAT(a, b, msg) do { \
    if (fabsf((a) - (b)) > 0.001f) { \
        fprintf(stderr, "FAIL: %s: expected %.4f, got %.4f\n", msg, (double)(b), (double)(a)); \
        return 1; \
    } \
} while(0)

#define ASSERT_EQ_STR(a, b, msg) do { \
    if (strcmp((a), (b)) != 0) { \
        fprintf(stderr, "FAIL: %s: expected '%s', got '%s'\n", msg, (b), (a)); \
        return 1; \
    } \
} while(0)

int main(void) {
    sq_engine_t orig, loaded;

    /* Setup and save */
    setup_engine(&orig);
    printf("Saving project to %s...\n", TEST_FILE);
    int rc = project_save(&orig, TEST_FILE);
    if (rc != 0) {
        fprintf(stderr, "FAIL: project_save returned %d\n", rc);
        return 1;
    }
    printf("Save OK.\n");

    /* Load into fresh engine */
    sq_engine_init(&loaded, 44100);
    printf("Loading project from %s...\n", TEST_FILE);
    rc = project_load(&loaded, TEST_FILE);
    if (rc != 0) {
        fprintf(stderr, "FAIL: project_load returned %d\n", rc);
        return 1;
    }
    printf("Load OK.\n");

    /* Verify transport */
    ASSERT_EQ_FLOAT((float)loaded.transport.bpm, 140.0f, "bpm");
    ASSERT_EQ_INT(loaded.transport.mode, MODE_SONG, "play_mode");

    /* Verify patterns */
    ASSERT_EQ_INT(loaded.num_patterns, 2, "num_patterns");
    ASSERT_EQ_STR(loaded.patterns[0].name, "Verse", "pattern0 name");
    ASSERT_EQ_STR(loaded.patterns[1].name, "Chorus", "pattern1 name");
    ASSERT_EQ_INT(loaded.patterns[0].num_tracks, 2, "pat0 num_tracks");
    ASSERT_EQ_INT(loaded.patterns[1].num_tracks, 1, "pat1 num_tracks");

    /* Verify track 0 */
    sq_track_t *lt0 = &loaded.patterns[0].tracks[0];
    ASSERT_EQ_INT(lt0->type, TRACK_SAMPLER, "t0 type");
    ASSERT_EQ_INT(lt0->length, 16, "t0 length");
    ASSERT_EQ_INT(lt0->sample_index, 0, "t0 sample_index");
    ASSERT_EQ_FLOAT(lt0->volume, 0.8f, "t0 volume");
    ASSERT_EQ_FLOAT(lt0->pan, -0.2f, "t0 pan");
    ASSERT_EQ_INT(lt0->steps[0].velocity, 127, "t0 step0 vel");
    ASSERT_EQ_INT(lt0->steps[4].velocity, 100, "t0 step4 vel");
    ASSERT_EQ_INT(lt0->steps[4].pitch_offset, 3, "t0 step4 pitch");
    ASSERT_EQ_INT(lt0->steps[1].velocity, 0, "t0 step1 vel (empty)");

    /* Verify track 1 (synth) */
    sq_track_t *lt1 = &loaded.patterns[0].tracks[1];
    ASSERT_EQ_INT(lt1->type, TRACK_SYNTH, "t1 type");
    ASSERT_EQ_INT(lt1->synth_preset, 1, "t1 synth_preset");
    ASSERT_EQ_FLOAT(lt1->volume, 0.6f, "t1 volume");
    ASSERT_EQ_FLOAT(lt1->pan, 0.3f, "t1 pan");
    ASSERT_EQ_INT(lt1->steps[0].velocity, 100, "t1 step0 vel");
    ASSERT_EQ_INT(lt1->steps[0].note, 60, "t1 step0 note");
    ASSERT_EQ_FLOAT(lt1->steps[0].length, 2.0f, "t1 step0 length");

    /* Verify synth preset */
    sq_synth_preset_t *lsp = &loaded.synth_presets[1];
    ASSERT_EQ_INT(lsp->osc1_wave, WAVE_SAW, "preset1 osc1_wave");
    ASSERT_EQ_FLOAT(lsp->filter_cutoff, 2000.0f, "preset1 cutoff");
    ASSERT_EQ_FLOAT(lsp->filter_resonance, 0.7f, "preset1 reso");
    ASSERT_EQ_FLOAT(lsp->amp_env.attack, 0.01f, "preset1 attack");
    ASSERT_EQ_FLOAT(lsp->amp_env.decay, 0.2f, "preset1 decay");
    ASSERT_EQ_FLOAT(lsp->amp_env.sustain, 0.5f, "preset1 sustain");
    ASSERT_EQ_FLOAT(lsp->amp_env.release, 0.3f, "preset1 release");

    /* Verify arrangement */
    ASSERT_EQ_INT(loaded.arrangement.num_sections, 3, "num_sections");
    ASSERT_EQ_INT(loaded.arrangement.sections[0].pattern_index, 0, "sec0 pat");
    ASSERT_EQ_INT(loaded.arrangement.sections[0].repeat_count, 2, "sec0 repeat");
    ASSERT_EQ_INT(loaded.arrangement.sections[1].pattern_index, 1, "sec1 pat");
    ASSERT_EQ_INT(loaded.arrangement.sections[1].repeat_count, 4, "sec1 repeat");
    ASSERT_EQ_INT(loaded.arrangement.sections[2].pattern_index, 0, "sec2 pat");
    ASSERT_EQ_INT(loaded.arrangement.sections[2].repeat_count, 1, "sec2 repeat");

    /* Verify master effects */
    ASSERT_EQ_INT(loaded.master_effects[0].type, EFFECT_REVERB, "master fx0 type");
    ASSERT_EQ_INT(loaded.master_effects[0].bypass, 0, "master fx0 bypass");
    ASSERT_EQ_FLOAT(loaded.master_effects[0].reverb.room_size, 0.8f, "master fx0 room");
    ASSERT_EQ_FLOAT(loaded.master_effects[0].reverb.damping, 0.5f, "master fx0 damp");
    ASSERT_EQ_FLOAT(loaded.master_effects[0].reverb.wet, 0.3f, "master fx0 wet");

    /* Cleanup */
    remove(TEST_FILE);

    printf("\n=== ALL PROJECT SAVE/LOAD TESTS PASSED ===\n");
    return 0;
}
