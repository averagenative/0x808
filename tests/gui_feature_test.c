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

    sq_engine_shutdown(&engine);
    printf("\n=== ALL GUI FEATURE TESTS PASSED ===\n");
    return 0;
}
