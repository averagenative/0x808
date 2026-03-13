/*
 * sf2.c — SoundFont2 loading and rendering via TinySoundFont.
 *
 * TinySoundFont handles all the heavy lifting: parsing the .sf2 file,
 * managing sample data, and synthesizing audio. We just wrap it with
 * a simple interface matching our engine's patterns.
 */

#define TSF_IMPLEMENTATION
#include "tsf.h"

#include "formats/sf2.h"
#include <string.h>
#include <stdio.h>

int sf2_load(sq_engine_t *engine, const char *filepath)
{
    /* Unload any existing SF2 first */
    sf2_unload(engine);

    tsf *f = tsf_load_filename(filepath);
    if (!f) {
        fprintf(stderr, "sf2: Failed to load %s\n", filepath);
        return -1;
    }

    /* Configure output */
    tsf_set_output(f, TSF_STEREO_INTERLEAVED, (int)engine->sample_rate, 0.0f);
    tsf_set_max_voices(f, 64);

    engine->sf2 = f;
    strncpy(engine->sf2_path, filepath, sizeof(engine->sf2_path) - 1);
    engine->sf2_path[sizeof(engine->sf2_path) - 1] = '\0';

    /* Enumerate presets */
    int count = tsf_get_presetcount(f);
    engine->num_sf2_presets = 0;

    for (int i = 0; i < count && engine->num_sf2_presets < SQ_MAX_SF2_PRESETS; i++) {
        const char *name = tsf_get_presetname(f, i);
        if (!name) continue;

        sq_sf2_preset_t *p = &engine->sf2_presets[engine->num_sf2_presets];
        strncpy(p->name, name, SQ_SF2_NAME_LEN - 1);
        p->name[SQ_SF2_NAME_LEN - 1] = '\0';
        p->tsf_preset_index = i;
        p->bank = 0;
        p->preset_number = i;
        engine->num_sf2_presets++;
    }

    printf("sf2: Loaded %s (%u presets)\n", filepath, engine->num_sf2_presets);
    return 0;
}

void sf2_unload(sq_engine_t *engine)
{
    if (engine->sf2) {
        tsf_close(engine->sf2);
        engine->sf2 = NULL;
    }
    engine->num_sf2_presets = 0;
    engine->sf2_path[0] = '\0';
}

void sf2_note_on(sq_engine_t *engine, int preset_idx, int key, float vel)
{
    if (!engine->sf2) return;
    if (preset_idx < 0 || (uint32_t)preset_idx >= engine->num_sf2_presets) return;

    int tsf_idx = engine->sf2_presets[preset_idx].tsf_preset_index;
    tsf_note_on(engine->sf2, tsf_idx, key, vel);
}

void sf2_note_off(sq_engine_t *engine, int preset_idx, int key)
{
    if (!engine->sf2) return;
    if (preset_idx < 0 || (uint32_t)preset_idx >= engine->num_sf2_presets) return;

    int tsf_idx = engine->sf2_presets[preset_idx].tsf_preset_index;
    tsf_note_off(engine->sf2, tsf_idx, key);
}

void sf2_render(sq_engine_t *engine, float *output, uint32_t num_frames)
{
    if (!engine->sf2) return;
    if (tsf_active_voice_count(engine->sf2) == 0) return;

    /* TSF renders and mixes into the buffer (flag_mixing=1) */
    tsf_render_float(engine->sf2, output, (int)num_frames, 1);
}
