/*
 * synth.c — Subtractive synthesizer engine.
 *
 * Signal flow per voice:
 *   Oscillator 1 ──┐
 *                   ├── Mix ──→ Biquad Filter ──→ Amp Envelope ──→ Pan ──→ Output
 *   Oscillator 2 ──┘       ↑                  ↑                ↑
 *                     Filter Env          Amp Env           Velocity
 *                           ↑
 *                          LFO (optional modulation)
 *
 * Oscillators use pre-computed wavetables (2048 samples per cycle).
 * A phase accumulator (0.0 to 1.0) indexes into the table.
 * Phase increment per sample = frequency / sample_rate.
 *
 * The biquad filter is a standard state-variable design supporting
 * lowpass, highpass, and bandpass modes.
 *
 * Unison detuning creates multiple slightly-detuned copies of the
 * oscillators and spreads them across the stereo field.
 */

#include "engine/synth.h"
#include "engine/envelope.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Max unison voices per synth voice */
#define MAX_UNISON 7

/* ─── Wavetable generation ───────────────────────────────────────────────── */

void synth_init_wavetables(sq_wavetables_t *wt)
{
    for (int i = 0; i < SQ_WAVETABLE_SIZE; i++) {
        double phase = (double)i / (double)SQ_WAVETABLE_SIZE;

        /* Sine — pure tone */
        wt->tables[WAVE_SINE][i] = sinf((float)(phase * 2.0 * M_PI));

        /* Saw — bright, buzzy (naive, bandlimited version later) */
        wt->tables[WAVE_SAW][i] = (float)(2.0 * phase - 1.0);

        /* Square — hollow, clarinet-like */
        wt->tables[WAVE_SQUARE][i] = (phase < 0.5) ? 1.0f : -1.0f;

        /* Triangle — soft, flute-like */
        if (phase < 0.25)
            wt->tables[WAVE_TRIANGLE][i] = (float)(4.0 * phase);
        else if (phase < 0.75)
            wt->tables[WAVE_TRIANGLE][i] = (float)(2.0 - 4.0 * phase);
        else
            wt->tables[WAVE_TRIANGLE][i] = (float)(4.0 * phase - 4.0);
    }
    wt->initialized = true;
}

/* ─── Default presets ────────────────────────────────────────────────────── */

