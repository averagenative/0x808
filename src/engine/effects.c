/*
 * effects.c — Audio effects: biquad filter, delay, reverb.
 */

#include "engine/effects.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ─── Biquad filter ──────────────────────────────────────────────────────── */

static void filter_compute_coeffs(sq_efx_filter_t *f, uint32_t sample_rate)
{
    if (f->cutoff == f->last_cutoff && f->resonance == f->last_resonance)
        return;

    float fc = f->cutoff;
    if (fc < 20.0f) fc = 20.0f;
    if (fc > (float)sample_rate * 0.45f) fc = (float)sample_rate * 0.45f;

    float Q = f->resonance;
    if (Q < 0.5f) Q = 0.5f;

    float w0 = 2.0f * (float)M_PI * fc / (float)sample_rate;
    float sinw = sinf(w0);
    float cosw = cosf(w0);
    float alpha = sinw / (2.0f * Q);

    float a0;
    switch (f->mode) {
    case EFX_FILTER_HP:
        f->b0 = (1.0f + cosw) / 2.0f;
        f->b1 = -(1.0f + cosw);
        f->b2 = (1.0f + cosw) / 2.0f;
        a0 = 1.0f + alpha;
        f->a1 = -2.0f * cosw;
        f->a2 = 1.0f - alpha;
        break;
    case EFX_FILTER_BP:
        f->b0 = alpha;
        f->b1 = 0.0f;
        f->b2 = -alpha;
        a0 = 1.0f + alpha;
        f->a1 = -2.0f * cosw;
        f->a2 = 1.0f - alpha;
        break;
    default: /* LP */
        f->b0 = (1.0f - cosw) / 2.0f;
        f->b1 = 1.0f - cosw;
        f->b2 = (1.0f - cosw) / 2.0f;
        a0 = 1.0f + alpha;
        f->a1 = -2.0f * cosw;
        f->a2 = 1.0f - alpha;
        break;
    }

    /* Normalize */
    f->b0 /= a0; f->b1 /= a0; f->b2 /= a0;
    f->a1 /= a0; f->a2 /= a0;

    f->last_cutoff = f->cutoff;
    f->last_resonance = f->resonance;
}

static void filter_process(sq_efx_filter_t *f, float *buf, uint32_t frames,
                           uint32_t sample_rate)
{
    filter_compute_coeffs(f, sample_rate);
    float wet = f->wet;

    for (uint32_t i = 0; i < frames; i++) {
        for (int ch = 0; ch < 2; ch++) {
            float in = buf[i * 2 + ch];

            /* Transposed direct form II */
            float y = f->b0 * in + f->z1[ch];
            f->z1[ch] = f->b1 * in - f->a1 * y + f->z2[ch];
            f->z2[ch] = f->b2 * in - f->a2 * y;

            buf[i * 2 + ch] = in + wet * (y - in);
        }
    }
}

/* ─── 3-band parametric EQ ──────────────────────────────────────────────────
 *
 * Robert Bristow-Johnson "Audio EQ Cookbook" coefficients. Each band is an
 * independent stereo biquad. Bands are processed in series. Coefficients
 * are recomputed only when the band's params actually change. */

static void eq_band_compute_coeffs(sq_eq_band_t *b, uint32_t sample_rate)
{
    if (b->type == b->last_type &&
        b->frequency == b->last_freq &&
        b->gain_db == b->last_gain &&
        b->q == b->last_q)
        return;

    float fc = b->frequency;
    if (fc < 20.0f) fc = 20.0f;
    if (fc > (float)sample_rate * 0.45f) fc = (float)sample_rate * 0.45f;

    float Q = b->q;
    if (Q < 0.1f) Q = 0.1f;
    if (Q > 10.0f) Q = 10.0f;

    float gain = b->gain_db;
    if (gain < -24.0f) gain = -24.0f;
    if (gain >  24.0f) gain =  24.0f;

    float A    = powf(10.0f, gain / 40.0f);  /* sqrt of linear gain */
    float w0   = 2.0f * (float)M_PI * fc / (float)sample_rate;
    float cosw = cosf(w0);
    float sinw = sinf(w0);
    float alpha = sinw / (2.0f * Q);
    float two_sqrtA_alpha = 2.0f * sqrtf(A) * alpha;

    float b0, b1, b2, a0, a1, a2;
    switch (b->type) {
    case EQ_BAND_LOW_SHELF:
        b0 =     A * ((A + 1) - (A - 1) * cosw + two_sqrtA_alpha);
        b1 = 2 * A * ((A - 1) - (A + 1) * cosw);
        b2 =     A * ((A + 1) - (A - 1) * cosw - two_sqrtA_alpha);
        a0 =         (A + 1) + (A - 1) * cosw + two_sqrtA_alpha;
        a1 =  -2 * ((A - 1) + (A + 1) * cosw);
        a2 =         (A + 1) + (A - 1) * cosw - two_sqrtA_alpha;
        break;
    case EQ_BAND_HIGH_SHELF:
        b0 =     A * ((A + 1) + (A - 1) * cosw + two_sqrtA_alpha);
        b1 =-2 * A * ((A - 1) + (A + 1) * cosw);
        b2 =     A * ((A + 1) + (A - 1) * cosw - two_sqrtA_alpha);
        a0 =         (A + 1) - (A - 1) * cosw + two_sqrtA_alpha;
        a1 =   2 * ((A - 1) - (A + 1) * cosw);
        a2 =         (A + 1) - (A - 1) * cosw - two_sqrtA_alpha;
        break;
    default: /* EQ_BAND_PEAK */
        b0 = 1 + alpha * A;
        b1 = -2 * cosw;
        b2 = 1 - alpha * A;
        a0 = 1 + alpha / A;
        a1 = -2 * cosw;
        a2 = 1 - alpha / A;
        break;
    }

    b->b0 = b0 / a0;
    b->b1 = b1 / a0;
    b->b2 = b2 / a0;
    b->a1 = a1 / a0;
    b->a2 = a2 / a0;

    b->last_type = b->type;
    b->last_freq = b->frequency;
    b->last_gain = b->gain_db;
    b->last_q    = b->q;
}

