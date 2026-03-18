/*
 * kits.c -- Drum kit definitions and switching logic.
 *
 * Each kit defines relative paths to WAV files in the samples/ directory.
 * sq_kit_load() frees old sampler samples, loads new ones, and re-assigns
 * sample_index on all sampler tracks across all patterns.
 */

#include "engine/kits.h"
#include "formats/sample_io.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>  /* realpath */

#define LOG_TAG "kits"
#include "core/log.h"

/* Current kit index (-1 = unknown/custom) */
int sq_current_kit = 0;

/*
 * Kit definitions.
 * Slot order: kick, snare, closed hihat, clap, open hihat, cowbell, rimshot, tom
 * This matches the default 808 loading order in main_gui.c.
 */
const sq_kit_def_t sq_kits[SQ_NUM_KITS] = {
    {
        "808",
        {
            "samples/808/01.BD.808.wav",
            "samples/808/01.SD5.808.wav",
            "samples/808/01.CH.808.wav",
            "samples/808/01.CP.808.wav",
            "samples/808/01.OH.808.wav",
            "samples/808/01.CB.808.wav",
            "samples/808/01.RS.808.wav",
            "samples/808/01.HT.808.wav",
        },
        8
    },
    {
        "909",
        {
            "samples/909/kick.wav",
            "samples/909/snare.wav",
            "samples/909/hihat-closed-1.wav",
            "samples/909/clap1.wav",
            "samples/909/hihat-open-1.wav",
            "samples/909/ride.wav",
            "samples/909/rim.wav",
            "samples/909/tom-h.wav",
        },
        8
    },
    {
        "505",
        {
            "samples/505/tr505-kick.wav",
            "samples/505/tr505-snare.wav",
            "samples/505/tr505-hihat-closed.wav",
            "samples/505/tr505-clap.wav",
            "samples/505/tr505-hihat-open.wav",
            "samples/505/tr505-cowb-h.wav",
            "samples/505/tr505-rim.wav",
            "samples/505/tr505-tom-h.wav",
        },
        8
    },
    {
        "MRK-2",
        {
            "samples/mrk2/kick.wav",
            "samples/mrk2/snare.wav",
            "samples/mrk2/hihat-closed.wav",
            "samples/mrk2/clave.wav",
            "samples/mrk2/hihat-open.wav",
            "samples/mrk2/cymball-short.wav",
            "samples/mrk2/block.wav",
            "samples/mrk2/tom.wav",
        },
        8
    },
    {
        "CR-78",
        {
            "samples/808/03.BD.808.wav",
            "samples/808/03.SD5.808.wav",
            "samples/808/03.CH.808.wav",
            "samples/808/03.CP.808.wav",
            "samples/808/03.OH.808.wav",
            "samples/808/01.CB.808.wav",
            "samples/808/01.RS.808.wav",
            "samples/808/01.MT.808.wav",
        },
        8
    },
    {
        "LM-2",
        {
            "samples/808/05.BD.808.wav",
            "samples/808/05.SD5.808.wav",
            "samples/808/05.CH.808.wav",
            "samples/808/05.CP.808.wav",
            "samples/808/05.OH.808.wav",
            "samples/808/01.LC.808.wav",
            "samples/808/01.MC.808.wav",
            "samples/808/01.LT.808.wav",
        },
        8
    },
};

int sq_kit_load(sq_engine_t *engine, int kit_index, const char *base_dir)
{
    if (kit_index < 0 || kit_index >= SQ_NUM_KITS) {
        LOG_ERROR("Invalid kit index: %d", kit_index);
        return -1;
    }

    const sq_kit_def_t *kit = &sq_kits[kit_index];
    LOG_INFO("Loading kit: %s (%d slots)", kit->name, kit->num_slots);

    /* Stop all active sampler voices to prevent dangling pointers */
    for (int v = 0; v < SQ_MAX_VOICES; v++) {
        engine->voices[v].active = false;
    }

    /* Free existing samples that belong to the kit slots (indices 0..num_slots-1).
     * We only replace the first kit->num_slots sample slots; any additional
     * user-loaded samples above that are preserved. */
    int slots_to_load = kit->num_slots;
    for (int i = 0; i < slots_to_load && (uint32_t)i < engine->num_samples; i++) {
        sample_io_free(&engine->samples[i]);
        memset(&engine->samples[i], 0, sizeof(sq_sample_t));
    }

    /* Load new kit samples into the first slots */
    int loaded = 0;
    for (int i = 0; i < slots_to_load; i++) {
        char full_path[1024];

        /* Try relative to base_dir first */
        if (base_dir && base_dir[0]) {
            snprintf(full_path, sizeof(full_path), "%s%s", base_dir, kit->paths[i]);
            if (sample_io_load(full_path, &engine->samples[i]) == 0) {
                loaded++;
                LOG_INFO("  [%d] %s", i, engine->samples[i].name);
                continue;
            }
        }

        /* Try relative to CWD */
        if (sample_io_load(kit->paths[i], &engine->samples[i]) == 0) {
            loaded++;
            LOG_INFO("  [%d] %s", i, engine->samples[i].name);
            continue;
        }

#ifdef __APPLE__
        /* macOS .app bundle: try Contents/Resources/ relative to exe dir.
         * Resolve with realpath() so the stored filepath has no ".."
         * components — otherwise path_is_safe() rejects it on reload. */
        if (base_dir && base_dir[0]) {
            snprintf(full_path, sizeof(full_path), "%s../Resources/%s",
                     base_dir, kit->paths[i]);
            char resolved[1024];
            if (realpath(full_path, resolved) &&
                sample_io_load(resolved, &engine->samples[i]) == 0) {
                loaded++;
                LOG_INFO("  [%d] %s (bundle)", i, engine->samples[i].name);
                continue;
            }
        }
#endif

        LOG_WARN("  [%d] FAILED: %s", i, kit->paths[i]);
    }

    /* Ensure num_samples covers the kit slots */
    if ((uint32_t)slots_to_load > engine->num_samples) {
        engine->num_samples = (uint32_t)slots_to_load;
    }

    /* Re-assign sample_index on all sampler tracks across ALL patterns */
    for (uint32_t p = 0; p < engine->num_patterns; p++) {
        sq_pattern_t *pat = &engine->patterns[p];
        int sampler_idx = 0;
        for (uint32_t t = 0; t < pat->num_tracks; t++) {
            if (pat->tracks[t].type == TRACK_SAMPLER) {
                /* Map this sampler track to the corresponding kit slot */
                if (sampler_idx < slots_to_load) {
                    pat->tracks[t].sample_index = sampler_idx;
                }
                sampler_idx++;
            }
        }
    }

    sq_current_kit = kit_index;
    LOG_INFO("Kit '%s' loaded: %d/%d samples", kit->name, loaded, slots_to_load);
    return 0;
}