void synth_init_presets(sq_engine_t *engine)
{
    /* Preset 0: "Bass" — saw + lowpass, short decay */
    {
        sq_synth_preset_t *p = &engine->synth_presets[0];
        memset(p, 0, sizeof(*p));
        snprintf(p->name, sizeof(p->name), "%s", "Bass");
        p->osc1_wave = WAVE_SAW;
        p->osc2_wave = WAVE_SAW;
        p->osc_mix = 0.0f;  /* osc1 only */
        p->osc2_detune = 0.0f;
        p->unison_voices = 1;
        p->unison_detune = 0.0f;
        p->filter_type = FILTER_LOWPASS;
        p->filter_cutoff = 800.0f;
        p->filter_resonance = 2.0f;
        p->filter_env_depth = 2000.0f;
        p->amp_env = (sq_adsr_params_t){0.005f, 0.3f, 0.0f, 0.05f};
        p->filter_env = (sq_adsr_params_t){0.005f, 0.2f, 0.0f, 0.05f};
        p->lfo = (sq_lfo_t){WAVE_SINE, 0.0f, 0.0f, LFO_DEST_NONE, 0.0};
    }

    /* Preset 1: "Lead" — square + saw, filter sweep */
    {
        sq_synth_preset_t *p = &engine->synth_presets[1];
        memset(p, 0, sizeof(*p));
        snprintf(p->name, sizeof(p->name), "%s", "Lead");
        p->osc1_wave = WAVE_SQUARE;
        p->osc2_wave = WAVE_SAW;
        p->osc_mix = 0.4f;
        p->osc2_detune = 0.1f;  /* slight detune for thickness */
        p->unison_voices = 1;
        p->filter_type = FILTER_LOWPASS;
        p->filter_cutoff = 2000.0f;
        p->filter_resonance = 3.0f;
        p->filter_env_depth = 4000.0f;
        p->amp_env = (sq_adsr_params_t){0.01f, 0.1f, 0.7f, 0.3f};
        p->filter_env = (sq_adsr_params_t){0.01f, 0.3f, 0.3f, 0.2f};
        p->lfo = (sq_lfo_t){WAVE_SINE, 5.0f, 0.1f, LFO_DEST_PITCH, 0.0};
    }

    /* Preset 2: "Pad" — triangle + sine, slow attack, long release, unison */
    {
        sq_synth_preset_t *p = &engine->synth_presets[2];
        memset(p, 0, sizeof(*p));
        snprintf(p->name, sizeof(p->name), "%s", "Pad");
        p->osc1_wave = WAVE_TRIANGLE;
        p->osc2_wave = WAVE_SINE;
        p->osc_mix = 0.5f;
        p->osc2_detune = 0.05f;
        p->unison_voices = 3;
        p->unison_detune = 15.0f;  /* 15 cents spread */
        p->filter_type = FILTER_LOWPASS;
        p->filter_cutoff = 3000.0f;
        p->filter_resonance = 1.0f;
        p->filter_env_depth = 1000.0f;
        p->amp_env = (sq_adsr_params_t){0.5f, 0.5f, 0.8f, 1.0f};
        p->filter_env = (sq_adsr_params_t){0.3f, 0.5f, 0.5f, 0.8f};
        p->lfo = (sq_lfo_t){WAVE_TRIANGLE, 0.3f, 0.2f, LFO_DEST_FILTER, 0.0};
    }

    /* Preset 3: "Pluck" — saw, fast attack, no sustain */
    {
        sq_synth_preset_t *p = &engine->synth_presets[3];
        memset(p, 0, sizeof(*p));
        snprintf(p->name, sizeof(p->name), "%s", "Pluck");
        p->osc1_wave = WAVE_SAW;
        p->osc2_wave = WAVE_SQUARE;
        p->osc_mix = 0.2f;
        p->unison_voices = 1;
        p->filter_type = FILTER_LOWPASS;
        p->filter_cutoff = 1000.0f;
        p->filter_resonance = 4.0f;
        p->filter_env_depth = 6000.0f;
        p->amp_env = (sq_adsr_params_t){0.001f, 0.15f, 0.0f, 0.01f};
        p->filter_env = (sq_adsr_params_t){0.001f, 0.1f, 0.0f, 0.01f};
        p->lfo = (sq_lfo_t){WAVE_SINE, 0.0f, 0.0f, LFO_DEST_NONE, 0.0};
    }

    /* Preset 4: "SuperSaw" — wide unison saw for testing */
    {
        sq_synth_preset_t *p = &engine->synth_presets[4];
        memset(p, 0, sizeof(*p));
        snprintf(p->name, sizeof(p->name), "%s", "SuperSaw");
        p->osc1_wave = WAVE_SAW;
        p->osc2_wave = WAVE_SAW;
        p->osc_mix = 0.5f;
        p->osc2_detune = 7.0f;  /* 7 semitones = fifth */
        p->unison_voices = 5;
        p->unison_detune = 20.0f;  /* 20 cents */
        p->filter_type = FILTER_LOWPASS;
        p->filter_cutoff = 4000.0f;
        p->filter_resonance = 1.5f;
        p->filter_env_depth = 3000.0f;
        p->amp_env = (sq_adsr_params_t){0.01f, 0.2f, 0.6f, 0.5f};
        p->filter_env = (sq_adsr_params_t){0.01f, 0.4f, 0.3f, 0.3f};
        p->lfo = (sq_lfo_t){WAVE_SINE, 0.0f, 0.0f, LFO_DEST_NONE, 0.0};
    }

    /* ── FM Presets ─────────────────────────────────────────────────────── */

    /* Preset 5: "FM Bell" — classic DX7 bell sound */
    {
        sq_synth_preset_t *p = &engine->synth_presets[5];
        memset(p, 0, sizeof(*p));
        snprintf(p->name, sizeof(p->name), "%s", "FM Bell");
        p->synth_mode = SYNTH_FM;
        p->fm_algorithm = 0; /* serial: 3→2→1→0 */
        p->amp_env = (sq_adsr_params_t){0.001f, 2.0f, 0.0f, 1.0f};
        /* Op 0 (carrier): fundamental */
        p->fm_ops[0] = (sq_fm_operator_t){1.0f, 1.0f, 0.0f,
            {0.001f, 2.0f, 0.0f, 1.0f}};
        /* Op 1 (modulator): inharmonic ratio for bell timbre */
        p->fm_ops[1] = (sq_fm_operator_t){3.5f, 0.8f, 0.0f,
            {0.001f, 1.5f, 0.0f, 0.5f}};
        /* Op 2: adds brightness */
        p->fm_ops[2] = (sq_fm_operator_t){7.0f, 0.4f, 0.0f,
            {0.001f, 0.8f, 0.0f, 0.3f}};
        /* Op 3: high harmonic shimmer */
        p->fm_ops[3] = (sq_fm_operator_t){11.0f, 0.2f, 0.0f,
            {0.001f, 0.5f, 0.0f, 0.2f}};
    }

    /* Preset 6: "FM EPiano" — Rhodes-like electric piano */
    {
        sq_synth_preset_t *p = &engine->synth_presets[6];
        memset(p, 0, sizeof(*p));
        snprintf(p->name, sizeof(p->name), "%s", "FM EPiano");
        p->synth_mode = SYNTH_FM;
        p->fm_algorithm = 2; /* two pairs: 3→2, 1→0 */
        p->amp_env = (sq_adsr_params_t){0.001f, 1.0f, 0.3f, 0.5f};
        /* Op 0 (carrier): fundamental */
        p->fm_ops[0] = (sq_fm_operator_t){1.0f, 1.0f, 0.0f,
            {0.001f, 1.0f, 0.3f, 0.5f}};
        /* Op 1 (modulator): harmonic */
        p->fm_ops[1] = (sq_fm_operator_t){1.0f, 0.5f, 0.0f,
            {0.001f, 0.8f, 0.1f, 0.3f}};
        /* Op 2 (carrier): octave up for brightness */
        p->fm_ops[2] = (sq_fm_operator_t){2.0f, 0.6f, 0.0f,
            {0.001f, 0.7f, 0.0f, 0.4f}};
        /* Op 3 (modulator): adds tine sound */
        p->fm_ops[3] = (sq_fm_operator_t){14.0f, 0.3f, 0.0f,
            {0.001f, 0.3f, 0.0f, 0.1f}};
    }

    /* Preset 7: "FM Metal" — metallic percussion hit */
    {
        sq_synth_preset_t *p = &engine->synth_presets[7];
        memset(p, 0, sizeof(*p));
        snprintf(p->name, sizeof(p->name), "%s", "FM Metal");
        p->synth_mode = SYNTH_FM;
        p->fm_algorithm = 1; /* 2→1→0, 3→0 */
        p->amp_env = (sq_adsr_params_t){0.001f, 0.3f, 0.0f, 0.1f};
        p->fm_ops[0] = (sq_fm_operator_t){1.0f, 1.0f, 0.0f,
            {0.001f, 0.3f, 0.0f, 0.1f}};
        p->fm_ops[1] = (sq_fm_operator_t){1.41f, 0.9f, 0.0f,
            {0.001f, 0.2f, 0.0f, 0.05f}};
        p->fm_ops[2] = (sq_fm_operator_t){2.82f, 0.6f, 0.0f,
            {0.001f, 0.15f, 0.0f, 0.05f}};
        p->fm_ops[3] = (sq_fm_operator_t){5.0f, 0.7f, 0.1f,
            {0.001f, 0.1f, 0.0f, 0.05f}};
    }

    /* Preset 8: "FM Bass" — deep FM bass */
    {
        sq_synth_preset_t *p = &engine->synth_presets[8];
        memset(p, 0, sizeof(*p));
        snprintf(p->name, sizeof(p->name), "%s", "FM Bass");
        p->synth_mode = SYNTH_FM;
        p->fm_algorithm = 0; /* serial */
        p->amp_env = (sq_adsr_params_t){0.001f, 0.4f, 0.0f, 0.05f};
        p->fm_ops[0] = (sq_fm_operator_t){1.0f, 1.0f, 0.0f,
            {0.001f, 0.4f, 0.0f, 0.05f}};
        p->fm_ops[1] = (sq_fm_operator_t){1.0f, 0.8f, 0.0f,
            {0.001f, 0.15f, 0.0f, 0.02f}};
        p->fm_ops[2] = (sq_fm_operator_t){2.0f, 0.3f, 0.0f,
            {0.001f, 0.1f, 0.0f, 0.01f}};
        p->fm_ops[3] = (sq_fm_operator_t){3.0f, 0.2f, 0.0f,
            {0.001f, 0.05f, 0.0f, 0.01f}};
    }

    /* Preset 9: "FM Pad" — evolving FM pad */
    {
        sq_synth_preset_t *p = &engine->synth_presets[9];
        memset(p, 0, sizeof(*p));
        snprintf(p->name, sizeof(p->name), "%s", "FM Pad");
        p->synth_mode = SYNTH_FM;
        p->fm_algorithm = 6; /* all carriers (additive) */
        p->amp_env = (sq_adsr_params_t){0.5f, 0.5f, 0.8f, 1.5f};
        p->fm_ops[0] = (sq_fm_operator_t){1.0f, 1.0f, 0.0f,
            {0.5f, 0.5f, 0.8f, 1.5f}};
        p->fm_ops[1] = (sq_fm_operator_t){2.0f, 0.5f, 0.0f,
            {0.8f, 0.5f, 0.6f, 1.0f}};
        p->fm_ops[2] = (sq_fm_operator_t){3.0f, 0.3f, 0.0f,
            {1.0f, 0.5f, 0.4f, 0.8f}};
        p->fm_ops[3] = (sq_fm_operator_t){5.0f, 0.15f, 0.05f,
            {0.3f, 1.0f, 0.2f, 2.0f}};
    }

    /* ── Wavetable Presets ────────────────────────────────────────────── */

    /* Preset 10: "WT Sweep" — analog bank with position sweep */
    {
        sq_synth_preset_t *p = &engine->synth_presets[10];
        memset(p, 0, sizeof(*p));
        snprintf(p->name, sizeof(p->name), "%s", "WT Sweep");
        p->synth_mode = SYNTH_WAVETABLE;
        p->wt_bank_index = 0; /* Analog */
        p->wt_position = 0.0f;
        p->wt_env_depth = 0.5f;
        p->wt_lfo_depth = 0.0f;
        p->amp_env = (sq_adsr_params_t){0.01f, 0.3f, 0.6f, 0.5f};
        p->filter_env = (sq_adsr_params_t){0.01f, 0.5f, 0.3f, 0.3f};
        p->lfo = (sq_lfo_t){WAVE_SINE, 0.0f, 0.0f, LFO_DEST_NONE, 0.0};
    }

    /* Preset 11: "WT Harmonic" — harmonics bank with LFO */
    {
        sq_synth_preset_t *p = &engine->synth_presets[11];
        memset(p, 0, sizeof(*p));
        snprintf(p->name, sizeof(p->name), "%s", "WT Harmonic");
        p->synth_mode = SYNTH_WAVETABLE;
        p->wt_bank_index = 1; /* Harmonics */
        p->wt_position = 0.3f;
        p->wt_env_depth = 0.0f;
        p->wt_lfo_depth = 0.3f;
        p->amp_env = (sq_adsr_params_t){0.5f, 0.5f, 0.8f, 1.0f};
        p->filter_env = (sq_adsr_params_t){0.001f, 0.5f, 0.5f, 0.5f};
        p->lfo = (sq_lfo_t){WAVE_TRIANGLE, 0.5f, 1.0f, LFO_DEST_FILTER, 0.0};
    }

    /* Preset 12: "WT PWM" — PWM bank */
    {
        sq_synth_preset_t *p = &engine->synth_presets[12];
        memset(p, 0, sizeof(*p));
        snprintf(p->name, sizeof(p->name), "%s", "WT PWM");
        p->synth_mode = SYNTH_WAVETABLE;
        p->wt_bank_index = 2; /* PWM */
        p->wt_position = 0.0f;
        p->wt_env_depth = 0.0f;
        p->wt_lfo_depth = 0.5f;
        p->amp_env = (sq_adsr_params_t){0.01f, 0.2f, 0.7f, 0.3f};
        p->filter_env = (sq_adsr_params_t){0.001f, 0.5f, 0.5f, 0.5f};
        p->lfo = (sq_lfo_t){WAVE_TRIANGLE, 2.0f, 1.0f, LFO_DEST_FILTER, 0.0};
    }

    /* Preset 13: "WT Vocal" — formant bank */
    {
        sq_synth_preset_t *p = &engine->synth_presets[13];
        memset(p, 0, sizeof(*p));
        snprintf(p->name, sizeof(p->name), "%s", "WT Vocal");
        p->synth_mode = SYNTH_WAVETABLE;
        p->wt_bank_index = 3; /* Formant */
        p->wt_position = 0.0f;
        p->wt_env_depth = 0.8f;
        p->wt_lfo_depth = 0.0f;
        p->amp_env = (sq_adsr_params_t){0.3f, 0.5f, 0.7f, 0.8f};
        p->filter_env = (sq_adsr_params_t){0.2f, 1.0f, 0.3f, 0.5f};
        p->lfo = (sq_lfo_t){WAVE_SINE, 0.0f, 0.0f, LFO_DEST_NONE, 0.0};
    }

    /* ── Classic / Historic Synth Presets ────────────────────────────── */

    /* Preset 14: "Moog Bass" — Minimoog-style thick bass */
    {
        sq_synth_preset_t *p = &engine->synth_presets[14];
        memset(p, 0, sizeof(*p));
        snprintf(p->name, sizeof(p->name), "%s", "Moog Bass");
        p->synth_mode = SYNTH_SUBTRACTIVE;
        p->osc1_wave = WAVE_SAW;
        p->osc2_wave = WAVE_SQUARE;
        p->osc_mix = 0.3f;
        p->osc2_detune = -12.0f; /* one octave down */
        p->unison_voices = 1;
        p->filter_type = FILTER_LOWPASS;
        p->filter_cutoff = 400.0f;
        p->filter_resonance = 4.0f;
        p->filter_env_depth = 3000.0f;
        p->amp_env = (sq_adsr_params_t){0.002f, 0.15f, 0.0f, 0.05f};
        p->filter_env = (sq_adsr_params_t){0.002f, 0.12f, 0.0f, 0.04f};
        p->lfo = (sq_lfo_t){WAVE_SINE, 0.0f, 0.0f, LFO_DEST_NONE, 0.0};
    }

    /* Preset 15: "303 Acid" — TB-303-style acid squelch */
    {
        sq_synth_preset_t *p = &engine->synth_presets[15];
        memset(p, 0, sizeof(*p));
        snprintf(p->name, sizeof(p->name), "%s", "303 Acid");
        p->synth_mode = SYNTH_SUBTRACTIVE;
        p->osc1_wave = WAVE_SAW;
        p->osc2_wave = WAVE_SAW;
        p->osc_mix = 0.0f; /* single osc */
        p->unison_voices = 1;
        p->filter_type = FILTER_LOWPASS;
        p->filter_cutoff = 300.0f;
        p->filter_resonance = 12.0f; /* high reso for squelch */
        p->filter_env_depth = 8000.0f;
        p->amp_env = (sq_adsr_params_t){0.001f, 0.2f, 0.0f, 0.02f};
        p->filter_env = (sq_adsr_params_t){0.001f, 0.08f, 0.0f, 0.02f};
        p->lfo = (sq_lfo_t){WAVE_SINE, 0.0f, 0.0f, LFO_DEST_NONE, 0.0};
    }

    /* Preset 16: "Juno Pad" — Roland Juno-style warm chorus pad */
    {
        sq_synth_preset_t *p = &engine->synth_presets[16];
        memset(p, 0, sizeof(*p));
        snprintf(p->name, sizeof(p->name), "%s", "Juno Pad");
        p->synth_mode = SYNTH_SUBTRACTIVE;
        p->osc1_wave = WAVE_SAW;
        p->osc2_wave = WAVE_SQUARE;
        p->osc_mix = 0.4f;
        p->osc2_detune = 0.08f; /* subtle detune */
        p->unison_voices = 5;
        p->unison_detune = 12.0f; /* chorus-like spread */
        p->filter_type = FILTER_LOWPASS;
        p->filter_cutoff = 2500.0f;
        p->filter_resonance = 1.5f;
        p->filter_env_depth = 500.0f;
        p->amp_env = (sq_adsr_params_t){0.3f, 0.3f, 0.9f, 1.5f};
        p->filter_env = (sq_adsr_params_t){0.4f, 0.5f, 0.6f, 1.0f};
        p->lfo = (sq_lfo_t){WAVE_TRIANGLE, 0.2f, 0.15f, LFO_DEST_FILTER, 0.0};
    }

    /* Preset 17: "OB Strings" — Oberheim OB-X style string ensemble */
    {
        sq_synth_preset_t *p = &engine->synth_presets[17];
        memset(p, 0, sizeof(*p));
        snprintf(p->name, sizeof(p->name), "%s", "OB Strings");
        p->synth_mode = SYNTH_SUBTRACTIVE;
        p->osc1_wave = WAVE_SAW;
        p->osc2_wave = WAVE_SAW;
        p->osc_mix = 0.5f;
        p->osc2_detune = 0.12f;
        p->unison_voices = 7; /* maximum lush */
        p->unison_detune = 18.0f;
        p->filter_type = FILTER_LOWPASS;
        p->filter_cutoff = 3500.0f;
        p->filter_resonance = 1.0f;
        p->filter_env_depth = 800.0f;
        p->amp_env = (sq_adsr_params_t){0.8f, 0.5f, 0.85f, 1.2f};
        p->filter_env = (sq_adsr_params_t){0.5f, 0.8f, 0.5f, 1.0f};
        p->lfo = (sq_lfo_t){WAVE_SINE, 4.5f, 0.05f, LFO_DEST_PITCH, 0.0};
    }

    /* Preset 18: "Prophet Brass" — Sequential Circuits-style brass stab */
    {
        sq_synth_preset_t *p = &engine->synth_presets[18];
        memset(p, 0, sizeof(*p));
        snprintf(p->name, sizeof(p->name), "%s", "Prophet Brass");
        p->synth_mode = SYNTH_SUBTRACTIVE;
        p->osc1_wave = WAVE_SAW;
        p->osc2_wave = WAVE_SAW;
        p->osc_mix = 0.5f;
        p->osc2_detune = 0.15f;
        p->unison_voices = 3;
        p->unison_detune = 8.0f;
        p->filter_type = FILTER_LOWPASS;
        p->filter_cutoff = 800.0f;
        p->filter_resonance = 2.0f;
        p->filter_env_depth = 6000.0f;
        p->amp_env = (sq_adsr_params_t){0.01f, 0.15f, 0.7f, 0.2f};
        p->filter_env = (sq_adsr_params_t){0.01f, 0.25f, 0.4f, 0.15f};
        p->lfo = (sq_lfo_t){WAVE_SINE, 0.0f, 0.0f, LFO_DEST_NONE, 0.0};
    }

    /* Preset 19: "ARP Lead" — ARP 2600-style piercing lead */
    {
        sq_synth_preset_t *p = &engine->synth_presets[19];
        memset(p, 0, sizeof(*p));
        snprintf(p->name, sizeof(p->name), "%s", "ARP Lead");
        p->synth_mode = SYNTH_SUBTRACTIVE;
        p->osc1_wave = WAVE_SQUARE;
        p->osc2_wave = WAVE_SAW;
        p->osc_mix = 0.5f;
        p->osc2_detune = 7.0f; /* fifth */
        p->unison_voices = 1;
        p->filter_type = FILTER_LOWPASS;
        p->filter_cutoff = 1500.0f;
        p->filter_resonance = 5.0f;
        p->filter_env_depth = 5000.0f;
        p->amp_env = (sq_adsr_params_t){0.005f, 0.1f, 0.8f, 0.1f};
        p->filter_env = (sq_adsr_params_t){0.005f, 0.15f, 0.5f, 0.1f};
        p->lfo = (sq_lfo_t){WAVE_SINE, 6.0f, 0.08f, LFO_DEST_PITCH, 0.0};
    }

    /* Preset 20: "SH Noise" — SH-101-style noise percussion */
    {
        sq_synth_preset_t *p = &engine->synth_presets[20];
        memset(p, 0, sizeof(*p));
        snprintf(p->name, sizeof(p->name), "%s", "SH Noise");
        p->synth_mode = SYNTH_SUBTRACTIVE;
        p->osc1_wave = WAVE_SAW; /* will sound noisy with high-res filter */
        p->osc2_wave = WAVE_SQUARE;
        p->osc_mix = 0.5f;
        p->osc2_detune = 0.5f; /* slightly detuned for texture */
        p->unison_voices = 3;
        p->unison_detune = 40.0f; /* heavy detune = noise-like */
        p->filter_type = FILTER_HIGHPASS;
        p->filter_cutoff = 2000.0f;
        p->filter_resonance = 6.0f;
        p->filter_env_depth = -1500.0f;
        p->amp_env = (sq_adsr_params_t){0.001f, 0.1f, 0.0f, 0.02f};
        p->filter_env = (sq_adsr_params_t){0.001f, 0.08f, 0.0f, 0.02f};
        p->lfo = (sq_lfo_t){WAVE_SINE, 0.0f, 0.0f, LFO_DEST_NONE, 0.0};
    }

    /* Preset 21: "Reese Bass" — DnB-style detuned reese bass */
    {
        sq_synth_preset_t *p = &engine->synth_presets[21];
        memset(p, 0, sizeof(*p));
        snprintf(p->name, sizeof(p->name), "%s", "Reese Bass");
        p->synth_mode = SYNTH_SUBTRACTIVE;
        p->osc1_wave = WAVE_SAW;
        p->osc2_wave = WAVE_SAW;
        p->osc_mix = 0.5f;
        p->osc2_detune = 0.1f;
        p->unison_voices = 3;
        p->unison_detune = 10.0f;
        p->filter_type = FILTER_LOWPASS;
        p->filter_cutoff = 600.0f;
        p->filter_resonance = 3.0f;
        p->filter_env_depth = 2000.0f;
        p->amp_env = (sq_adsr_params_t){0.01f, 0.5f, 0.6f, 0.3f};
        p->filter_env = (sq_adsr_params_t){0.01f, 0.8f, 0.2f, 0.3f};
        p->lfo = (sq_lfo_t){WAVE_SINE, 0.3f, 0.15f, LFO_DEST_FILTER, 0.0};
    }

    /* Preset 22: "DX Piano" — FM electric piano (brighter than EPiano) */
    {
        sq_synth_preset_t *p = &engine->synth_presets[22];
        memset(p, 0, sizeof(*p));
        snprintf(p->name, sizeof(p->name), "%s", "DX Piano");
        p->synth_mode = SYNTH_FM;
        p->fm_algorithm = 2; /* two pairs */
        p->amp_env = (sq_adsr_params_t){0.001f, 1.5f, 0.2f, 0.8f};
        p->fm_ops[0] = (sq_fm_operator_t){1.0f, 1.0f, 0.0f,
            {0.001f, 1.5f, 0.2f, 0.8f}};
        p->fm_ops[1] = (sq_fm_operator_t){1.0f, 0.7f, 0.0f,
            {0.001f, 0.5f, 0.0f, 0.2f}};
        p->fm_ops[2] = (sq_fm_operator_t){3.0f, 0.5f, 0.0f,
            {0.001f, 0.8f, 0.0f, 0.3f}};
        p->fm_ops[3] = (sq_fm_operator_t){7.0f, 0.25f, 0.0f,
            {0.001f, 0.2f, 0.0f, 0.1f}};
    }

    /* Preset 23: "DX Vibes" — FM vibraphone */
    {
        sq_synth_preset_t *p = &engine->synth_presets[23];
        memset(p, 0, sizeof(*p));
        snprintf(p->name, sizeof(p->name), "%s", "DX Vibes");
        p->synth_mode = SYNTH_FM;
        p->fm_algorithm = 0; /* serial */
        p->amp_env = (sq_adsr_params_t){0.001f, 3.0f, 0.0f, 1.5f};
        p->fm_ops[0] = (sq_fm_operator_t){1.0f, 1.0f, 0.0f,
            {0.001f, 3.0f, 0.0f, 1.5f}};
        p->fm_ops[1] = (sq_fm_operator_t){4.0f, 0.5f, 0.0f,
            {0.001f, 1.0f, 0.0f, 0.5f}};
        p->fm_ops[2] = (sq_fm_operator_t){10.0f, 0.15f, 0.0f,
            {0.001f, 0.3f, 0.0f, 0.1f}};
        p->fm_ops[3] = (sq_fm_operator_t){1.0f, 0.1f, 0.0f,
            {0.001f, 0.5f, 0.0f, 0.3f}};
    }

    /* Preset 24: "Hoover" — classic rave hoover sound */
    {
        sq_synth_preset_t *p = &engine->synth_presets[24];
        memset(p, 0, sizeof(*p));
        snprintf(p->name, sizeof(p->name), "%s", "Hoover");
        p->synth_mode = SYNTH_SUBTRACTIVE;
        p->osc1_wave = WAVE_SAW;
        p->osc2_wave = WAVE_SAW;
        p->osc_mix = 0.5f;
        p->osc2_detune = 5.0f; /* major third up */
        p->unison_voices = 5;
        p->unison_detune = 25.0f;
        p->filter_type = FILTER_LOWPASS;
        p->filter_cutoff = 2000.0f;
        p->filter_resonance = 2.5f;
        p->filter_env_depth = 4000.0f;
        p->amp_env = (sq_adsr_params_t){0.01f, 0.3f, 0.7f, 0.5f};
        p->filter_env = (sq_adsr_params_t){0.01f, 0.5f, 0.4f, 0.4f};
        p->lfo = (sq_lfo_t){WAVE_SINE, 3.0f, 0.2f, LFO_DEST_PITCH, 0.0};
    }

    /* Preset 25: "Stab" — classic house/techno chord stab */
    {
        sq_synth_preset_t *p = &engine->synth_presets[25];
        memset(p, 0, sizeof(*p));
        snprintf(p->name, sizeof(p->name), "%s", "Stab");
        p->synth_mode = SYNTH_SUBTRACTIVE;
        p->osc1_wave = WAVE_SAW;
        p->osc2_wave = WAVE_SQUARE;
        p->osc_mix = 0.4f;
        p->osc2_detune = 0.0f;
        p->unison_voices = 3;
        p->unison_detune = 6.0f;
        p->filter_type = FILTER_BANDPASS;
        p->filter_cutoff = 1200.0f;
        p->filter_resonance = 5.0f;
        p->filter_env_depth = 3000.0f;
        p->amp_env = (sq_adsr_params_t){0.001f, 0.08f, 0.0f, 0.03f};
        p->filter_env = (sq_adsr_params_t){0.001f, 0.06f, 0.0f, 0.02f};
        p->lfo = (sq_lfo_t){WAVE_SINE, 0.0f, 0.0f, LFO_DEST_NONE, 0.0};
    }

    /* ── More Classic Synths ─────────────────────────────────────────── */

    /* Preset 26: "CS-80 Brass" — Yamaha CS-80 polysynth brass (Blade Runner) */
    {
        sq_synth_preset_t *p = &engine->synth_presets[26];
        memset(p, 0, sizeof(*p));
        snprintf(p->name, sizeof(p->name), "%s", "CS-80 Brass");
        p->synth_mode = SYNTH_SUBTRACTIVE;
        p->osc1_wave = WAVE_SAW;
        p->osc2_wave = WAVE_SQUARE;
        p->osc_mix = 0.45f;
        p->osc2_detune = 0.06f;
        p->unison_voices = 5;
        p->unison_detune = 14.0f;
        p->filter_type = FILTER_LOWPASS;
        p->filter_cutoff = 1200.0f;
        p->filter_resonance = 2.5f;
        p->filter_env_depth = 5000.0f;
        p->amp_env = (sq_adsr_params_t){0.02f, 0.2f, 0.65f, 0.4f};
        p->filter_env = (sq_adsr_params_t){0.015f, 0.3f, 0.35f, 0.3f};
        p->lfo = (sq_lfo_t){WAVE_SINE, 5.5f, 0.06f, LFO_DEST_PITCH, 0.0};
    }

    /* Preset 27: "MS-20 Growl" — Korg MS-20 aggressive bass/lead */
    {
        sq_synth_preset_t *p = &engine->synth_presets[27];
        memset(p, 0, sizeof(*p));
        snprintf(p->name, sizeof(p->name), "%s", "MS-20 Growl");
        p->synth_mode = SYNTH_SUBTRACTIVE;
        p->osc1_wave = WAVE_SAW;
        p->osc2_wave = WAVE_SQUARE;
        p->osc_mix = 0.6f;
        p->osc2_detune = -0.05f;
        p->unison_voices = 1;
        p->filter_type = FILTER_HIGHPASS;
        p->filter_cutoff = 200.0f;
        p->filter_resonance = 15.0f;  /* near self-oscillation */
        p->filter_env_depth = 3000.0f;
        p->amp_env = (sq_adsr_params_t){0.005f, 0.3f, 0.6f, 0.15f};
        p->filter_env = (sq_adsr_params_t){0.002f, 0.2f, 0.1f, 0.1f};
        p->lfo = (sq_lfo_t){WAVE_SQUARE, 8.0f, 0.05f, LFO_DEST_FILTER, 0.0};
    }

    /* Preset 28: "Poly-6 Keys" — Korg Polysix classic poly keys */
    {
        sq_synth_preset_t *p = &engine->synth_presets[28];
        memset(p, 0, sizeof(*p));
        snprintf(p->name, sizeof(p->name), "%s", "Poly-6 Keys");
        p->synth_mode = SYNTH_SUBTRACTIVE;
        p->osc1_wave = WAVE_SAW;
        p->osc2_wave = WAVE_SAW;
        p->osc_mix = 0.0f;  /* single oscillator */
        p->unison_voices = 3;
        p->unison_detune = 8.0f;
        p->filter_type = FILTER_LOWPASS;
        p->filter_cutoff = 3000.0f;
        p->filter_resonance = 2.0f;
        p->filter_env_depth = 1500.0f;
        p->amp_env = (sq_adsr_params_t){0.005f, 0.4f, 0.5f, 0.4f};
        p->filter_env = (sq_adsr_params_t){0.005f, 0.35f, 0.3f, 0.3f};
        p->lfo = (sq_lfo_t){WAVE_TRIANGLE, 0.3f, 0.1f, LFO_DEST_FILTER, 0.0};
    }

    /* Preset 29: "JP-8 Pad" — Roland Jupiter-8 lush evolving pad */
    {
        sq_synth_preset_t *p = &engine->synth_presets[29];
        memset(p, 0, sizeof(*p));
        snprintf(p->name, sizeof(p->name), "%s", "JP-8 Pad");
        p->synth_mode = SYNTH_SUBTRACTIVE;
        p->osc1_wave = WAVE_SAW;
        p->osc2_wave = WAVE_TRIANGLE;
        p->osc_mix = 0.35f;
        p->osc2_detune = 0.07f;
        p->unison_voices = 7;
        p->unison_detune = 16.0f;
        p->filter_type = FILTER_LOWPASS;
        p->filter_cutoff = 1800.0f;
        p->filter_resonance = 1.2f;
        p->filter_env_depth = 1200.0f;
        p->amp_env = (sq_adsr_params_t){1.0f, 0.5f, 0.9f, 2.0f};
        p->filter_env = (sq_adsr_params_t){0.8f, 1.0f, 0.5f, 1.5f};
        p->lfo = (sq_lfo_t){WAVE_SINE, 0.15f, 0.2f, LFO_DEST_FILTER, 0.0};
    }

    /* Preset 30: "Sub 37" — Moog Sub 37 mono lead */
    {
        sq_synth_preset_t *p = &engine->synth_presets[30];
        memset(p, 0, sizeof(*p));
        snprintf(p->name, sizeof(p->name), "%s", "Sub 37");
        p->synth_mode = SYNTH_SUBTRACTIVE;
        p->osc1_wave = WAVE_SQUARE;
        p->osc2_wave = WAVE_SAW;
        p->osc_mix = 0.4f;
        p->osc2_detune = -12.0f;  /* sub oscillator one octave down */
        p->unison_voices = 1;
        p->filter_type = FILTER_LOWPASS;
        p->filter_cutoff = 1000.0f;
        p->filter_resonance = 3.5f;
        p->filter_env_depth = 4500.0f;
        p->amp_env = (sq_adsr_params_t){0.003f, 0.2f, 0.7f, 0.15f};
        p->filter_env = (sq_adsr_params_t){0.003f, 0.18f, 0.4f, 0.12f};
        p->lfo = (sq_lfo_t){WAVE_TRIANGLE, 5.0f, 0.04f, LFO_DEST_PITCH, 0.0};
    }

    /* ── FM Additions ─────────────────────────────────────────────── */

    /* Preset 31: "DX Organ" — FM Hammond-style organ */
    {
        sq_synth_preset_t *p = &engine->synth_presets[31];
        memset(p, 0, sizeof(*p));
        snprintf(p->name, sizeof(p->name), "%s", "DX Organ");
        p->synth_mode = SYNTH_FM;
        p->fm_algorithm = 6;  /* all carriers (additive) */
        p->amp_env = (sq_adsr_params_t){0.001f, 0.01f, 1.0f, 0.05f};
        p->fm_ops[0] = (sq_fm_operator_t){1.0f, 1.0f, 0.0f,
            {0.001f, 0.01f, 1.0f, 0.05f}};
        p->fm_ops[1] = (sq_fm_operator_t){2.0f, 0.7f, 0.0f,
            {0.001f, 0.01f, 1.0f, 0.05f}};
        p->fm_ops[2] = (sq_fm_operator_t){3.0f, 0.4f, 0.0f,
            {0.001f, 0.01f, 1.0f, 0.05f}};
        p->fm_ops[3] = (sq_fm_operator_t){4.0f, 0.2f, 0.0f,
            {0.001f, 0.01f, 0.8f, 0.05f}};
    }

    /* Preset 32: "DX Marimba" — FM marimba/xylophone */
    {
        sq_synth_preset_t *p = &engine->synth_presets[32];
        memset(p, 0, sizeof(*p));
        snprintf(p->name, sizeof(p->name), "%s", "DX Marimba");
        p->synth_mode = SYNTH_FM;
        p->fm_algorithm = 0;  /* serial */
        p->amp_env = (sq_adsr_params_t){0.001f, 0.6f, 0.0f, 0.3f};
        p->fm_ops[0] = (sq_fm_operator_t){1.0f, 1.0f, 0.0f,
            {0.001f, 0.6f, 0.0f, 0.3f}};
        p->fm_ops[1] = (sq_fm_operator_t){4.0f, 0.6f, 0.0f,
            {0.001f, 0.15f, 0.0f, 0.05f}};
        p->fm_ops[2] = (sq_fm_operator_t){10.0f, 0.2f, 0.0f,
            {0.001f, 0.05f, 0.0f, 0.02f}};
        p->fm_ops[3] = (sq_fm_operator_t){1.0f, 0.0f, 0.0f,
            {0.001f, 0.01f, 0.0f, 0.01f}};
    }

    /* Preset 33: "DX Clav" — FM clavinet */
    {
        sq_synth_preset_t *p = &engine->synth_presets[33];
        memset(p, 0, sizeof(*p));
        snprintf(p->name, sizeof(p->name), "%s", "DX Clav");
        p->synth_mode = SYNTH_FM;
        p->fm_algorithm = 2;  /* two pairs */
        p->amp_env = (sq_adsr_params_t){0.001f, 0.3f, 0.0f, 0.1f};
        p->fm_ops[0] = (sq_fm_operator_t){1.0f, 1.0f, 0.0f,
            {0.001f, 0.3f, 0.0f, 0.1f}};
        p->fm_ops[1] = (sq_fm_operator_t){3.0f, 0.8f, 0.0f,
            {0.001f, 0.05f, 0.0f, 0.02f}};
        p->fm_ops[2] = (sq_fm_operator_t){2.0f, 0.6f, 0.0f,
            {0.001f, 0.2f, 0.0f, 0.08f}};
        p->fm_ops[3] = (sq_fm_operator_t){6.0f, 0.5f, 0.0f,
            {0.001f, 0.03f, 0.0f, 0.01f}};
    }

    /* Preset 34: "DX Strings" — FM string ensemble */
    {
        sq_synth_preset_t *p = &engine->synth_presets[34];
        memset(p, 0, sizeof(*p));
        snprintf(p->name, sizeof(p->name), "%s", "DX Strings");
        p->synth_mode = SYNTH_FM;
        p->fm_algorithm = 5;  /* 3→2, 1, 0 */
        p->amp_env = (sq_adsr_params_t){0.6f, 0.4f, 0.85f, 1.0f};
        p->fm_ops[0] = (sq_fm_operator_t){1.0f, 1.0f, 0.0f,
            {0.6f, 0.4f, 0.85f, 1.0f}};
        p->fm_ops[1] = (sq_fm_operator_t){2.0f, 0.6f, 0.0f,
            {0.8f, 0.5f, 0.7f, 0.8f}};
        p->fm_ops[2] = (sq_fm_operator_t){3.0f, 0.4f, 0.0f,
            {0.5f, 0.6f, 0.5f, 0.6f}};
        p->fm_ops[3] = (sq_fm_operator_t){1.0f, 0.3f, 0.05f,
            {0.4f, 0.3f, 0.6f, 0.5f}};
    }

    /* Preset 35: "DX Koto" — FM plucked string */
    {
        sq_synth_preset_t *p = &engine->synth_presets[35];
        memset(p, 0, sizeof(*p));
        snprintf(p->name, sizeof(p->name), "%s", "DX Koto");
        p->synth_mode = SYNTH_FM;
        p->fm_algorithm = 1;  /* 2→1→0, 3→0 */
        p->amp_env = (sq_adsr_params_t){0.001f, 1.0f, 0.0f, 0.5f};
        p->fm_ops[0] = (sq_fm_operator_t){1.0f, 1.0f, 0.0f,
            {0.001f, 1.0f, 0.0f, 0.5f}};
        p->fm_ops[1] = (sq_fm_operator_t){1.0f, 0.9f, 0.0f,
            {0.001f, 0.3f, 0.0f, 0.1f}};
        p->fm_ops[2] = (sq_fm_operator_t){5.0f, 0.4f, 0.0f,
            {0.001f, 0.1f, 0.0f, 0.05f}};
        p->fm_ops[3] = (sq_fm_operator_t){8.0f, 0.3f, 0.0f,
            {0.001f, 0.05f, 0.0f, 0.02f}};
    }

    /* Preset 36: "DX Flute" — FM breathy flute */
    {
        sq_synth_preset_t *p = &engine->synth_presets[36];
        memset(p, 0, sizeof(*p));
        snprintf(p->name, sizeof(p->name), "%s", "DX Flute");
        p->synth_mode = SYNTH_FM;
        p->fm_algorithm = 3;  /* 3→2→1, 0 */
        p->amp_env = (sq_adsr_params_t){0.05f, 0.1f, 0.8f, 0.3f};
        p->fm_ops[0] = (sq_fm_operator_t){1.0f, 1.0f, 0.0f,
            {0.05f, 0.1f, 0.8f, 0.3f}};
        p->fm_ops[1] = (sq_fm_operator_t){1.0f, 0.15f, 0.0f,
            {0.05f, 0.2f, 0.1f, 0.2f}};
        p->fm_ops[2] = (sq_fm_operator_t){2.0f, 0.1f, 0.0f,
            {0.1f, 0.3f, 0.05f, 0.1f}};
        p->fm_ops[3] = (sq_fm_operator_t){3.0f, 0.05f, 0.15f,
            {0.001f, 0.01f, 0.8f, 0.1f}};
    }

    /* Preset 37: "DX Harm" — FM harmonica/accordion */
    {
        sq_synth_preset_t *p = &engine->synth_presets[37];
        memset(p, 0, sizeof(*p));
        snprintf(p->name, sizeof(p->name), "%s", "DX Harm");
        p->synth_mode = SYNTH_FM;
        p->fm_algorithm = 4;  /* 3,2,1→0 */
        p->amp_env = (sq_adsr_params_t){0.02f, 0.15f, 0.75f, 0.2f};
        p->fm_ops[0] = (sq_fm_operator_t){1.0f, 1.0f, 0.0f,
            {0.02f, 0.15f, 0.75f, 0.2f}};
        p->fm_ops[1] = (sq_fm_operator_t){2.0f, 0.4f, 0.0f,
            {0.02f, 0.2f, 0.3f, 0.15f}};
        p->fm_ops[2] = (sq_fm_operator_t){1.0f, 0.3f, 0.0f,
            {0.001f, 0.01f, 0.9f, 0.1f}};
        p->fm_ops[3] = (sq_fm_operator_t){3.0f, 0.5f, 0.0f,
            {0.01f, 0.01f, 0.85f, 0.1f}};
    }

    /* ── Wavetable Additions ──────────────────────────────────────── */

    /* Preset 38: "WT Glass" — harmonics bank with fast env sweep */
    {
        sq_synth_preset_t *p = &engine->synth_presets[38];
        memset(p, 0, sizeof(*p));
        snprintf(p->name, sizeof(p->name), "%s", "WT Glass");
        p->synth_mode = SYNTH_WAVETABLE;
        p->wt_bank_index = 1;  /* Harmonics */
        p->wt_position = 0.8f;
        p->wt_env_depth = -0.7f;
        p->wt_lfo_depth = 0.0f;
        p->amp_env = (sq_adsr_params_t){0.001f, 1.5f, 0.0f, 0.8f};
        p->filter_env = (sq_adsr_params_t){0.001f, 0.8f, 0.0f, 0.3f};
        p->lfo = (sq_lfo_t){WAVE_SINE, 0.0f, 0.0f, LFO_DEST_NONE, 0.0};
    }

    /* Preset 39: "WT Wobble" — analog bank with LFO wobble */
    {
        sq_synth_preset_t *p = &engine->synth_presets[39];
        memset(p, 0, sizeof(*p));
        snprintf(p->name, sizeof(p->name), "%s", "WT Wobble");
        p->synth_mode = SYNTH_WAVETABLE;
        p->wt_bank_index = 0;  /* Analog */
        p->wt_position = 0.2f;
        p->wt_env_depth = 0.0f;
        p->wt_lfo_depth = 0.8f;
        p->amp_env = (sq_adsr_params_t){0.01f, 0.3f, 0.7f, 0.3f};
        p->filter_env = (sq_adsr_params_t){0.001f, 0.5f, 0.5f, 0.5f};
        p->lfo = (sq_lfo_t){WAVE_SINE, 3.0f, 1.0f, LFO_DEST_FILTER, 0.0};
    }

    /* ── More Subtractive Classics ────────────────────────────────── */

    /* Preset 40: "Sync Lead" — hard sync-like aggressive lead */
    {
        sq_synth_preset_t *p = &engine->synth_presets[40];
        memset(p, 0, sizeof(*p));
        snprintf(p->name, sizeof(p->name), "%s", "Sync Lead");
        p->synth_mode = SYNTH_SUBTRACTIVE;
        p->osc1_wave = WAVE_SAW;
        p->osc2_wave = WAVE_SAW;
        p->osc_mix = 0.5f;
        p->osc2_detune = 19.0f; /* octave + fifth */
        p->unison_voices = 1;
        p->filter_type = FILTER_LOWPASS;
        p->filter_cutoff = 2000.0f;
        p->filter_resonance = 4.0f;
        p->filter_env_depth = 5000.0f;
        p->amp_env = (sq_adsr_params_t){0.003f, 0.15f, 0.7f, 0.15f};
        p->filter_env = (sq_adsr_params_t){0.003f, 0.2f, 0.3f, 0.1f};
        p->lfo = (sq_lfo_t){WAVE_SINE, 0.0f, 0.0f, LFO_DEST_NONE, 0.0};
    }

    /* Preset 41: "Whistle" — pure sine lead with vibrato */
    {
        sq_synth_preset_t *p = &engine->synth_presets[41];
        memset(p, 0, sizeof(*p));
        snprintf(p->name, sizeof(p->name), "%s", "Whistle");
        p->synth_mode = SYNTH_SUBTRACTIVE;
        p->osc1_wave = WAVE_SINE;
        p->osc2_wave = WAVE_SINE;
        p->osc_mix = 0.0f;
        p->unison_voices = 1;
        p->filter_type = FILTER_LOWPASS;
        p->filter_cutoff = 20000.0f;  /* wide open */
        p->filter_resonance = 0.7f;
        p->filter_env_depth = 0.0f;
        p->amp_env = (sq_adsr_params_t){0.05f, 0.1f, 0.9f, 0.2f};
        p->filter_env = (sq_adsr_params_t){0.001f, 0.01f, 1.0f, 0.01f};
        p->lfo = (sq_lfo_t){WAVE_SINE, 5.5f, 0.12f, LFO_DEST_PITCH, 0.0};
    }

    /* Preset 42: "Tape Strings" — Mellotron-like slow strings */
    {
        sq_synth_preset_t *p = &engine->synth_presets[42];
        memset(p, 0, sizeof(*p));
        snprintf(p->name, sizeof(p->name), "%s", "Tape Strings");
        p->synth_mode = SYNTH_SUBTRACTIVE;
        p->osc1_wave = WAVE_SAW;
        p->osc2_wave = WAVE_TRIANGLE;
        p->osc_mix = 0.5f;
        p->osc2_detune = 0.03f;
        p->unison_voices = 5;
        p->unison_detune = 22.0f;
        p->filter_type = FILTER_LOWPASS;
        p->filter_cutoff = 2200.0f;
        p->filter_resonance = 0.8f;
        p->filter_env_depth = 400.0f;
        p->amp_env = (sq_adsr_params_t){0.4f, 0.3f, 0.7f, 0.8f};
        p->filter_env = (sq_adsr_params_t){0.3f, 0.4f, 0.4f, 0.5f};
        p->lfo = (sq_lfo_t){WAVE_SINE, 4.0f, 0.03f, LFO_DEST_PITCH, 0.0};
    }

    /* Preset 43: "Detuned Saw" — wide saw stack for EDM */
    {
        sq_synth_preset_t *p = &engine->synth_presets[43];
        memset(p, 0, sizeof(*p));
        snprintf(p->name, sizeof(p->name), "%s", "Detuned Saw");
        p->synth_mode = SYNTH_SUBTRACTIVE;
        p->osc1_wave = WAVE_SAW;
        p->osc2_wave = WAVE_SAW;
        p->osc_mix = 0.5f;
        p->osc2_detune = 0.2f;
        p->unison_voices = 7;
        p->unison_detune = 30.0f;
        p->filter_type = FILTER_LOWPASS;
        p->filter_cutoff = 5000.0f;
        p->filter_resonance = 1.0f;
        p->filter_env_depth = 2000.0f;
        p->amp_env = (sq_adsr_params_t){0.01f, 0.2f, 0.8f, 0.4f};
        p->filter_env = (sq_adsr_params_t){0.01f, 0.3f, 0.5f, 0.3f};
        p->lfo = (sq_lfo_t){WAVE_SINE, 0.0f, 0.0f, LFO_DEST_NONE, 0.0};
    }

    /* Preset 44: "Rezzy Pluck" — high-resonance filter pluck */
    {
        sq_synth_preset_t *p = &engine->synth_presets[44];
        memset(p, 0, sizeof(*p));
        snprintf(p->name, sizeof(p->name), "%s", "Rezzy Pluck");
        p->synth_mode = SYNTH_SUBTRACTIVE;
        p->osc1_wave = WAVE_SAW;
        p->osc2_wave = WAVE_SAW;
        p->osc_mix = 0.0f;
        p->unison_voices = 1;
        p->filter_type = FILTER_LOWPASS;
        p->filter_cutoff = 400.0f;
        p->filter_resonance = 10.0f;
        p->filter_env_depth = 7000.0f;
        p->amp_env = (sq_adsr_params_t){0.001f, 0.2f, 0.0f, 0.02f};
        p->filter_env = (sq_adsr_params_t){0.001f, 0.12f, 0.0f, 0.01f};
        p->lfo = (sq_lfo_t){WAVE_SINE, 0.0f, 0.0f, LFO_DEST_NONE, 0.0};
    }

    /* Preset 45: "Soft Keys" — warm electric piano feel */
    {
        sq_synth_preset_t *p = &engine->synth_presets[45];
        memset(p, 0, sizeof(*p));
        snprintf(p->name, sizeof(p->name), "%s", "Soft Keys");
        p->synth_mode = SYNTH_SUBTRACTIVE;
        p->osc1_wave = WAVE_TRIANGLE;
        p->osc2_wave = WAVE_SINE;
        p->osc_mix = 0.4f;
        p->osc2_detune = 0.0f;
        p->unison_voices = 3;
        p->unison_detune = 5.0f;
        p->filter_type = FILTER_LOWPASS;
        p->filter_cutoff = 4000.0f;
        p->filter_resonance = 1.0f;
        p->filter_env_depth = 1000.0f;
        p->amp_env = (sq_adsr_params_t){0.01f, 0.5f, 0.4f, 0.5f};
        p->filter_env = (sq_adsr_params_t){0.01f, 0.4f, 0.2f, 0.3f};
        p->lfo = (sq_lfo_t){WAVE_SINE, 4.0f, 0.02f, LFO_DEST_AMP, 0.0};
    }

    /* Preset 46: "Sub Bass" — deep sub-bass sine */
    {
        sq_synth_preset_t *p = &engine->synth_presets[46];
        memset(p, 0, sizeof(*p));
        snprintf(p->name, sizeof(p->name), "%s", "Sub Bass");
        p->synth_mode = SYNTH_SUBTRACTIVE;
        p->osc1_wave = WAVE_SINE;
        p->osc2_wave = WAVE_SINE;
        p->osc_mix = 0.0f;
        p->unison_voices = 1;
        p->filter_type = FILTER_LOWPASS;
        p->filter_cutoff = 200.0f;
        p->filter_resonance = 1.0f;
        p->filter_env_depth = 200.0f;
        p->amp_env = (sq_adsr_params_t){0.01f, 0.3f, 0.8f, 0.2f};
        p->filter_env = (sq_adsr_params_t){0.01f, 0.2f, 0.5f, 0.1f};
        p->lfo = (sq_lfo_t){WAVE_SINE, 0.0f, 0.0f, LFO_DEST_NONE, 0.0};
    }

    /* Preset 47: "Reso Sweep" — filter resonance sweep pad */
    {
        sq_synth_preset_t *p = &engine->synth_presets[47];
        memset(p, 0, sizeof(*p));
        snprintf(p->name, sizeof(p->name), "%s", "Reso Sweep");
        p->synth_mode = SYNTH_SUBTRACTIVE;
        p->osc1_wave = WAVE_SAW;
        p->osc2_wave = WAVE_SAW;
        p->osc_mix = 0.5f;
        p->osc2_detune = 0.08f;
        p->unison_voices = 3;
        p->unison_detune = 10.0f;
        p->filter_type = FILTER_LOWPASS;
        p->filter_cutoff = 200.0f;
        p->filter_resonance = 8.0f;
        p->filter_env_depth = 6000.0f;
        p->amp_env = (sq_adsr_params_t){0.5f, 0.5f, 0.8f, 1.0f};
        p->filter_env = (sq_adsr_params_t){2.0f, 1.0f, 0.3f, 1.5f};
        p->lfo = (sq_lfo_t){WAVE_SINE, 0.0f, 0.0f, LFO_DEST_NONE, 0.0};
    }

    /* Preset 48: "DX Sitar" — FM sitar/twang */
    {
        sq_synth_preset_t *p = &engine->synth_presets[48];
        memset(p, 0, sizeof(*p));
        snprintf(p->name, sizeof(p->name), "%s", "DX Sitar");
        p->synth_mode = SYNTH_FM;
        p->fm_algorithm = 0;  /* serial chain */
        p->amp_env = (sq_adsr_params_t){0.001f, 1.5f, 0.0f, 0.8f};
        p->fm_ops[0] = (sq_fm_operator_t){1.0f, 1.0f, 0.0f,
            {0.001f, 1.5f, 0.0f, 0.8f}};
        p->fm_ops[1] = (sq_fm_operator_t){1.01f, 1.0f, 0.0f,
            {0.001f, 0.8f, 0.0f, 0.4f}};
        p->fm_ops[2] = (sq_fm_operator_t){5.0f, 0.5f, 0.0f,
            {0.001f, 0.3f, 0.0f, 0.15f}};
        p->fm_ops[3] = (sq_fm_operator_t){9.0f, 0.3f, 0.1f,
            {0.001f, 0.15f, 0.0f, 0.08f}};
    }

    /* Preset 49: "WT Choir" — formant bank with slow LFO */
    {
        sq_synth_preset_t *p = &engine->synth_presets[49];
        memset(p, 0, sizeof(*p));
        snprintf(p->name, sizeof(p->name), "%s", "WT Choir");
        p->synth_mode = SYNTH_WAVETABLE;
        p->wt_bank_index = 3;  /* Formant */
        p->wt_position = 0.2f;
        p->wt_env_depth = 0.0f;
        p->wt_lfo_depth = 0.4f;
        p->amp_env = (sq_adsr_params_t){0.8f, 0.5f, 0.85f, 1.5f};
        p->filter_env = (sq_adsr_params_t){0.001f, 0.5f, 0.5f, 0.5f};
        p->lfo = (sq_lfo_t){WAVE_SINE, 0.2f, 1.0f, LFO_DEST_FILTER, 0.0};
    }

    engine->num_synth_presets = 50;
}

