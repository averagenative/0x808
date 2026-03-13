/*
 * transport.c — BPM clock converting sample position to step position.
 *
 * How the timing math works:
 *
 * We think in "16th notes" (steps). At 120 BPM:
 *   - 1 beat = 1 quarter note = 4 steps (16th notes)
 *   - beats per second = BPM / 60 = 2
 *   - steps per second = beats_per_second * 4 = 8
 *   - samples per step = sample_rate / steps_per_second = 44100 / 8 = 5512.5
 *
 * We track position as a fractional beat count (double precision).
 * Each audio buffer advances the beat position by:
 *   beat_increment = num_frames * (bpm / 60.0) / sample_rate
 *
 * The current step is: floor(current_beat * 4) % pattern_length
 * (because there are 4 sixteenth-note steps per beat)
 *
 * Swing:
 * Swing delays every other 16th note (odd-indexed steps within each beat).
 * With swing=0.0, steps are evenly spaced at 0.00, 0.25, 0.50, 0.75 beats.
 * With swing=1.0 (max), odd steps shift to triplet positions:
 *   0.00, 0.417, 0.50, 0.917 (2/3 through each half-beat).
 * The swing offset = swing * (1/6) of a beat.
 */

#include "engine/transport.h"
#include <math.h>

/* Steps per beat — we use 16th note resolution (4 steps per quarter note) */
#define STEPS_PER_BEAT 4

void transport_init(sq_transport_t *transport)
{
    transport->bpm             = 120.0;
    transport->current_beat    = 0.0;
    transport->sample_position = 0;
    transport->playing         = false;
    transport->mode            = MODE_PATTERN;
    transport->current_pattern = 0;
    transport->current_step    = 0;
    transport->current_section = 0;
    transport->queued_section  = -1;
    transport->swing           = 0.0f;
}

/*
 * Convert a beat position to a step number, accounting for swing.
 *
 * Within each beat (4 steps), the boundaries are:
 *   Step 0: 0.00
 *   Step 1: 0.25 + swing_offset  (delayed)
 *   Step 2: 0.50
 *   Step 3: 0.75 + swing_offset  (delayed)
 *
 * swing_offset = swing * (1/6), max ≈ 0.167 beats
 * This shifts odd steps from 50% to ~67% (triplet feel) at max swing.
 */
static int beat_to_step(double beat, float swing)
{
    double sw = (double)swing * (1.0 / 6.0);
    int base_beat = (int)floor(beat);
    double frac = beat - (double)base_beat;

    /* Determine which step within this beat */
    int step_in_beat;
    if (frac < 0.25 + sw)
        step_in_beat = 0;
    else if (frac < 0.50)
        step_in_beat = 1;
    else if (frac < 0.75 + sw)
        step_in_beat = 2;
    else
        step_in_beat = 3;

    return base_beat * STEPS_PER_BEAT + step_in_beat;
}

int transport_advance(sq_transport_t *transport, uint32_t sample_rate,
                      uint32_t num_frames, uint32_t pattern_length)
{
    if (!transport->playing || pattern_length == 0) {
        return -1;
    }

    /* Remember the step we were on before advancing */
    int old_step = transport->current_step;

    /*
     * Advance beat position.
     * beat_increment = how many beats this buffer covers
     *
     * Example at 120 BPM, 44100 Hz, 512-frame buffer:
     *   beat_increment = 512 * (120 / 60) / 44100 = 512 * 2 / 44100 ≈ 0.0232 beats
     */
    double beat_increment = (double)num_frames * (transport->bpm / 60.0)
                          / (double)sample_rate;
    transport->current_beat += beat_increment;
    transport->sample_position += num_frames;

    /*
     * Convert beat position to step number with swing applied.
     */
    int absolute_step = beat_to_step(transport->current_beat, transport->swing);
    int new_step = absolute_step % (int)pattern_length;

    transport->current_step = new_step;

    /* Detect pattern wrap (step went from end back to beginning) */
    if (new_step != old_step && new_step == 0 && old_step > 0) {
        transport->pattern_completed = true;
    }

    /*
     * Did we cross into a new step?
     * If yes, return the new step number so the sequencer knows to trigger.
     * If no, return -1 (nothing to do).
     */
    if (new_step != old_step) {
        return new_step;
    }

    return -1;
}
