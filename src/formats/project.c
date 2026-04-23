/*
 * project.c — Save/load project state as JSON.
 *
 * Format: .sqproj (JSON)
 * {
 *   "version": 1,
 *   "bpm": 120.0,
 *   "master_volume": 1.0,
 *   "sample_paths": ["samples/kicks/kick.wav", ...],
 *   "synth_presets": [...],
 *   "patterns": [...],
 *   "arrangement": {...},
 *   "master_effects": [...]
 * }
 */

#define LOG_TAG "project"
#include "core/log.h"
#include "formats/project.h"
#include "formats/sample_io.h"
#include "engine/synth.h"
#include "engine/kits.h"

#include "cJSON.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#ifdef _WIN32
#include <windows.h>
#endif

/* ─── Path safety helper ─────────────────────────────────────────────────── */

static bool path_is_safe(const char *path)
{
    if (!path || !path[0]) return false;
    /* Reject path traversal (directory escape) */
    if (strstr(path, "..")) return false;
    /* Allow absolute paths — needed for autosave with full filepaths */
    return true;
}

/* Resolve a path to its canonical form (no "..", normalized separators).
 * Returns resolved on success, NULL on failure. */
static char *resolve_path(const char *path, char *resolved, size_t resolved_size)
{
#ifdef _WIN32
    if (_fullpath(resolved, path, resolved_size)) {
        for (char *p = resolved; *p; p++)
            if (*p == '/') *p = '\\';
        return resolved;
    }
    return NULL;
#else
    (void)resolved_size;
    return realpath(path, resolved);
#endif
}

/* ─── Clamp helpers ──────────────────────────────────────────────────────── */