/* ─── Wavetable oscillator ───────────────────────────────────────────────── */

/* Read a sample from a wavetable with linear interpolation */
static float wavetable_read(const float *table, double phase)
{
    double pos = phase * SQ_WAVETABLE_SIZE;
    int idx = (int)pos;
    float frac = (float)(pos - idx);
    int next = (idx + 1) % SQ_WAVETABLE_SIZE;
    return table[idx] * (1.0f - frac) + table[next] * frac;
}

/* ─── Biquad filter ──────────────────────────────────────────────────────── */

typedef struct {
    float a1, a2, a3, k;
} filter_coeffs_t;

static void filter_calc_coeffs(filter_coeffs_t *c, float cutoff_hz,
                                float resonance, uint32_t sample_rate)
{
    if (cutoff_hz < 20.0f) cutoff_hz = 20.0f;
    float max_co = (float)sample_rate * 0.45f;
    if (cutoff_hz > max_co) cutoff_hz = max_co;

    float g = tanf((float)M_PI * cutoff_hz / (float)sample_rate);
    c->k = 1.0f / resonance;
    c->a1 = 1.0f / (1.0f + g * (g + c->k));
    c->a2 = g * c->a1;
    c->a3 = g * c->a2;
}

static inline void filter_apply(sq_filter_t *f, const filter_coeffs_t *c,
                                float *left, float *right)
{
    for (int ch = 0; ch < 2; ch++) {
        float input = (ch == 0) ? *left : *right;
        float v3 = input - f->z2[ch];
        float v1 = c->a1 * f->z1[ch] + c->a2 * v3;
        float v2 = f->z2[ch] + c->a2 * f->z1[ch] + c->a3 * v3;
        f->z1[ch] = 2.0f * v1 - f->z1[ch];
        f->z2[ch] = 2.0f * v2 - f->z2[ch];

        /* Clamp state to prevent numerical blowup at high resonance */
        if (f->z1[ch] > 4.0f) f->z1[ch] = 4.0f;
        else if (f->z1[ch] < -4.0f) f->z1[ch] = -4.0f;
        if (f->z2[ch] > 4.0f) f->z2[ch] = 4.0f;
        else if (f->z2[ch] < -4.0f) f->z2[ch] = -4.0f;

        float out;
        switch (f->type) {
        case FILTER_LOWPASS:  out = v2; break;
        case FILTER_HIGHPASS: out = input - c->k * v1 - v2; break;
        case FILTER_BANDPASS: out = v1; break;
        default:              out = v2; break;
        }
        if (ch == 0) *left = out; else *right = out;
    }
}