static void eq_process(sq_efx_eq_t *eq, float *buf, uint32_t frames,
                       uint32_t sample_rate)
{
    for (int bi = 0; bi < EQ_NUM_BANDS; bi++) {
        sq_eq_band_t *b = &eq->bands[bi];
        /* 0 dB peak/shelf is a no-op — skip to keep flat = bit-perfect. */
        if (b->gain_db == 0.0f && b->type != EQ_BAND_PEAK) continue;
        if (b->gain_db == 0.0f && b->type == EQ_BAND_PEAK) continue;

        eq_band_compute_coeffs(b, sample_rate);

        for (uint32_t i = 0; i < frames; i++) {
            for (int ch = 0; ch < 2; ch++) {
                float in = buf[i * 2 + ch];
                /* Transposed direct form II */
                float y = b->b0 * in + b->z1[ch];
                b->z1[ch] = b->b1 * in - b->a1 * y + b->z2[ch];
                b->z2[ch] = b->b2 * in - b->a2 * y;
                buf[i * 2 + ch] = y;
            }
        }
    }
}

/* ─── Brickwall limiter ─────────────────────────────────────────────────────
 *
 * Lookahead peak limiter. Inputs are written to a circular delay line; the
 * peak across the lookahead window determines a per-sample gain that ducks
 * the delayed output below the configured ceiling. Attack is instant within
 * the lookahead window; release is exponential. */

static void limiter_process(sq_efx_limiter_t *lim, float *buf, uint32_t frames,
                            uint32_t sample_rate)
{
    if (!lim->allocated) return;

    float ceiling = lim->ceiling;
    if (ceiling < 0.001f) ceiling = 0.001f;
    if (ceiling > 1.0f)   ceiling = 1.0f;

    float release_ms = lim->release_ms;
    if (release_ms < 1.0f)   release_ms = 1.0f;
    if (release_ms > 2000.0f) release_ms = 2000.0f;

    /* One-pole release coefficient: env approaches 1.0 with this time constant */
    float release_coef = expf(-1.0f / ((release_ms / 1000.0f) * (float)sample_rate));

    int la = LIMITER_LOOKAHEAD_SAMPLES;

    for (uint32_t i = 0; i < frames; i++) {
        float in_l = buf[i * 2];
        float in_r = buf[i * 2 + 1];

        /* Write current input into the delay line */
        lim->lookahead[lim->write_pos * 2]     = in_l;
        lim->lookahead[lim->write_pos * 2 + 1] = in_r;

        /* Peak across the lookahead window. We scan the whole window every
         * sample — O(N) per sample, but N=256 is fine for our buffer sizes. */
        float peak = 0.0f;
        for (int s = 0; s < la; s++) {
            float a = fabsf(lim->lookahead[s * 2]);
            float b = fabsf(lim->lookahead[s * 2 + 1]);
            if (a > peak) peak = a;
            if (b > peak) peak = b;
        }

        /* Target gain: 1.0 unless peak exceeds ceiling, then duck */
        float target = (peak > ceiling) ? (ceiling / peak) : 1.0f;

        /* Instant attack (drop), exponential release (recovery) */
        if (target < lim->envelope) {
            lim->envelope = target;
        } else {
            lim->envelope = target + (lim->envelope - target) * release_coef;
        }

        /* Read the delayed output and apply the gain envelope */
        int read_pos = (lim->write_pos + 1) % la;
        float out_l = lim->lookahead[read_pos * 2]     * lim->envelope;
        float out_r = lim->lookahead[read_pos * 2 + 1] * lim->envelope;

        /* Final clamp catches any overshoot from envelope smoothing */
        if (out_l >  ceiling) out_l =  ceiling;
        if (out_l < -ceiling) out_l = -ceiling;
        if (out_r >  ceiling) out_r =  ceiling;
        if (out_r < -ceiling) out_r = -ceiling;

        buf[i * 2]     = out_l;
        buf[i * 2 + 1] = out_r;

        lim->write_pos = (lim->write_pos + 1) % la;
    }
}

/* ─── Delay ──────────────────────────────────────────────────────────────── */