static float clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static int clampi(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/* ─── Save helpers ───────────────────────────────────────────────────────── */

static cJSON *adsr_to_json(const sq_adsr_params_t *env)
{
    cJSON *j = cJSON_CreateObject();
    cJSON_AddNumberToObject(j, "a", env->attack);
    cJSON_AddNumberToObject(j, "d", env->decay);
    cJSON_AddNumberToObject(j, "s", env->sustain);
    cJSON_AddNumberToObject(j, "r", env->release);
    return j;
}

static cJSON *lfo_to_json(const sq_lfo_t *lfo)
{
    cJSON *j = cJSON_CreateObject();
    cJSON_AddNumberToObject(j, "waveform", lfo->waveform);
    cJSON_AddNumberToObject(j, "rate", lfo->rate);
    cJSON_AddNumberToObject(j, "depth", lfo->depth);
    cJSON_AddNumberToObject(j, "dest", lfo->dest);
    return j;
}

static cJSON *preset_to_json(const sq_synth_preset_t *p)
{
    cJSON *j = cJSON_CreateObject();
    cJSON_AddStringToObject(j, "name", p->name);
    cJSON_AddNumberToObject(j, "osc1_wave", p->osc1_wave);
    cJSON_AddNumberToObject(j, "osc2_wave", p->osc2_wave);
    cJSON_AddNumberToObject(j, "osc_mix", p->osc_mix);
    cJSON_AddNumberToObject(j, "osc2_detune", p->osc2_detune);
    cJSON_AddNumberToObject(j, "unison_voices", p->unison_voices);
    cJSON_AddNumberToObject(j, "unison_detune", p->unison_detune);
    cJSON_AddNumberToObject(j, "filter_type", p->filter_type);
    cJSON_AddNumberToObject(j, "filter_cutoff", p->filter_cutoff);
    cJSON_AddNumberToObject(j, "filter_resonance", p->filter_resonance);
    cJSON_AddNumberToObject(j, "filter_env_depth", p->filter_env_depth);
    cJSON_AddItemToObject(j, "amp_env", adsr_to_json(&p->amp_env));
    cJSON_AddItemToObject(j, "filter_env", adsr_to_json(&p->filter_env));
    cJSON_AddItemToObject(j, "lfo", lfo_to_json(&p->lfo));
    cJSON_AddBoolToObject(j, "lfo_bpm_sync", p->lfo_bpm_sync);
    cJSON_AddNumberToObject(j, "lfo_sync_division", p->lfo_sync_division);
    cJSON_AddNumberToObject(j, "synth_mode", p->synth_mode);

    /* Wavetable parameters */
    if (p->synth_mode == SYNTH_WAVETABLE) {
        cJSON_AddNumberToObject(j, "wt_bank", p->wt_bank_index);
        cJSON_AddNumberToObject(j, "wt_pos", p->wt_position);
        cJSON_AddNumberToObject(j, "wt_env_depth", p->wt_env_depth);
        cJSON_AddNumberToObject(j, "wt_lfo_depth", p->wt_lfo_depth);
    }

    /* FM parameters */
    if (p->synth_mode == SYNTH_FM) {
        cJSON_AddNumberToObject(j, "fm_algorithm", p->fm_algorithm);
        cJSON *ops = cJSON_CreateArray();
        for (int o = 0; o < FM_NUM_OPERATORS; o++) {
            cJSON *op = cJSON_CreateObject();
            cJSON_AddNumberToObject(op, "ratio", p->fm_ops[o].freq_ratio);
            cJSON_AddNumberToObject(op, "level", p->fm_ops[o].level);
            cJSON_AddNumberToObject(op, "feedback", p->fm_ops[o].feedback);
            cJSON_AddItemToObject(op, "env", adsr_to_json(&p->fm_ops[o].env));
            cJSON_AddItemToArray(ops, op);
        }
        cJSON_AddItemToObject(j, "fm_ops", ops);
    }
    return j;
}

static cJSON *step_to_json(const sq_step_t *s)
{
    /* Only save non-empty steps to keep file small */
    if (s->velocity == 0 && s->note == 0 && s->pitch_offset == 0)
        return NULL;

    cJSON *j = cJSON_CreateObject();
    cJSON_AddNumberToObject(j, "vel", s->velocity);
    if (s->pitch_offset != 0)
        cJSON_AddNumberToObject(j, "pitch", s->pitch_offset);
    if (s->note != 0)
        cJSON_AddNumberToObject(j, "note", s->note);
    if (s->length > 0.0f)
        cJSON_AddNumberToObject(j, "len", s->length);
    if (s->probability > 0)
        cJSON_AddNumberToObject(j, "prob", s->probability);
    if (s->retrigger > 0)
        cJSON_AddNumberToObject(j, "retrig", s->retrigger);
    if (s->micro_offset != 0.0f)
        cJSON_AddNumberToObject(j, "utime", s->micro_offset);
    /* Parameter locks (only save non-zero) */
    for (int p = 0; p < 4; p++) {
        if (s->param[p] != 0.0f) {
            char key[8];
            snprintf(key, sizeof(key), "p%d", p);
            cJSON_AddNumberToObject(j, key, s->param[p]);
        }
    }
    return j;
}

static cJSON *track_to_json(const sq_track_t *t)
{
    cJSON *j = cJSON_CreateObject();
    cJSON_AddNumberToObject(j, "type", t->type);
    cJSON_AddNumberToObject(j, "length", t->length);
    cJSON_AddNumberToObject(j, "sample_index", t->sample_index);
    cJSON_AddNumberToObject(j, "synth_preset", t->synth_preset);
    cJSON_AddNumberToObject(j, "volume", t->volume);
    cJSON_AddNumberToObject(j, "pan", t->pan);
    cJSON_AddBoolToObject(j, "mute", t->mute);
    cJSON_AddBoolToObject(j, "solo", t->solo);
    cJSON_AddNumberToObject(j, "humanize", t->humanize);
    cJSON_AddNumberToObject(j, "color_index", t->color_index);
    if (t->choke_group > 0)
        cJSON_AddNumberToObject(j, "choke_group", t->choke_group);
    if (t->timing_humanize > 0.0f)
        cJSON_AddNumberToObject(j, "timing_humanize", t->timing_humanize);
    if (t->sample_start > 0)
        cJSON_AddNumberToObject(j, "sample_start", t->sample_start);
    if (t->sample_end > 0)
        cJSON_AddNumberToObject(j, "sample_end", t->sample_end);
    if (t->sample_reverse)
        cJSON_AddBoolToObject(j, "sample_reverse", t->sample_reverse);
    cJSON_AddNumberToObject(j, "sf2_preset", t->sf2_preset);

    /* Steps — sparse: only save non-empty ones with their index */
    cJSON *steps = cJSON_CreateObject();
    for (uint32_t s = 0; s < t->length; s++) {
        cJSON *sj = step_to_json(&t->steps[s]);
        if (sj) {
            char key[12];
            snprintf(key, sizeof(key), "%u", s);
            cJSON_AddItemToObject(steps, key, sj);
        }
    }
    cJSON_AddItemToObject(j, "steps", steps);

    /* Effects */
    cJSON *effects = cJSON_CreateArray();
    for (int e = 0; e < MAX_TRACK_EFFECTS; e++) {
        cJSON *ej = cJSON_CreateObject();
        cJSON_AddNumberToObject(ej, "type", t->effects[e].type);
        cJSON_AddBoolToObject(ej, "bypass", t->effects[e].bypass);

        switch (t->effects[e].type) {
        case EFFECT_FILTER:
            cJSON_AddNumberToObject(ej, "mode", t->effects[e].filter.mode);
            cJSON_AddNumberToObject(ej, "cutoff", t->effects[e].filter.cutoff);
            cJSON_AddNumberToObject(ej, "resonance", t->effects[e].filter.resonance);
            cJSON_AddNumberToObject(ej, "wet", t->effects[e].filter.wet);
            break;
        case EFFECT_DELAY:
            cJSON_AddNumberToObject(ej, "time", t->effects[e].delay.time);
            cJSON_AddNumberToObject(ej, "feedback", t->effects[e].delay.feedback);
            cJSON_AddNumberToObject(ej, "wet", t->effects[e].delay.wet);
            cJSON_AddBoolToObject(ej, "bpm_sync", t->effects[e].delay.bpm_sync);
            cJSON_AddNumberToObject(ej, "sync_div", t->effects[e].delay.sync_division);
            break;
        case EFFECT_REVERB:
            cJSON_AddNumberToObject(ej, "room_size", t->effects[e].reverb.room_size);
            cJSON_AddNumberToObject(ej, "damping", t->effects[e].reverb.damping);
            cJSON_AddNumberToObject(ej, "wet", t->effects[e].reverb.wet);
            break;
        case EFFECT_EQ: {
            cJSON *bands = cJSON_CreateArray();
            for (int bi = 0; bi < EQ_NUM_BANDS; bi++) {
                cJSON *b = cJSON_CreateObject();
                cJSON_AddNumberToObject(b, "type", t->effects[e].eq.bands[bi].type);
                cJSON_AddNumberToObject(b, "freq", t->effects[e].eq.bands[bi].frequency);
                cJSON_AddNumberToObject(b, "gain", t->effects[e].eq.bands[bi].gain_db);
                cJSON_AddNumberToObject(b, "q",    t->effects[e].eq.bands[bi].q);
                cJSON_AddItemToArray(bands, b);
            }
            cJSON_AddItemToObject(ej, "bands", bands);
            break;
        }
        case EFFECT_LIMITER:
            cJSON_AddNumberToObject(ej, "ceiling", t->effects[e].limiter.ceiling);
            cJSON_AddNumberToObject(ej, "release_ms", t->effects[e].limiter.release_ms);
            break;
        default:
            break;
        }
        cJSON_AddItemToArray(effects, ej);
    }
    cJSON_AddItemToObject(j, "effects", effects);

    return j;
}

static cJSON *effect_slot_to_json(const sq_effect_slot_t *slot)
{
    cJSON *j = cJSON_CreateObject();
    cJSON_AddNumberToObject(j, "type", slot->type);
    cJSON_AddBoolToObject(j, "bypass", slot->bypass);

    switch (slot->type) {
    case EFFECT_FILTER:
        cJSON_AddNumberToObject(j, "mode", slot->filter.mode);
        cJSON_AddNumberToObject(j, "cutoff", slot->filter.cutoff);
        cJSON_AddNumberToObject(j, "resonance", slot->filter.resonance);
        cJSON_AddNumberToObject(j, "wet", slot->filter.wet);
        break;
    case EFFECT_DELAY:
        cJSON_AddNumberToObject(j, "time", slot->delay.time);
        cJSON_AddNumberToObject(j, "feedback", slot->delay.feedback);
        cJSON_AddNumberToObject(j, "wet", slot->delay.wet);
        cJSON_AddBoolToObject(j, "bpm_sync", slot->delay.bpm_sync);
        cJSON_AddNumberToObject(j, "sync_div", slot->delay.sync_division);
        break;
    case EFFECT_REVERB:
        cJSON_AddNumberToObject(j, "room_size", slot->reverb.room_size);
        cJSON_AddNumberToObject(j, "damping", slot->reverb.damping);
        cJSON_AddNumberToObject(j, "wet", slot->reverb.wet);
        break;
    case EFFECT_EQ: {
        cJSON *bands = cJSON_CreateArray();
        for (int bi = 0; bi < EQ_NUM_BANDS; bi++) {
            cJSON *b = cJSON_CreateObject();
            cJSON_AddNumberToObject(b, "type", slot->eq.bands[bi].type);
            cJSON_AddNumberToObject(b, "freq", slot->eq.bands[bi].frequency);
            cJSON_AddNumberToObject(b, "gain", slot->eq.bands[bi].gain_db);
            cJSON_AddNumberToObject(b, "q",    slot->eq.bands[bi].q);
            cJSON_AddItemToArray(bands, b);
        }
        cJSON_AddItemToObject(j, "bands", bands);
        break;
    }
    case EFFECT_LIMITER:
        cJSON_AddNumberToObject(j, "ceiling", slot->limiter.ceiling);
        cJSON_AddNumberToObject(j, "release_ms", slot->limiter.release_ms);
        break;
    default:
        break;
    }
    return j;
}

/* ─── Save ───────────────────────────────────────────────────────────────── */

int project_save(const sq_engine_t *engine, const char *filepath)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        LOG_ERROR("Failed to allocate root JSON object");
        return -1;
    }
    cJSON_AddNumberToObject(root, "version", 1);
    cJSON_AddNumberToObject(root, "bpm", engine->transport.bpm);
    cJSON_AddNumberToObject(root, "play_mode", engine->transport.mode);
    cJSON_AddNumberToObject(root, "swing", engine->transport.swing);
    cJSON_AddNumberToObject(root, "master_volume", engine->master_volume);
    cJSON_AddNumberToObject(root, "sample_rate", engine->sample_rate);

    /* MIDI CC map — only save non-default entries */
    {
        cJSON *cc_map = cJSON_CreateObject();
        for (int i = 0; i < 128; i++) {
            if (engine->cc_map.map[i] != -1) {
                char key[8];
                snprintf(key, sizeof(key), "%d", i);
                cJSON_AddNumberToObject(cc_map, key, engine->cc_map.map[i]);
            }
        }
        cJSON_AddItemToObject(root, "cc_map", cc_map);
    }

    /* Sample paths */
    cJSON *samples = cJSON_CreateArray();
    if (!samples) {
        LOG_ERROR("Failed to allocate sample_paths JSON array");
        cJSON_Delete(root);
        return -1;
    }
    for (uint32_t i = 0; i < engine->num_samples; i++) {
        const char *path = engine->samples[i].filepath[0]
                         ? engine->samples[i].filepath
                         : (engine->samples[i].name[0]
                            ? engine->samples[i].name
                            : NULL);
        /* Slot is empty (failed kit load left it zeroed) — fall back to the
         * current kit's default path so reload can recover instead of
         * persisting "" and locking the user out of the kit selector. */
        if (!path && sq_current_kit >= 0 && sq_current_kit < SQ_NUM_KITS
            && i < (uint32_t)sq_kits[sq_current_kit].num_slots) {
            path = sq_kits[sq_current_kit].paths[i];
        }
        cJSON_AddItemToArray(samples, cJSON_CreateString(path ? path : ""));
    }
    cJSON_AddItemToObject(root, "sample_paths", samples);

    /* Synth presets */
    cJSON *presets = cJSON_CreateArray();
    if (!presets) {
        LOG_ERROR("Failed to allocate synth_presets JSON array");
        cJSON_Delete(root);
        return -1;
    }
    for (uint32_t i = 0; i < engine->num_synth_presets; i++) {
        cJSON_AddItemToArray(presets, preset_to_json(&engine->synth_presets[i]));
    }
    cJSON_AddItemToObject(root, "synth_presets", presets);

    /* Patterns */
    cJSON *patterns = cJSON_CreateArray();
    if (!patterns) {
        LOG_ERROR("Failed to allocate patterns JSON array");
        cJSON_Delete(root);
        return -1;
    }
    for (uint32_t p = 0; p < engine->num_patterns; p++) {
        const sq_pattern_t *pat = &engine->patterns[p];
        cJSON *pj = cJSON_CreateObject();
        cJSON_AddStringToObject(pj, "name", pat->name);
        cJSON_AddNumberToObject(pj, "num_tracks", pat->num_tracks);

        cJSON *tracks = cJSON_CreateArray();
        for (uint32_t t = 0; t < pat->num_tracks; t++) {
            cJSON_AddItemToArray(tracks, track_to_json(&pat->tracks[t]));
        }
        cJSON_AddItemToObject(pj, "tracks", tracks);
        cJSON_AddItemToArray(patterns, pj);
    }
    cJSON_AddItemToObject(root, "patterns", patterns);

    /* Arrangement */
    cJSON *arr = cJSON_CreateObject();
    cJSON_AddNumberToObject(arr, "num_sections", engine->arrangement.num_sections);
    cJSON *sections = cJSON_CreateArray();
    for (uint32_t s = 0; s < engine->arrangement.num_sections; s++) {
        cJSON *sj = cJSON_CreateObject();
        cJSON_AddNumberToObject(sj, "pattern_index",
                                engine->arrangement.sections[s].pattern_index);
        cJSON_AddNumberToObject(sj, "repeat_count",
                                engine->arrangement.sections[s].repeat_count);
        cJSON_AddItemToArray(sections, sj);
    }
    cJSON_AddItemToObject(arr, "sections", sections);
    cJSON_AddItemToObject(root, "arrangement", arr);

    /* SF2 path */
    if (engine->sf2_path[0]) {
        cJSON_AddStringToObject(root, "sf2_path", engine->sf2_path);
    }

    /* Master effects */
    cJSON *mfx = cJSON_CreateArray();
    for (int i = 0; i < MAX_TRACK_EFFECTS; i++) {
        cJSON_AddItemToArray(mfx, effect_slot_to_json(&engine->master_effects[i]));
    }
    cJSON_AddItemToObject(root, "master_effects", mfx);

    /* Write to file */
    char *json_str = cJSON_Print(root);
    cJSON_Delete(root);

    if (!json_str) {
        LOG_ERROR("Failed to serialize JSON");
        return -1;
    }

    FILE *f = fopen(filepath, "wb");
    if (!f) {
        LOG_ERROR("Failed to open %s for writing", filepath);
        free(json_str);
        return -1;
    }

    size_t len = strlen(json_str);
    size_t written = fwrite(json_str, 1, len, f);
    fflush(f);
    fclose(f);
    if (written != len) {
        LOG_ERROR("Short write: expected %zu, wrote %zu", len, written);
    }
    free(json_str);

    LOG_INFO("Project saved: %s", filepath);
    return 0;
}

