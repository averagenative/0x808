/*
 * pattern_presets.cpp — Drum pattern and bass line preset library (Dear ImGui port).
 *
 * Contains common industry-standard drum patterns (four-on-floor, trap,
 * boom bap, DnB, reggaeton, etc.) and bass line generators that write
 * notes into synth tracks.
 *
 * Drum patterns assume 16-step resolution (4/4 time, 4 steps per beat).
 * Bass lines write MIDI notes into synth track steps.
 */

#include "imgui.h"

extern "C" {
#include "engine/engine.h"
#include "gui/undo.h"
}

extern "C" {
#define LOG_TAG "presets"
#include "core/log.h"
}

#include <cstring>
#include <cstdio>

/* --- Drum Pattern Definitions --- */

struct drum_preset_t {
    const char *name;
    uint8_t tracks[6][16];  /* velocity per track per step */
    float   suggested_bpm;
};

static const drum_preset_t s_drum_presets[] = {
    /* House / Four-on-floor */
    {
        "House",
        {
            {120, 0, 0, 0, 110, 0, 0, 0, 120, 0, 0, 0, 110, 0, 0, 0},
            {0, 0, 0, 0, 127, 0, 0, 0, 0, 0, 0, 0, 127, 0, 0, 0},
            {0, 0, 100, 0, 0, 0, 100, 0, 0, 0, 100, 0, 0, 0, 100, 0},
            {0, 0, 0, 0, 100, 0, 0, 0, 0, 0, 0, 0, 100, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 0, 80, 0, 0, 0, 0, 0, 0, 0, 80},
            {60, 40, 60, 40, 60, 40, 60, 40, 60, 40, 60, 40, 60, 40, 60, 40},
        },
        124.0f,
    },
    /* Boom Bap (Hip-Hop) */
    {
        "Boom Bap",
        {
            {120, 0, 0, 0, 0, 0, 0, 0, 0, 0, 110, 0, 120, 0, 0, 0},
            {0, 0, 0, 0, 127, 0, 0, 50, 0, 0, 0, 0, 127, 0, 0, 0},
            {90, 0, 70, 0, 90, 0, 70, 0, 90, 0, 70, 0, 90, 0, 70, 0},
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 70, 0, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
        },
        92.0f,
    },
    /* Trap */
    {
        "Trap",
        {
            {120, 0, 0, 0, 0, 0, 100, 0, 0, 0, 0, 110, 0, 0, 100, 0},
            {0, 0, 0, 0, 0, 0, 0, 0, 127, 0, 0, 0, 0, 0, 0, 0},
            {100, 60, 80, 60, 100, 60, 80, 60, 100, 60, 80, 60, 100, 60, 80, 90},
            {0, 0, 0, 0, 0, 0, 0, 0, 110, 0, 0, 0, 0, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 80},
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
        },
        140.0f,
    },
    /* Drum & Bass */
    {
        "DnB",
        {
            {120, 0, 0, 0, 0, 0, 0, 0, 0, 0, 110, 0, 0, 0, 0, 0},
            {0, 0, 0, 0, 127, 0, 0, 0, 0, 0, 0, 0, 127, 0, 0, 0},
            {90, 60, 70, 60, 90, 60, 70, 60, 90, 60, 70, 60, 90, 60, 70, 60},
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 0, 80, 0, 0, 0, 0, 0, 0, 0, 80},
            {70, 0, 70, 0, 70, 0, 70, 0, 70, 0, 70, 0, 70, 0, 70, 0},
        },
        174.0f,
    },
    /* Reggaeton */
    {
        "Reggaeton",
        {
            {120, 0, 0, 100, 0, 0, 0, 0, 120, 0, 0, 100, 0, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 120, 0, 0, 0, 0, 0, 0, 0, 120, 0},
            {80, 0, 80, 0, 80, 0, 80, 0, 80, 0, 80, 0, 80, 0, 80, 0},
            {0, 0, 0, 0, 0, 0, 100, 0, 0, 0, 0, 0, 0, 0, 100, 0},
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
        },
        95.0f,
    },
    /* Disco */
    {
        "Disco",
        {
            {120, 0, 0, 0, 120, 0, 0, 0, 120, 0, 0, 0, 120, 0, 0, 0},
            {0, 0, 0, 0, 120, 0, 0, 0, 0, 0, 0, 0, 120, 0, 0, 0},
            {100, 70, 80, 70, 100, 70, 80, 70, 100, 70, 80, 70, 100, 70, 80, 70},
            {0, 0, 0, 0, 110, 0, 0, 0, 0, 0, 0, 0, 110, 0, 0, 0},
            {0, 0, 80, 0, 0, 0, 80, 0, 0, 0, 80, 0, 0, 0, 80, 0},
            {60, 0, 60, 0, 60, 0, 60, 0, 60, 0, 60, 0, 60, 0, 60, 0},
        },
        120.0f,
    },
    /* Techno */
    {
        "Techno",
        {
            {127, 0, 0, 0, 120, 0, 0, 0, 127, 0, 0, 0, 120, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 80, 0, 0, 0, 0, 0, 0, 0, 80, 0},
            {100, 70, 80, 70, 100, 70, 80, 70, 100, 70, 80, 70, 100, 70, 80, 70},
            {0, 0, 0, 0, 110, 0, 0, 0, 0, 0, 0, 0, 110, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
        },
        130.0f,
    },
    /* Breakbeat */
    {
        "Breakbeat",
        {
            {120, 0, 0, 0, 0, 0, 0, 100, 0, 0, 110, 0, 0, 0, 0, 0},
            {0, 0, 0, 0, 127, 0, 0, 0, 0, 0, 0, 0, 0, 0, 127, 0},
            {90, 0, 70, 0, 90, 0, 70, 0, 90, 0, 70, 0, 90, 0, 70, 0},
            {0, 0, 0, 0, 100, 0, 0, 0, 0, 0, 0, 0, 0, 0, 100, 0},
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 80, 0, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
        },
        130.0f,
    },
    /* Lo-fi Hip-Hop */
    {
        "Lo-fi",
        {
            {100, 0, 0, 0, 0, 0, 90, 0, 0, 0, 0, 80, 100, 0, 0, 0},
            {0, 0, 0, 0, 100, 0, 0, 40, 0, 0, 0, 0, 100, 0, 0, 40},
            {70, 0, 50, 0, 70, 0, 50, 0, 70, 0, 50, 0, 70, 0, 50, 0},
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 60, 0, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
        },
        80.0f,
    },
    /* Rock / Pop */
    {
        "Rock",
        {
            {120, 0, 0, 0, 0, 0, 0, 0, 120, 0, 100, 0, 0, 0, 0, 0},
            {0, 0, 0, 0, 127, 0, 0, 0, 0, 0, 0, 0, 127, 0, 0, 0},
            {100, 0, 80, 0, 100, 0, 80, 0, 100, 0, 80, 0, 100, 0, 80, 0},
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 80, 0},
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
        },
        120.0f,
    },
    /* Afrobeat */
    {
        "Afrobeat",
        {
            {120, 0, 0, 0, 0, 0, 100, 0, 0, 0, 0, 0, 120, 0, 0, 0},
            {0, 0, 0, 40, 110, 0, 0, 0, 0, 0, 40, 0, 110, 0, 0, 0},
            {90, 60, 80, 60, 90, 60, 80, 60, 90, 60, 80, 60, 90, 60, 80, 60},
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 0, 80, 0, 0, 0, 0, 0, 0, 0, 80},
            {70, 50, 70, 50, 70, 50, 70, 50, 70, 50, 70, 50, 70, 50, 70, 50},
        },
        108.0f,
    },
    /* Bossa Nova */
    {
        "Bossa Nova",
        {
            {100, 0, 0, 90, 0, 0, 100, 0, 0, 0, 90, 0, 0, 0, 0, 0},
            {0, 0, 80, 0, 0, 0, 80, 0, 0, 0, 80, 0, 0, 0, 80, 0},
            {70, 0, 70, 0, 70, 0, 70, 0, 70, 0, 70, 0, 70, 0, 70, 0},
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {50, 40, 50, 40, 50, 40, 50, 40, 50, 40, 50, 40, 50, 40, 50, 40},
        },
        140.0f,
    },
};
#define NUM_DRUM_PRESETS ((int)(sizeof(s_drum_presets) / sizeof(s_drum_presets[0])))