static void delay_process(sq_efx_delay_t *d, float *buf, uint32_t frames,
                          uint32_t sample_rate, double bpm)
{
    /* Buffer should have been pre-allocated by effect_init() */
    if (!d->allocated) return;

    float delay_time = d->time;

    /* BPM sync */
    if (d->bpm_sync && bpm > 0) {
        float beat_sec = 60.0f / (float)bpm;
        float divisions[] = {4.0f, 2.0f, 1.0f, 0.5f, 0.25f};
        int div = d->sync_division;
        if (div < 0) div = 0;
        if (div > 4) div = 4;
        delay_time = beat_sec * divisions[div];
    }

    int delay_samples = (int)(delay_time * (float)sample_rate);
    if (delay_samples < 1) delay_samples = 1;
    if (delay_samples >= DELAY_MAX_SAMPLES) delay_samples = DELAY_MAX_SAMPLES - 1;

    float wet = d->wet;
    float fb = d->feedback;
    if (fb > 0.95f) fb = 0.95f;

    for (uint32_t i = 0; i < frames; i++) {
        for (int ch = 0; ch < 2; ch++) {
            int idx = i * 2 + ch;
            int read_pos = d->write_pos - delay_samples * 2 + ch;
            if (read_pos < 0) read_pos += d->buffer_size * 2;

            float delayed = d->buffer[read_pos % (d->buffer_size * 2)];
            float in = buf[idx];

            /* Write input + feedback to buffer */
            d->buffer[d->write_pos % (d->buffer_size * 2)] = in + delayed * fb;

            /* Mix */
            buf[idx] = in + delayed * wet;

            if (ch == 1) d->write_pos = (d->write_pos + 2) % (d->buffer_size * 2);
        }
    }
}

/* ─── Reverb (Freeverb-inspired) ─────────────────────────────────────────── */

/* Comb filter sizes (prime-ish, spread for L/R) */
static const int comb_sizes_l[] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
static const int comb_sizes_r[] = {1139, 1211, 1300, 1379, 1445, 1514, 1580, 1640};
static const int allpass_sizes_l[] = {556, 441, 341, 225};
static const int allpass_sizes_r[] = {579, 464, 364, 248};

static float comb_process(reverb_comb_t *c, float input, float feedback, float damp)
{
    float output = c->buffer[c->idx];
    c->filterstore = output * (1.0f - damp) + c->filterstore * damp;
    c->buffer[c->idx] = input + c->filterstore * feedback;
    c->idx = (c->idx + 1) % c->size;
    return output;
}

static float allpass_process(reverb_allpass_t *a, float input)
{
    float buffered = a->buffer[a->idx];
    float output = -input + buffered;
    a->buffer[a->idx] = input + buffered * 0.5f;
    a->idx = (a->idx + 1) % a->size;
    return output;
}

static void reverb_process(sq_efx_reverb_t *r, float *buf, uint32_t frames)
{
    /* Buffers should have been pre-allocated by effect_init() */
    if (!r->combs || !r->allpasses) return;

    float feedback = r->room_size * 0.28f + 0.7f;  /* scale to 0.7-0.98 */
    float damp = r->damping;
    float wet = r->wet;

    /* Freeverb-style input gain compensation. 8 parallel comb filters
     * with feedback up to ~0.98 sum to many multiples of the input over
     * the decay time — without input attenuation a single loud transient
     * produces a wet tail several times louder than dry and clips the
     * output. 0.05 keeps it safe while still producing an audible tail
     * when wet is cranked. */
    const float fixedgain = 0.05f;

    /* Wet is treated as a SEND: dry is always passed through, wet adds
     * the reverb tail on top. This avoids the awkward "wet=1 silences
     * the transient" problem (combs need ~25ms to fill before producing
     * any output, so killing dry on a kick gives silence-then-tail).
     * Matches the typical drum-machine reverb model. */
    for (uint32_t i = 0; i < frames; i++) {
        float inL = buf[i * 2];
        float inR = buf[i * 2 + 1];
        float input = (inL + inR) * 0.5f * fixedgain;

        float outL = 0, outR = 0;

        /* Parallel comb filters */
        for (int c = 0; c < REVERB_NUM_COMBS; c++) {
            outL += comb_process(&r->combs[c], input, feedback, damp);
            outR += comb_process(&r->combs[REVERB_NUM_COMBS + c], input, feedback, damp);
        }

        /* Series allpass filters */
        for (int a = 0; a < REVERB_NUM_ALLPASS; a++) {
            outL = allpass_process(&r->allpasses[a], outL);
            outR = allpass_process(&r->allpasses[REVERB_NUM_ALLPASS + a], outR);
        }

        buf[i * 2]     = inL + outL * wet;
        buf[i * 2 + 1] = inR + outR * wet;
    }
}

/* ─── Overdrive (soft-clipping saturation) ────────────────────────────────── */

static void overdrive_process(sq_efx_overdrive_t *od, float *buf, uint32_t frames,
                              uint32_t sample_rate)
{
    /* Drive maps to gain: 1x at 0, ~40x at 1 */
    float gain = 1.0f + od->drive * 39.0f;
    float mix = od->mix;

    /* Tone filter: one-pole LP, cutoff from 800 Hz (tone=0) to 12000 Hz (tone=1) */
    float fc = 800.0f + od->tone * 11200.0f;
    float rc = 1.0f / (2.0f * (float)M_PI * fc / (float)sample_rate + 1.0f);

    for (uint32_t i = 0; i < frames; i++) {
        for (int ch = 0; ch < 2; ch++) {
            float in = buf[i * 2 + ch];
            /* Apply gain */
            float x = in * gain;
            /* Soft clip using tanh approximation: x / (1 + |x|) */
            float clipped = x / (1.0f + fabsf(x));
            /* Tone filter (one-pole LP) */
            od->tone_z1[ch] += rc * (clipped - od->tone_z1[ch]);
            float wet_out = od->tone_z1[ch];
            /* Mix */
            buf[i * 2 + ch] = in * (1.0f - mix) + wet_out * mix;
        }
    }
}