/* ─── FM Algorithm definitions ───────────────────────────────────────────── */

/*
 * Each algorithm specifies:
 *   - mod_sources[op][j]: which operators modulate operator 'op'
 *                         (-1 = no source, terminated by -1)
 *   - is_carrier[op]:     true if operator 'op' outputs to audio
 *
 * Operators numbered 0-3 (1-4 in DX7 parlance).
 * Convention: higher-numbered operators modulate lower-numbered ones.
 */
typedef struct {
    int  mod_sources[FM_NUM_OPERATORS][FM_NUM_OPERATORS]; /* -1 terminated */
    bool is_carrier[FM_NUM_OPERATORS];
} fm_algorithm_t;

static const fm_algorithm_t fm_algorithms[FM_NUM_ALGORITHMS] = {
    /* 0: Serial chain: 3→2→1→0  (carrier: 0) */
    {{{1, -1}, {2, -1}, {3, -1}, {-1}},
     {true, false, false, false}},

    /* 1: 2→1→0, 3→0  (carrier: 0) */
    {{{1, 3, -1}, {2, -1}, {-1}, {-1}},
     {true, false, false, false}},

    /* 2: Two pairs: 3→2, 1→0  (carriers: 0, 2) */
    {{{1, -1}, {-1}, {3, -1}, {-1}},
     {true, false, true, false}},

    /* 3: 3→2→1, 0 free  (carriers: 0, 1) */
    {{{-1}, {2, -1}, {3, -1}, {-1}},
     {true, true, false, false}},

    /* 4: 3, 2, 1→0  (carriers: 0, 2, 3) */
    {{{1, -1}, {-1}, {-1}, {-1}},
     {true, false, true, true}},

    /* 5: 3→2, 1, 0  (carriers: 0, 1, 2) */
    {{{-1}, {-1}, {3, -1}, {-1}},
     {true, true, true, false}},

    /* 6: All carriers (additive): 3, 2, 1, 0 */
    {{{-1}, {-1}, {-1}, {-1}},
     {true, true, true, true}},

    /* 7: 3→(1,2), 0  (carrier: 0, 1, 2) */
    {{{-1}, {3, -1}, {3, -1}, {-1}},
     {true, true, true, false}},
};