/* --- Bass Line Definitions --- */

struct bass_step_t {
    uint8_t note;
    uint8_t velocity;
    float   length;
};

struct bass_preset_t {
    const char *name;
    bass_step_t steps[16];
    int synth_preset;
    float suggested_bpm;
};

static const bass_preset_t s_bass_presets[] = {
    {"Octave Bass", {{36,110,1},{48,80,1},{0,0,0},{48,70,1},{36,110,1},{48,80,1},{0,0,0},{48,70,1},{36,110,1},{48,80,1},{0,0,0},{48,70,1},{36,110,1},{48,80,1},{0,0,0},{48,70,1}}, 0, 124.0f},
    {"Root-Fifth", {{36,110,2},{0,0,0},{43,90,2},{0,0,0},{36,100,2},{0,0,0},{43,80,2},{0,0,0},{36,110,2},{0,0,0},{43,90,2},{0,0,0},{36,100,1},{0,0,0},{43,90,1},{41,70,1}}, 0, 120.0f},
    {"Walking Bass", {{36,100,1},{0,0,0},{38,90,1},{0,0,0},{40,100,1},{0,0,0},{41,90,1},{0,0,0},{43,100,1},{0,0,0},{41,90,1},{0,0,0},{40,100,1},{0,0,0},{38,90,1},{0,0,0}}, 14, 100.0f},
    {"Sub Bass", {{36,120,4},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{36,110,2},{0,0,0},{39,100,2},{0,0,0},{43,100,2},{0,0,0},{41,90,2},{0,0,0}}, 46, 128.0f},
    {"808 Trap", {{36,120,3},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{36,100,2},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{39,110,2},{0,0,0},{0,0,0},{36,100,2},{0,0,0}}, 46, 140.0f},
    {"Reese DnB", {{36,110,2},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{43,90,2},{0,0,0},{0,0,0},{41,100,2},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{38,90,2},{0,0,0},{36,80,1}}, 21, 174.0f},
    {"Acid 303", {{36,120,1},{0,0,0},{36,80,1},{48,90,1},{36,110,1},{0,0,0},{39,100,1},{0,0,0},{36,120,1},{41,70,1},{0,0,0},{43,90,1},{36,110,1},{0,0,0},{48,80,1},{36,90,1}}, 15, 130.0f},
    {"Disco Funk", {{36,110,1},{0,0,0},{36,70,1},{43,80,1},{0,0,0},{36,100,1},{0,0,0},{43,80,1},{41,100,1},{0,0,0},{41,70,1},{43,80,1},{0,0,0},{41,100,1},{0,0,0},{36,80,1}}, 14, 120.0f},
    {"Dembow Bass", {{36,120,2},{0,0,0},{0,0,0},{36,80,1},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{36,120,2},{0,0,0},{0,0,0},{36,80,1},{0,0,0},{0,0,0},{0,0,0},{0,0,0}}, 46, 95.0f},
    {"Minimal Pulse", {{36,100,1},{0,0,0},{0,0,0},{0,0,0},{36,90,1},{0,0,0},{0,0,0},{0,0,0},{36,100,1},{0,0,0},{0,0,0},{0,0,0},{36,90,1},{0,0,0},{0,0,0},{38,70,1}}, 0, 120.0f},
};
#define NUM_BASS_PRESETS ((int)(sizeof(s_bass_presets) / sizeof(s_bass_presets[0])))