/* ─── Fuzz (hard-clipping distortion) ─────────────────────────────────────── */

static void fuzz_process(sq_efx_fuzz_t *fz, float *buf, uint32_t frames,
                         uint32_t sample_rate)
{
    /* Gain maps to boost: 1x at 0, ~100x at 1 */
    float gain = 1.0f + fz->gain * 99.0f;
    float mix = fz->mix;

    /* Tone filter: one-pole LP */
    float fc = 600.0f + fz->tone * 9400.0f;
    float rc = 1.0f / (2.0f * (float)M_PI * fc / (float)sample_rate + 1.0f);

    for (uint32_t i = 0; i < frames; i++) {
        for (int ch = 0; ch < 2; ch++) {
            float in = buf[i * 2 + ch];
            float x = in * gain;
            /* Hard clip to [-1, 1] */
            float clipped = x;
            if (clipped > 1.0f) clipped = 1.0f;
            else if (clipped < -1.0f) clipped = -1.0f;
            /* Tone filter */
            fz->tone_z1[ch] += rc * (clipped - fz->tone_z1[ch]);
            float wet_out = fz->tone_z1[ch];
            buf[i * 2 + ch] = in * (1.0f - mix) + wet_out * mix;
        }
    }
}

/* ─── Chorus (modulated delay) ────────────────────────────────────────────── */

static void chorus_process(sq_efx_chorus_t *ch_fx, float *buf, uint32_t frames,
                           uint32_t sample_rate)
{
    if (!ch_fx->allocated) return;

    float mix = ch_fx->mix;
    float depth = ch_fx->depth;
    float rate = ch_fx->rate;
    /* Base delay ~7ms, modulation range ~7ms */
    float base_delay = 0.007f * (float)sample_rate;
    float mod_range = 0.007f * (float)sample_rate * depth;
    float phase_inc = rate / (float)sample_rate;

    for (uint32_t i = 0; i < frames; i++) {
        /* LFO (sine) */
        float lfo = sinf(ch_fx->lfo_phase * 2.0f * (float)M_PI);
        ch_fx->lfo_phase += phase_inc;
        if (ch_fx->lfo_phase >= 1.0f) ch_fx->lfo_phase -= 1.0f;

        float delay_samples = base_delay + lfo * mod_range;
        if (delay_samples < 1.0f) delay_samples = 1.0f;
        if (delay_samples >= (float)(ch_fx->buffer_size - 1))
            delay_samples = (float)(ch_fx->buffer_size - 2);

        for (int c = 0; c < 2; c++) {
            float in = buf[i * 2 + c];

            /* Write to circular buffer */
            ch_fx->buffer[ch_fx->write_pos * 2 + c] = in;

            /* Read with linear interpolation */
            float read_f = (float)ch_fx->write_pos - delay_samples;
            if (read_f < 0.0f) read_f += (float)ch_fx->buffer_size;
            int read_i = (int)read_f;
            float frac = read_f - (float)read_i;
            int r0 = read_i % ch_fx->buffer_size;
            int r1 = (read_i + 1) % ch_fx->buffer_size;
            float delayed = ch_fx->buffer[r0 * 2 + c] * (1.0f - frac)
                          + ch_fx->buffer[r1 * 2 + c] * frac;

            buf[i * 2 + c] = in * (1.0f - mix) + delayed * mix;
        }

        ch_fx->write_pos = (ch_fx->write_pos + 1) % ch_fx->buffer_size;
    }
}

/* ─── Bitcrusher ──────────────────────────────────────────────────────────── */

static void bitcrusher_process(sq_efx_bitcrusher_t *bc, float *buf, uint32_t frames)
{
    float mix = bc->mix;
    int ds = (int)bc->downsample;
    if (ds < 1) ds = 1;
    float levels = powf(2.0f, bc->bits) - 1.0f;
    if (levels < 1.0f) levels = 1.0f;

    for (uint32_t i = 0; i < frames; i++) {
        float inL = buf[i * 2];
        float inR = buf[i * 2 + 1];

        bc->hold_count--;
        if (bc->hold_count <= 0) {
            bc->hold_count = ds;
            /* Quantize */
            bc->hold_l = roundf(inL * levels) / levels;
            bc->hold_r = roundf(inR * levels) / levels;
        }

        buf[i * 2]     = inL * (1.0f - mix) + bc->hold_l * mix;
        buf[i * 2 + 1] = inR * (1.0f - mix) + bc->hold_r * mix;
    }
}

/* ─── Compressor ──────────────────────────────────────────────────────────── */

static void compressor_process(sq_efx_compressor_t *cp, float *buf, uint32_t frames,
                               uint32_t sample_rate)
{
    float attack_coeff  = expf(-1.0f / (cp->attack  * (float)sample_rate));
    float release_coeff = expf(-1.0f / (cp->release * (float)sample_rate));
    float threshold = cp->threshold;
    float ratio = cp->ratio;
    float makeup = cp->makeup;

    for (uint32_t i = 0; i < frames; i++) {
        /* Peak detection (stereo max) */
        float peak = fabsf(buf[i * 2]);
        float r = fabsf(buf[i * 2 + 1]);
        if (r > peak) peak = r;

        /* Envelope follower */
        if (peak > cp->envelope)
            cp->envelope = attack_coeff * cp->envelope + (1.0f - attack_coeff) * peak;
        else
            cp->envelope = release_coeff * cp->envelope + (1.0f - release_coeff) * peak;

        /* Gain computation */
        float gain = 1.0f;
        if (cp->envelope > threshold && threshold > 0.0f) {
            /* dB domain compression */
            float over = cp->envelope / threshold;
            float compressed = threshold * powf(over, 1.0f / ratio - 1.0f);
            gain = compressed;
        }
        gain *= makeup;

        buf[i * 2]     *= gain;
        buf[i * 2 + 1] *= gain;
    }
}

