/*
 * envelope.c — ADSR envelope generator and LFO.
 *
 * ADSR (Attack-Decay-Sustain-Release) shapes the amplitude of a sound:
 *
 *    1.0  ─────╲
 *              │ ╲  Decay
 *    Attack    │  ╲─────────── Sustain level
 *       ╱      │              ╲
 *      ╱       │               ╲ Release
 *    0.0 ──────┴────────────────╲──────
 *         note-on            note-off
 *
 * The rate for each stage = 1.0 / (time_in_seconds * sample_rate)
 * So each sample, we add `rate` to the level during attack,
 * or subtract it during decay/release.
 *
 * LFO generates a slow oscillation (-1.0 to 1.0) used to modulate
 * pitch, filter cutoff, or amplitude for movement in the sound.
 */

#include "engine/envelope.h"
#include <math.h>

/* ─── ADSR Envelope ──────────────────────────────────────────────────────── */

void envelope_init(sq_envelope_t *env)
{
    env->stage = ENV_IDLE;
    env->level = 0.0f;
    env->rate  = 0.0f;
}

void envelope_trigger(sq_envelope_t *env, const sq_adsr_params_t *params,
                      uint32_t sample_rate)
{
    env->stage = ENV_ATTACK;
    env->level = 0.0f;

    /* Rate = how much level changes per sample.
     * Attack goes from 0 → 1 in `attack` seconds.
     * Minimum time clamped to 0.001s to avoid division by zero. */
    float attack_time = params->attack > 0.001f ? params->attack : 0.001f;
    env->rate = 1.0f / (attack_time * (float)sample_rate);
}

void envelope_release(sq_envelope_t *env, const sq_adsr_params_t *params,
                      uint32_t sample_rate)
{
    if (env->stage == ENV_IDLE) return;

    env->stage = ENV_RELEASE;

    /* Release goes from current level → 0 in `release` seconds */
    float release_time = params->release > 0.001f ? params->release : 0.001f;
    env->rate = env->level / (release_time * (float)sample_rate);
}

float envelope_process(sq_envelope_t *env, const sq_adsr_params_t *params,
                       uint32_t sample_rate)
{
    switch (env->stage) {
    case ENV_IDLE:
        return 0.0f;

    case ENV_ATTACK:
        env->level += env->rate;
        if (env->level >= 1.0f) {
            env->level = 1.0f;
            env->stage = ENV_DECAY;
            /* Decay goes from 1.0 → sustain in `decay` seconds */
            float decay_time = params->decay > 0.001f ? params->decay : 0.001f;
            env->rate = (1.0f - params->sustain) / (decay_time * (float)sample_rate);
        }
        return env->level;

    case ENV_DECAY:
        env->level -= env->rate;
        if (env->level <= params->sustain) {
            env->level = params->sustain;
            env->stage = ENV_SUSTAIN;
        }
        return env->level;

    case ENV_SUSTAIN:
        /* Hold at sustain level until note-off */
        return env->level;

    case ENV_RELEASE:
        env->level -= env->rate;
        if (env->level <= 0.0f) {
            env->level = 0.0f;
            env->stage = ENV_IDLE;
        }
        return env->level;
    }

    return 0.0f;
}

/* ─── LFO ────────────────────────────────────────────────────────────────── */

void lfo_init(sq_lfo_t *lfo)
{
    lfo->phase = 0.0;
}

float lfo_process(sq_lfo_t *lfo, uint32_t sample_rate,
                  const sq_wavetables_t *wt)
{
    if (lfo->rate <= 0.0f || lfo->depth <= 0.0f)
        return 0.0f;

    /* Advance phase */
    double phase_inc = (double)lfo->rate / (double)sample_rate;
    lfo->phase += phase_inc;
    if (lfo->phase >= 1.0) lfo->phase -= 1.0;

    /* Generate LFO value based on waveform shape */
    float val = 0.0f;
    double p = lfo->phase;

    switch (lfo->waveform) {
    case WAVE_SINE:
        val = sinf((float)(p * 2.0 * 3.14159265358979));
        break;
    case WAVE_TRIANGLE:
        /* 0→0.25: 0→1, 0.25→0.75: 1→-1, 0.75→1: -1→0 */
        if (p < 0.5)
            val = (float)(4.0 * p - 1.0);
        else
            val = (float)(3.0 - 4.0 * p);
        break;
    case WAVE_SQUARE:
        val = (p < 0.5) ? 1.0f : -1.0f;
        break;
    case WAVE_SAW:
        val = (float)(2.0 * p - 1.0);
        break;
    }

    (void)wt; /* wavetables not used for LFO — direct computation is fine */
    return val * lfo->depth;
}