/* --- Application Logic --- */

static void apply_drum_preset(sq_engine_t *engine, int preset_idx)
{
    int pat_idx = engine->transport.current_pattern;
    if (pat_idx < 0 || (uint32_t)pat_idx >= engine->num_patterns) return;
    sq_pattern_t *p = &engine->patterns[pat_idx];

    const drum_preset_t *preset = &s_drum_presets[preset_idx];

    undo_push(engine);

    int applied = 0;
    for (uint32_t t = 0; t < p->num_tracks && applied < 6; t++) {
        if (p->tracks[t].type != TRACK_SAMPLER) continue;

        for (int s = 0; s < 16 && (uint32_t)s < p->tracks[t].length; s++) {
            p->tracks[t].steps[s].velocity = preset->tracks[applied][s];
        }
        applied++;
    }

    LOG_INFO("Applied drum preset: %s (BPM suggestion: %.0f)",
             preset->name, preset->suggested_bpm);
}

static void apply_bass_preset(sq_engine_t *engine, int preset_idx)
{
    int pat_idx = engine->transport.current_pattern;
    if (pat_idx < 0 || (uint32_t)pat_idx >= engine->num_patterns) return;
    sq_pattern_t *p = &engine->patterns[pat_idx];

    const bass_preset_t *preset = &s_bass_presets[preset_idx];

    undo_push(engine);

    /* Find first synth track, or create one */
    int target = -1;
    for (uint32_t t = 0; t < p->num_tracks; t++) {
        if (p->tracks[t].type == TRACK_SYNTH) {
            target = (int)t;
            break;
        }
    }

    if (target < 0 && p->num_tracks < SQ_MAX_TRACKS) {
        target = (int)p->num_tracks;
        p->num_tracks++;
        p->tracks[target].type = TRACK_SYNTH;
        p->tracks[target].length = 16;
        p->tracks[target].volume = 0.7f;
        p->tracks[target].pan = 0.0f;
        p->tracks[target].mute = false;
        p->tracks[target].solo = false;
    }

    if (target < 0) return;

    p->tracks[target].synth_preset = preset->synth_preset;

    for (int s = 0; s < 16 && (uint32_t)s < p->tracks[target].length; s++) {
        p->tracks[target].steps[s].velocity = preset->steps[s].velocity;
        p->tracks[target].steps[s].note     = preset->steps[s].note;
        p->tracks[target].steps[s].length   = preset->steps[s].length;
        p->tracks[target].steps[s].pitch_offset = 0;
    }

    LOG_INFO("Applied bass preset: %s (synth preset %d, BPM suggestion: %.0f)",
             preset->name, preset->synth_preset, preset->suggested_bpm);
}