/* ─── Phaser ──────────────────────────────────────────────────────────────── */

static void phaser_process(sq_efx_phaser_t *ph, float *buf, uint32_t frames,
                           uint32_t sample_rate)
{
    float mix = ph->mix;
    float fb = ph->feedback;
    float phase_inc = ph->rate / (float)sample_rate;

    for (uint32_t i = 0; i < frames; i++) {
        /* LFO */
        float lfo = sinf(ph->lfo_phase * 2.0f * (float)M_PI);
        ph->lfo_phase += phase_inc;
        if (ph->lfo_phase >= 1.0f) ph->lfo_phase -= 1.0f;

        /* Modulated allpass coefficient: sweep from ~200 Hz to ~4000 Hz */
        float d = 0.5f + 0.45f * lfo * ph->depth;

        for (int ch = 0; ch < 2; ch++) {
            float in = buf[i * 2 + ch] + ph->last_out[ch] * fb;
            float out = in;

            /* Chain of first-order allpass filters: y = -a*x + z; z = x + a*y */
            for (int s = 0; s < PHASER_NUM_STAGES; s++) {
                float ap_out = -d * out + ph->ap_z1[ch][s];
                ph->ap_z1[ch][s] = out + d * ap_out;
                out = ap_out;
            }

            ph->last_out[ch] = out;
            buf[i * 2 + ch] = in * (1.0f - mix) + out * mix;
        }
    }
}

/* ─── Flanger ─────────────────────────────────────────────────────────────── */

static void flanger_process(sq_efx_flanger_t *fl, float *buf, uint32_t frames,
                            uint32_t sample_rate)
{
    if (!fl->allocated) return;

    float mix = fl->mix;
    float fb = fl->feedback;
    float phase_inc = fl->rate / (float)sample_rate;
    /* Delay range: 0.1ms to 5ms */
    float min_delay = 0.0001f * (float)sample_rate;
    float max_delay = 0.005f * (float)sample_rate;
    float mod_range = (max_delay - min_delay) * fl->depth;

    for (uint32_t i = 0; i < frames; i++) {
        float lfo = sinf(fl->lfo_phase * 2.0f * (float)M_PI);
        fl->lfo_phase += phase_inc;
        if (fl->lfo_phase >= 1.0f) fl->lfo_phase -= 1.0f;

        float delay_samples = min_delay + (lfo * 0.5f + 0.5f) * mod_range;
        if (delay_samples < 1.0f) delay_samples = 1.0f;
        if (delay_samples >= (float)(fl->buffer_size - 1))
            delay_samples = (float)(fl->buffer_size - 2);

        for (int c = 0; c < 2; c++) {
            float in = buf[i * 2 + c];

            /* Read with linear interpolation */
            float read_f = (float)fl->write_pos - delay_samples;
            if (read_f < 0.0f) read_f += (float)fl->buffer_size;
            int read_i = (int)read_f;
            float frac = read_f - (float)read_i;
            int r0 = read_i % fl->buffer_size;
            int r1 = (read_i + 1) % fl->buffer_size;
            float delayed = fl->buffer[r0 * 2 + c] * (1.0f - frac)
                          + fl->buffer[r1 * 2 + c] * frac;

            /* Write input + feedback to buffer */
            fl->buffer[fl->write_pos * 2 + c] = in + delayed * fb;

            buf[i * 2 + c] = in * (1.0f - mix) + delayed * mix;
        }

        fl->write_pos = (fl->write_pos + 1) % fl->buffer_size;
    }
}

/* ─── Tremolo ─────────────────────────────────────────────────────────────── */

static void tremolo_process(sq_efx_tremolo_t *tr, float *buf, uint32_t frames,
                            uint32_t sample_rate)
{
    float phase_inc = tr->rate / (float)sample_rate;
    float depth = tr->depth;

    for (uint32_t i = 0; i < frames; i++) {
        float mod;
        switch (tr->wave) {
        case 1: /* Square */
            mod = (tr->lfo_phase < 0.5f) ? 1.0f : 0.0f;
            break;
        case 2: /* Triangle */
            mod = (tr->lfo_phase < 0.5f)
                ? tr->lfo_phase * 4.0f - 1.0f
                : 3.0f - tr->lfo_phase * 4.0f;
            mod = mod * 0.5f + 0.5f; /* normalize to 0-1 */
            break;
        default: /* Sine */
            mod = sinf(tr->lfo_phase * 2.0f * (float)M_PI) * 0.5f + 0.5f;
            break;
        }

        float gain = 1.0f - depth * (1.0f - mod);

        buf[i * 2]     *= gain;
        buf[i * 2 + 1] *= gain;

        tr->lfo_phase += phase_inc;
        if (tr->lfo_phase >= 1.0f) tr->lfo_phase -= 1.0f;
    }
}

/* ─── Ring Modulator ──────────────────────────────────────────────────────── */