/* ─── Parameter smoothing ────────────────────────────────────────────────── */

/* One-pole lowpass smoother: prevents clicks when knob values change.
 * coeff controls smoothing speed: higher = faster tracking.
 * At 44100 Hz, coeff=0.01 gives ~7ms smoothing time. */
static inline float smooth_param(float current, float target, float coeff)
{
    return current + coeff * (target - current);
}

/* ─── Voice management ───────────────────────────────────────────────────── */

void synth_trigger(sq_engine_t *engine, int preset_index,
                   float velocity, int pitch_offset,
                   float volume, float pan, uint8_t note)
{
    if (preset_index < 0 || (uint32_t)preset_index >= engine->num_synth_presets)
        return;

    /* Find a free voice, or steal the oldest */
    int vi = -1;
    uint64_t oldest_time = UINT64_MAX;
    int oldest_idx = 0;

    for (int i = 0; i < SQ_MAX_SYNTH_VOICES; i++) {
        if (!engine->synth_voices[i].active) {
            vi = i;
            break;
        }
        if (engine->synth_voices[i].start_time < oldest_time) {
            oldest_time = engine->synth_voices[i].start_time;
            oldest_idx = i;
        }
    }
    if (vi < 0) vi = oldest_idx;

    sq_synth_voice_t *v = &engine->synth_voices[vi];
    sq_synth_preset_t *p = &engine->synth_presets[preset_index];

    /* MIDI note to frequency: A4 (note 69) = 440 Hz
     * freq = 440 * 2^((note - 69 + pitch_offset) / 12) */
    float midi_note = (note > 0) ? (float)note : 60.0f;  /* default C4 */
    midi_note += (float)pitch_offset;
    v->frequency = 440.0f * powf(2.0f, (midi_note - 69.0f) / 12.0f);

    v->active = true;
    v->preset_index = preset_index;
    v->osc1_phase = 0.0;
    v->osc2_phase = 0.0;
    v->velocity = velocity;
    v->volume = volume;
    v->pan = pan;
    v->start_time = engine->transport.sample_position;

    /* Initialize smoothed filter cutoff to preset value */
    v->smoothed_cutoff = p->filter_cutoff;

    /* Initialize unison phases — spread starting phases for width */
    int uv = p->unison_voices;
    if (uv < 1) uv = 1;
    if (uv > MAX_UNISON) uv = MAX_UNISON;
    for (int u = 0; u < uv; u++) {
        v->unison_phases[u] = (double)u / (double)uv; /* spread phases */
    }

    /* Initialize envelopes */
    envelope_trigger(&v->amp_env, &p->amp_env, engine->sample_rate);
    envelope_trigger(&v->filter_env, &p->filter_env, engine->sample_rate);

    /* Initialize filter state */
    v->filter.type = p->filter_type;
    v->filter.cutoff = p->filter_cutoff;
    v->filter.resonance = p->filter_resonance;
    memset(v->filter.z1, 0, sizeof(v->filter.z1));
    memset(v->filter.z2, 0, sizeof(v->filter.z2));

    /* Initialize LFO */
    v->lfo = p->lfo;
    v->lfo.phase = 0.0;

    /* Initialize wavetable state */
    if (p->synth_mode == SYNTH_WAVETABLE) {
        v->wt_phase = 0.0;
        v->wt_smoothed_pos = p->wt_position;
    }

    /* Initialize FM operator state */
    if (p->synth_mode == SYNTH_FM) {
        for (int op = 0; op < FM_NUM_OPERATORS; op++) {
            v->fm_phase[op] = 0.0;
            v->fm_feedback_state[op] = 0.0f;
            envelope_trigger(&v->fm_env[op], &p->fm_ops[op].env,
                             engine->sample_rate);
        }
    }
}

