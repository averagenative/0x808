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

    /*
     * Find the longest track length in this pattern.
     * The transport needs to know the pattern length to wrap around.
     * We use the first track's length for now (all tracks typically match).
     * Phase 2 TASK-017 adds polymetric support (per-track lengths).
     */
    uint32_t pattern_length = pattern->tracks[0].length;

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

        /* Trigger based on track type */
        if (track->type == TRACK_SAMPLER && track->sample_index >= 0) {
            sampler_trigger(engine, track->sample_index,
                           vel, s->pitch_offset,
                           track->volume, track->pan);
        } else if (track->type == TRACK_SYNTH && track->synth_preset >= 0) {
            synth_trigger(engine, track->synth_preset,
                         vel, s->pitch_offset,
                         track->volume, track->pan, s->note);
        } else if (track->type == TRACK_SF2 && track->sf2_preset >= 0) {
            int key = (s->note > 0) ? s->note : 60;
            key += s->pitch_offset;
            if (key < 0) key = 0;
            if (key > 127) key = 127;
            sf2_note_on(engine, track->sf2_preset, key, vel * track->volume);
        }
    }
}