static void ringmod_process(sq_efx_ringmod_t *rm, float *buf, uint32_t frames,
                            uint32_t sample_rate)
{
    float mix = rm->mix;
    float phase_inc = rm->freq / (float)sample_rate;

    for (uint32_t i = 0; i < frames; i++) {
        float carrier = sinf(rm->phase * 2.0f * (float)M_PI);
        rm->phase += phase_inc;
        if (rm->phase >= 1.0f) rm->phase -= 1.0f;

        for (int ch = 0; ch < 2; ch++) {
            float in = buf[i * 2 + ch];
            float wet = in * carrier;
            buf[i * 2 + ch] = in * (1.0f - mix) + wet * mix;
        }
    }
}

/* ─── Tape Saturation ─────────────────────────────────────────────────────── */

static void tape_process(sq_efx_tape_t *tp, float *buf, uint32_t frames,
                         uint32_t sample_rate)
{
    float mix = tp->mix;
    /* Drive maps to gain: 1x at 0, ~10x at 1 */
    float gain = 1.0f + tp->drive * 9.0f;

    /* Warmth filter: one-pole LP, cutoff from 20kHz (warmth=0) to 2kHz (warmth=1) */
    float fc = 20000.0f - tp->warmth * 18000.0f;
    float rc = 1.0f / (2.0f * (float)M_PI * fc / (float)sample_rate + 1.0f);

    for (uint32_t i = 0; i < frames; i++) {
        for (int ch = 0; ch < 2; ch++) {
            float in = buf[i * 2 + ch];
            /* Soft saturation via tanh */
            float x = in * gain;
            float sat = tanhf(x);
            /* Warmth filter */
            tp->warmth_z1[ch] += rc * (sat - tp->warmth_z1[ch]);
            float wet = tp->warmth_z1[ch];
            buf[i * 2 + ch] = in * (1.0f - mix) + wet * mix;
        }
    }
}

/* ─── Shimmer Reverb ──────────────────────────────────────────────────────── */

static void shimmer_process(sq_efx_shimmer_t *sh, float *buf, uint32_t frames,
                            uint32_t sample_rate)
{
    if (!sh->allocated) return;

    float mix = sh->mix;
    float decay = sh->decay * 0.85f + 0.1f; /* scale to 0.1-0.95 */
    float shimmer_amt = sh->shimmer;
    float lfo_inc = 0.3f / (float)sample_rate; /* slow phaser LFO */
    int buf_len = sh->buffer_size;

    for (uint32_t i = 0; i < frames; i++) {
        float inL = buf[i * 2];
        float inR = buf[i * 2 + 1];
        float input = (inL + inR) * 0.5f;

        /* Read from buffer at normal position (reverb tap) */
        int tap1 = (sh->write_pos - (int)(0.03f * (float)sample_rate) + buf_len) % buf_len;
        int tap2 = (sh->write_pos - (int)(0.07f * (float)sample_rate) + buf_len) % buf_len;
        int tap3 = (sh->write_pos - (int)(0.11f * (float)sample_rate) + buf_len) % buf_len;
        float reverb_out = sh->buffer[tap1 * 2] * 0.4f
                         + sh->buffer[tap2 * 2 + 1] * 0.3f
                         + sh->buffer[tap3 * 2] * 0.3f;

        /* Octave-up pitch shift: read at double rate */
        float shifted = 0.0f;
        if (shimmer_amt > 0.0f) {
            int read_i = (int)sh->read_phase;
            float frac = sh->read_phase - (float)read_i;
            int r0 = read_i % buf_len;
            int r1 = (read_i + 1) % buf_len;
            shifted = sh->buffer[r0 * 2] * (1.0f - frac)
                    + sh->buffer[r1 * 2] * frac;
            /* Advance read position at 2x rate (octave up) */
            sh->read_phase += 2.0f;
            if (sh->read_phase >= (float)buf_len)
                sh->read_phase -= (float)buf_len;
        }

        float wet_out = reverb_out + shifted * shimmer_amt * 0.5f;

        /* Slight phaser modulation on wet signal */
        float lfo = sinf(sh->lfo_phase * 2.0f * (float)M_PI);
        sh->lfo_phase += lfo_inc;
        if (sh->lfo_phase >= 1.0f) sh->lfo_phase -= 1.0f;
        float d = 0.5f + 0.3f * lfo;
        for (int ch = 0; ch < 2; ch++) {
            float ap_out = -d * wet_out + sh->ap_z1[ch];
            sh->ap_z1[ch] = wet_out + d * ap_out;
            wet_out = (wet_out + ap_out) * 0.5f;
        }

        /* Write to buffer: input + decayed feedback */
        sh->buffer[sh->write_pos * 2]     = input + reverb_out * decay;
        sh->buffer[sh->write_pos * 2 + 1] = input + reverb_out * decay;
        sh->write_pos = (sh->write_pos + 1) % buf_len;

        buf[i * 2]     = inL * (1.0f - mix) + wet_out * mix;
        buf[i * 2 + 1] = inR * (1.0f - mix) + wet_out * mix;
    }
}

/* ─── Public API ─────────────────────────────────────────────────────────── */

