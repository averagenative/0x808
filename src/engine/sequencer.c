/*
 * sequencer.c — Pattern step sequencing.
 *
 * How it works:
 * 1. Each audio buffer, we call transport_advance() to check if we've
 *    moved to a new step.
 * 2. If we have, we walk through every track in the current pattern.
 * 3. For each track, we check if the current step has velocity > 0.
 * 4. If it does, we trigger a voice in the sampler (or synth, later).
 *
 * The sequencer doesn't produce audio directly — it just decides WHEN
 * to trigger sounds. The sampler/synth handles the actual audio rendering.
 */

#include "engine/sequencer.h"
#include "engine/transport.h"
#include "engine/sampler.h"
#include "engine/synth.h"
#include "engine/envelope.h"
#include "formats/sf2.h"

/*
 * Simple xorshift32 PRNG — fast, no stdlib, audio-thread safe.
 * Returns a float in [-1.0, 1.0].
 */
static float rng_next(uint32_t *state)
{
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    /* Convert to float in [-1.0, 1.0] */
    return (float)(int32_t)x / 2147483648.0f;
}

void sequencer_tick(sq_engine_t *engine)
{
    if (!engine->transport.playing) return;

    /* Get the current pattern */
    int pat_idx = engine->transport.current_pattern;
    if (pat_idx < 0 || (uint32_t)pat_idx >= engine->num_patterns) return;

    sq_pattern_t *pattern = &engine->patterns[pat_idx];
    if (pattern->num_tracks == 0) return;

    /* Find the longest track length for transport wrap (polymeter support) */
    uint32_t pattern_length = 16;
    for (uint32_t t = 0; t < pattern->num_tracks; t++) {
        if (pattern->tracks[t].length > pattern_length)
            pattern_length = pattern->tracks[t].length;
    }

    /*
     * Advance the transport and check if we've hit a new step.
     * transport_advance() returns the new step number, or -1 if
     * we're still on the same step.
     */
    int new_step = transport_advance(&engine->transport, engine->sample_rate,
                                     0, /* num_frames=0 here — see note below */
                                     pattern_length);

    /*
     * NOTE: We're passing num_frames=0 because transport_advance() is
     * called from sq_engine_process() which advances by the buffer size.
     * We need to restructure this — for now, the advance happens in
     * sq_engine_process() and sequencer_tick() just checks the step.
     * This will be cleaned up when we wire them together properly.
     *
     * For the initial implementation, we'll call transport_advance()
     * from engine_process() directly and pass the step to sequencer_tick().
     * Let's adjust the approach...
     */
    (void)new_step; /* This approach needs restructuring — see engine.c */
}

/*
 * Trigger voices for all active tracks at the given step.
 * Called by engine.c when the transport crosses a step boundary.
 */
