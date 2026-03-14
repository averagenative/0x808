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
    float dry = 1.0f - wet;

    for (uint32_t i = 0; i < frames; i++) {
        float inL = buf[i * 2];
        float inR = buf[i * 2 + 1];
        float input = (inL + inR) * 0.5f; /* mono input to reverb */

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

        buf[i * 2]     = inL * dry + outL * wet;
        buf[i * 2 + 1] = inR * dry + outR * wet;
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
    case EFFECT_OVERDRIVE:
    case EFFECT_FUZZ:
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