void effect_free(sq_effect_slot_t *slot)
{
    if (!slot) return;

    switch (slot->type) {
    case EFFECT_DELAY:
        if (slot->delay.buffer) {
            free(slot->delay.buffer);
            slot->delay.buffer = NULL;
        }
        slot->delay.allocated = false;
        break;
    case EFFECT_REVERB:
        if (slot->reverb.combs) {
            free(slot->reverb.combs);
            slot->reverb.combs = NULL;
        }
        if (slot->reverb.allpasses) {
            free(slot->reverb.allpasses);
            slot->reverb.allpasses = NULL;
        }
        slot->reverb.initialized = false;
        break;
    case EFFECT_CHORUS:
        if (slot->chorus.buffer) {
            free(slot->chorus.buffer);
            slot->chorus.buffer = NULL;
        }
        slot->chorus.allocated = false;
        break;
    case EFFECT_FLANGER:
        if (slot->flanger.buffer) {
            free(slot->flanger.buffer);
            slot->flanger.buffer = NULL;
        }
        slot->flanger.allocated = false;
        break;
    case EFFECT_SHIMMER:
        if (slot->shimmer.buffer) {
            free(slot->shimmer.buffer);
            slot->shimmer.buffer = NULL;
        }
        slot->shimmer.allocated = false;
        break;
    case EFFECT_LIMITER:
        if (slot->limiter.lookahead) {
            free(slot->limiter.lookahead);
            slot->limiter.lookahead = NULL;
        }
        slot->limiter.allocated = false;
        break;
    case EFFECT_OVERDRIVE:
    case EFFECT_FUZZ:
    case EFFECT_BITCRUSHER:
    case EFFECT_COMPRESSOR:
    case EFFECT_PHASER:
    case EFFECT_TREMOLO:
    case EFFECT_RINGMOD:
    case EFFECT_TAPE:
    default:
        break;
    }

    slot->type = EFFECT_NONE;
}

void effect_init(sq_effect_slot_t *slot, sq_effect_type_t type, uint32_t sample_rate)
{
    (void)sample_rate;
    effect_free(slot);
    memset(slot, 0, sizeof(*slot));
    slot->type = type;
    slot->bypass = false;

    switch (type) {
    case EFFECT_FILTER:
        slot->filter.mode = EFX_FILTER_LP;
        slot->filter.cutoff = 1000.0f;
        slot->filter.resonance = 1.0f;
        slot->filter.wet = 1.0f;
        break;
    case EFFECT_DELAY:
        slot->delay.time = 0.25f;
        slot->delay.feedback = 0.3f;
        slot->delay.wet = 0.3f;
        slot->delay.buffer_size = DELAY_MAX_SAMPLES;
        /* Pre-allocate delay buffer so the audio thread never calls malloc */
        slot->delay.buffer = calloc(DELAY_MAX_SAMPLES * 2, sizeof(float));
        slot->delay.allocated = (slot->delay.buffer != NULL);
        break;
    case EFFECT_REVERB:
        slot->reverb.room_size = 0.5f;
        slot->reverb.damping = 0.5f;
        slot->reverb.wet = 0.3f;
        /* Pre-allocate reverb buffers so the audio thread never calls malloc */
        slot->reverb.combs = calloc(2 * REVERB_NUM_COMBS, sizeof(reverb_comb_t));
        slot->reverb.allpasses = calloc(2 * REVERB_NUM_ALLPASS, sizeof(reverb_allpass_t));
        if (slot->reverb.combs && slot->reverb.allpasses) {
            for (int i = 0; i < REVERB_NUM_COMBS; i++) {
                slot->reverb.combs[i].size = comb_sizes_l[i];
                slot->reverb.combs[REVERB_NUM_COMBS + i].size = comb_sizes_r[i];
            }
            for (int i = 0; i < REVERB_NUM_ALLPASS; i++) {
                slot->reverb.allpasses[i].size = allpass_sizes_l[i];
                slot->reverb.allpasses[REVERB_NUM_ALLPASS + i].size = allpass_sizes_r[i];
            }
            slot->reverb.initialized = true;
        } else {
            free(slot->reverb.combs);
            free(slot->reverb.allpasses);
            slot->reverb.combs = NULL;
            slot->reverb.allpasses = NULL;
            slot->reverb.initialized = false;
        }
        break;
    case EFFECT_OVERDRIVE:
        slot->overdrive.drive = 0.3f;
        slot->overdrive.tone = 0.5f;
        slot->overdrive.mix = 1.0f;
        break;
    case EFFECT_FUZZ:
        slot->fuzz.gain = 0.4f;
        slot->fuzz.tone = 0.5f;
        slot->fuzz.mix = 1.0f;
        break;
    case EFFECT_CHORUS:
        slot->chorus.rate = 1.0f;
        slot->chorus.depth = 0.5f;
        slot->chorus.mix = 0.5f;
        slot->chorus.buffer_size = CHORUS_MAX_SAMPLES;
        slot->chorus.buffer = calloc(CHORUS_MAX_SAMPLES * 2, sizeof(float));
        slot->chorus.allocated = (slot->chorus.buffer != NULL);
        break;
    case EFFECT_BITCRUSHER:
        slot->bitcrusher.bits = 8.0f;
        slot->bitcrusher.downsample = 1.0f;
        slot->bitcrusher.mix = 1.0f;
        slot->bitcrusher.hold_count = 1;
        break;
    case EFFECT_COMPRESSOR:
        slot->compressor.threshold = 0.5f;
        slot->compressor.ratio = 4.0f;
        slot->compressor.attack = 0.01f;
        slot->compressor.release = 0.1f;
        slot->compressor.makeup = 1.0f;
        slot->compressor.envelope = 0.0f;
        break;
    case EFFECT_PHASER:
        slot->phaser.rate = 0.5f;
        slot->phaser.depth = 0.5f;
        slot->phaser.feedback = 0.5f;
        slot->phaser.mix = 0.5f;
        break;
    case EFFECT_FLANGER:
        slot->flanger.rate = 0.5f;
        slot->flanger.depth = 0.5f;
        slot->flanger.feedback = 0.3f;
        slot->flanger.mix = 0.5f;
        slot->flanger.buffer_size = FLANGER_MAX_SAMPLES;
        slot->flanger.buffer = calloc(FLANGER_MAX_SAMPLES * 2, sizeof(float));
        slot->flanger.allocated = (slot->flanger.buffer != NULL);
        break;
    case EFFECT_TREMOLO:
        slot->tremolo.rate = 5.0f;
        slot->tremolo.depth = 0.5f;
        slot->tremolo.wave = 0;
        break;
    case EFFECT_RINGMOD:
        slot->ringmod.freq = 440.0f;
        slot->ringmod.mix = 0.5f;
        break;
    case EFFECT_TAPE:
        slot->tape.drive = 0.3f;
        slot->tape.warmth = 0.3f;
        slot->tape.mix = 1.0f;
        break;
    case EFFECT_SHIMMER:
        slot->shimmer.decay = 0.5f;
        slot->shimmer.shimmer = 0.3f;
        slot->shimmer.mix = 0.3f;
        slot->shimmer.buffer_size = SHIMMER_BUF_SIZE;
        slot->shimmer.buffer = calloc(SHIMMER_BUF_SIZE * 2, sizeof(float));
        slot->shimmer.allocated = (slot->shimmer.buffer != NULL);
        break;
    case EFFECT_LIMITER:
        slot->limiter.ceiling = 0.97f;       /* ~-0.3 dBFS */
        slot->limiter.release_ms = 50.0f;
        slot->limiter.envelope = 1.0f;
        slot->limiter.write_pos = 0;
        slot->limiter.lookahead = calloc(LIMITER_LOOKAHEAD_SAMPLES * 2,
                                         sizeof(float));
        slot->limiter.allocated = (slot->limiter.lookahead != NULL);
        break;
    case EFFECT_EQ:
        slot->eq.bands[0].type = EQ_BAND_LOW_SHELF;
        slot->eq.bands[0].frequency = 250.0f;
        slot->eq.bands[0].gain_db = 0.0f;
        slot->eq.bands[0].q = 0.707f;

        slot->eq.bands[1].type = EQ_BAND_PEAK;
        slot->eq.bands[1].frequency = 1000.0f;
        slot->eq.bands[1].gain_db = 0.0f;
        slot->eq.bands[1].q = 1.0f;

        slot->eq.bands[2].type = EQ_BAND_HIGH_SHELF;
        slot->eq.bands[2].frequency = 5000.0f;
        slot->eq.bands[2].gain_db = 0.0f;
        slot->eq.bands[2].q = 0.707f;
        break;
    default:
        break;
    }
}