/* ─── Load helpers ───────────────────────────────────────────────────────── */

static void json_to_adsr(const cJSON *j, sq_adsr_params_t *env)
{
    if (!j) return;
    cJSON *a = cJSON_GetObjectItem(j, "a");
    cJSON *d = cJSON_GetObjectItem(j, "d");
    cJSON *s = cJSON_GetObjectItem(j, "s");
    cJSON *r = cJSON_GetObjectItem(j, "r");
    if (a) env->attack  = (float)a->valuedouble;
    if (d) env->decay   = (float)d->valuedouble;
    if (s) env->sustain  = (float)s->valuedouble;
    if (r) env->release  = (float)r->valuedouble;
}

static void json_to_lfo(const cJSON *j, sq_lfo_t *lfo)
{
    if (!j) return;
    cJSON *w = cJSON_GetObjectItem(j, "waveform");
    cJSON *r = cJSON_GetObjectItem(j, "rate");
    cJSON *d = cJSON_GetObjectItem(j, "depth");
    cJSON *dest = cJSON_GetObjectItem(j, "dest");
    if (w) lfo->waveform = (sq_waveform_t)(int)w->valuedouble;
    if (r) lfo->rate     = (float)r->valuedouble;
    if (d) lfo->depth    = (float)d->valuedouble;
    if (dest) lfo->dest  = (sq_lfo_dest_t)(int)dest->valuedouble;
}