/* --- GUI State --- */

static int s_drum_preset_idx = 0;
static int s_bass_preset_idx = 0;
static int s_show_presets = 0;

/* --- Draw --- */

extern "C" void pattern_presets_draw(sq_engine_t *engine)
{
    if (!s_show_presets) return;

    bool open = true;
    ImGui::SetNextWindowSize(ImVec2(450, 380), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(200, 100), ImGuiCond_FirstUseEver);

    if (!ImGui::Begin("Pattern Presets", &open)) {
        ImGui::End();
        if (!open) s_show_presets = 0;
        return;
    }

    if (!open) {
        s_show_presets = 0;
        ImGui::End();
        return;
    }

    /* --- Drum Presets --- */
    ImGui::Text("Drum Patterns:");

    /* Build drum preset name list */
    const char *drum_names[NUM_DRUM_PRESETS];
    for (int i = 0; i < NUM_DRUM_PRESETS; i++)
        drum_names[i] = s_drum_presets[i].name;

    ImGui::SetNextItemWidth(180.0f);
    ImGui::Combo("##DrumPreset", &s_drum_preset_idx, drum_names, NUM_DRUM_PRESETS);
    ImGui::SameLine();

    if (ImGui::Button("Apply Drums")) {
        apply_drum_preset(engine, s_drum_preset_idx);
    }
    ImGui::SameLine();

    {
        char bpm_label[32];
        snprintf(bpm_label, sizeof(bpm_label), "BPM: %.0f",
                 s_drum_presets[s_drum_preset_idx].suggested_bpm);
        if (ImGui::Button(bpm_label)) {
            engine->transport.bpm =
                (double)s_drum_presets[s_drum_preset_idx].suggested_bpm;
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    /* --- Bass Presets --- */
    ImGui::Text("Bass Lines:");

    const char *bass_names[NUM_BASS_PRESETS];
    for (int i = 0; i < NUM_BASS_PRESETS; i++)
        bass_names[i] = s_bass_presets[i].name;

    ImGui::SetNextItemWidth(180.0f);
    ImGui::Combo("##BassPreset", &s_bass_preset_idx, bass_names, NUM_BASS_PRESETS);
    ImGui::SameLine();

    if (ImGui::Button("Apply Bass")) {
        apply_bass_preset(engine, s_bass_preset_idx);
    }
    ImGui::SameLine();

    {
        char bpm_label[32];
        snprintf(bpm_label, sizeof(bpm_label), "BPM: %.0f",
                 s_bass_presets[s_bass_preset_idx].suggested_bpm);
        if (ImGui::Button(bpm_label)) {
            engine->transport.bpm =
                (double)s_bass_presets[s_bass_preset_idx].suggested_bpm;
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    /* --- Combined (apply both drum + bass) --- */
    if (ImGui::Button("Apply Both", ImVec2(130, 30))) {
        apply_drum_preset(engine, s_drum_preset_idx);
        apply_bass_preset(engine, s_bass_preset_idx);
        engine->transport.bpm =
            (double)s_drum_presets[s_drum_preset_idx].suggested_bpm;
    }
    ImGui::SameLine();

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.2f, 0.2f, 1.0f));
    if (ImGui::Button("Clear Pattern", ImVec2(130, 30))) {
        int pat_idx = engine->transport.current_pattern;
        if (pat_idx >= 0 && (uint32_t)pat_idx < engine->num_patterns) {
            undo_push(engine);
            sq_pattern_t *p = &engine->patterns[pat_idx];
            for (uint32_t t = 0; t < p->num_tracks; t++) {
                for (uint32_t s = 0; s < p->tracks[t].length; s++) {
                    memset(&p->tracks[t].steps[s], 0, sizeof(sq_step_t));
                }
            }
            LOG_INFO("Cleared pattern %d", pat_idx);
        }
    }
    ImGui::PopStyleColor();

    ImGui::Spacing();

    /* Info tips */
    ImGui::TextColored(ImVec4(0.55f, 0.55f, 0.55f, 1.0f),
                       "Tip: Select a synth track to see the piano roll below");
    ImGui::TextColored(ImVec4(0.55f, 0.55f, 0.55f, 1.0f),
                       "Click notes in piano roll to edit. Right-click to delete.");

    ImGui::End();
}

extern "C" int *pattern_presets_visible_ptr(void)
{
    return &s_show_presets;
}