void effect_process(sq_effect_slot_t *slot, float *buffer, uint32_t num_frames,
                    uint32_t sample_rate, double bpm)
{
    if (slot->type == EFFECT_NONE || slot->bypass) return;

    switch (slot->type) {
    case EFFECT_FILTER:
        filter_process(&slot->filter, buffer, num_frames, sample_rate);
        break;
    case EFFECT_DELAY:
        delay_process(&slot->delay, buffer, num_frames, sample_rate, bpm);
        break;
    case EFFECT_REVERB:
        reverb_process(&slot->reverb, buffer, num_frames);
        break;
    case EFFECT_OVERDRIVE:
        overdrive_process(&slot->overdrive, buffer, num_frames, sample_rate);
        break;
    case EFFECT_FUZZ:
        fuzz_process(&slot->fuzz, buffer, num_frames, sample_rate);
        break;
    case EFFECT_CHORUS:
        chorus_process(&slot->chorus, buffer, num_frames, sample_rate);
        break;
    case EFFECT_BITCRUSHER:
        bitcrusher_process(&slot->bitcrusher, buffer, num_frames);
        break;
    case EFFECT_COMPRESSOR:
        compressor_process(&slot->compressor, buffer, num_frames, sample_rate);
        break;
    case EFFECT_PHASER:
        phaser_process(&slot->phaser, buffer, num_frames, sample_rate);
        break;
    case EFFECT_FLANGER:
        flanger_process(&slot->flanger, buffer, num_frames, sample_rate);
        break;
    case EFFECT_TREMOLO:
        tremolo_process(&slot->tremolo, buffer, num_frames, sample_rate);
        break;
    case EFFECT_RINGMOD:
        ringmod_process(&slot->ringmod, buffer, num_frames, sample_rate);
        break;
    case EFFECT_TAPE:
        tape_process(&slot->tape, buffer, num_frames, sample_rate);
        break;
    case EFFECT_SHIMMER:
        shimmer_process(&slot->shimmer, buffer, num_frames, sample_rate);
        break;
    case EFFECT_EQ:
        eq_process(&slot->eq, buffer, num_frames, sample_rate);
        break;
    case EFFECT_LIMITER:
        limiter_process(&slot->limiter, buffer, num_frames, sample_rate);
        break;
    default:
        break;
    }
}

void effects_chain_process(sq_effect_slot_t *slots, int num_slots,
                           float *buffer, uint32_t num_frames,
                           uint32_t sample_rate, double bpm)
{
    for (int i = 0; i < num_slots; i++) {
        effect_process(&slots[i], buffer, num_frames, sample_rate, bpm);
    }
}