void sequencer_trigger_step(sq_engine_t *engine, int step)
{
    int pat_idx = engine->transport.current_pattern;
    if (pat_idx < 0 || (uint32_t)pat_idx >= engine->num_patterns) return;

    sq_pattern_t *pattern = &engine->patterns[pat_idx];

    /* Check if any tracks are soloed */
    bool any_solo = false;
    for (uint32_t t = 0; t < pattern->num_tracks; t++) {
        if (pattern->tracks[t].solo) {
            any_solo = true;
            break;
        }
    }

    /* Walk every track and trigger voices for active steps */
    for (uint32_t t = 0; t < pattern->num_tracks; t++) {
        sq_track_t *track = &pattern->tracks[t];

        /* Skip muted tracks */
        if (track->mute) continue;

        /* If any track is soloed, skip non-soloed tracks */
        if (any_solo && !track->solo) continue;

        /* Wrap step to this track's length (for polymetric support) */
        int track_step = step % (int)track->length;

        /* Check if this step is active (velocity > 0) */
        sq_step_t *s = &track->steps[track_step];
        if (s->velocity == 0) continue;

        /* Step probability: 0 means always trigger (100%), 1-100 = percentage */
        if (s->probability > 0 && s->probability < 100) {
            uint32_t rval = engine->rng_state;
            rval ^= rval << 13;
            rval ^= rval >> 17;
            rval ^= rval << 5;
            engine->rng_state = rval;
            if ((rval % 100) >= (uint32_t)s->probability)
                continue; /* probability check failed — skip this step */
        }

        /* Convert velocity from 0-127 to 0.0-1.0 */
        float vel = (float)s->velocity / 127.0f;

        /*
         * Apply humanization: random velocity variation.
         * humanize=0.0 means no variation, humanize=1.0 means ±50% variation.
         * Clamped to [0.05, 1.0] so notes never go silent or above max.
         */
        if (track->humanize > 0.0f) {
            float variation = rng_next(&engine->rng_state) * track->humanize * 0.5f;
            vel += variation;
            if (vel < 0.05f) vel = 0.05f;
            if (vel > 1.0f)  vel = 1.0f;
        }

        /* Micro-timing offset + timing humanize → delay trigger */
        float total_offset = s->micro_offset;
        if (track->timing_humanize > 0.0f) {
            float r = rng_next(&engine->rng_state);
            total_offset += r * track->timing_humanize * 0.3f; /* ±30% of a step */
        }
        if (total_offset > 0.01f) {
            /* Positive offset = late trigger. Defer via retrigger queue. */
            double sps = (60.0 / engine->transport.bpm / 4.0) *
                         (double)engine->sample_rate;
            uint32_t delay = (uint32_t)(total_offset * sps);
            if (delay > 0) {
                for (int ri = 0; ri < SQ_MAX_RETRIGGERS; ri++) {
                    if (engine->retriggers[ri].count == 0) {
                        engine->retriggers[ri].remaining = delay;
                        engine->retriggers[ri].interval  = 0;
                        engine->retriggers[ri].count     = 1;
                        engine->retriggers[ri].track     = (uint8_t)t;
                        engine->retriggers[ri].velocity  = vel;
                        engine->retriggers[ri].vel_decay = 1.0f;
                        engine->retriggers[ri].pitch_offset = s->pitch_offset;
                        engine->retriggers[ri].note      = s->note;
                        break;
                    }
                }
                continue; /* skip immediate trigger */
            }
        }

        /* Choke group: silence other tracks in the same group */
        if (track->choke_group > 0) {
            for (uint32_t ct = 0; ct < pattern->num_tracks; ct++) {
                if (ct == t) continue;
                sq_track_t *other = &pattern->tracks[ct];
                if (other->choke_group != track->choke_group) continue;

                if (other->type == TRACK_SAMPLER && other->sample_index >= 0) {
                    /* Stop sampler voice immediately */
                    int si = other->sample_index;
                    for (int v = 0; v < SQ_MAX_VOICES; v++) {
                        if (engine->voices[v].active &&
                            engine->voices[v].sample_index == si) {
                            engine->voices[v].active = false;
                        }
                    }
                } else if (other->type == TRACK_SYNTH && other->synth_preset >= 0) {
                    /* Release synth voices for this preset */
                    for (int v = 0; v < SQ_MAX_SYNTH_VOICES; v++) {
                        if (engine->synth_voices[v].active &&
                            engine->synth_voices[v].preset_index == other->synth_preset) {
                            int pi = engine->synth_voices[v].preset_index;
                            if (pi >= 0 && (uint32_t)pi < engine->num_synth_presets) {
                                envelope_release(&engine->synth_voices[v].amp_env,
                                                 &engine->synth_presets[pi].amp_env,
                                                 engine->sample_rate);
                            }
                        }
                    }
                }
            }
        }

        /* Trigger based on track type */
        if (track->type == TRACK_SAMPLER && track->sample_index >= 0) {
            sampler_trigger(engine, track->sample_index,
                           vel, s->pitch_offset,
                           track->volume, track->pan, (int)t);
            /* Apply sample clip start/end/reverse from track settings */
            if (track->sample_start > 0 || track->sample_end > 0 || track->sample_reverse) {
                /* Find the voice we just triggered (most recently started) */
                for (int v = SQ_MAX_VOICES - 1; v >= 0; v--) {
                    if (engine->voices[v].active &&
                        engine->voices[v].sample_index == track->sample_index) {
                        engine->voices[v].clip_start = track->sample_start;
                        engine->voices[v].clip_end   = track->sample_end;
                        engine->voices[v].reverse    = track->sample_reverse;
                        if (track->sample_reverse)
                            engine->voices[v].position = 0.0; /* reset so render init handles it */
                        break;
                    }
                }
            }
        } else if (track->type == TRACK_SYNTH && track->synth_preset >= 0) {
            int vi = synth_trigger(engine, track->synth_preset,
                                   vel, s->pitch_offset,
                                   track->volume, track->pan, s->note,
                                   (int)t);

            /* Apply parameter locks to the voice (not the preset).
             * param[0] = filter cutoff override (>0)
             * param[1] = filter resonance override (>0) */
            if (vi >= 0) {
                engine->synth_voices[vi].plock_cutoff = s->param[0];
                engine->synth_voices[vi].plock_resonance = s->param[1];
            }

            /* Track note-off if step has a length > 0 */
            if (vi >= 0 && s->length > 0.0f) {
                /* Calculate duration in samples:
                 * samples_per_step = 60 / BPM / steps_per_beat * sample_rate */
                double sps = (60.0 / engine->transport.bpm / 4.0) *
                             (double)engine->sample_rate;
                uint32_t duration = (uint32_t)(s->length * sps);
                if (duration > 0) {
                    /* Find a free slot in active_notes */
                    for (int an = 0; an < SQ_MAX_ACTIVE_NOTES; an++) {
                        if (engine->active_notes[an].remaining == 0) {
                            engine->active_notes[an].voice_index = vi;
                            engine->active_notes[an].remaining = duration;
                            break;
                        }
                    }
                }
            }
        } else if (track->type == TRACK_SF2 && track->sf2_preset >= 0) {
            int key = (s->note > 0) ? s->note : 60;
            key += s->pitch_offset;
            if (key < 0) key = 0;
            if (key > 127) key = 127;
            sf2_note_on(engine, track->sf2_preset, key, vel * track->volume);
        }

        /* Schedule retriggers (note repeat / ratcheting) */
        if (s->retrigger >= 2 && s->retrigger <= 4) {
            double sps = (60.0 / engine->transport.bpm / 4.0) *
                         (double)engine->sample_rate;
            uint32_t interval = (uint32_t)(sps / s->retrigger);
            if (interval > 0) {
                for (int ri = 0; ri < SQ_MAX_RETRIGGERS; ri++) {
                    if (engine->retriggers[ri].count == 0) {
                        engine->retriggers[ri].remaining = interval;
                        engine->retriggers[ri].interval  = interval;
                        engine->retriggers[ri].count     = s->retrigger - 1;
                        engine->retriggers[ri].track     = (uint8_t)t;
                        engine->retriggers[ri].velocity  = vel;
                        engine->retriggers[ri].vel_decay = 0.75f;
                        engine->retriggers[ri].pitch_offset = s->pitch_offset;
                        engine->retriggers[ri].note      = s->note;
                        break;
                    }
                }
            }
        }
    }
}

