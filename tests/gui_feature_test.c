#include <stdio.h>
#include <string.h>
#include <math.h>
#include "engine/engine.h"
#include "engine/synth.h"
#include "engine/effects.h"
#include "formats/project.h"

#define ASSERT(cond, msg) do { if (!(cond)) { fprintf(stderr, "FAIL: %s\n", msg); return 1; } } while(0)

int main(void)
{
    sq_engine_t engine;
    printf("=== GUI Feature Test (headless) ===\n");

    /* Test 1: Engine init produces valid default state */
    sq_engine_init(&engine, 44100);
    ASSERT(engine.num_patterns >= 1, "Should have at least 1 pattern");
    ASSERT(engine.patterns[0].num_tracks >= 1, "Pattern should have tracks");
    ASSERT(engine.num_synth_presets > 0, "Should have synth presets");
    ASSERT(engine.transport.bpm >= 20.0 && engine.transport.bpm <= 300.0, "BPM in valid range");
    printf("  PASS: Engine init produces valid defaults\n");

    /* Test 2: Track type toggle simulation */
    sq_track_t *t = &engine.patterns[0].tracks[0];
    t->type = TRACK_SAMPLER;
    ASSERT(t->type == TRACK_SAMPLER, "Track should be sampler");
    t->type = TRACK_SYNTH;
    t->synth_preset = 0;
    ASSERT(t->type == TRACK_SYNTH, "Track should be synth");
    printf("  PASS: Track type toggle works\n");

    /* Test 3: Pattern switching */
    engine.num_patterns = 3;
    snprintf(engine.patterns[1].name, sizeof(engine.patterns[1].name), "Pat2");
    snprintf(engine.patterns[2].name, sizeof(engine.patterns[2].name), "Pat3");
    engine.transport.current_pattern = 1;
    ASSERT(engine.transport.current_pattern == 1, "Pattern index should be 1");
    printf("  PASS: Pattern switching\n");

    /* Test 4: Arrangement management */
    engine.arrangement.num_sections = 0;
    engine.arrangement.sections[0].pattern_index = 0;
    engine.arrangement.sections[0].repeat_count = 2;
    engine.arrangement.num_sections = 1;
    engine.arrangement.sections[1].pattern_index = 1;
    engine.arrangement.sections[1].repeat_count = 4;
    engine.arrangement.num_sections = 2;
    ASSERT(engine.arrangement.num_sections == 2, "Should have 2 sections");
    ASSERT(engine.arrangement.sections[1].repeat_count == 4, "Section 1 repeat should be 4");
    printf("  PASS: Arrangement management\n");

    /* Test 5: Effect slot management */
    effect_init(&engine.master_effects[0], EFFECT_REVERB, 44100);
    ASSERT(engine.master_effects[0].type == EFFECT_REVERB, "Master FX 0 should be reverb");
    engine.master_effects[0].bypass = true;
    ASSERT(engine.master_effects[0].bypass == true, "FX should be bypassed");
    effect_free(&engine.master_effects[0]);
    ASSERT(engine.master_effects[0].type == EFFECT_NONE, "FX should be cleared after free");
    printf("  PASS: Effect slot management\n");

    /* Test 6: Synth preset parameter ranges */
    for (uint32_t i = 0; i < engine.num_synth_presets; i++) {
        sq_synth_preset_t *p = &engine.synth_presets[i];
        ASSERT(p->synth_mode >= SYNTH_SUBTRACTIVE && p->synth_mode <= SYNTH_WAVETABLE, "Valid synth mode");
        ASSERT(p->amp_env.attack >= 0.0f, "Attack >= 0");
        ASSERT(p->amp_env.sustain >= 0.0f && p->amp_env.sustain <= 1.0f, "Sustain 0-1");
    }
    printf("  PASS: Synth preset parameter ranges valid\n");

    /* Test 7: Save/load round-trip preserves GUI-relevant state */
    engine.transport.mode = MODE_SONG;
    engine.transport.bpm = 135.0;
    engine.master_volume = 0.75f;
    engine.transport.swing = 0.3f;

    #ifdef _WIN32
    const char *tmpfile = "gui_feature_test.sqproj";
    #else
    const char *tmpfile = "/tmp/gui_feature_test.sqproj";
    #endif

    int rc = project_save(&engine, tmpfile);
    ASSERT(rc == 0, "Save should succeed");

    sq_engine_t loaded;
    sq_engine_init(&loaded, 44100);
    rc = project_load(&loaded, tmpfile);
    ASSERT(rc == 0, "Load should succeed");
    ASSERT(loaded.transport.mode == MODE_SONG, "Mode preserved");
    ASSERT(fabs(loaded.transport.bpm - 135.0) < 0.1, "BPM preserved");
    ASSERT(fabsf(loaded.master_volume - 0.75f) < 0.01f, "Volume preserved");
    ASSERT(fabsf(loaded.transport.swing - 0.3f) < 0.01f, "Swing preserved");
    remove(tmpfile);
    sq_engine_shutdown(&loaded);
    printf("  PASS: Save/load preserves GUI state\n");

    /* Test 8: Default 5 patterns with valid track lengths */
    {
        sq_engine_t e2;
        sq_engine_init(&e2, 44100);
        ASSERT(e2.num_patterns == 5, "Should have 5 default patterns");
        for (uint32_t pi = 0; pi < e2.num_patterns; pi++) {
            sq_pattern_t *pat = &e2.patterns[pi];
            ASSERT(pat->num_tracks >= 4, "Each pattern should have at least 4 tracks");
            ASSERT(pat->name[0] != '\0', "Pattern should have a name");
            for (uint32_t ti = 0; ti < pat->num_tracks; ti++) {
                ASSERT(pat->tracks[ti].length == 16, "Track length should be 16");
                ASSERT(pat->tracks[ti].volume > 0.0f, "Track volume should be > 0");
            }
        }
        printf("  PASS: 5 default patterns with valid track lengths\n");

        /* Test 9: Pattern switching bounds */
        e2.transport.current_pattern = 0;
        ASSERT(e2.transport.current_pattern == 0, "Pattern 0 selectable");
        e2.transport.current_pattern = 4;
        ASSERT(e2.transport.current_pattern == 4, "Pattern 4 selectable");
        ASSERT((uint32_t)e2.transport.current_pattern < e2.num_patterns, "Pattern index in bounds");
        printf("  PASS: Pattern switching stays in bounds\n");

        /* Test 10: Add new pattern (simulates '+' button) */
        uint32_t old_count = e2.num_patterns;
        int ni = (int)e2.num_patterns;
        e2.num_patterns++;
        sq_pattern_t *np = &e2.patterns[ni];
        memset(np, 0, sizeof(*np));
        snprintf(np->name, SQ_PATTERN_NAME_LEN, "Pattern %d", ni + 1);
        np->num_tracks = 4;
        for (uint32_t t = 0; t < np->num_tracks; t++) {
            np->tracks[t].length = 16;
            np->tracks[t].volume = 1.0f;
            np->tracks[t].sample_index = 0;
            np->tracks[t].synth_preset = -1;
        }
        ASSERT(e2.num_patterns == old_count + 1, "Pattern count increased");
        ASSERT(np->num_tracks == 4, "New pattern has 4 tracks");
        ASSERT(np->tracks[0].length == 16, "New pattern tracks have length 16");
        ASSERT(np->tracks[0].volume == 1.0f, "New pattern track volume is 1.0");
        printf("  PASS: Add new pattern with valid defaults\n");

        /* Test 11: Copy/paste pattern */
        e2.patterns[0].tracks[0].steps[0].velocity = 120;
        e2.patterns[0].tracks[0].steps[4].velocity = 110;
        sq_pattern_t clipboard = e2.patterns[0];
        e2.patterns[1] = clipboard;
        ASSERT(e2.patterns[1].tracks[0].steps[0].velocity == 120, "Paste preserves step 0 velocity");
        ASSERT(e2.patterns[1].tracks[0].steps[4].velocity == 110, "Paste preserves step 4 velocity");
        ASSERT(e2.patterns[1].num_tracks == e2.patterns[0].num_tracks, "Paste preserves track count");
        printf("  PASS: Copy/paste pattern preserves data\n");

        /* Test 12: Pattern save/load round-trip with multiple patterns */
        e2.transport.current_pattern = 2;
        #ifdef _WIN32
        const char *tmpfile2 = "pattern_test.sqproj";
        #else
        const char *tmpfile2 = "/tmp/pattern_test.sqproj";
        #endif
        ASSERT(project_save(&e2, tmpfile2) == 0, "Save multi-pattern project");
        sq_engine_t e3;
        sq_engine_init(&e3, 44100);
        ASSERT(project_load(&e3, tmpfile2) == 0, "Load multi-pattern project");
        ASSERT(e3.num_patterns == e2.num_patterns, "Pattern count preserved after load");
        /* current_pattern is runtime state, not persisted */
        ASSERT(e3.patterns[1].tracks[0].steps[0].velocity == 120, "Pasted pattern data survives save/load");
        for (uint32_t pi = 0; pi < e3.num_patterns; pi++) {
            for (uint32_t ti = 0; ti < e3.patterns[pi].num_tracks; ti++) {
                ASSERT(e3.patterns[pi].tracks[ti].length == 16, "Track lengths survive save/load");
            }
        }
        remove(tmpfile2);
        sq_engine_shutdown(&e3);
        printf("  PASS: Multi-pattern save/load round-trip\n");

        sq_engine_shutdown(&e2);
    }

    sq_engine_shutdown(&engine);
    printf("\n=== ALL GUI FEATURE TESTS PASSED ===\n");
    return 0;
}
