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
#include <stdlib.h>  /* realpath / _fullpath */

#ifdef _WIN32
#include <windows.h>
#endif

#define LOG_TAG "kits"
#include "core/log.h"

/* Resolve a path to its canonical form (no "..", normalized separators).
 * Returns resolved on success, NULL on failure. */
static char *resolve_path(const char *path, char *resolved, size_t resolved_size)
{
#ifdef _WIN32
    if (_fullpath(resolved, path, resolved_size)) {
        /* Normalize forward slashes to backslashes */
        for (char *p = resolved; *p; p++)
            if (*p == '/') *p = '\\';
        return resolved;
    }
    return NULL;
#else
    return realpath(path, resolved);
#endif
}

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
    /* CR-78 — real Roland CompuRythm samples from the Oramics `sampled`
     * repo (Public Domain, sourced from boxedear.com). Replaces the
     * misleading 808-alias that was here. Slot mapping picks the
     * closest CR-78 element where the original drum-machine had no
     * direct equivalent (e.g. bongo for the rim-shot slot). */
    {
        "CR-78",
        {
            "samples/cr-78-real/kick.wav",
            "samples/cr-78-real/snare.wav",
            "samples/cr-78-real/hihat.wav",
            "samples/cr-78-real/rim.wav",
            "samples/cr-78-real/hihat-metal.wav",
            "samples/cr-78-real/cowbell.wav",
            "samples/cr-78-real/bongo-h.wav",
            "samples/cr-78-real/conga-l.wav",
        },
        8
    },

    /* LinnDrum — real LinnDrum LM-2 samples from Oramics (Public Domain,
     * sourced from machines.hyperreal.org). The pop-rock workhorse of
     * the 80s. Replaces the misleading 808-alias that was here. */
    {
        "LinnDrum",
        {
            "samples/linndrum/kick.wav",
            "samples/linndrum/snare-h.wav",
            "samples/linndrum/hihat-closed.wav",
            "samples/linndrum/clap.wav",
            "samples/linndrum/hihat-open.wav",
            "samples/linndrum/cowb.wav",
            "samples/linndrum/stick-h.wav",
            "samples/linndrum/tom-h.wav",
        },
        8
    },

    /* ── New kits built from existing samples (no downloads needed) ──── */

    /* Modern Trap — deep 808 sub-kicks + 808 snap/clap layer + closed
     * hat from 808/01.CH for that trap-style ride. Tom slot uses the
     * 808-synth tom-low for sub thump. */
    {
        "Trap",
        {
            "samples/808-synth/808-sub-kick.wav",
            "samples/808/01.SD5.808.wav",
            "samples/808/01.CH.808.wav",
            "samples/808-synth/808-clap.wav",
            "samples/808/01.OH.808.wav",
            "samples/808/01.CB.808.wav",
            "samples/808-synth/808-snap.wav",
            "samples/808-synth/808-tom-low.wav",
        },
        8
    },

    /* Acoustic — Pearl Master Studio Pack 1 by enoe (CC-BY 3.0).
     * Real recorded acoustic kit; the pack has no dedicated handclap,
     * so snare-03 (a softer snare hit) sits in the clap slot. Crash
     * fills the percussion slot. */
    {
        "Acoustic",
        {
            "samples/pearl/kick-01.wav",
            "samples/pearl/snare-01.wav",
            "samples/pearl/hihat-closed.wav",
            "samples/pearl/snare-03.wav",
            "samples/pearl/hihat-open.wav",
            "samples/pearl/crash-01.wav",
            "samples/pearl/snare-02.wav",
            "samples/pearl/tom-01.wav",
        },
        8
    },

    /* Lo-Fi — the secondary / dustier 909 samples paired with
     * percussion folder textures. Snare-short + clap2 are the
     * usable lo-fi-flavored variants we already have. */
    {
        "Lo-Fi",
        {
            "samples/909/kick-2.wav",
            "samples/909/snare-short.wav",
            "samples/909/hihat-closed-2.wav",
            "samples/909/clap2.wav",
            "samples/909/hihat-open-2.wav",
            "samples/percussion/shaker.wav",
            "samples/909/rim.wav",
            "samples/909/tom-l.wav",
        },
        8
    },

    /* Percussion — for users who want hand-percussion / latin-flavored
     * patterns. Bongo/clave/cowbells front-and-centre. */
    {
        "Percussion",
        {
            "samples/mrk2/bongo.wav",
            "samples/505/tr505-timbal.wav",
            "samples/percussion/clave.wav",
            "samples/percussion/clap.wav",
            "samples/percussion/shaker-long.wav",
            "samples/505/tr505-cowb-l.wav",
            "samples/505/tr505-cowb-h.wav",
            "samples/505/tr505-conga-h.wav",
        },
        8
    },

    /* Sub 808 — Mailbox Badger PD/CC-BY hybrid. Deep sub-bass kick,
     * sine/noise snare, tight analog hats. Re-pitch the kick in the
     * engine for melodic 808 lines. */
    {
        "Sub 808",
        {
            "samples/trap-808/kick.wav",
            "samples/trap-808/snare.wav",
            "samples/trap-808/hihat-closed.wav",
            "samples/trap-808/clap.wav",
            "samples/trap-808/hihat-open.wav",
            "samples/trap-808/perc-sub.wav",
            "samples/trap-808/rim.wav",
            "samples/trap-808/tom-sub.wav",
        },
        8
    },

    /* Cassette 808 — Chris Beckstrom's TR-808 reamped through a
     * cassette boombox (CC-BY 4.0). Saturated, hissy, wobbly — the
     * dedicated lo-fi character kit. */
    {
        "Cassette",
        {
            "samples/cassette/cassette_808_BD.wav",
            "samples/cassette/cassette_808_SD.wav",
            "samples/cassette/cassette_808_HH.wav",
            "samples/cassette/cassette_808_CP.wav",
            "samples/cassette/cassette_808_OHH.wav",
            "samples/cassette/cassette_808_CB.wav",
            "samples/cassette/cassette_808_RIM.wav",
            "samples/cassette/cassette_808_TOM1.wav",
        },
        8
    },

    /* Sound Canvas SC-8850 — Roland's GM-era ROMpler (PD Mark 1.0,
     * Paisley Computer). Clean, slightly synthetic 90s digital
     * character; sits between the analog 808/909 and the acoustic
     * Pearl kit. SC-8850 has no rim/sidestick — Agogo Low fills
     * the slot as a tight metallic accent. */
    {
        "SC-8850",
        {
            "samples/sc8850/kick.wav",
            "samples/sc8850/snare.wav",
            "samples/sc8850/hihat-closed.wav",
            "samples/sc8850/clap.wav",
            "samples/sc8850/hihat-open.wav",
            "samples/sc8850/cowbell.wav",
            "samples/sc8850/rim.wav",
            "samples/sc8850/tom.wav",
        },
        8
    },

    /* Tube 808 — same TR-808 as Cassette but reamped through a vacuum
     * tube preamp (CC-BY 4.0, Beckstrom). Warm and saturated rather
     * than wobbly. Use when you want grit without the hiss. */
    {
        "Tube 808",
        {
            "samples/tube-808/tube_808_BD.wav",
            "samples/tube-808/tube_808_SD.wav",
            "samples/tube-808/tube_808_HH.wav",
            "samples/tube-808/tube_808_CP.wav",
            "samples/tube-808/tube_808_OHH.wav",
            "samples/tube-808/tube_808_CB.wav",
            "samples/tube-808/tube_808_RIM.wav",
            "samples/tube-808/tube_808_TOM1.wav",
        },
        8
    },

    /* Paint Can 808 — TR-808 played through a paint can resonator
     * (CC-BY 4.0, Beckstrom). Aggressive metallic texture for
     * industrial/IDM. */
    {
        "Paint Can",
        {
            "samples/paint-can-808/808_BD.wav",
            "samples/paint-can-808/808_SD.wav",
            "samples/paint-can-808/808_HH.wav",
            "samples/paint-can-808/808_CP.wav",
            "samples/paint-can-808/808_OHH.wav",
            "samples/paint-can-808/808_CB.wav",
            "samples/paint-can-808/808_RIM.wav",
            "samples/paint-can-808/808_TOM1.wav",
        },
        8
    },

    /* Volca Modular — drum/percussion patches synthesized on a Korg
     * Volca Modular (CC-BY 4.0, Beckstrom). Tonal, often inharmonic;
     * IDM/glitch/electroacoustic textures. The pack has no white-noise
     * generator — NoisyHit + CymbalHH substitute for the hat slots. */
    {
        "Volca Mod",
        {
            "samples/volca-modular/kick.wav",
            "samples/volca-modular/snare.wav",
            "samples/volca-modular/hihat-closed.wav",
            "samples/volca-modular/clap.wav",
            "samples/volca-modular/hihat-open.wav",
            "samples/volca-modular/cowbell.wav",
            "samples/volca-modular/rim.wav",
            "samples/volca-modular/tom.wav",
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

        /* Try relative to base_dir first, resolving to canonical path
         * so the stored filepath is clean for autosave/reload. */
        if (base_dir && base_dir[0]) {
            char resolved[1024];
            snprintf(full_path, sizeof(full_path), "%s%s", base_dir, kit->paths[i]);
            if (resolve_path(full_path, resolved, sizeof(resolved)) &&
                sample_io_load(resolved, &engine->samples[i]) == 0) {
                loaded++;
                LOG_INFO("  [%d] %s", i, engine->samples[i].name);
                continue;
            }
            /* Fall back to unresolved path */
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
        /* macOS .app bundle: try Contents/Resources/ relative to exe dir. */
        if (base_dir && base_dir[0]) {
            snprintf(full_path, sizeof(full_path), "%s../Resources/%s",
                     base_dir, kit->paths[i]);
            char resolved[1024];
            if (resolve_path(full_path, resolved, sizeof(resolved)) &&
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