static void json_to_preset(const cJSON *j, sq_synth_preset_t *p)
{
    if (!j) return;
    cJSON *name = cJSON_GetObjectItem(j, "name");
    if (name && name->valuestring) {
        strncpy(p->name, name->valuestring, sizeof(p->name) - 1);
        p->name[sizeof(p->name) - 1] = '\0';
    }

    cJSON *v;
    v = cJSON_GetObjectItem(j, "osc1_wave");     if (v) p->osc1_wave = (sq_waveform_t)clampi((int)v->valuedouble, 0, 3);
    v = cJSON_GetObjectItem(j, "osc2_wave");     if (v) p->osc2_wave = (sq_waveform_t)clampi((int)v->valuedouble, 0, 3);
    v = cJSON_GetObjectItem(j, "osc_mix");        if (v) p->osc_mix = clampf((float)v->valuedouble, 0.0f, 1.0f);
    v = cJSON_GetObjectItem(j, "osc2_detune");    if (v) p->osc2_detune = clampf((float)v->valuedouble, -24.0f, 24.0f);
    v = cJSON_GetObjectItem(j, "unison_voices");  if (v) {
        int uv = (int)v->valuedouble;
        if (uv < 1 || uv > 7) {
            LOG_WARN("unison_voices %d clamped to [1, 7]", uv);
        }
        p->unison_voices = clampi(uv, 1, 7);
    }
    v = cJSON_GetObjectItem(j, "unison_detune");  if (v) p->unison_detune = clampf((float)v->valuedouble, 0.0f, 50.0f);
    v = cJSON_GetObjectItem(j, "filter_type");    if (v) p->filter_type = (sq_filter_type_t)clampi((int)v->valuedouble, 0, 2);
    v = cJSON_GetObjectItem(j, "filter_cutoff");  if (v) {
        float fc = (float)v->valuedouble;
        if (fc < 20.0f || fc > 20000.0f) {
            LOG_WARN("filter_cutoff %.1f clamped to [20, 20000]", fc);
        }
        p->filter_cutoff = clampf(fc, 20.0f, 20000.0f);
    }
    v = cJSON_GetObjectItem(j, "filter_resonance"); if (v) {
        float fr = (float)v->valuedouble;
        if (fr < 0.5f || fr > 20.0f) {
            LOG_WARN("filter_resonance %.2f clamped to [0.5, 20]", fr);
        }
        p->filter_resonance = clampf(fr, 0.5f, 20.0f);
    }
    v = cJSON_GetObjectItem(j, "filter_env_depth"); if (v) p->filter_env_depth = clampf((float)v->valuedouble, -1.0f, 1.0f);

    json_to_adsr(cJSON_GetObjectItem(j, "amp_env"), &p->amp_env);
    json_to_adsr(cJSON_GetObjectItem(j, "filter_env"), &p->filter_env);
    json_to_lfo(cJSON_GetObjectItem(j, "lfo"), &p->lfo);

    v = cJSON_GetObjectItem(j, "lfo_bpm_sync");   if (v) p->lfo_bpm_sync = cJSON_IsTrue(v);
    v = cJSON_GetObjectItem(j, "lfo_sync_division"); if (v) p->lfo_sync_division = clampi((int)v->valuedouble, 0, 5);
    v = cJSON_GetObjectItem(j, "synth_mode");    if (v) {
        int sm = (int)v->valuedouble;
        if (sm < 0 || sm > 2) {
            LOG_WARN("synth_mode %d clamped to [0, 2]", sm);
        }
        p->synth_mode = (sq_synth_mode_t)clampi(sm, 0, 2);
    }

    /* Wavetable parameters */
    v = cJSON_GetObjectItem(j, "wt_bank");      if (v) {
        int wb = (int)v->valuedouble;
        if (wb < 0 || wb >= SQ_WT_MAX_BANKS) {
            LOG_WARN("wt_bank_index %d clamped to [0, %d]", wb, SQ_WT_MAX_BANKS - 1);
        }
        p->wt_bank_index = clampi(wb, 0, SQ_WT_MAX_BANKS - 1);
    }
    v = cJSON_GetObjectItem(j, "wt_pos");        if (v) {
        float wp = (float)v->valuedouble;
        if (wp < 0.0f || wp > 1.0f) {
            LOG_WARN("wt_position %.3f clamped to [0, 1]", wp);
        }
        p->wt_position = clampf(wp, 0.0f, 1.0f);
    }
    v = cJSON_GetObjectItem(j, "wt_env_depth");  if (v) p->wt_env_depth = clampf((float)v->valuedouble, -1.0f, 1.0f);
    v = cJSON_GetObjectItem(j, "wt_lfo_depth");  if (v) p->wt_lfo_depth = clampf((float)v->valuedouble, 0.0f, 1.0f);

    /* FM parameters */
    v = cJSON_GetObjectItem(j, "fm_algorithm");  if (v) {
        int fa = (int)v->valuedouble;
        if (fa < 0 || fa > 7) {
            LOG_WARN("fm_algorithm %d clamped to [0, 7]", fa);
        }
        p->fm_algorithm = clampi(fa, 0, 7);
    }
    cJSON *ops = cJSON_GetObjectItem(j, "fm_ops");
    if (ops && cJSON_IsArray(ops)) {
        int n = cJSON_GetArraySize(ops);
        for (int o = 0; o < n && o < FM_NUM_OPERATORS; o++) {
            cJSON *op = cJSON_GetArrayItem(ops, o);
            cJSON *rv;
            rv = cJSON_GetObjectItem(op, "ratio");    if (rv) p->fm_ops[o].freq_ratio = clampf((float)rv->valuedouble, 0.5f, 16.0f);
            rv = cJSON_GetObjectItem(op, "level");    if (rv) p->fm_ops[o].level = clampf((float)rv->valuedouble, 0.0f, 1.0f);
            rv = cJSON_GetObjectItem(op, "feedback");  if (rv) p->fm_ops[o].feedback = clampf((float)rv->valuedouble, 0.0f, 1.0f);
            json_to_adsr(cJSON_GetObjectItem(op, "env"), &p->fm_ops[o].env);
        }
    }
}