/* ─── LFO BPM sync ──────────────────────────────────────────────────────── */

/* Convert a BPM-synced division to Hz.
 * division: 0=1/1, 1=1/2, 2=1/4, 3=1/8, 4=1/16, 5=1/32
 * Returns rate in Hz for the given BPM. */
static float lfo_bpm_sync_rate(double bpm, int division)
{
    /* beats per second */
    double bps = bpm / 60.0;
    /* divisions: each step halves the period */
    static const double div_mult[] = {
        0.25,   /* 1/1 = whole note = 4 beats -> 0.25 * bps */
        0.5,    /* 1/2 = half note = 2 beats */
        1.0,    /* 1/4 = quarter note = 1 beat */
        2.0,    /* 1/8 */
        4.0,    /* 1/16 */
        8.0     /* 1/32 */
    };
    if (division < 0) division = 0;
    if (division > 5) division = 5;
    return (float)(bps * div_mult[division]);
}

/* ─── FM render for one voice ────────────────────────────────────────────── */

static void fm_render_voice(sq_engine_t *engine, sq_synth_voice_t *v,
                            float *output, uint32_t num_frames)
{
    sq_synth_preset_t *p = &engine->synth_presets[v->preset_index];
    int alg_idx = p->fm_algorithm;
    if (alg_idx < 0 || alg_idx >= FM_NUM_ALGORITHMS) alg_idx = 0;
    const fm_algorithm_t *alg = &fm_algorithms[alg_idx];

    float sr = (float)engine->sample_rate;
    float base_freq = v->frequency;
    float pan_l = (1.0f - v->pan) * 0.5f;
    float pan_r = (1.0f + v->pan) * 0.5f;
    float base_gain = v->velocity * v->volume;

    /* Pre-compute operator frequency increments */
    double phase_inc[FM_NUM_OPERATORS];
    for (int op = 0; op < FM_NUM_OPERATORS; op++) {
        phase_inc[op] = (double)(base_freq * p->fm_ops[op].freq_ratio) / (double)sr;
    }

    for (uint32_t i = 0; i < num_frames; i++) {
        /* Process amplitude envelope (shared for the voice) */
        float amp = envelope_process(&v->amp_env, &p->amp_env, engine->sample_rate);
        if (v->amp_env.stage == ENV_IDLE) {
            v->active = false;
            break;
        }

        /* Process per-operator envelopes */
        float op_env[FM_NUM_OPERATORS];
        for (int op = 0; op < FM_NUM_OPERATORS; op++) {
            op_env[op] = envelope_process(&v->fm_env[op], &p->fm_ops[op].env,
                                          engine->sample_rate);
        }

        /* Compute operators in reverse order (3→2→1→0)
         * so modulators are computed before carriers */
        float op_out[FM_NUM_OPERATORS];
        for (int op = FM_NUM_OPERATORS - 1; op >= 0; op--) {
            /* Sum modulation from source operators */
            float mod = 0.0f;
            for (int j = 0; j < FM_NUM_OPERATORS; j++) {
                int src = alg->mod_sources[op][j];
                if (src < 0) break;
                mod += op_out[src] * p->fm_ops[src].level * op_env[src];
            }

            /* Add self-feedback */
            if (p->fm_ops[op].feedback > 0.0f) {
                mod += v->fm_feedback_state[op] * p->fm_ops[op].feedback;
            }

            /* Compute this operator's output: sin(2π * phase + mod) */
            double phase = v->fm_phase[op] + (double)mod;
            op_out[op] = sinf((float)(phase * 2.0 * M_PI));

            /* Update feedback state */
            v->fm_feedback_state[op] = op_out[op];

            /* Advance phase */
            v->fm_phase[op] += phase_inc[op];
            if (v->fm_phase[op] >= 1.0) v->fm_phase[op] -= 1.0;
        }

        /* Sum carrier outputs */
        float sample = 0.0f;
        int num_carriers = 0;
        for (int op = 0; op < FM_NUM_OPERATORS; op++) {
            if (alg->is_carrier[op]) {
                sample += op_out[op] * p->fm_ops[op].level * op_env[op];
                num_carriers++;
            }
        }
        /* Normalize by number of carriers */
        if (num_carriers > 1) sample /= sqrtf((float)num_carriers);

        /* Apply voice envelope and gain */
        float gain = amp * base_gain;
        output[i * 2]     += sample * pan_l * gain;
        output[i * 2 + 1] += sample * pan_r * gain;
    }
}

