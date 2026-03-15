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
    /* Preset 0: "Bass" — punchy saw bass, dark and weighty
     * Low cutoff keeps it warm, moderate env depth for attack punch,
     * longer decay (0.5s) gives body instead of a click */
    {
        sq_synth_preset_t *p = &engine->synth_presets[0];
        memset(p, 0, sizeof(*p));
        snprintf(p->name, sizeof(p->name), "%s", "Bass");
        p->osc1_wave = WAVE_SAW;
        p->osc2_wave = WAVE_SAW;
        p->osc_mix = 0.3f;  /* blend both saws for thickness */
        p->osc2_detune = -0.08f;  /* slight detune down for fatness */
        p->unison_voices = 1;
        p->unison_detune = 0.0f;
        p->filter_type = FILTER_LOWPASS;
        p->filter_cutoff = 400.0f;  /* dark: warmth comes from being dark */
        p->filter_resonance = 1.5f;  /* gentle resonance, no shriek */
        p->filter_env_depth = 1200.0f;  /* moderate punch on attack */
        p->amp_env = (sq_adsr_params_t){0.003f, 0.5f, 0.0f, 0.08f};
        p->filter_env = (sq_adsr_params_t){0.003f, 0.25f, 0.0f, 0.06f};
        p->lfo = (sq_lfo_t){WAVE_SINE, 0.0f, 0.0f, LFO_DEST_NONE, 0.0};
    }

    /* Preset 1: "Lead" — classic saw+square lead that cuts through a mix
     * Moderate filter (not too bright), high sustain (0.8) so notes hold,
     * gentle vibrato for expression, resonance at 2.0 for presence not shriek */
    {
        sq_synth_preset_t *p = &engine->synth_presets[1];
        memset(p, 0, sizeof(*p));
        snprintf(p->name, sizeof(p->name), "%s", "Lead");
        p->osc1_wave = WAVE_SAW;
        p->osc2_wave = WAVE_SQUARE;
        p->osc_mix = 0.4f;
        p->osc2_detune = 0.1f;  /* slight detune for thickness */
        p->unison_voices = 2;  /* stereo width without excess */
        p->unison_detune = 8.0f;
        p->filter_type = FILTER_LOWPASS;
        p->filter_cutoff = 1200.0f;  /* moderate: lets it cut without being harsh */
        p->filter_resonance = 2.0f;  /* presence, not shriek */
        p->filter_env_depth = 2500.0f;  /* attack bite from envelope */
        p->amp_env = (sq_adsr_params_t){0.008f, 1.2f, 0.0f, 0.35f};  /* Lead: was S=0.8 (hung forever) */
        p->filter_env = (sq_adsr_params_t){0.005f, 1.1f, 0.0f, 0.2f};
        p->lfo = (sq_lfo_t){WAVE_SINE, 5.0f, 0.06f, LFO_DEST_PITCH, 0.0};
    }

    /* Preset 2: "Pad" — cinematic warm pad, Blade Runner-inspired
     * Saw+saw detuned for chorus width (not triangle+sine which is thin),
     * 5 unison voices at 20 cents for lush spread, cutoff under 1000Hz
     * for warmth, slow LFO on filter for gentle movement */
    {
        sq_synth_preset_t *p = &engine->synth_presets[2];
        memset(p, 0, sizeof(*p));
        snprintf(p->name, sizeof(p->name), "%s", "Pad");
        p->osc1_wave = WAVE_SAW;
        p->osc2_wave = WAVE_SAW;
        p->osc_mix = 0.5f;
        p->osc2_detune = 0.08f;  /* detune for chorus character */
        p->unison_voices = 5;  /* wide stereo image */
        p->unison_detune = 20.0f;  /* 20 cents: lush but not washy */
        p->filter_type = FILTER_LOWPASS;
        p->filter_cutoff = 900.0f;  /* dark and warm, beauty is in detuning */
        p->filter_resonance = 0.8f;  /* no resonance: pure Juno warmth */
        p->filter_env_depth = 400.0f;  /* subtle brightness swell */
        p->amp_env = (sq_adsr_params_t){1.5f, 3.0f, 0.0f, 2.0f};  /* Pad: was S=0.9 */
        p->filter_env = (sq_adsr_params_t){1.0f, 2.5f, 0.0f, 1.5f};
        p->lfo = (sq_lfo_t){WAVE_TRIANGLE, 0.2f, 0.15f, LFO_DEST_FILTER, 0.0};
    }

    /* Preset 3: "Pluck" — full-bodied pluck, not a bleep
     * KEY: decay 0.4s (not 0.15s = bleep), resonance under 2.0,
     * moderate env depth (1500, not 6000 = ice pick), 2 unison for body */
    {
        sq_synth_preset_t *p = &engine->synth_presets[3];
        memset(p, 0, sizeof(*p));
        snprintf(p->name, sizeof(p->name), "%s", "Pluck");
        p->osc1_wave = WAVE_SAW;
        p->osc2_wave = WAVE_SQUARE;
        p->osc_mix = 0.3f;
        p->osc2_detune = 0.05f;  /* subtle detune for width */
        p->unison_voices = 2;  /* stereo body */
        p->unison_detune = 8.0f;
        p->filter_type = FILTER_LOWPASS;
        p->filter_cutoff = 600.0f;  /* start dark, env opens it */
        p->filter_resonance = 1.5f;  /* under 2.0: body not bleep */
        p->filter_env_depth = 1500.0f;  /* moderate sweep, not extreme */
        p->amp_env = (sq_adsr_params_t){0.001f, 0.4f, 0.0f, 0.15f};
        p->filter_env = (sq_adsr_params_t){0.001f, 0.3f, 0.0f, 0.1f};
        p->lfo = (sq_lfo_t){WAVE_SINE, 0.0f, 0.0f, LFO_DEST_NONE, 0.0};
    }

    /* Preset 4: "SuperSaw" — massive JP-8000-style supersaw
     * 7 unison voices at 25 cents for that wall-of-sound trance feel,
     * cutoff 1800 (not 4000 = harsh), osc2 at unison (not 5th = chord) */
    {
        sq_synth_preset_t *p = &engine->synth_presets[4];
        memset(p, 0, sizeof(*p));
        snprintf(p->name, sizeof(p->name), "%s", "SuperSaw");
        p->osc1_wave = WAVE_SAW;
        p->osc2_wave = WAVE_SAW;
        p->osc_mix = 0.5f;
        p->osc2_detune = 0.12f;  /* subtle detune, not a fifth */
        p->unison_voices = 7;  /* maximum width */
        p->unison_detune = 25.0f;  /* 25 cents: wide but coherent */
        p->filter_type = FILTER_LOWPASS;
        p->filter_cutoff = 1800.0f;  /* warm but present, not harsh */
        p->filter_resonance = 0.8f;  /* no resonance, pure width */
        p->filter_env_depth = 1500.0f;  /* subtle brightness on attack */
        p->amp_env = (sq_adsr_params_t){0.01f, 1.5f, 0.0f, 0.6f};  /* SuperSaw: was S=0.8 */
        p->filter_env = (sq_adsr_params_t){0.01f, 1.3f, 0.0f, 0.4f};
        p->lfo = (sq_lfo_t){WAVE_SINE, 0.1f, 0.08f, LFO_DEST_FILTER, 0.0};
    }

    /* ── FM Presets ─────────────────────────────────────────────────────── */

    /* Preset 5: "FM Bell" — classic DX7 bell, long shimmering decay
     * High inharmonic ratios (3.5, 7.0) create metallic bell character,
     * long decay (3s) and release (2s) for natural bell sustain */
    {
        sq_synth_preset_t *p = &engine->synth_presets[5];
        memset(p, 0, sizeof(*p));
        snprintf(p->name, sizeof(p->name), "%s", "FM Bell");
        p->synth_mode = SYNTH_FM;
        p->fm_algorithm = 0; /* serial: 3->2->1->0 */
        p->amp_env = (sq_adsr_params_t){0.001f, 3.0f, 0.0f, 2.0f};
        /* Op 0 (carrier): fundamental, long ring */
        p->fm_ops[0] = (sq_fm_operator_t){1.0f, 1.0f, 0.0f,
            {0.001f, 3.0f, 0.0f, 2.0f}};
        /* Op 1 (modulator): inharmonic ratio for bell character */
        p->fm_ops[1] = (sq_fm_operator_t){3.5f, 0.6f, 0.0f,
            {0.001f, 2.0f, 0.0f, 1.0f}};
        /* Op 2: upper partials, decay faster than fundamental */
        p->fm_ops[2] = (sq_fm_operator_t){7.0f, 0.3f, 0.0f,
            {0.001f, 1.0f, 0.0f, 0.5f}};
        /* Op 3: shimmer, decays quickest */
        p->fm_ops[3] = (sq_fm_operator_t){11.0f, 0.15f, 0.0f,
            {0.001f, 0.6f, 0.0f, 0.3f}};
    }

    /* Preset 6: "FM EPiano" — DX7 Rhodes: warm body + gentle tine attack
     * Low mod index (0.35) for smoothness, 1:1 ratio keeps it pure,
     * tine op decays fast leaving warm fundamental body */
    {
        sq_synth_preset_t *p = &engine->synth_presets[6];
        memset(p, 0, sizeof(*p));
        snprintf(p->name, sizeof(p->name), "%s", "FM EPiano");
        p->synth_mode = SYNTH_FM;
        p->fm_algorithm = 2; /* two pairs: 3->2, 1->0 */
        p->amp_env = (sq_adsr_params_t){0.001f, 2.5f, 0.0f, 0.8f};  /* FM EPiano: was S=0.25 */
        /* Op 0 (carrier): warm fundamental body */
        p->fm_ops[0] = (sq_fm_operator_t){1.0f, 1.0f, 0.0f,
            {0.001f, 1.5f, 0.25f, 0.8f}};
        /* Op 1 (modulator): gentle 1:1 for warmth, low level */
        p->fm_ops[1] = (sq_fm_operator_t){1.0f, 0.35f, 0.0f,
            {0.001f, 0.6f, 0.05f, 0.3f}};
        /* Op 2 (carrier): octave tine, lower level for subtlety */
        p->fm_ops[2] = (sq_fm_operator_t){2.0f, 0.4f, 0.0f,
            {0.001f, 0.5f, 0.0f, 0.3f}};
        /* Op 3 (modulator): tine attack brightness, decays fast */
        p->fm_ops[3] = (sq_fm_operator_t){14.0f, 0.2f, 0.0f,
            {0.001f, 0.15f, 0.0f, 0.08f}};
    }

    /* Preset 7: "FM Metal" — metallic percussion, industrial character
     * Inharmonic ratios (1.41, 2.82 = sqrt2 series) for metallic timbre,
     * feedback on op3 adds grit, longer decay (0.5s) for body */
    {
        sq_synth_preset_t *p = &engine->synth_presets[7];
        memset(p, 0, sizeof(*p));
        snprintf(p->name, sizeof(p->name), "%s", "FM Metal");
        p->synth_mode = SYNTH_FM;
        p->fm_algorithm = 1; /* 2->1->0, 3->0 */
        p->amp_env = (sq_adsr_params_t){0.001f, 0.5f, 0.0f, 0.2f};
        p->fm_ops[0] = (sq_fm_operator_t){1.0f, 1.0f, 0.0f,
            {0.001f, 0.5f, 0.0f, 0.2f}};
        p->fm_ops[1] = (sq_fm_operator_t){1.41f, 0.8f, 0.0f,
            {0.001f, 0.3f, 0.0f, 0.1f}};
        p->fm_ops[2] = (sq_fm_operator_t){2.82f, 0.5f, 0.0f,
            {0.001f, 0.2f, 0.0f, 0.08f}};
        p->fm_ops[3] = (sq_fm_operator_t){5.0f, 0.6f, 0.15f,
            {0.001f, 0.15f, 0.0f, 0.06f}};
    }

    /* Preset 8: "FM Bass" — deep punchy FM bass with harmonic click
     * 1:1 ratio on mod for warmth, higher ops decay fast giving attack
     * transient that fades to pure fundamental — heavy and musical */
    {
        sq_synth_preset_t *p = &engine->synth_presets[8];
        memset(p, 0, sizeof(*p));
        snprintf(p->name, sizeof(p->name), "%s", "FM Bass");
        p->synth_mode = SYNTH_FM;
        p->fm_algorithm = 0; /* serial */
        p->amp_env = (sq_adsr_params_t){0.001f, 0.6f, 0.0f, 0.1f};
        p->fm_ops[0] = (sq_fm_operator_t){1.0f, 1.0f, 0.0f,
            {0.001f, 0.6f, 0.0f, 0.1f}};
        p->fm_ops[1] = (sq_fm_operator_t){1.0f, 0.6f, 0.0f,
            {0.001f, 0.12f, 0.0f, 0.03f}};
        p->fm_ops[2] = (sq_fm_operator_t){2.0f, 0.25f, 0.0f,
            {0.001f, 0.08f, 0.0f, 0.02f}};
        p->fm_ops[3] = (sq_fm_operator_t){3.0f, 0.15f, 0.0f,
            {0.001f, 0.04f, 0.0f, 0.01f}};
    }

    /* Preset 9: "FM Pad" — additive FM pad, cinematic slow evolve
     * All carriers (additive), each harmonic has different attack time
     * creating spectral evolution — slow attack, high sustain, long release */
    {
        sq_synth_preset_t *p = &engine->synth_presets[9];
        memset(p, 0, sizeof(*p));
        snprintf(p->name, sizeof(p->name), "%s", "FM Pad");
        p->synth_mode = SYNTH_FM;
        p->fm_algorithm = 6; /* all carriers (additive) */
        p->amp_env = (sq_adsr_params_t){1.0f, 3.0f, 0.0f, 2.0f};  /* FM Pad: was S=0.85 */
        p->fm_ops[0] = (sq_fm_operator_t){1.0f, 1.0f, 0.0f,
            {0.8f, 0.5f, 0.85f, 2.0f}};
        p->fm_ops[1] = (sq_fm_operator_t){2.0f, 0.4f, 0.0f,
            {1.2f, 0.5f, 0.6f, 1.5f}};
        p->fm_ops[2] = (sq_fm_operator_t){3.0f, 0.2f, 0.0f,
            {1.5f, 0.5f, 0.35f, 1.0f}};
        p->fm_ops[3] = (sq_fm_operator_t){5.0f, 0.1f, 0.05f,
            {0.5f, 1.5f, 0.15f, 2.5f}};
    }

    /* ── Wavetable Presets ────────────────────────────────────────────── */

    /* Preset 10: "WT Sweep" — analog bank with slow wavetable sweep
     * Position envelope sweeps through timbres on each note,
     * moderate sustain for musical playing, not just a blip */
    {
        sq_synth_preset_t *p = &engine->synth_presets[10];
        memset(p, 0, sizeof(*p));
        snprintf(p->name, sizeof(p->name), "%s", "WT Sweep");
        p->synth_mode = SYNTH_WAVETABLE;
        p->wt_bank_index = 0; /* Analog */
        p->wt_position = 0.0f;
        p->wt_env_depth = 0.6f;
        p->wt_lfo_depth = 0.1f;  /* subtle movement */
        p->amp_env = (sq_adsr_params_t){0.01f, 1.8f, 0.0f, 0.5f};  /* was S=0.7 */
        p->filter_env = (sq_adsr_params_t){0.01f, 1.4f, 0.0f, 0.3f};
        p->lfo = (sq_lfo_t){WAVE_TRIANGLE, 0.15f, 0.1f, LFO_DEST_FILTER, 0.0};
    }

    /* Preset 11: "WT Harmonic" — evolving harmonic pad with slow LFO
     * Harmonics bank gives organ-like timbres, LFO on wavetable position
     * creates spectral movement — slow attack for pad use */
    {
        sq_synth_preset_t *p = &engine->synth_presets[11];
        memset(p, 0, sizeof(*p));
        snprintf(p->name, sizeof(p->name), "%s", "WT Harmonic");
        p->synth_mode = SYNTH_WAVETABLE;
        p->wt_bank_index = 1; /* Harmonics */
        p->wt_position = 0.3f;
        p->wt_env_depth = 0.15f;  /* subtle env sweep too */
        p->wt_lfo_depth = 0.35f;
        p->amp_env = (sq_adsr_params_t){0.8f, 3.0f, 0.0f, 1.5f};
        p->filter_env = (sq_adsr_params_t){0.5f, 1.5f, 0.0f, 0.8f};
        p->lfo = (sq_lfo_t){WAVE_TRIANGLE, 0.3f, 0.8f, LFO_DEST_FILTER, 0.0};
    }

    /* Preset 12: "WT PWM" — pulse width modulation for hollow/reedy tone
     * LFO sweeps pulse width for classic analog character,
     * slower LFO rate (0.8Hz) for musical sweep, good sustain for leads */
    {
        sq_synth_preset_t *p = &engine->synth_presets[12];
        memset(p, 0, sizeof(*p));
        snprintf(p->name, sizeof(p->name), "%s", "WT PWM");
        p->synth_mode = SYNTH_WAVETABLE;
        p->wt_bank_index = 2; /* PWM */
        p->wt_position = 0.2f;  /* start slightly off-center */
        p->wt_env_depth = 0.0f;
        p->wt_lfo_depth = 0.5f;
        p->amp_env = (sq_adsr_params_t){0.01f, 2.6f, 0.0f, 0.4f};
        p->filter_env = (sq_adsr_params_t){0.01f, 1.4f, 0.0f, 0.3f};
        p->lfo = (sq_lfo_t){WAVE_TRIANGLE, 0.8f, 0.8f, LFO_DEST_FILTER, 0.0};
    }

    /* Preset 13: "WT Vocal" — formant vowel sweep, choir-like
     * Envelope sweeps through vowel formants on attack,
     * slow LFO adds subtle movement, high sustain for pads */
    {
        sq_synth_preset_t *p = &engine->synth_presets[13];
        memset(p, 0, sizeof(*p));
        snprintf(p->name, sizeof(p->name), "%s", "WT Vocal");
        p->synth_mode = SYNTH_WAVETABLE;
        p->wt_bank_index = 3; /* Formant */
        p->wt_position = 0.0f;
        p->wt_env_depth = 0.6f;
        p->wt_lfo_depth = 0.15f;  /* subtle vowel drift */
        p->amp_env = (sq_adsr_params_t){0.5f, 2.9f, 0.0f, 1.0f};
        p->filter_env = (sq_adsr_params_t){0.3f, 1.6f, 0.0f, 0.6f};
        p->lfo = (sq_lfo_t){WAVE_SINE, 0.2f, 0.1f, LFO_DEST_FILTER, 0.0};
    }

    /* ── Classic / Historic Synth Presets ────────────────────────────── */

    /* Preset 14: "Moog Bass" — Minimoog-style thick warm bass
     * Warmth comes from being DARK (cutoff 500Hz), not from resonance.
     * Saw + square sub octave, slight env depth for pluck, longer decay */
    {
        sq_synth_preset_t *p = &engine->synth_presets[14];
        memset(p, 0, sizeof(*p));
        snprintf(p->name, sizeof(p->name), "%s", "Moog Bass");
        p->synth_mode = SYNTH_SUBTRACTIVE;
        p->osc1_wave = WAVE_SAW;
        p->osc2_wave = WAVE_SQUARE;
        p->osc_mix = 0.35f;
        p->osc2_detune = -12.0f; /* one octave down for sub weight */
        p->unison_voices = 1;
        p->filter_type = FILTER_LOWPASS;
        p->filter_cutoff = 500.0f;  /* dark and warm, Moog ladder character */
        p->filter_resonance = 2.0f;  /* gentle, not screaming */
        p->filter_env_depth = 1500.0f;  /* moderate punch, not screechy */
        p->amp_env = (sq_adsr_params_t){0.002f, 0.4f, 0.0f, 0.08f};
        p->filter_env = (sq_adsr_params_t){0.002f, 0.2f, 0.0f, 0.06f};
        p->lfo = (sq_lfo_t){WAVE_SINE, 0.0f, 0.0f, LFO_DEST_NONE, 0.0};
    }

    /* Preset 15: "303 Acid" — TB-303 acid squelch
     * Single saw, HIGH resonance (10) is correct for 303 character,
     * fast filter env decay (0.1s) creates the signature "squelch",
     * cutoff 250Hz baseline, env depth 5000 for range. Amp decay 0.3s. */
    {
        sq_synth_preset_t *p = &engine->synth_presets[15];
        memset(p, 0, sizeof(*p));
        snprintf(p->name, sizeof(p->name), "%s", "303 Acid");
        p->synth_mode = SYNTH_SUBTRACTIVE;
        p->osc1_wave = WAVE_SAW;
        p->osc2_wave = WAVE_SAW;
        p->osc_mix = 0.0f; /* single osc: 303 has one oscillator */
        p->unison_voices = 1;
        p->filter_type = FILTER_LOWPASS;
        p->filter_cutoff = 250.0f;  /* low baseline, env opens it */
        p->filter_resonance = 10.0f; /* high reso IS the 303 character */
        p->filter_env_depth = 5000.0f;  /* wide sweep for squelch */
        p->amp_env = (sq_adsr_params_t){0.001f, 0.3f, 0.0f, 0.05f};
        p->filter_env = (sq_adsr_params_t){0.001f, 0.1f, 0.0f, 0.03f};
        p->lfo = (sq_lfo_t){WAVE_SINE, 0.0f, 0.0f, LFO_DEST_NONE, 0.0};
    }

    /* Preset 16: "Juno Pad" — Roland Juno-106 warm chorus pad
     * Saw only (Juno character), NO resonance (pure warmth),
     * cutoff 800-1200Hz range, unison detune IS the chorus effect */
    {
        sq_synth_preset_t *p = &engine->synth_presets[16];
        memset(p, 0, sizeof(*p));
        snprintf(p->name, sizeof(p->name), "%s", "Juno Pad");
        p->synth_mode = SYNTH_SUBTRACTIVE;
        p->osc1_wave = WAVE_SAW;
        p->osc2_wave = WAVE_SAW;  /* Juno: saw through chorus */
        p->osc_mix = 0.5f;
        p->osc2_detune = 0.07f;
        p->unison_voices = 5;
        p->unison_detune = 15.0f; /* chorus spread */
        p->filter_type = FILTER_LOWPASS;
        p->filter_cutoff = 1000.0f;  /* warm, not bright */
        p->filter_resonance = 0.7f;  /* NO resonance: pure Juno warmth */
        p->filter_env_depth = 400.0f;  /* barely any env: static warmth */
        p->amp_env = (sq_adsr_params_t){0.5f, 3.0f, 0.0f, 1.5f};
        p->filter_env = (sq_adsr_params_t){0.4f, 1.9f, 0.0f, 1.0f};
        p->lfo = (sq_lfo_t){WAVE_TRIANGLE, 0.15f, 0.1f, LFO_DEST_FILTER, 0.0};
    }

    /* Preset 17: "OB Strings" — Oberheim OB-X string ensemble
     * Max unison (7) for massive width, moderate cutoff for warmth,
     * slow attack simulates bowed strings, vibrato for expression */
    {
        sq_synth_preset_t *p = &engine->synth_presets[17];
        memset(p, 0, sizeof(*p));
        snprintf(p->name, sizeof(p->name), "%s", "OB Strings");
        p->synth_mode = SYNTH_SUBTRACTIVE;
        p->osc1_wave = WAVE_SAW;
        p->osc2_wave = WAVE_SAW;
        p->osc_mix = 0.5f;
        p->osc2_detune = 0.1f;
        p->unison_voices = 7; /* maximum lush */
        p->unison_detune = 20.0f;  /* wider spread */
        p->filter_type = FILTER_LOWPASS;
        p->filter_cutoff = 2000.0f;  /* warm string tone, not bright */
        p->filter_resonance = 0.8f;  /* smooth, no edge */
        p->filter_env_depth = 600.0f;  /* gentle brightness swell */
        p->amp_env = (sq_adsr_params_t){1.0f, 3.0f, 0.0f, 1.5f};
        p->filter_env = (sq_adsr_params_t){0.8f, 1.8f, 0.0f, 1.0f};
        p->lfo = (sq_lfo_t){WAVE_SINE, 4.5f, 0.04f, LFO_DEST_PITCH, 0.0};
    }

    /* Preset 18: "Prophet Brass" — Prophet-5 brass stab, punchy and wide
     * Fast filter env gives brass attack bite, moderate sustain holds notes,
     * 3 unison voices for width without muddiness */
    {
        sq_synth_preset_t *p = &engine->synth_presets[18];
        memset(p, 0, sizeof(*p));
        snprintf(p->name, sizeof(p->name), "%s", "Prophet Brass");
        p->synth_mode = SYNTH_SUBTRACTIVE;
        p->osc1_wave = WAVE_SAW;
        p->osc2_wave = WAVE_SAW;
        p->osc_mix = 0.5f;
        p->osc2_detune = 0.12f;
        p->unison_voices = 3;
        p->unison_detune = 10.0f;
        p->filter_type = FILTER_LOWPASS;
        p->filter_cutoff = 600.0f;  /* dark baseline */
        p->filter_resonance = 1.8f;  /* slight edge, not harsh */
        p->filter_env_depth = 4000.0f;  /* big sweep for brass bite */
        p->amp_env = (sq_adsr_params_t){0.008f, 2.3f, 0.0f, 0.25f};
        p->filter_env = (sq_adsr_params_t){0.005f, 0.9f, 0.0f, 0.15f};
        p->lfo = (sq_lfo_t){WAVE_SINE, 0.0f, 0.0f, LFO_DEST_NONE, 0.0};
    }

    /* Preset 19: "ARP Lead" — ARP 2600-style aggressive lead
     * Square+saw for harmonically rich tone, resonance at 2.5 (not 5.0),
     * osc2 at 7 semitones (fifth) for power chord character */
    {
        sq_synth_preset_t *p = &engine->synth_presets[19];
        memset(p, 0, sizeof(*p));
        snprintf(p->name, sizeof(p->name), "%s", "ARP Lead");
        p->synth_mode = SYNTH_SUBTRACTIVE;
        p->osc1_wave = WAVE_SQUARE;
        p->osc2_wave = WAVE_SAW;
        p->osc_mix = 0.5f;
        p->osc2_detune = 7.0f; /* fifth for power */
        p->unison_voices = 1;
        p->filter_type = FILTER_LOWPASS;
        p->filter_cutoff = 1000.0f;  /* moderate, env gives attack bite */
        p->filter_resonance = 2.5f;  /* presence, not pain */
        p->filter_env_depth = 3000.0f;  /* attack bite from envelope */
        p->amp_env = (sq_adsr_params_t){0.005f, 2.6f, 0.0f, 0.15f};
        p->filter_env = (sq_adsr_params_t){0.003f, 0.9f, 0.0f, 0.1f};
        p->lfo = (sq_lfo_t){WAVE_SINE, 5.5f, 0.06f, LFO_DEST_PITCH, 0.0};
    }

    /* Preset 20: "SH Noise" — SH-101 noise percussion / hi-hat
     * Heavy detune (40 cents) on 3 voices creates noise-like texture,
     * HP filter sweeps down on attack for snare/hat character,
     * longer decay (0.15s) for body */
    {
        sq_synth_preset_t *p = &engine->synth_presets[20];
        memset(p, 0, sizeof(*p));
        snprintf(p->name, sizeof(p->name), "%s", "SH Noise");
        p->synth_mode = SYNTH_SUBTRACTIVE;
        p->osc1_wave = WAVE_SAW;
        p->osc2_wave = WAVE_SQUARE;
        p->osc_mix = 0.5f;
        p->osc2_detune = 0.5f;
        p->unison_voices = 3;
        p->unison_detune = 40.0f; /* heavy detune = noise-like */
        p->filter_type = FILTER_HIGHPASS;
        p->filter_cutoff = 2000.0f;
        p->filter_resonance = 4.0f;  /* less extreme than 6.0 */
        p->filter_env_depth = -1500.0f;
        p->amp_env = (sq_adsr_params_t){0.001f, 0.15f, 0.0f, 0.04f};
        p->filter_env = (sq_adsr_params_t){0.001f, 0.1f, 0.0f, 0.03f};
        p->lfo = (sq_lfo_t){WAVE_SINE, 0.0f, 0.0f, LFO_DEST_NONE, 0.0};
    }

    /* Preset 21: "Reese Bass" — DnB reese: two detuned saws, dark and moving
     * LOW cutoff (300Hz), moderate resonance (2.0), slow LFO on filter
     * for the signature reese movement. Higher sustain for held notes. */
    {
        sq_synth_preset_t *p = &engine->synth_presets[21];
        memset(p, 0, sizeof(*p));
        snprintf(p->name, sizeof(p->name), "%s", "Reese Bass");
        p->synth_mode = SYNTH_SUBTRACTIVE;
        p->osc1_wave = WAVE_SAW;
        p->osc2_wave = WAVE_SAW;
        p->osc_mix = 0.5f;  /* equal mix of both saws */
        p->osc2_detune = 0.12f;  /* detune creates the movement */
        p->unison_voices = 2;  /* keep it tight for bass */
        p->unison_detune = 6.0f;
        p->filter_type = FILTER_LOWPASS;
        p->filter_cutoff = 300.0f;  /* dark: 200-400Hz range */
        p->filter_resonance = 2.0f;  /* moderate resonance for edge */
        p->filter_env_depth = 800.0f;  /* subtle attack brightness */
        p->amp_env = (sq_adsr_params_t){0.01f, 2.6f, 0.0f, 0.3f};
        p->filter_env = (sq_adsr_params_t){0.01f, 1.2f, 0.0f, 0.3f};
        p->lfo = (sq_lfo_t){WAVE_SINE, 0.25f, 0.2f, LFO_DEST_FILTER, 0.0};
    }

    /* Preset 22: "DX Piano" — FM acoustic piano, brighter attack than EPiano
     * Two-pair algo: warm body + bright hammer attack that decays fast,
     * longer overall decay (2s) for natural piano ring */
    {
        sq_synth_preset_t *p = &engine->synth_presets[22];
        memset(p, 0, sizeof(*p));
        snprintf(p->name, sizeof(p->name), "%s", "DX Piano");
        p->synth_mode = SYNTH_FM;
        p->fm_algorithm = 2; /* two pairs */
        p->amp_env = (sq_adsr_params_t){0.001f, 2.5f, 0.0f, 1.0f};
        p->fm_ops[0] = (sq_fm_operator_t){1.0f, 1.0f, 0.0f,
            {0.001f, 2.0f, 0.15f, 1.0f}};
        p->fm_ops[1] = (sq_fm_operator_t){1.0f, 0.5f, 0.0f,
            {0.001f, 0.3f, 0.0f, 0.15f}};
        p->fm_ops[2] = (sq_fm_operator_t){3.0f, 0.4f, 0.0f,
            {0.001f, 0.6f, 0.0f, 0.25f}};
        p->fm_ops[3] = (sq_fm_operator_t){7.0f, 0.2f, 0.0f,
            {0.001f, 0.12f, 0.0f, 0.06f}};
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

    /* Preset 24: "Hoover" — classic rave hoover, menacing and wide
     * Detuned saw stack (5 voices), pitch LFO gives the swooping character,
     * moderate cutoff, filter env for attack punch */
    {
        sq_synth_preset_t *p = &engine->synth_presets[24];
        memset(p, 0, sizeof(*p));
        snprintf(p->name, sizeof(p->name), "%s", "Hoover");
        p->synth_mode = SYNTH_SUBTRACTIVE;
        p->osc1_wave = WAVE_SAW;
        p->osc2_wave = WAVE_SAW;
        p->osc_mix = 0.5f;
        p->osc2_detune = 5.0f; /* major third up for chord character */
        p->unison_voices = 5;
        p->unison_detune = 25.0f;
        p->filter_type = FILTER_LOWPASS;
        p->filter_cutoff = 1200.0f;  /* darker baseline */
        p->filter_resonance = 1.8f;  /* less harsh */
        p->filter_env_depth = 3000.0f;  /* moderate sweep */
        p->amp_env = (sq_adsr_params_t){0.01f, 2.5f, 0.0f, 0.5f};
        p->filter_env = (sq_adsr_params_t){0.01f, 1.3f, 0.0f, 0.4f};
        p->lfo = (sq_lfo_t){WAVE_SINE, 2.5f, 0.15f, LFO_DEST_PITCH, 0.0};
    }

    /* Preset 25: "Stab" — house/techno chord stab with body
     * Bandpass for honky character, decay 0.15s (not 0.08 = click),
     * resonance 3.0 (not 5.0), moderate env depth for punch */
    {
        sq_synth_preset_t *p = &engine->synth_presets[25];
        memset(p, 0, sizeof(*p));
        snprintf(p->name, sizeof(p->name), "%s", "Stab");
        p->synth_mode = SYNTH_SUBTRACTIVE;
        p->osc1_wave = WAVE_SAW;
        p->osc2_wave = WAVE_SQUARE;
        p->osc_mix = 0.4f;
        p->osc2_detune = 0.05f;
        p->unison_voices = 3;
        p->unison_detune = 8.0f;
        p->filter_type = FILTER_BANDPASS;
        p->filter_cutoff = 1000.0f;
        p->filter_resonance = 3.0f;  /* honky but not harsh */
        p->filter_env_depth = 2000.0f;
        p->amp_env = (sq_adsr_params_t){0.001f, 0.15f, 0.0f, 0.06f};
        p->filter_env = (sq_adsr_params_t){0.001f, 0.1f, 0.0f, 0.04f};
        p->lfo = (sq_lfo_t){WAVE_SINE, 0.0f, 0.0f, LFO_DEST_NONE, 0.0};
    }

    /* ── More Classic Synths ─────────────────────────────────────────── */

    /* Preset 26: "CS-80 Brass" — Blade Runner brass, cinematic and lush
     * Slow attack (0.03s) for brass swell, filter env opens warmly,
     * 5 unison for width, vibrato for expression */
    {
        sq_synth_preset_t *p = &engine->synth_presets[26];
        memset(p, 0, sizeof(*p));
        snprintf(p->name, sizeof(p->name), "%s", "CS-80 Brass");
        p->synth_mode = SYNTH_SUBTRACTIVE;
        p->osc1_wave = WAVE_SAW;
        p->osc2_wave = WAVE_SAW;  /* both saws for Blade Runner character */
        p->osc_mix = 0.5f;
        p->osc2_detune = 0.08f;
        p->unison_voices = 5;
        p->unison_detune = 18.0f;  /* wider for cinematic feel */
        p->filter_type = FILTER_LOWPASS;
        p->filter_cutoff = 800.0f;  /* darker, filter env opens it */
        p->filter_resonance = 1.5f;  /* smooth, not edgy */
        p->filter_env_depth = 3500.0f;  /* moderate brass sweep */
        p->amp_env = (sq_adsr_params_t){0.03f, 2.3f, 0.0f, 0.5f};
        p->filter_env = (sq_adsr_params_t){0.02f, 1.1f, 0.0f, 0.35f};
        p->lfo = (sq_lfo_t){WAVE_SINE, 5.0f, 0.05f, LFO_DEST_PITCH, 0.0};
    }

    /* Preset 27: "MS-20 Growl" — Korg MS-20 aggressive growl
     * HP filter with high resonance IS the MS-20 character,
     * resonance 12 (not 15 = unstable), filter env creates the growl */
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
        p->filter_resonance = 12.0f;  /* MS-20 HP character, aggressive */
        p->filter_env_depth = 2500.0f;
        p->amp_env = (sq_adsr_params_t){0.005f, 2.2f, 0.0f, 0.2f};
        p->filter_env = (sq_adsr_params_t){0.002f, 0.6f, 0.0f, 0.12f};
        p->lfo = (sq_lfo_t){WAVE_SQUARE, 6.0f, 0.04f, LFO_DEST_FILTER, 0.0};
    }

    /* Preset 28: "Poly-6 Keys" — Korg Polysix keys, warm and intimate
     * Single saw through chorus (3 unison), lower cutoff for warmth,
     * moderate decay, sustain for held chords */
    {
        sq_synth_preset_t *p = &engine->synth_presets[28];
        memset(p, 0, sizeof(*p));
        snprintf(p->name, sizeof(p->name), "%s", "Poly-6 Keys");
        p->synth_mode = SYNTH_SUBTRACTIVE;
        p->osc1_wave = WAVE_SAW;
        p->osc2_wave = WAVE_SAW;
        p->osc_mix = 0.0f;  /* single oscillator, Polysix style */
        p->unison_voices = 3;
        p->unison_detune = 10.0f;  /* chorus spread */
        p->filter_type = FILTER_LOWPASS;
        p->filter_cutoff = 1800.0f;  /* warm, not bright */
        p->filter_resonance = 1.5f;  /* gentle */
        p->filter_env_depth = 1000.0f;
        p->amp_env = (sq_adsr_params_t){0.005f, 2.2f, 0.0f, 0.5f};
        p->filter_env = (sq_adsr_params_t){0.005f, 1.0f, 0.0f, 0.35f};
        p->lfo = (sq_lfo_t){WAVE_TRIANGLE, 0.25f, 0.1f, LFO_DEST_FILTER, 0.0};
    }

    /* Preset 29: "JP-8 Pad" — Roland Jupiter-8 lush evolving pad
     * Max unison (7) at 22 cents for massive width, saw+saw for richness,
     * cutoff under 1000 for warmth, slow LFO filter movement */
    {
        sq_synth_preset_t *p = &engine->synth_presets[29];
        memset(p, 0, sizeof(*p));
        snprintf(p->name, sizeof(p->name), "%s", "JP-8 Pad");
        p->synth_mode = SYNTH_SUBTRACTIVE;
        p->osc1_wave = WAVE_SAW;
        p->osc2_wave = WAVE_SAW;  /* both saws for full spectrum */
        p->osc_mix = 0.5f;
        p->osc2_detune = 0.08f;
        p->unison_voices = 7;
        p->unison_detune = 22.0f;  /* wider for massive image */
        p->filter_type = FILTER_LOWPASS;
        p->filter_cutoff = 900.0f;  /* warm, beauty is in the chorus */
        p->filter_resonance = 0.8f;  /* no resonance, pure warmth */
        p->filter_env_depth = 500.0f;  /* subtle brightness swell */
        p->amp_env = (sq_adsr_params_t){1.5f, 3.2f, 0.0f, 2.5f};
        p->filter_env = (sq_adsr_params_t){1.2f, 2.0f, 0.0f, 2.0f};
        p->lfo = (sq_lfo_t){WAVE_SINE, 0.12f, 0.15f, LFO_DEST_FILTER, 0.0};
    }

    /* Preset 30: "Sub 37" — Moog Sub 37 mono lead, fat and present
     * Square+saw sub octave, moderate cutoff, filter env for bite,
     * sustain 0.75 for held lead notes, subtle pitch vibrato */
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
        p->filter_cutoff = 800.0f;  /* darker baseline */
        p->filter_resonance = 2.5f;  /* reduced from 3.5 */
        p->filter_env_depth = 3000.0f;  /* moderate attack bite */
        p->amp_env = (sq_adsr_params_t){0.003f, 2.5f, 0.0f, 0.2f};
        p->filter_env = (sq_adsr_params_t){0.003f, 0.9f, 0.0f, 0.15f};
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
        p->amp_env = (sq_adsr_params_t){0.001f, 3.0f, 0.0f, 0.05f};
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
        p->amp_env = (sq_adsr_params_t){0.6f, 2.9f, 0.0f, 1.0f};
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
        p->amp_env = (sq_adsr_params_t){0.05f, 2.5f, 0.0f, 0.3f};
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
        p->amp_env = (sq_adsr_params_t){0.02f, 2.4f, 0.0f, 0.2f};
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

    /* Preset 38: "WT Glass" — crystalline glass tones with long ring
     * Harmonics bank starting bright, env sweeps down to fundamental,
     * long decay (2s) and release for natural glass resonance */
    {
        sq_synth_preset_t *p = &engine->synth_presets[38];
        memset(p, 0, sizeof(*p));
        snprintf(p->name, sizeof(p->name), "%s", "WT Glass");
        p->synth_mode = SYNTH_WAVETABLE;
        p->wt_bank_index = 1;  /* Harmonics */
        p->wt_position = 0.8f;
        p->wt_env_depth = -0.6f;  /* sweeps from bright to dark */
        p->wt_lfo_depth = 0.05f;  /* subtle shimmer */
        p->amp_env = (sq_adsr_params_t){0.001f, 2.0f, 0.0f, 1.2f};
        p->filter_env = (sq_adsr_params_t){0.001f, 1.0f, 0.0f, 0.5f};
        p->lfo = (sq_lfo_t){WAVE_SINE, 3.0f, 0.03f, LFO_DEST_PITCH, 0.0};
    }

    /* Preset 39: "WT Wobble" — dubstep/bass music wobble bass
     * LFO on both wavetable position AND filter for dual-axis movement,
     * high sustain so it wobbles as long as note is held */
    {
        sq_synth_preset_t *p = &engine->synth_presets[39];
        memset(p, 0, sizeof(*p));
        snprintf(p->name, sizeof(p->name), "%s", "WT Wobble");
        p->synth_mode = SYNTH_WAVETABLE;
        p->wt_bank_index = 0;  /* Analog */
        p->wt_position = 0.2f;
        p->wt_env_depth = 0.0f;
        p->wt_lfo_depth = 0.7f;
        p->amp_env = (sq_adsr_params_t){0.01f, 2.7f, 0.0f, 0.3f};
        p->filter_env = (sq_adsr_params_t){0.01f, 1.3f, 0.0f, 0.3f};
        p->lfo = (sq_lfo_t){WAVE_SINE, 2.0f, 0.8f, LFO_DEST_FILTER, 0.0};
    }

    /* ── More Subtractive Classics ────────────────────────────────── */

    /* Preset 40: "Sync Lead" — hard-sync-like aggressive lead
     * Osc2 at octave+fifth creates sync-like harmonics,
     * resonance 2.5 (not 4.0), moderate env depth for attack edge */
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
        p->filter_cutoff = 1200.0f;  /* moderate, env adds brightness */
        p->filter_resonance = 2.5f;  /* present but not harsh */
        p->filter_env_depth = 3500.0f;  /* reduced sweep */
        p->amp_env = (sq_adsr_params_t){0.003f, 2.5f, 0.0f, 0.2f};
        p->filter_env = (sq_adsr_params_t){0.003f, 0.8f, 0.0f, 0.12f};
        p->lfo = (sq_lfo_t){WAVE_SINE, 5.0f, 0.04f, LFO_DEST_PITCH, 0.0};
    }

    /* Preset 41: "Whistle" — pure sine lead with expressive vibrato
     * Sine wave needs no filtering, vibrato depth 0.08 is musical,
     * slow attack (0.08s) simulates breath onset */
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
        p->filter_cutoff = 8000.0f;  /* open but not harsh */
        p->filter_resonance = 0.7f;
        p->filter_env_depth = 0.0f;
        p->amp_env = (sq_adsr_params_t){0.08f, 2.8f, 0.0f, 0.3f};
        p->filter_env = (sq_adsr_params_t){0.001f, 2.0f, 0.0f, 0.01f};
        p->lfo = (sq_lfo_t){WAVE_SINE, 5.5f, 0.08f, LFO_DEST_PITCH, 0.0};
    }

    /* Preset 42: "Tape Strings" — Mellotron-like lo-fi string ensemble
     * Heavy detune (25 cents) simulates tape wow/flutter,
     * lower cutoff for the muffled tape character, pitch wobble via LFO */
    {
        sq_synth_preset_t *p = &engine->synth_presets[42];
        memset(p, 0, sizeof(*p));
        snprintf(p->name, sizeof(p->name), "%s", "Tape Strings");
        p->synth_mode = SYNTH_SUBTRACTIVE;
        p->osc1_wave = WAVE_SAW;
        p->osc2_wave = WAVE_TRIANGLE;
        p->osc_mix = 0.5f;
        p->osc2_detune = 0.05f;
        p->unison_voices = 5;
        p->unison_detune = 25.0f;  /* tape wow/flutter */
        p->filter_type = FILTER_LOWPASS;
        p->filter_cutoff = 1500.0f;  /* muffled tape character */
        p->filter_resonance = 0.7f;  /* no resonance, tape is smooth */
        p->filter_env_depth = 300.0f;  /* barely any movement */
        p->amp_env = (sq_adsr_params_t){0.6f, 2.5f, 0.0f, 1.0f};
        p->filter_env = (sq_adsr_params_t){0.4f, 1.4f, 0.0f, 0.6f};
        p->lfo = (sq_lfo_t){WAVE_SINE, 3.5f, 0.04f, LFO_DEST_PITCH, 0.0};
    }

    /* Preset 43: "Detuned Saw" — massive EDM saw stack, wide and warm
     * 7 voices at 25 cents, cutoff 2000 (not 5000 = harsh),
     * useful for chords and drops, high sustain */
    {
        sq_synth_preset_t *p = &engine->synth_presets[43];
        memset(p, 0, sizeof(*p));
        snprintf(p->name, sizeof(p->name), "%s", "Detuned Saw");
        p->synth_mode = SYNTH_SUBTRACTIVE;
        p->osc1_wave = WAVE_SAW;
        p->osc2_wave = WAVE_SAW;
        p->osc_mix = 0.5f;
        p->osc2_detune = 0.15f;
        p->unison_voices = 7;
        p->unison_detune = 25.0f;
        p->filter_type = FILTER_LOWPASS;
        p->filter_cutoff = 2000.0f;  /* warm, not harsh */
        p->filter_resonance = 0.7f;  /* no resonance, pure width */
        p->filter_env_depth = 1000.0f;
        p->amp_env = (sq_adsr_params_t){0.01f, 2.8f, 0.0f, 0.5f};
        p->filter_env = (sq_adsr_params_t){0.01f, 1.4f, 0.0f, 0.3f};
        p->lfo = (sq_lfo_t){WAVE_SINE, 0.1f, 0.06f, LFO_DEST_FILTER, 0.0};
    }

    /* Preset 44: "Rezzy Pluck" — resonant pluck with character, not bleep
     * Resonance at 4.0 (not 10.0 = painful), env depth 2500 (not 7000),
     * decay 0.35s for body, 2 unison voices for width */
    {
        sq_synth_preset_t *p = &engine->synth_presets[44];
        memset(p, 0, sizeof(*p));
        snprintf(p->name, sizeof(p->name), "%s", "Rezzy Pluck");
        p->synth_mode = SYNTH_SUBTRACTIVE;
        p->osc1_wave = WAVE_SAW;
        p->osc2_wave = WAVE_SAW;
        p->osc_mix = 0.3f;
        p->osc2_detune = 0.06f;
        p->unison_voices = 2;  /* stereo width */
        p->unison_detune = 8.0f;
        p->filter_type = FILTER_LOWPASS;
        p->filter_cutoff = 350.0f;
        p->filter_resonance = 4.0f;  /* character but not painful */
        p->filter_env_depth = 2500.0f;  /* moderate sweep */
        p->amp_env = (sq_adsr_params_t){0.001f, 0.35f, 0.0f, 0.1f};
        p->filter_env = (sq_adsr_params_t){0.001f, 0.2f, 0.0f, 0.06f};
        p->lfo = (sq_lfo_t){WAVE_SINE, 0.0f, 0.0f, LFO_DEST_NONE, 0.0};
    }

    /* Preset 45: "Soft Keys" — warm mellow keys, Fender Rhodes feel
     * Triangle+sine for soft character, lower cutoff (2500) for warmth,
     * tremolo LFO for vintage character, good sustain for chords */
    {
        sq_synth_preset_t *p = &engine->synth_presets[45];
        memset(p, 0, sizeof(*p));
        snprintf(p->name, sizeof(p->name), "%s", "Soft Keys");
        p->synth_mode = SYNTH_SUBTRACTIVE;
        p->osc1_wave = WAVE_TRIANGLE;
        p->osc2_wave = WAVE_SINE;
        p->osc_mix = 0.4f;
        p->osc2_detune = 0.03f;  /* tiny detune for life */
        p->unison_voices = 3;
        p->unison_detune = 6.0f;
        p->filter_type = FILTER_LOWPASS;
        p->filter_cutoff = 2500.0f;  /* warm but still defined */
        p->filter_resonance = 0.8f;
        p->filter_env_depth = 800.0f;
        p->amp_env = (sq_adsr_params_t){0.008f, 2.0f, 0.0f, 0.5f};
        p->filter_env = (sq_adsr_params_t){0.005f, 0.9f, 0.0f, 0.3f};
        p->lfo = (sq_lfo_t){WAVE_SINE, 4.0f, 0.03f, LFO_DEST_AMP, 0.0};
    }

    /* Preset 46: "Sub Bass" — 808-style deep sub, weight and presence
     * Pure sine for sub-frequency weight, minimal harmonics,
     * LONG decay (2s) — the weight comes from sustaining low frequencies,
     * filter env click on attack for transient definition */
    {
        sq_synth_preset_t *p = &engine->synth_presets[46];
        memset(p, 0, sizeof(*p));
        snprintf(p->name, sizeof(p->name), "%s", "Sub Bass");
        p->synth_mode = SYNTH_SUBTRACTIVE;
        p->osc1_wave = WAVE_SINE;
        p->osc2_wave = WAVE_TRIANGLE;  /* slight harmonics for definition */
        p->osc_mix = 0.1f;  /* mostly sine, touch of triangle */
        p->unison_voices = 1;
        p->filter_type = FILTER_LOWPASS;
        p->filter_cutoff = 180.0f;  /* very dark, sub-only */
        p->filter_resonance = 0.8f;  /* no resonance, clean sub */
        p->filter_env_depth = 600.0f;  /* slight click on attack */
        p->amp_env = (sq_adsr_params_t){0.003f, 2.0f, 0.0f, 0.5f};
        p->filter_env = (sq_adsr_params_t){0.001f, 0.1f, 0.0f, 0.05f};
        p->lfo = (sq_lfo_t){WAVE_SINE, 0.0f, 0.0f, LFO_DEST_NONE, 0.0};
    }

    /* Preset 47: "Reso Sweep" — slow filter sweep pad, dramatic
     * Resonance at 5.0 (reduced from 8.0) for musicality,
     * very slow filter env attack (3s) creates cinematic sweep,
     * 5 unison for width to support the resonant peak */
    {
        sq_synth_preset_t *p = &engine->synth_presets[47];
        memset(p, 0, sizeof(*p));
        snprintf(p->name, sizeof(p->name), "%s", "Reso Sweep");
        p->synth_mode = SYNTH_SUBTRACTIVE;
        p->osc1_wave = WAVE_SAW;
        p->osc2_wave = WAVE_SAW;
        p->osc_mix = 0.5f;
        p->osc2_detune = 0.08f;
        p->unison_voices = 5;  /* more width to support the sweep */
        p->unison_detune = 15.0f;
        p->filter_type = FILTER_LOWPASS;
        p->filter_cutoff = 200.0f;
        p->filter_resonance = 5.0f;  /* dramatic but not painful */
        p->filter_env_depth = 4000.0f;  /* wide sweep range */
        p->amp_env = (sq_adsr_params_t){0.8f, 3.0f, 0.0f, 1.5f};
        p->filter_env = (sq_adsr_params_t){3.0f, 1.6f, 0.0f, 2.0f};
        p->lfo = (sq_lfo_t){WAVE_SINE, 0.1f, 0.08f, LFO_DEST_FILTER, 0.0};
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

    /* Preset 49: "WT Choir" — ethereal choir pad, slow and wide
     * Formant bank with slow LFO sweeping vowel positions,
     * long attack (1.2s) for choir swell, high sustain */
    {
        sq_synth_preset_t *p = &engine->synth_presets[49];
        memset(p, 0, sizeof(*p));
        snprintf(p->name, sizeof(p->name), "%s", "WT Choir");
        p->synth_mode = SYNTH_WAVETABLE;
        p->wt_bank_index = 3;  /* Formant */
        p->wt_position = 0.2f;
        p->wt_env_depth = 0.1f;  /* subtle env sweep */
        p->wt_lfo_depth = 0.35f;
        p->amp_env = (sq_adsr_params_t){1.2f, 3.0f, 0.0f, 2.0f};
        p->filter_env = (sq_adsr_params_t){0.8f, 1.5f, 0.0f, 1.0f};
        p->lfo = (sq_lfo_t){WAVE_SINE, 0.15f, 0.8f, LFO_DEST_FILTER, 0.0};
    }

    /* Preset 50: "Trap 808" — MASSIVE sustaining 808 boom
     * Pure sine for low-end weight, triangle adds slight definition,
     * LONG decay (3s) is essential — 808 weight = long sustain time,
     * filter click on attack for transient punch, then pure sub */
    {
        sq_synth_preset_t *p = &engine->synth_presets[50];
        memset(p, 0, sizeof(*p));
        snprintf(p->name, sizeof(p->name), "%s", "Trap 808");
        p->synth_mode = SYNTH_SUBTRACTIVE;
        p->osc1_wave = WAVE_SINE;
        p->osc2_wave = WAVE_TRIANGLE;
        p->osc_mix = 0.12f;  /* mostly sine, hint of triangle */
        p->unison_voices = 1;
        p->filter_type = FILTER_LOWPASS;
        p->filter_cutoff = 150.0f;  /* very low, sub territory */
        p->filter_resonance = 0.7f;  /* clean, no resonance */
        p->filter_env_depth = 800.0f;  /* attack click, not brightness */
        p->amp_env = (sq_adsr_params_t){0.001f, 3.0f, 0.0f, 1.5f};
        p->filter_env = (sq_adsr_params_t){0.001f, 0.1f, 0.0f, 0.3f};
        p->lfo = (sq_lfo_t){WAVE_SINE, 0.0f, 0.0f, LFO_DEST_NONE, 0.0};
    }

    /* Preset 51: "Trap Lead" — dark menacing trap melody lead
     * Detuned for width, dark filter with slow LFO movement,
     * high sustain (0.75) for held melodic lines */
    {
        sq_synth_preset_t *p = &engine->synth_presets[51];
        memset(p, 0, sizeof(*p));
        snprintf(p->name, sizeof(p->name), "%s", "Trap Lead");
        p->synth_mode = SYNTH_SUBTRACTIVE;
        p->osc1_wave = WAVE_SAW;
        p->osc2_wave = WAVE_SQUARE;  /* saw+square for more character */
        p->osc_mix = 0.45f;
        p->osc2_detune = 0.1f;
        p->unison_voices = 3;
        p->unison_detune = 12.0f;
        p->filter_type = FILTER_LOWPASS;
        p->filter_cutoff = 500.0f;  /* dark and menacing */
        p->filter_resonance = 1.8f;
        p->filter_env_depth = 2000.0f;
        p->amp_env = (sq_adsr_params_t){0.015f, 2.6f, 0.0f, 0.5f};
        p->filter_env = (sq_adsr_params_t){0.01f, 1.4f, 0.0f, 0.4f};
        p->lfo = (sq_lfo_t){WAVE_SINE, 0.15f, 0.1f, LFO_DEST_FILTER, 0.0};
    }

    /* Preset 52: "Dark Pad" — Stranger Things-style ominous atmosphere
     * Square+saw for hollow darkness, cutoff under 500Hz,
     * max unison for massive width, slow LFO = ominous movement */
    {
        sq_synth_preset_t *p = &engine->synth_presets[52];
        memset(p, 0, sizeof(*p));
        snprintf(p->name, sizeof(p->name), "%s", "Dark Pad");
        p->synth_mode = SYNTH_SUBTRACTIVE;
        p->osc1_wave = WAVE_SQUARE;  /* hollow, ominous character */
        p->osc2_wave = WAVE_SAW;
        p->osc_mix = 0.45f;
        p->osc2_detune = 0.08f;
        p->unison_voices = 7;
        p->unison_detune = 22.0f;
        p->filter_type = FILTER_LOWPASS;
        p->filter_cutoff = 450.0f;  /* very dark, ominous */
        p->filter_resonance = 1.0f;
        p->filter_env_depth = 400.0f;  /* barely opens */
        p->amp_env = (sq_adsr_params_t){0.8f, 2.5f, 0.0f, 1.0f};
        p->filter_env = (sq_adsr_params_t){1.0f, 2.0f, 0.0f, 0.8f};
        p->lfo = (sq_lfo_t){WAVE_TRIANGLE, 0.15f, 0.12f, LFO_DEST_FILTER, 0.0};
    }

    /* Preset 53: "Dark Arp" — sinister pluck for arpeggios, body not bleep
     * KEY anti-bleep rules: decay 0.45s, resonance 1.5, env depth 1800,
     * 2 unison voices for stereo body */
    {
        sq_synth_preset_t *p = &engine->synth_presets[53];
        memset(p, 0, sizeof(*p));
        snprintf(p->name, sizeof(p->name), "%s", "Dark Arp");
        p->synth_mode = SYNTH_SUBTRACTIVE;
        p->osc1_wave = WAVE_SAW;
        p->osc2_wave = WAVE_SQUARE;
        p->osc_mix = 0.4f;
        p->osc2_detune = 0.04f;
        p->unison_voices = 3;  /* wider for arp shimmer */
        p->unison_detune = 10.0f;
        p->filter_type = FILTER_LOWPASS;
        p->filter_cutoff = 350.0f;  /* dark baseline */
        p->filter_resonance = 1.5f;  /* under 2.0: body not bleep */
        p->filter_env_depth = 1800.0f;  /* moderate, not extreme */
        p->amp_env = (sq_adsr_params_t){0.003f, 0.45f, 0.0f, 0.2f};
        p->filter_env = (sq_adsr_params_t){0.001f, 0.35f, 0.0f, 0.15f};
        p->lfo = (sq_lfo_t){WAVE_SINE, 0.0f, 0.0f, LFO_DEST_NONE, 0.0};
    }

    /* Preset 54: "Dark Bass" — menacing growling darkwave bass
     * Saw+square for harmonic richness, low cutoff (300Hz),
     * LFO on filter for subtle growl, sustain 0.6 for held notes */
    {
        sq_synth_preset_t *p = &engine->synth_presets[54];
        memset(p, 0, sizeof(*p));
        snprintf(p->name, sizeof(p->name), "%s", "Dark Bass");
        p->synth_mode = SYNTH_SUBTRACTIVE;
        p->osc1_wave = WAVE_SAW;
        p->osc2_wave = WAVE_SQUARE;
        p->osc_mix = 0.5f;
        p->osc2_detune = -0.06f;  /* slight detune down for weight */
        p->unison_voices = 2;
        p->unison_detune = 5.0f;
        p->filter_type = FILTER_LOWPASS;
        p->filter_cutoff = 300.0f;  /* dark, in 200-600Hz bass range */
        p->filter_resonance = 2.0f;  /* slight edge */
        p->filter_env_depth = 1200.0f;  /* moderate attack punch */
        p->amp_env = (sq_adsr_params_t){0.003f, 2.3f, 0.0f, 0.2f};
        p->filter_env = (sq_adsr_params_t){0.002f, 0.7f, 0.0f, 0.12f};
        p->lfo = (sq_lfo_t){WAVE_SINE, 0.3f, 0.1f, LFO_DEST_FILTER, 0.0};
    }

    /* Preset 55: "Synth Pad" — lush warm synthwave pad
     * Max unison (7) at 20 cents, cutoff 1000Hz for warmth,
     * NO resonance (pure Juno-like warmth), slow LFO filter movement */
    {
        sq_synth_preset_t *p = &engine->synth_presets[55];
        memset(p, 0, sizeof(*p));
        snprintf(p->name, sizeof(p->name), "%s", "Synth Pad");
        p->synth_mode = SYNTH_SUBTRACTIVE;
        p->osc1_wave = WAVE_SAW;
        p->osc2_wave = WAVE_SAW;
        p->osc_mix = 0.5f;
        p->osc2_detune = 0.08f;
        p->unison_voices = 7;
        p->unison_detune = 20.0f;
        p->filter_type = FILTER_LOWPASS;
        p->filter_cutoff = 1000.0f;  /* warm, not bright */
        p->filter_resonance = 0.7f;  /* no resonance */
        p->filter_env_depth = 400.0f;  /* barely opens */
        p->amp_env = (sq_adsr_params_t){1.2f, 3.2f, 0.0f, 2.5f};
        p->filter_env = (sq_adsr_params_t){1.5f, 2.2f, 0.0f, 2.0f};
        p->lfo = (sq_lfo_t){WAVE_TRIANGLE, 0.12f, 0.1f, LFO_DEST_FILTER, 0.0};
    }

    /* Preset 56: "Synth Lead" — expressive synthwave lead
     * Saw+square for rich harmonics, moderate filter for presence,
     * sustain 0.8 for held melody lines, subtle vibrato for expression */
    {
        sq_synth_preset_t *p = &engine->synth_presets[56];
        memset(p, 0, sizeof(*p));
        snprintf(p->name, sizeof(p->name), "%s", "Synth Lead");
        p->synth_mode = SYNTH_SUBTRACTIVE;
        p->osc1_wave = WAVE_SAW;
        p->osc2_wave = WAVE_SQUARE;
        p->osc_mix = 0.4f;
        p->osc2_detune = 0.08f;
        p->unison_voices = 3;
        p->unison_detune = 10.0f;
        p->filter_type = FILTER_LOWPASS;
        p->filter_cutoff = 1400.0f;  /* moderate, cuts through mix */
        p->filter_resonance = 1.5f;
        p->filter_env_depth = 1800.0f;  /* attack bite */
        p->amp_env = (sq_adsr_params_t){0.01f, 2.6f, 0.0f, 0.4f};
        p->filter_env = (sq_adsr_params_t){0.008f, 1.3f, 0.0f, 0.3f};
        p->lfo = (sq_lfo_t){WAVE_SINE, 5.0f, 0.05f, LFO_DEST_PITCH, 0.0};
    }

    /* Preset 57: "Synth Bass" — fat synthwave bass, punchy and warm
     * Two saws slightly detuned down, cutoff 350Hz for warmth,
     * filter env gives attack definition, longer decay (0.5s) */
    {
        sq_synth_preset_t *p = &engine->synth_presets[57];
        memset(p, 0, sizeof(*p));
        snprintf(p->name, sizeof(p->name), "%s", "Synth Bass");
        p->synth_mode = SYNTH_SUBTRACTIVE;
        p->osc1_wave = WAVE_SAW;
        p->osc2_wave = WAVE_SAW;
        p->osc_mix = 0.5f;
        p->osc2_detune = -0.08f;
        p->unison_voices = 2;
        p->unison_detune = 4.0f;
        p->filter_type = FILTER_LOWPASS;
        p->filter_cutoff = 350.0f;  /* dark and warm */
        p->filter_resonance = 1.8f;
        p->filter_env_depth = 1500.0f;  /* attack punch */
        p->amp_env = (sq_adsr_params_t){0.002f, 1.9f, 0.0f, 0.15f};
        p->filter_env = (sq_adsr_params_t){0.001f, 0.4f, 0.0f, 0.1f};
        p->lfo = (sq_lfo_t){WAVE_SINE, 0.0f, 0.0f, LFO_DEST_NONE, 0.0};
    }

    /* Preset 58: "Retro Arp" — warm 80s arpeggio pluck, body not bleep
     * Triangle+saw for softer attack, cutoff 1200 (not too bright),
     * decay 0.4s gives ring and body, resonance 1.0 = warm not harsh,
     * 3 unison voices for retro chorus width */
    {
        sq_synth_preset_t *p = &engine->synth_presets[58];
        memset(p, 0, sizeof(*p));
        snprintf(p->name, sizeof(p->name), "%s", "Retro Arp");
        p->synth_mode = SYNTH_SUBTRACTIVE;
        p->osc1_wave = WAVE_TRIANGLE;
        p->osc2_wave = WAVE_SAW;
        p->osc_mix = 0.35f;
        p->osc2_detune = 0.06f;
        p->unison_voices = 3;  /* more chorus for retro feel */
        p->unison_detune = 12.0f;
        p->filter_type = FILTER_LOWPASS;
        p->filter_cutoff = 1200.0f;  /* warm, not harsh */
        p->filter_resonance = 1.0f;  /* smooth */
        p->filter_env_depth = 1200.0f;  /* moderate sweep */
        p->amp_env = (sq_adsr_params_t){0.003f, 0.4f, 0.0f, 0.18f};
        p->filter_env = (sq_adsr_params_t){0.002f, 0.3f, 0.0f, 0.12f};
        p->lfo = (sq_lfo_t){WAVE_SINE, 0.0f, 0.0f, LFO_DEST_NONE, 0.0};
    }

    engine->num_synth_presets = 59;
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

int synth_trigger(sq_engine_t *engine, int preset_index,
                  float velocity, int pitch_offset,
                  float volume, float pan, uint8_t note)
{
    if (preset_index < 0 || (uint32_t)preset_index >= engine->num_synth_presets)
        return -1;

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

    return vi;
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
