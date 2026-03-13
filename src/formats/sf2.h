/*
 * sf2.h — SoundFont2 (.sf2) loading and playback via TinySoundFont.
 */

#ifndef SQ_SF2_H
#define SQ_SF2_H

#include "engine/engine.h"

/*
 * Load a SoundFont file. Populates engine->sf2 and engine->sf2_presets.
 * Returns 0 on success, -1 on error.
 */
int sf2_load(sq_engine_t *engine, const char *filepath);

/*
 * Unload the current SoundFont and free resources.
 */
void sf2_unload(sq_engine_t *engine);

/*
 * Trigger a note on a SF2 preset.
 *   preset_idx: index into engine->sf2_presets[]
 *   key: MIDI note (0-127)
 *   vel: velocity (0.0-1.0)
 */
void sf2_note_on(sq_engine_t *engine, int preset_idx, int key, float vel);

/*
 * Stop a note on a SF2 preset.
 */
void sf2_note_off(sq_engine_t *engine, int preset_idx, int key);

/*
 * Render SF2 audio into a buffer (mixes into existing data).
 *   output: interleaved stereo float buffer
 *   num_frames: number of stereo frames to render
 */
void sf2_render(sq_engine_t *engine, float *output, uint32_t num_frames);

#endif /* SQ_SF2_H */