/* ─── Wavetable bank generation ──────────────────────────────────────────── */

void synth_init_wt_banks(sq_engine_t *engine)
{
    if (!engine->wt_banks) return;

    /* Bank 0: "Analog" — morph saw → square → triangle → sine */
    {
        sq_wt_bank_t *b = &engine->wt_banks[0];
        snprintf(b->name, sizeof(b->name), "%s", "Analog");
        b->num_frames = 16;
        for (int f = 0; f < 16; f++) {
            float t = (float)f / 15.0f; /* 0.0 to 1.0 */
            for (int i = 0; i < SQ_WAVETABLE_SIZE; i++) {
                double phase = (double)i / (double)SQ_WAVETABLE_SIZE;
                float saw = (float)(2.0 * phase - 1.0);
                float square = (phase < 0.5) ? 1.0f : -1.0f;
                float tri;
                if (phase < 0.25) tri = (float)(4.0 * phase);
                else if (phase < 0.75) tri = (float)(2.0 - 4.0 * phase);
                else tri = (float)(4.0 * phase - 4.0);
                float sine = sinf((float)(phase * 2.0 * M_PI));

                /* Blend through stages */
                float val;
                if (t < 0.333f) {
                    float m = t / 0.333f;
                    val = saw * (1.0f - m) + square * m;
                } else if (t < 0.667f) {
                    float m = (t - 0.333f) / 0.334f;
                    val = square * (1.0f - m) + tri * m;
                } else {
                    float m = (t - 0.667f) / 0.333f;
                    val = tri * (1.0f - m) + sine * m;
                }
                b->frames[f][i] = val;
            }
        }
    }

    /* Bank 1: "Harmonics" — additive with increasing harmonic count */
    {
        sq_wt_bank_t *b = &engine->wt_banks[1];
        snprintf(b->name, sizeof(b->name), "%s", "Harmonics");
        b->num_frames = 16;
        for (int f = 0; f < 16; f++) {
            int num_harmonics = f + 1; /* 1 to 16 harmonics */
            for (int i = 0; i < SQ_WAVETABLE_SIZE; i++) {
                double phase = (double)i / (double)SQ_WAVETABLE_SIZE;
                float val = 0.0f;
                for (int h = 1; h <= num_harmonics; h++) {
                    val += sinf((float)(phase * 2.0 * M_PI * h)) / (float)h;
                }
                /* Normalize */
                b->frames[f][i] = val * 0.5f;
            }
        }
    }

    /* Bank 2: "PWM" — pulse width from 50% down to 5% */
    {
        sq_wt_bank_t *b = &engine->wt_banks[2];
        snprintf(b->name, sizeof(b->name), "%s", "PWM");
        b->num_frames = 16;
        for (int f = 0; f < 16; f++) {
            float pw = 0.5f - (float)f * 0.03f; /* 0.50 → 0.05 */
            if (pw < 0.05f) pw = 0.05f;
            for (int i = 0; i < SQ_WAVETABLE_SIZE; i++) {
                double phase = (double)i / (double)SQ_WAVETABLE_SIZE;
                b->frames[f][i] = (phase < (double)pw) ? 1.0f : -1.0f;
            }
        }
    }

    /* Bank 3: "Formant" — vowel-like resonances */
    {
        sq_wt_bank_t *b = &engine->wt_banks[3];
        snprintf(b->name, sizeof(b->name), "%s", "Formant");
        b->num_frames = 16;
        /* Formant frequencies (Hz at 256Hz fundamental) for vowels A-E-I-O-U */
        static const float formants[][2] = {
            {800, 1200}, {400, 2200}, {250, 2600}, {450, 800}, {300, 700},
        };
        for (int f = 0; f < 16; f++) {
            /* Interpolate between vowels */
            float idx = (float)f / 15.0f * 4.0f;
            int v0 = (int)idx;
            if (v0 > 3) v0 = 3;
            int v1 = v0 + 1;
            float frac = idx - (float)v0;
            float f1 = formants[v0][0] * (1.0f - frac) + formants[v1][0] * frac;
            float f2 = formants[v0][1] * (1.0f - frac) + formants[v1][1] * frac;

            for (int i = 0; i < SQ_WAVETABLE_SIZE; i++) {
                double phase = (double)i / (double)SQ_WAVETABLE_SIZE;
                float val = sinf((float)(phase * 2.0 * M_PI));
                /* Add formant harmonics */
                float r1 = f1 / 256.0f; /* ratio to base */
                float r2 = f2 / 256.0f;
                val += 0.5f * sinf((float)(phase * 2.0 * M_PI * r1));
                val += 0.3f * sinf((float)(phase * 2.0 * M_PI * r2));
                b->frames[f][i] = val * 0.4f;
            }
        }
    }

    engine->num_wt_banks = 4;
}

/* ─── Wavetable render for one voice ─────────────────────────────────────── */

/* Read from a wavetable bank with position interpolation between frames */
static float wt_bank_read(const sq_wt_bank_t *bank, double phase, float position)
{
    if (bank->num_frames < 1) return 0.0f;

    /* Position maps to frame index */
    float pos = position * (float)(bank->num_frames - 1);
    int f0 = (int)pos;
    if (f0 >= bank->num_frames - 1) f0 = bank->num_frames - 2;
    if (f0 < 0) f0 = 0;
    int f1 = f0 + 1;
    float frac = pos - (float)f0;

    /* Read from both frames with linear interpolation */
    float s0 = wavetable_read(bank->frames[f0], phase);
    float s1 = wavetable_read(bank->frames[f1], phase);
    return s0 * (1.0f - frac) + s1 * frac;
}