/* ─── Pattern randomize ──────────────────────────────────────────────────── */

void sequencer_randomize_track(sq_engine_t *engine, int track_idx, float density)
{
    int pat_idx = engine->transport.current_pattern;
    if (pat_idx < 0 || (uint32_t)pat_idx >= engine->num_patterns) return;
    sq_pattern_t *pat = &engine->patterns[pat_idx];
    if (track_idx < 0 || (uint32_t)track_idx >= pat->num_tracks) return;

    sq_track_t *track = &pat->tracks[track_idx];
    for (uint32_t s = 0; s < track->length; s++) {
        /* Generate random value 0-99 using engine PRNG */
        uint32_t r = engine->rng_state;
        r ^= r << 13; r ^= r >> 17; r ^= r << 5;
        engine->rng_state = r;

        if ((float)(r % 100) / 100.0f < density) {
            /* Random velocity 60-127 */
            uint32_t r2 = engine->rng_state;
            r2 ^= r2 << 13; r2 ^= r2 >> 17; r2 ^= r2 << 5;
            engine->rng_state = r2;
            track->steps[s].velocity = (uint8_t)(60 + (r2 % 68));
        } else {
            track->steps[s].velocity = 0;
        }
    }
}

/* ─── Euclidean rhythm (Bjorklund algorithm) ─────────────────────────────── */

void sequencer_euclidean_fill(sq_engine_t *engine, int track_idx,
                               int pulses, int steps, int rotation,
                               uint8_t velocity)
{
    int pat_idx = engine->transport.current_pattern;
    if (pat_idx < 0 || (uint32_t)pat_idx >= engine->num_patterns) return;
    sq_pattern_t *pat = &engine->patterns[pat_idx];
    if (track_idx < 0 || (uint32_t)track_idx >= pat->num_tracks) return;
    if (steps < 1 || steps > (int)SQ_MAX_STEPS) return;
    if (pulses < 0) pulses = 0;
    if (pulses > steps) pulses = steps;

    /* Bjorklund algorithm — build pattern in temp array */
    uint8_t pattern[SQ_MAX_STEPS];
    memset(pattern, 0, sizeof(pattern));

    if (pulses == 0) {
        /* All silent — already zeroed */
    } else if (pulses == steps) {
        /* All active */
        for (int i = 0; i < steps; i++) pattern[i] = 1;
    } else {
        /* Bresenham-style distribution */
        for (int i = 0; i < steps; i++) {
            /* Threshold: if (i * pulses / steps) changes, place a hit */
            if ((i * pulses % steps) < pulses)
                pattern[i] = 1;
        }
    }

    /* Apply to track with rotation */
    sq_track_t *track = &pat->tracks[track_idx];
    track->length = (uint32_t)steps;
    for (int s = 0; s < steps; s++) {
        int src = ((s - rotation) % steps + steps) % steps;
        track->steps[s].velocity = pattern[src] ? velocity : 0;
    }
}

/* ─── Groove templates ───────────────────────────────────────────────────── */