static void json_to_effect_slot(const cJSON *j, sq_effect_slot_t *slot, uint32_t sr)
{
    if (!j) return;
    cJSON *t = cJSON_GetObjectItem(j, "type");
    if (!t) return;

    int type_val = (int)t->valuedouble;
    if (type_val < 0 || type_val >= EFFECT_TYPE_COUNT) {
        LOG_WARN("effect type %d invalid, clamped to EFFECT_NONE", type_val);
        type_val = EFFECT_NONE;
    }
    sq_effect_type_t type = (sq_effect_type_t)type_val;
    effect_init(slot, type, sr);

    cJSON *bp = cJSON_GetObjectItem(j, "bypass");
    if (bp) slot->bypass = cJSON_IsTrue(bp);

    cJSON *v;
    switch (type) {
    case EFFECT_FILTER:
        v = cJSON_GetObjectItem(j, "mode");      if (v) slot->filter.mode = (sq_efx_filter_mode_t)clampi((int)v->valuedouble, 0, 2);
        v = cJSON_GetObjectItem(j, "cutoff");     if (v) slot->filter.cutoff = clampf((float)v->valuedouble, 20.0f, 20000.0f);
        v = cJSON_GetObjectItem(j, "resonance");  if (v) slot->filter.resonance = clampf((float)v->valuedouble, 0.5f, 20.0f);
        v = cJSON_GetObjectItem(j, "wet");        if (v) slot->filter.wet = clampf((float)v->valuedouble, 0.0f, 1.0f);
        break;
    case EFFECT_DELAY:
        v = cJSON_GetObjectItem(j, "time");       if (v) slot->delay.time = clampf((float)v->valuedouble, 0.001f, 2.0f);
        v = cJSON_GetObjectItem(j, "feedback");   if (v) slot->delay.feedback = clampf((float)v->valuedouble, 0.0f, 0.95f);
        v = cJSON_GetObjectItem(j, "wet");        if (v) slot->delay.wet = clampf((float)v->valuedouble, 0.0f, 1.0f);
        v = cJSON_GetObjectItem(j, "bpm_sync");   if (v) slot->delay.bpm_sync = cJSON_IsTrue(v);
        v = cJSON_GetObjectItem(j, "sync_div");   if (v) slot->delay.sync_division = clampi((int)v->valuedouble, 0, 3);
        break;
    case EFFECT_REVERB:
        v = cJSON_GetObjectItem(j, "room_size");  if (v) slot->reverb.room_size = clampf((float)v->valuedouble, 0.0f, 1.0f);
        v = cJSON_GetObjectItem(j, "damping");    if (v) slot->reverb.damping = clampf((float)v->valuedouble, 0.0f, 1.0f);
        v = cJSON_GetObjectItem(j, "wet");        if (v) slot->reverb.wet = clampf((float)v->valuedouble, 0.0f, 1.0f);
        break;
    case EFFECT_EQ: {
        cJSON *bands = cJSON_GetObjectItem(j, "bands");
        if (bands && cJSON_IsArray(bands)) {
            int n = cJSON_GetArraySize(bands);
            if (n > EQ_NUM_BANDS) n = EQ_NUM_BANDS;
            for (int bi = 0; bi < n; bi++) {
                cJSON *band = cJSON_GetArrayItem(bands, bi);
                if (!band) continue;
                v = cJSON_GetObjectItem(band, "type");
                if (v) slot->eq.bands[bi].type =
                    (sq_eq_band_type_t)clampi((int)v->valuedouble, 0, 2);
                v = cJSON_GetObjectItem(band, "freq");
                if (v) slot->eq.bands[bi].frequency =
                    clampf((float)v->valuedouble, 20.0f, 20000.0f);
                v = cJSON_GetObjectItem(band, "gain");
                if (v) slot->eq.bands[bi].gain_db =
                    clampf((float)v->valuedouble, -24.0f, 24.0f);
                v = cJSON_GetObjectItem(band, "q");
                if (v) slot->eq.bands[bi].q =
                    clampf((float)v->valuedouble, 0.1f, 10.0f);
            }
        }
        break;
    }
    case EFFECT_LIMITER:
        v = cJSON_GetObjectItem(j, "ceiling");
        if (v) slot->limiter.ceiling = clampf((float)v->valuedouble, 0.001f, 1.0f);
        v = cJSON_GetObjectItem(j, "release_ms");
        if (v) slot->limiter.release_ms = clampf((float)v->valuedouble, 1.0f, 2000.0f);
        break;
    default:
        break;
    }
}

/* ─── Load ───────────────────────────────────────────────────────────────── */

