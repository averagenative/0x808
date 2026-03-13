/*
 * sampler.c — Polyphonic sample playback with pitch shifting.
 *
 * How it works:
 * - Each "voice" is a currently-playing instance of a sample.
 * - We maintain a pool of SQ_MAX_VOICES voices.
 * - When a sample is triggered, we find a free voice (or steal the oldest).
 * - Each voice has a fractional position that advances by `rate` per output frame.
 * - rate = 1.0 means normal pitch. rate = 2.0 means +12 semitones (double speed).
 * - We use Hermite interpolation between samples for smooth pitch shifting.
 */

#include "engine/sampler.h"
#include <math.h>
#include <string.h>

/* ─── Hermite interpolation ───────────────────────────────────────────────── *
 *
 * When pitch-shifting, our playback position falls between actual samples.
 * Linear interpolation (blending two neighbors) works but sounds "buzzy"
 * at large pitch shifts. Hermite uses 4 neighboring samples for a smoother
 * curve, which sounds much cleaner.
 *
 * y0 = sample before the pair
 * y1 = sample just before position
 * y2 = sample just after position
 * y3 = sample after the pair
 * frac = fractional position between y1 and y2 (0.0 to 1.0)
 */
static inline float hermite_interp(float y0, float y1, float y2, float y3, float frac)
{
    float c0 = y1;
    float c1 = 0.5f * (y2 - y0);
    float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * frac + c2) * frac + c1) * frac + c0;
}

/* ─── Find a free voice, or steal the oldest ──────────────────────────────── */

static int find_voice(sq_engine_t *engine)
{
    /* First pass: find an inactive voice */
    for (int i = 0; i < SQ_MAX_VOICES; i++) {
        if (!engine->voices[i].active) {
            return i;
        }
    }

    /* All voices active — steal the one that started earliest */
    int oldest = 0;
    uint64_t oldest_time = engine->voices[0].start_time;
    for (int i = 1; i < SQ_MAX_VOICES; i++) {
        if (engine->voices[i].start_time < oldest_time) {
            oldest_time = engine->voices[i].start_time;
            oldest = i;
        }
    }
    return oldest;
}

/* ─── Public API ──────────────────────────────────────────────────────────── */

void sampler_trigger(sq_engine_t *engine, int sample_index,
                     float velocity, int pitch_offset,
                     float volume, float pan)
{
    /* Bounds check: make sure sample exists */
    if (sample_index < 0 || (uint32_t)sample_index >= engine->num_samples)
        return;
    if (!engine->samples[sample_index].data)
        return;

    int vi = find_voice(engine);
    sq_voice_t *v = &engine->voices[vi];

    v->active       = true;
    v->sample_index = sample_index;
    v->position     = 0.0;
    v->velocity     = velocity;
    v->volume       = volume;
    v->pan          = pan;
    v->start_time   = engine->transport.sample_position;

    /*
     * Pitch shifting via playback rate:
     *   rate = 2^(semitones / 12)
     *
     * +12 semitones = 2.0 (double speed, one octave up)
     *  -12 semitones = 0.5 (half speed, one octave down)
     *    0 semitones = 1.0 (original pitch)
     */
    v->rate = pow(2.0, (double)pitch_offset / 12.0);
}

void sampler_render(sq_engine_t *engine, float *output, uint32_t num_frames)
{
    for (int vi = 0; vi < SQ_MAX_VOICES; vi++) {
        sq_voice_t *v = &engine->voices[vi];
        if (!v->active) continue;

        sq_sample_t *s = &engine->samples[v->sample_index];
        if (!s->data) {
            v->active = false;
            continue;
        }

        uint32_t total = s->num_frames;
        uint32_t ch    = s->num_channels;

        /* Compute gain: velocity * track volume */
        float gain = v->velocity * v->volume;

        /*
         * Compute left/right gain from pan.
         * pan = -1.0 (full left), 0.0 (center), 1.0 (full right)
         * Using constant-power pan law:
         *   left  = cos(pan * pi/4 + pi/4) ... simplified to linear for now
         */
        float left_gain  = gain * (1.0f - v->pan) * 0.5f;
        float right_gain = gain * (1.0f + v->pan) * 0.5f;

        for (uint32_t f = 0; f < num_frames; f++) {
            /* Check if we've reached the end of the sample */
            int pos = (int)v->position;
            if (pos >= (int)total - 1) {
                v->active = false;
                break;
            }

            /* Get the fractional part for interpolation */
            float frac = (float)(v->position - (double)pos);

            /*
             * Hermite interpolation needs 4 sample points:
             * y0 (before), y1 (at pos), y2 (next), y3 (after next)
             * Clamp indices to valid range.
             */
            int i0 = (pos > 0) ? pos - 1 : 0;
            int i1 = pos;
            int i2 = (pos + 1 < (int)total) ? pos + 1 : (int)total - 1;
            int i3 = (pos + 2 < (int)total) ? pos + 2 : (int)total - 1;

            float sample_l, sample_r;

            if (ch == 1) {
                /* Mono sample — use same value for L and R */
                float val = hermite_interp(
                    s->data[i0], s->data[i1],
                    s->data[i2], s->data[i3], frac);
                sample_l = val;
                sample_r = val;
            } else {
                /* Stereo sample — interleaved: [L0,R0,L1,R1,...] */
                sample_l = hermite_interp(
                    s->data[i0 * 2], s->data[i1 * 2],
                    s->data[i2 * 2], s->data[i3 * 2], frac);
                sample_r = hermite_interp(
                    s->data[i0 * 2 + 1], s->data[i1 * 2 + 1],
                    s->data[i2 * 2 + 1], s->data[i3 * 2 + 1], frac);
            }

            /* Add to output buffer (don't overwrite — voices are additive) */
            output[f * 2]     += sample_l * left_gain;
            output[f * 2 + 1] += sample_r * right_gain;

            /* Advance playback position by the pitch rate */
            v->position += v->rate;
        }
    }
}