static void wt_render_voice(sq_engine_t *engine, sq_synth_voice_t *v,
                            float *output, uint32_t num_frames)
{
    sq_synth_preset_t *p = &engine->synth_presets[v->preset_index];

    int bank_idx = p->wt_bank_index;
    if (bank_idx < 0 || (uint32_t)bank_idx >= engine->num_wt_banks)
        bank_idx = 0;
    if (engine->num_wt_banks == 0 || !engine->wt_banks) {
        v->active = false;
        return;
    }
    const sq_wt_bank_t *bank = &engine->wt_banks[bank_idx];

    float sr = (float)engine->sample_rate;
    double phase_inc = (double)v->frequency / (double)sr;
    float pan_l = (1.0f - v->pan) * 0.5f;
    float pan_r = (1.0f + v->pan) * 0.5f;
    float base_gain = v->velocity * v->volume;
    float smooth_coeff = 1.0f - expf(-1.0f / (0.005f * sr));

    for (uint32_t i = 0; i < num_frames; i++) {
        /* Process envelopes */
        float amp = envelope_process(&v->amp_env, &p->amp_env,
                                     engine->sample_rate);
        float filt_env = envelope_process(&v->filter_env, &p->filter_env,
                                          engine->sample_rate);

        if (v->amp_env.stage == ENV_IDLE) {
            v->active = false;
            break;
        }

        /* LFO */
        float lfo_val = 0.0f;
        if (v->lfo.dest != LFO_DEST_NONE && v->lfo.depth > 0.0f) {
            lfo_val = lfo_process(&v->lfo, engine->sample_rate,
                                  &engine->wavetables);
        }

        /* Compute modulated wavetable position */
        float target_pos = p->wt_position
                         + filt_env * p->wt_env_depth
                         + lfo_val * p->wt_lfo_depth;
        if (target_pos < 0.0f) target_pos = 0.0f;
        if (target_pos > 1.0f) target_pos = 1.0f;

        /* Smooth the position to prevent clicks */
        v->wt_smoothed_pos = v->wt_smoothed_pos
                           + smooth_coeff * (target_pos - v->wt_smoothed_pos);

        /* Read from wavetable */
        float sample = wt_bank_read(bank, v->wt_phase, v->wt_smoothed_pos);

        /* Advance phase */
        v->wt_phase += phase_inc;
        if (v->wt_phase >= 1.0) v->wt_phase -= 1.0;

        /* Apply gain and output */
        float gain = amp * base_gain;
        output[i * 2]     += sample * pan_l * gain;
        output[i * 2 + 1] += sample * pan_r * gain;
    }
}

/* ─── Release all active synth voices ────────────────────────────────────── */

void synth_release_all(sq_engine_t *engine)
{
    for (int v = 0; v < SQ_MAX_SYNTH_VOICES; v++) {
        sq_synth_voice_t *voice = &engine->synth_voices[v];
        if (!voice->active) continue;

        int pi = voice->preset_index;
        if (pi < 0 || (uint32_t)pi >= engine->num_synth_presets) continue;
        sq_synth_preset_t *p = &engine->synth_presets[pi];

        envelope_release(&voice->amp_env, &p->amp_env, engine->sample_rate);

        /* Also release FM operator envelopes */
        if (p->synth_mode == SYNTH_FM) {
            for (int op = 0; op < FM_NUM_OPERATORS; op++) {
                envelope_release(&voice->fm_env[op], &p->fm_ops[op].env,
                                 engine->sample_rate);
            }
        }
    }
}

/* ─── Render all synth voices ────────────────────────────────────────────── */

void synth_render(sq_engine_t *engine, float *output, uint32_t num_frames)
{
    if (!engine->wavetables.initialized) return;

    float sr = (float)engine->sample_rate;
    /* Smoothing coefficient: ~3ms at 44100 Hz */
    float smooth_coeff = 1.0f - expf(-1.0f / (0.003f * sr));

    for (int vi = 0; vi < SQ_MAX_SYNTH_VOICES; vi++) {
        sq_synth_voice_t *v = &engine->synth_voices[vi];
        if (!v->active) continue;

        sq_synth_preset_t *p = &engine->synth_presets[v->preset_index];

        /* Dispatch to FM or wavetable renderer if needed */
        if (p->synth_mode == SYNTH_FM) {
            fm_render_voice(engine, v, output, num_frames);
            continue;
        }
        if (p->synth_mode == SYNTH_WAVETABLE) {
            wt_render_voice(engine, v, output, num_frames);
            continue;
        }

        const float *table1 = engine->wavetables.tables[p->osc1_wave];
        const float *table2 = engine->wavetables.tables[p->osc2_wave];

        /* Pre-compute constants outside the sample loop */
        float base_freq = v->frequency;
        double osc2_detune_ratio = pow(2.0, (double)p->osc2_detune / 12.0);
        float osc1_mix = 1.0f - p->osc_mix;
        float osc2_mix = p->osc_mix;
        float pan_l = (1.0f - v->pan) * 0.5f;
        float pan_r = (1.0f + v->pan) * 0.5f;
        float base_gain = v->velocity * v->volume;

        /* Unison setup */
        int unison_count = p->unison_voices;
        if (unison_count < 1) unison_count = 1;
        if (unison_count > MAX_UNISON) unison_count = MAX_UNISON;
        float unison_gain = 1.0f / sqrtf((float)unison_count); /* normalize */

        /* Pre-compute unison detune ratios and pan positions */
        double unison_ratios[MAX_UNISON];
        float  unison_pan_l[MAX_UNISON];
        float  unison_pan_r[MAX_UNISON];
        for (int u = 0; u < unison_count; u++) {
            if (unison_count == 1) {
                unison_ratios[u] = 1.0;
                unison_pan_l[u] = pan_l;
                unison_pan_r[u] = pan_r;
            } else {
                /* Spread detune: -detune to +detune across voices */
                float detune_cents = p->unison_detune *
                    (-1.0f + 2.0f * (float)u / (float)(unison_count - 1));
                unison_ratios[u] = pow(2.0, (double)detune_cents / 1200.0);

                /* Spread stereo: leftmost to rightmost */
                float spread = -1.0f + 2.0f * (float)u / (float)(unison_count - 1);
                float u_pan = v->pan + spread * 0.8f; /* 80% stereo spread */
                if (u_pan < -1.0f) u_pan = -1.0f;
                if (u_pan >  1.0f) u_pan =  1.0f;
                unison_pan_l[u] = (1.0f - u_pan) * 0.5f;
                unison_pan_r[u] = (1.0f + u_pan) * 0.5f;
            }
        }

        /* Base phase increment (osc2 uses detune ratio on the phase directly) */
        double base_phase_inc1 = (double)base_freq / (double)sr;

        /* LFO BPM sync: override rate if synced */
        if (v->lfo.dest != LFO_DEST_NONE && p->lfo_bpm_sync) {
            v->lfo.rate = lfo_bpm_sync_rate(engine->transport.bpm,
                                             p->lfo_sync_division);
        }

        /* Filter coefficient tracking */
        filter_coeffs_t fc;
        filter_calc_coeffs(&fc, v->smoothed_cutoff, p->filter_resonance,
                           engine->sample_rate);
        int filter_update_counter = 0;

        /* Track whether LFO needs per-sample processing */
        bool lfo_pitch  = (v->lfo.dest == LFO_DEST_PITCH  && v->lfo.depth > 0.0f);
        bool lfo_filter = (v->lfo.dest == LFO_DEST_FILTER && v->lfo.depth > 0.0f);
        bool lfo_amp    = (v->lfo.dest == LFO_DEST_AMP    && v->lfo.depth > 0.0f);

        for (uint32_t i = 0; i < num_frames; i++) {
            /* Process envelopes */
            float amp = envelope_process(&v->amp_env, &p->amp_env,
                                         engine->sample_rate);
            float filt_env = envelope_process(&v->filter_env, &p->filter_env,
                                              engine->sample_rate);

            /* Voice done? */
            if (v->amp_env.stage == ENV_IDLE) {
                v->active = false;
                break;
            }

            /* LFO */
            float lfo_val = 0.0f;
            if (lfo_pitch || lfo_filter || lfo_amp) {
                lfo_val = lfo_process(&v->lfo, engine->sample_rate,
                                      &engine->wavetables);
            }

            /* Phase increment — only recompute if LFO modulates pitch */
            double phase_inc1 = base_phase_inc1;
            if (lfo_pitch) {
                float pitch_mult = 1.0f + lfo_val * (2.0f / 12.0f);
                phase_inc1 = base_phase_inc1 * pitch_mult;
            }

            /* Render oscillators with unison */
            float left = 0.0f, right = 0.0f;

            for (int u = 0; u < unison_count; u++) {
                double phase = v->unison_phases[u];

                /* Read osc1 */
                float osc1 = wavetable_read(table1, phase);
                /* Read osc2 with detune */
                double osc2_phase = phase * osc2_detune_ratio;
                osc2_phase -= (int)osc2_phase; /* wrap */
                float osc2 = wavetable_read(table2, osc2_phase);

                float mix = osc1 * osc1_mix + osc2 * osc2_mix;
                left  += mix * unison_pan_l[u] * unison_gain;
                right += mix * unison_pan_r[u] * unison_gain;

                /* Advance this unison voice's phase */
                v->unison_phases[u] += phase_inc1 * unison_ratios[u];
                if (v->unison_phases[u] >= 1.0)
                    v->unison_phases[u] -= 1.0;
            }

            /* Update filter coefficients every 32 samples with smoothing */
            if (++filter_update_counter >= 32) {
                filter_update_counter = 0;
                float target_cutoff = p->filter_cutoff
                                    + filt_env * p->filter_env_depth;
                if (lfo_filter) target_cutoff += lfo_val * 2000.0f;

                /* Smooth the cutoff to prevent clicks (TASK-043) */
                v->smoothed_cutoff = smooth_param(v->smoothed_cutoff,
                                                   target_cutoff, smooth_coeff * 32.0f);
                filter_calc_coeffs(&fc, v->smoothed_cutoff, p->filter_resonance,
                                   engine->sample_rate);
            }

            /* Apply filter */
            filter_apply(&v->filter, &fc, &left, &right);

            /* Apply amplitude with smoothed gain */
            float amp_mod = lfo_amp ? (1.0f + lfo_val * 0.5f) : 1.0f;
            float gain = amp * base_gain * amp_mod;
            output[i * 2]     += left * gain;
            output[i * 2 + 1] += right * gain;
        }
    }
}

/* ─── Wavetable loading from WAV files ─────────────────────────────────── */

#include "formats/sample_io.h"
#include <stdlib.h>

int synth_load_wt_bank(sq_engine_t *engine, const char *filepath, const char *name)
{
    if (!engine->wt_banks) return -1;
    if (engine->num_wt_banks >= SQ_WT_MAX_BANKS) return -1;

    /* Load the WAV file */
    sq_sample_t sample;
    memset(&sample, 0, sizeof(sample));
    if (sample_io_load(filepath, &sample) != 0) return -1;

    /* Calculate number of frames (each cycle = SQ_WAVETABLE_SIZE samples) */
    int num_frames = (int)(sample.num_frames / SQ_WAVETABLE_SIZE);
    if (num_frames < 1) {
        if (sample.data) free(sample.data);
        return -1;
    }
    if (num_frames > SQ_WT_MAX_FRAMES) num_frames = SQ_WT_MAX_FRAMES;

    int idx = (int)engine->num_wt_banks;
    sq_wt_bank_t *b = &engine->wt_banks[idx];
    memset(b, 0, sizeof(*b));
    snprintf(b->name, sizeof(b->name), "%s", name ? name : "Custom");
    b->num_frames = num_frames;

    /* Copy each cycle into the bank, mono-mixing if stereo */
    for (int f = 0; f < num_frames; f++) {
        uint32_t offset = (uint32_t)f * SQ_WAVETABLE_SIZE;
        for (int i = 0; i < SQ_WAVETABLE_SIZE; i++) {
            if (sample.num_channels == 1) {
                b->frames[f][i] = sample.data[offset + i];
            } else {
                /* Stereo: average L+R */
                uint32_t si = (offset + i) * sample.num_channels;
                b->frames[f][i] = (sample.data[si] + sample.data[si + 1]) * 0.5f;
            }
        }
    }

    if (sample.data) free(sample.data);
    engine->num_wt_banks++;
    return idx;
}