static const sq_groove_template_t s_grooves[SQ_NUM_GROOVE_TEMPLATES] = {
    { "Straight",
      {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
      {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1} },
    { "MPC 54% Swing",
      {0,0.04f,0,0.04f,0,0.04f,0,0.04f,0,0.04f,0,0.04f,0,0.04f,0,0.04f},
      {1,0.8f,1,0.8f,1,0.8f,1,0.8f,1,0.8f,1,0.8f,1,0.8f,1,0.8f} },
    { "808 Shuffle",
      {0,0.08f,0,0.08f,0,0.08f,0,0.08f,0,0.08f,0,0.08f,0,0.08f,0,0.08f},
      {1,0.7f,1,0.7f,1,0.7f,1,0.7f,1,0.7f,1,0.7f,1,0.7f,1,0.7f} },
    { "Linndrum 66%",
      {0,0.12f,0,0.12f,0,0.12f,0,0.12f,0,0.12f,0,0.12f,0,0.12f,0,0.12f},
      {1,0.65f,1,0.65f,1,0.65f,1,0.65f,1,0.65f,1,0.65f,1,0.65f,1,0.65f} },
};

const sq_groove_template_t *sequencer_get_groove(int index)
{
    if (index < 0 || index >= SQ_NUM_GROOVE_TEMPLATES) return &s_grooves[0];
    return &s_grooves[index];
}

void sequencer_apply_groove(sq_engine_t *engine, int groove_index)
{
    if (groove_index < 0 || groove_index >= SQ_NUM_GROOVE_TEMPLATES) return;
    const sq_groove_template_t *g = &s_grooves[groove_index];

    int pat_idx = engine->transport.current_pattern;
    if (pat_idx < 0 || (uint32_t)pat_idx >= engine->num_patterns) return;
    sq_pattern_t *pat = &engine->patterns[pat_idx];

    for (uint32_t t = 0; t < pat->num_tracks; t++) {
        sq_track_t *track = &pat->tracks[t];
        for (uint32_t s = 0; s < track->length; s++) {
            /* Apply groove timing (wraps every 16 steps) */
            track->steps[s].micro_offset = g->timing[s % 16];
            /* Apply groove velocity scaling (only for active steps, skip Straight) */
            if (track->steps[s].velocity > 0 && groove_index > 0) {
                int v = (int)(track->steps[s].velocity * g->velocity[s % 16]);
                if (v < 1) v = 1;
                if (v > 127) v = 127;
                track->steps[s].velocity = (uint8_t)v;
            }
        }
    }
}

/* ─── Sample slicing (onset detection) ───────────────────────────────────── */

int sequencer_slice_sample(sq_engine_t *engine, int track_idx, int sample_idx,
                            float threshold)
{
    int pat_idx = engine->transport.current_pattern;
    if (pat_idx < 0 || (uint32_t)pat_idx >= engine->num_patterns) return 0;
    sq_pattern_t *pat = &engine->patterns[pat_idx];
    if (track_idx < 0 || (uint32_t)track_idx >= pat->num_tracks) return 0;
    if (sample_idx < 0 || (uint32_t)sample_idx >= engine->num_samples) return 0;

    sq_sample_t *s = &engine->samples[sample_idx];
    if (!s->data || s->num_frames < 256) return 0;

    sq_track_t *track = &pat->tracks[track_idx];
    uint32_t ch = s->num_channels;

    /* Onset detection: compute RMS energy in windows and detect rises */
    uint32_t window = 512;
    uint32_t slice_starts[64];
    int num_slices = 0;
    float prev_rms = 0.0f;

    slice_starts[0] = 0;
    num_slices = 1;

    for (uint32_t f = 0; f + window < s->num_frames && num_slices < 64; f += window) {
        float sum = 0.0f;
        for (uint32_t i = f; i < f + window; i++) {
            float val = s->data[i * ch]; /* use left channel */
            sum += val * val;
        }
        float rms = sum / (float)window;

        /* Onset: RMS jumps above threshold relative to previous */
        if (rms > threshold && prev_rms < threshold * 0.3f && f > 0) {
            slice_starts[num_slices++] = f;
        }
        prev_rms = rms;
    }

    /* Map slices to steps */
    int max_steps = num_slices;
    if (max_steps > (int)SQ_MAX_STEPS) max_steps = (int)SQ_MAX_STEPS;

    /* Clear track and set up */
    for (uint32_t i = 0; i < SQ_MAX_STEPS; i++)
        track->steps[i].velocity = 0;

    track->type = TRACK_SAMPLER;
    track->sample_index = sample_idx;
    track->length = (uint32_t)max_steps;

    for (int i = 0; i < max_steps; i++) {
        track->steps[i].velocity = 100;
        track->sample_start = slice_starts[i];
        track->sample_end = (i + 1 < num_slices)
                            ? slice_starts[i + 1]
                            : s->num_frames;
    }

    return num_slices;
}
