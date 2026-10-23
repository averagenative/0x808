/*
 * midi_out.h — MIDI note layout, and the sequencer's MIDI output queue.
 *
 * The layout is shared by MIDI output and the plugin's MIDI input:
 *   - Sampler (drum) tracks use channel 10 and the General MIDI drum note
 *     of their kit slot (kick 36, snare 38, closed hat 42, clap 39, ...).
 *   - Synth and SoundFont tracks take channels 1, 2, 3, ... in track
 *     order, skipping 10, and use their own notes.
 *
 * Output is off unless engine->midi_out_enabled is set. The engine calls
 * these from sq_engine_process(); everything here is real-time safe.
 */

#ifndef SQ_MIDI_OUT_H
#define SQ_MIDI_OUT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "engine/engine.h"

#define SQ_MIDI_DRUM_CHANNEL 9   /* 0-based: MIDI channel 10 */

/* Channel (0-15) a track's notes use */
int sq_midi_track_channel(const sq_pattern_t *pattern, int track);

/* Note a track plays for a step's note and pitch offset */
int sq_midi_track_note(const sq_track_t *track, int track_idx,
                       uint8_t note, int8_t pitch_offset);

/*
 * Track that should play an incoming note, or -1. Drum notes on channel 10
 * find the drum track with that note. Other channels find the melodic track
 * on that channel, or the first melodic track if none uses it.
 */
int sq_midi_find_track(const sq_pattern_t *pattern, int channel, int note);

/* Called by sq_engine_process() around each buffer */
void sq_midi_out_begin(sq_engine_t *engine);
void sq_midi_out_end(sq_engine_t *engine, uint32_t num_frames);

/*
 * Send a note for a track at engine->midi_frame, with its note-off
 * length_frames later. Does nothing unless MIDI output is enabled.
 */
void sq_midi_out_note(sq_engine_t *engine, int track, uint8_t note,
                      int8_t pitch_offset, float velocity,
                      uint32_t length_frames);

/* Send note-offs due by the end of [frame, frame + num_frames] */
void sq_midi_out_advance(sq_engine_t *engine, uint32_t frame,
                         uint32_t num_frames);

/* Send every pending note-off now (playback stopped) */
void sq_midi_out_flush(sq_engine_t *engine, uint32_t frame);

#ifdef __cplusplus
}
#endif

#endif /* SQ_MIDI_OUT_H */
