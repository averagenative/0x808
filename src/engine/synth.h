/*
 * synth.h — Subtractive synthesizer: oscillators, filter, envelopes.
 */

#ifndef SQ_SYNTH_H
#define SQ_SYNTH_H

#include "engine/engine.h"

/* Generate wavetables at startup (saw, square, triangle, sine) */
void synth_init_wavetables(sq_wavetables_t *wt);

/* Generate bundled wavetable banks */
void synth_init_wt_banks(sq_engine_t *engine);

/* Load a wavetable bank from a WAV file.
 * Each cycle is SQ_WAVETABLE_SIZE (2048) samples.
 * Total frames in bank = file_length / cycle_length.
 * Returns the bank index on success, -1 on failure.
 */
int synth_load_wt_bank(sq_engine_t *engine, const char *filepath, const char *name);

/* Create default synth presets */
void synth_init_presets(sq_engine_t *engine);

/* Trigger a new synth voice */
void synth_trigger(sq_engine_t *engine, int preset_index,
                   float velocity, int pitch_offset,
                   float volume, float pan, uint8_t note);

/* Release all active synth voices (enter release phase) */
void synth_release_all(sq_engine_t *engine);

/* Render all active synth voices into the output buffer (additive) */
void synth_render(sq_engine_t *engine, float *output, uint32_t num_frames);

#endif /* SQ_SYNTH_H */