int project_load(sq_engine_t *engine, const char *filepath)
{
    FILE *f = fopen(filepath, "rb");
    if (!f) {
        LOG_ERROR("Failed to open %s (errno=%d)", filepath, errno);
        return -1;
    }

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    if (fsize < 0) {
        LOG_ERROR("ftell failed for %s", filepath);
        fclose(f);
        return -1;
    }
    if (fsize > 50 * 1024 * 1024) {
        LOG_ERROR("Project file too large: %ld bytes", fsize);
        fclose(f);
        return -1;
    }
    fseek(f, 0, SEEK_SET);

    char *json_str = malloc((size_t)fsize + 1);
    if (!json_str) {
        LOG_ERROR("Failed to allocate %ld bytes for project file", fsize);
        fclose(f);
        return -1;
    }
    size_t read_bytes = fread(json_str, 1, (size_t)fsize, f);
    if ((long)read_bytes != fsize) {
        LOG_ERROR("Short read: expected %ld, got %zu", fsize, read_bytes);
        free(json_str);
        fclose(f);
        return -1;
    }
    json_str[fsize] = '\0';
    fclose(f);

    cJSON *root = cJSON_Parse(json_str);
    const char *parse_err = cJSON_GetErrorPtr(); /* save before free */
    free(json_str);

    if (!root) {
        LOG_ERROR("Failed to parse JSON: %s", parse_err ? parse_err : "(unknown)");
        return -1;
    }

    /* Preserve base_dir across re-init (sq_engine_init memsets to zero) */
    char saved_base_dir[512];
    memcpy(saved_base_dir, engine->base_dir, sizeof(saved_base_dir));

    /* Shutdown existing engine state */
    sq_engine_shutdown(engine);

    /* Re-init with saved sample rate */
    cJSON *sr = cJSON_GetObjectItem(root, "sample_rate");
    uint32_t sample_rate = sr ? (uint32_t)sr->valuedouble : 44100;
    sq_engine_init(engine, sample_rate);

    /* Restore base_dir so kit reload and reset-to-defaults can find samples */
    memcpy(engine->base_dir, saved_base_dir, sizeof(engine->base_dir));

    /* BPM and volume */
    cJSON *bpm = cJSON_GetObjectItem(root, "bpm");
    if (bpm) {
        engine->transport.bpm = bpm->valuedouble;
        if (engine->transport.bpm < 20.0 || engine->transport.bpm > 300.0) {
            LOG_WARN("bpm %.1f clamped to [20, 300]", engine->transport.bpm);
            engine->transport.bpm = clampf((float)engine->transport.bpm, 20.0f, 300.0f);
        }
    }
    cJSON *mode = cJSON_GetObjectItem(root, "play_mode");
    if (mode) {
        int m = (int)mode->valuedouble;
        if (m < 0 || m > 2) {
            LOG_WARN("play_mode %d clamped to [0, 2]", m);
            m = clampi(m, 0, 2);
        }
        engine->transport.mode = (sq_play_mode_t)m;
    }
    cJSON *sw = cJSON_GetObjectItem(root, "swing");
    if (sw) {
        engine->transport.swing = (float)sw->valuedouble;
        if (engine->transport.swing < 0.0f || engine->transport.swing > 1.0f) {
            LOG_WARN("swing %.3f clamped to [0, 1]", engine->transport.swing);
            engine->transport.swing = clampf(engine->transport.swing, 0.0f, 1.0f);
        }
    }
    cJSON *vol = cJSON_GetObjectItem(root, "master_volume");
    if (vol) {
        engine->master_volume = (float)vol->valuedouble;
        if (engine->master_volume < 0.0f || engine->master_volume > 2.0f) {
            LOG_WARN("master_volume %.3f clamped to [0, 2]", engine->master_volume);
            engine->master_volume = clampf(engine->master_volume, 0.0f, 2.0f);
        }
    }

    /* MIDI CC map */
    cJSON *cc_map = cJSON_GetObjectItem(root, "cc_map");
    if (cc_map && cJSON_IsObject(cc_map)) {
        /* Start from factory defaults, then override with saved values */
        cJSON *entry = NULL;
        cJSON_ArrayForEach(entry, cc_map) {
            int cc = atoi(entry->string);
            if (cc >= 0 && cc < 128) {
                int param = (int)entry->valuedouble;
                if (param >= -1 && param < SQ_PARAM_COUNT)
                    engine->cc_map.map[cc] = (int8_t)param;
            }
        }
    }

    /* Load samples (by path — try to find them).
     * Skip if samples are already loaded with matching names (e.g., autosave
     * restoring after demo pattern already loaded the same kit). */
    cJSON *samples = cJSON_GetObjectItem(root, "sample_paths");
    if (samples && cJSON_IsArray(samples)) {
        int n = cJSON_GetArraySize(samples);

        /* Check if the already-loaded samples match the project's sample list */
        bool samples_match = ((int)engine->num_samples >= n);
        if (samples_match) {
            for (int i = 0; i < n; i++) {
                cJSON *sp = cJSON_GetArrayItem(samples, i);
                if (!sp || !sp->valuestring) { samples_match = false; break; }
                /* Compare by name (strip path prefix if any) */
                const char *saved_name = sp->valuestring;
                const char *slash = strrchr(saved_name, '/');
                if (!slash) slash = strrchr(saved_name, '\\');
                if (slash) saved_name = slash + 1;
                bool found = false;
                for (uint32_t e = 0; e < engine->num_samples; e++) {
                    if (strstr(engine->samples[e].name, saved_name)) {
                        found = true; break;
                    }
                }
                if (!found) { samples_match = false; break; }
            }
        }
        if (samples_match) {
            LOG_INFO("Samples already loaded (%d match, %d in engine) — skipping reload",
                     n, engine->num_samples);
        } else {
            LOG_INFO("Samples don't match (project has %d, engine has %d) — reloading",
                     n, engine->num_samples);
        }

        for (int i = 0; i < n && !samples_match && engine->num_samples < SQ_MAX_SAMPLES; i++) {
            cJSON *sp = cJSON_GetArrayItem(samples, i);
            if (sp && sp->valuestring) {
                /* Reject unsafe paths (traversal, absolute) */
                if (!path_is_safe(sp->valuestring)) {
                    LOG_WARN("Rejected unsafe sample path: %s", sp->valuestring);
                    continue;
                }
                /* Try loading from several locations */
                int idx = (int)engine->num_samples;
                bool loaded = false;

                /* 1. Try the saved path directly (resolve to canonical form) */
                {
                    char resolved[1024];
                    if (resolve_path(sp->valuestring, resolved, sizeof(resolved)) &&
                        sample_io_load(resolved, &engine->samples[idx]) == 0) {
                        engine->num_samples++;
                        loaded = true;
                        LOG_INFO("Loaded sample [%d]: %s", idx, engine->samples[idx].name);
                    }
                }
                /* 1b. Try unresolved saved path as fallback */
                if (!loaded &&
                    sample_io_load(sp->valuestring, &engine->samples[idx]) == 0) {
                    engine->num_samples++;
                    loaded = true;
                    LOG_INFO("Loaded sample [%d]: %s", idx, engine->samples[idx].name);
                }

                /* 2. Try relative to base_dir (e.g., base_dir/samples/808/name) */
                if (!loaded && engine->base_dir[0]) {
                    const char *kit_dirs[] = {"samples/808/", "samples/909/",
                                              "samples/505/", "samples/mrk2/",
                                              "samples/", NULL};
                    for (int kd = 0; kit_dirs[kd] && !loaded; kd++) {
                        char full[1024], resolved[1024];
                        snprintf(full, sizeof(full), "%s%s%s",
                                 engine->base_dir, kit_dirs[kd], sp->valuestring);
                        const char *try_path = full;
                        if (resolve_path(full, resolved, sizeof(resolved)))
                            try_path = resolved;
                        if (sample_io_load(try_path, &engine->samples[idx]) == 0) {
                            engine->num_samples++;
                            loaded = true;
                            LOG_INFO("Loaded sample [%d]: %s (from %s)",
                                     idx, engine->samples[idx].name, kit_dirs[kd]);
                        }
                    }
                }
#ifdef __APPLE__
                /* 3. macOS .app bundle: try Contents/Resources/ */
                if (!loaded && engine->base_dir[0]) {
                    char full[1024], resolved[1024];
                    snprintf(full, sizeof(full), "%s../Resources/%s",
                             engine->base_dir, sp->valuestring);
                    const char *try_path = full;
                    if (resolve_path(full, resolved, sizeof(resolved)))
                        try_path = resolved;
                    if (sample_io_load(try_path, &engine->samples[idx]) == 0) {
                        engine->num_samples++;
                        loaded = true;
                        LOG_INFO("Loaded sample [%d]: %s (bundle)", idx,
                                 engine->samples[idx].name);
                    }
                }
#endif
                if (!loaded) {
                    LOG_WARN("Could not load sample: %s", sp->valuestring);
                    /* Add placeholder with name */
                    strncpy(engine->samples[idx].name, sp->valuestring,
                            SQ_SAMPLE_NAME_LEN - 1);
                    engine->samples[idx].name[SQ_SAMPLE_NAME_LEN - 1] = '\0';
                    engine->num_samples++;
                }
            }
        }

        /* Self-heal: if the project listed samples but we ended up with
         * none loaded (e.g. autosave with empty/stale paths), fall back
         * to the saved kit so the user isn't stranded with zero samples
         * and a dead kit selector. */
        if (!samples_match && engine->num_samples == 0 && n > 0) {
            int kit = (sq_current_kit >= 0 && sq_current_kit < SQ_NUM_KITS)
                      ? sq_current_kit : 0;
            LOG_WARN("All %d sample paths failed to load — restoring kit %d",
                     n, kit);
            sq_kit_load(engine, kit, engine->base_dir);
        }
    }

    /* Synth presets */
    cJSON *presets = cJSON_GetObjectItem(root, "synth_presets");
    if (presets && cJSON_IsArray(presets)) {
        int n = cJSON_GetArraySize(presets);
        engine->num_synth_presets = 0;
        for (int i = 0; i < n && (uint32_t)i < SQ_MAX_SYNTH_PRESETS; i++) {
            json_to_preset(cJSON_GetArrayItem(presets, i), &engine->synth_presets[i]);
            engine->num_synth_presets++;
        }
    }

    /* Patterns */
    cJSON *patterns = cJSON_GetObjectItem(root, "patterns");
    if (patterns && cJSON_IsArray(patterns)) {
        int n = cJSON_GetArraySize(patterns);
        engine->num_patterns = 0;
        for (int p = 0; p < n && (uint32_t)p < SQ_MAX_PATTERNS; p++) {
            cJSON *pj = cJSON_GetArrayItem(patterns, p);
            sq_pattern_t *pat = &engine->patterns[p];
            memset(pat, 0, sizeof(*pat));

            cJSON *name = cJSON_GetObjectItem(pj, "name");
            if (name && name->valuestring) {
                strncpy(pat->name, name->valuestring, SQ_PATTERN_NAME_LEN - 1);
                pat->name[SQ_PATTERN_NAME_LEN - 1] = '\0';
            }

            cJSON *nt = cJSON_GetObjectItem(pj, "num_tracks");
            if (nt) {
                pat->num_tracks = (uint32_t)nt->valuedouble;
                if (pat->num_tracks > SQ_MAX_TRACKS) {
                    LOG_WARN("num_tracks %u clamped to %d", pat->num_tracks, SQ_MAX_TRACKS);
                    pat->num_tracks = SQ_MAX_TRACKS;
                }
            }

            cJSON *tracks = cJSON_GetObjectItem(pj, "tracks");
            if (tracks && cJSON_IsArray(tracks)) {
                int tn = cJSON_GetArraySize(tracks);
                for (int t = 0; t < tn && (uint32_t)t < SQ_MAX_TRACKS; t++) {
                    cJSON *tj = cJSON_GetArrayItem(tracks, t);
                    sq_track_t *track = &pat->tracks[t];

                    cJSON *v;
                    v = cJSON_GetObjectItem(tj, "type");     if (v) track->type = (sq_track_type_t)clampi((int)v->valuedouble, 0, 2);
                    v = cJSON_GetObjectItem(tj, "length");   if (v) {
                        int tl = (int)v->valuedouble;
                        if (tl < 1 || tl > SQ_MAX_STEPS) {
                            LOG_WARN("track.length %d clamped to [1, %d]", tl, SQ_MAX_STEPS);
                        }
                        track->length = (uint32_t)clampi(tl, 1, SQ_MAX_STEPS);
                    }
                    v = cJSON_GetObjectItem(tj, "sample_index"); if (v) {
                        int si_val = (int)v->valuedouble;
                        if (si_val < -1 || si_val >= SQ_MAX_SAMPLES) {
                            LOG_WARN("sample_index %d clamped to [-1, %d]", si_val, SQ_MAX_SAMPLES - 1);
                        }
                        track->sample_index = clampi(si_val, -1, SQ_MAX_SAMPLES - 1);
                    }
                    v = cJSON_GetObjectItem(tj, "synth_preset"); if (v) {
                        int sp_val = (int)v->valuedouble;
                        if (sp_val < -1 || sp_val >= SQ_MAX_SYNTH_PRESETS) {
                            LOG_WARN("synth_preset %d clamped to [-1, %d]", sp_val, SQ_MAX_SYNTH_PRESETS - 1);
                        }
                        track->synth_preset = clampi(sp_val, -1, SQ_MAX_SYNTH_PRESETS - 1);
                    }
                    v = cJSON_GetObjectItem(tj, "volume");   if (v) {
                        float tv = (float)v->valuedouble;
                        if (tv < 0.0f || tv > 1.0f) {
                            LOG_WARN("track volume %.3f clamped to [0, 1]", tv);
                        }
                        track->volume = clampf(tv, 0.0f, 1.0f);
                    }
                    v = cJSON_GetObjectItem(tj, "pan");      if (v) {
                        float tp = (float)v->valuedouble;
                        if (tp < -1.0f || tp > 1.0f) {
                            LOG_WARN("track pan %.3f clamped to [-1, 1]", tp);
                        }
                        track->pan = clampf(tp, -1.0f, 1.0f);
                    }
                    v = cJSON_GetObjectItem(tj, "mute");     if (v) track->mute = cJSON_IsTrue(v);
                    v = cJSON_GetObjectItem(tj, "solo");     if (v) track->solo = cJSON_IsTrue(v);
                    v = cJSON_GetObjectItem(tj, "humanize"); if (v) {
                        float h = (float)v->valuedouble;
                        if (h < 0.0f || h > 1.0f) {
                            LOG_WARN("humanize %.3f clamped to [0, 1]", h);
                        }
                        track->humanize = clampf(h, 0.0f, 1.0f);
                    }
                    v = cJSON_GetObjectItem(tj, "color_index"); if (v) track->color_index = (uint8_t)clampi((int)v->valuedouble, 0, 7);
                    v = cJSON_GetObjectItem(tj, "choke_group"); if (v) track->choke_group = (uint8_t)clampi((int)v->valuedouble, 0, 8);
                    v = cJSON_GetObjectItem(tj, "timing_humanize"); if (v) track->timing_humanize = clampf((float)v->valuedouble, 0.0f, 1.0f);
                    v = cJSON_GetObjectItem(tj, "sample_start"); if (v) track->sample_start = (uint32_t)v->valuedouble;
                    v = cJSON_GetObjectItem(tj, "sample_end"); if (v) track->sample_end = (uint32_t)v->valuedouble;
                    v = cJSON_GetObjectItem(tj, "sample_reverse"); if (v) track->sample_reverse = cJSON_IsTrue(v);
                    v = cJSON_GetObjectItem(tj, "sf2_preset"); if (v) track->sf2_preset = clampi((int)v->valuedouble, -1, SQ_MAX_SF2_PRESETS - 1);

                    /* Steps (sparse) */
                    cJSON *steps = cJSON_GetObjectItem(tj, "steps");
                    if (steps) {
                        cJSON *step_item = NULL;
                        cJSON_ArrayForEach(step_item, steps) {
                            int si = atoi(step_item->string);
                            if (si >= 0 && (uint32_t)si < SQ_MAX_STEPS) {
                                sq_step_t *s = &track->steps[si];
                                v = cJSON_GetObjectItem(step_item, "vel");   if (v) {
                                    int vel = (int)v->valuedouble;
                                    if (vel < 0 || vel > 127) {
                                        LOG_WARN("velocity %d clamped to [0, 127]", vel);
                                    }
                                    s->velocity = (uint8_t)clampi(vel, 0, 127);
                                }
                                v = cJSON_GetObjectItem(step_item, "pitch"); if (v) s->pitch_offset = (int8_t)clampi((int)v->valuedouble, -24, 24);
                                v = cJSON_GetObjectItem(step_item, "note");  if (v) s->note = (uint8_t)clampi((int)v->valuedouble, 0, 127);
                                v = cJSON_GetObjectItem(step_item, "len");   if (v) s->length = clampf((float)v->valuedouble, 0.0f, 64.0f);
                                v = cJSON_GetObjectItem(step_item, "prob");  if (v) s->probability = (uint8_t)clampi((int)v->valuedouble, 0, 100);
                                v = cJSON_GetObjectItem(step_item, "retrig"); if (v) s->retrigger = (uint8_t)clampi((int)v->valuedouble, 0, 4);
                                v = cJSON_GetObjectItem(step_item, "utime"); if (v) s->micro_offset = clampf((float)v->valuedouble, -0.5f, 0.5f);
                                for (int p = 0; p < 4; p++) {
                                    char pk[4]; snprintf(pk, sizeof(pk), "p%d", p);
                                    v = cJSON_GetObjectItem(step_item, pk);
                                    if (v) s->param[p] = (float)v->valuedouble;
                                }
                            }
                        }
                    }

                    /* Track effects */
                    cJSON *effects = cJSON_GetObjectItem(tj, "effects");
                    if (effects && cJSON_IsArray(effects)) {
                        int en = cJSON_GetArraySize(effects);
                        for (int e = 0; e < en && e < MAX_TRACK_EFFECTS; e++) {
                            json_to_effect_slot(cJSON_GetArrayItem(effects, e),
                                                &track->effects[e], sample_rate);
                        }
                    }
                }
            }

            engine->num_patterns++;
        }
    }

    /* Arrangement */
    cJSON *arr = cJSON_GetObjectItem(root, "arrangement");
    if (arr) {
        cJSON *ns = cJSON_GetObjectItem(arr, "num_sections");
        if (ns) {
            int nsv = (int)ns->valuedouble;
            if (nsv < 0 || nsv > SQ_MAX_SECTIONS) {
                LOG_WARN("num_sections %d clamped to [0, %d]", nsv, SQ_MAX_SECTIONS);
                nsv = clampi(nsv, 0, SQ_MAX_SECTIONS);
            }
            engine->arrangement.num_sections = (uint32_t)nsv;
        }

        cJSON *sections = cJSON_GetObjectItem(arr, "sections");
        if (sections && cJSON_IsArray(sections)) {
            int n = cJSON_GetArraySize(sections);
            for (int s = 0; s < n && (uint32_t)s < SQ_MAX_SECTIONS; s++) {
                cJSON *sj = cJSON_GetArrayItem(sections, s);
                cJSON *pi = cJSON_GetObjectItem(sj, "pattern_index");
                cJSON *rc = cJSON_GetObjectItem(sj, "repeat_count");
                if (pi) engine->arrangement.sections[s].pattern_index = clampi((int)pi->valuedouble, 0, SQ_MAX_PATTERNS - 1);
                if (rc) engine->arrangement.sections[s].repeat_count = clampi((int)rc->valuedouble, 1, 99);
            }
        }
    }

    /* Master effects */
    cJSON *mfx = cJSON_GetObjectItem(root, "master_effects");
    if (mfx && cJSON_IsArray(mfx)) {
        int n = cJSON_GetArraySize(mfx);
        for (int i = 0; i < n && i < MAX_TRACK_EFFECTS; i++) {
            json_to_effect_slot(cJSON_GetArrayItem(mfx, i),
                                &engine->master_effects[i], sample_rate);
        }
    }

    /* SF2 SoundFont */
    cJSON *sf2p = cJSON_GetObjectItem(root, "sf2_path");
    if (sf2p && sf2p->valuestring && sf2p->valuestring[0]) {
        if (!path_is_safe(sf2p->valuestring)) {
            LOG_WARN("Rejected unsafe SF2 path: %s", sf2p->valuestring);
        } else {
            /* Try to load SF2 — non-fatal if it fails */
            extern int sf2_load(sq_engine_t *e, const char *path);
            sf2_load(engine, sf2p->valuestring);
        }
    }

    cJSON_Delete(root);
    LOG_INFO("Project loaded: %s (samples=%d, presets=%d, patterns=%d)",
             filepath, engine->num_samples, engine->num_synth_presets,
             engine->num_patterns);
    /* Log first pattern tracks for debugging */
    if (engine->num_patterns > 0) {
        sq_pattern_t *p0 = &engine->patterns[0];
        for (uint32_t t = 0; t < p0->num_tracks && t < 4; t++) {
            int active = 0;
            for (uint32_t s = 0; s < p0->tracks[t].length; s++)
                if (p0->tracks[t].steps[s].velocity > 0) active++;
            LOG_INFO("  Track %d: type=%d sample=%d preset=%d len=%d active_steps=%d",
                     t, p0->tracks[t].type, p0->tracks[t].sample_index,
                     p0->tracks[t].synth_preset, p0->tracks[t].length, active);
        }
    }
    return 0;
}
