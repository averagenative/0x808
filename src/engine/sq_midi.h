/*
 * sq_midi.h — MIDI input for standalone frontends.
 *
 * Wraps RtMidi's C API. MIDI note-on/off messages are pushed to the
 * engine's lock-free command queue for processing in the audio thread.
 *
 * Not used in plugin builds (DAW provides MIDI via CPLUG).
 */

#ifndef SQ_MIDI_H
#define SQ_MIDI_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "engine/engine.h"

/* ─── MIDI CC mapping ────────────────────────────────────────────────────── */

#define SQ_MIDI_CC_UNASSIGNED (-1)

/* CC-to-parameter mapping table: 128 CC slots → param ID */
typedef struct {
    int8_t map[128];  /* SQ_PARAM_* or SQ_MIDI_CC_UNASSIGNED (-1) */
} sq_midi_cc_map_t;

/* Initialize CC map with factory defaults (GM2 + common controller mappings) */
void sq_midi_cc_map_init(sq_midi_cc_map_t *m);

/* Opaque handle */
typedef struct sq_midi sq_midi_t;

/*
 * Initialize MIDI subsystem. Returns handle or NULL on failure.
 * cmd_queue: engine command queue for pushing note events.
 * preset: synth preset index to use for triggered notes.
 */
sq_midi_t *sq_midi_init(sq_command_queue_t *cmd_queue, int preset);

/* Shut down and free resources. Safe to call with NULL. */
void sq_midi_shutdown(sq_midi_t *midi);

/* Get number of available MIDI input ports. */
int sq_midi_get_port_count(sq_midi_t *midi);

/* Get name of a MIDI input port. Returns "" on invalid index. */
const char *sq_midi_get_port_name(sq_midi_t *midi, int index);

/* Open a MIDI input port by index. Returns 0 on success, -1 on failure. */
int sq_midi_open_port(sq_midi_t *midi, int index);

/* Close the currently open port (if any). */
void sq_midi_close_port(sq_midi_t *midi);

/* Set the synth preset used for MIDI-triggered notes. */
void sq_midi_set_preset(sq_midi_t *midi, int preset);

/* Get index of currently open port (-1 if none). */
int sq_midi_get_open_port(sq_midi_t *midi);

/* Get/set the CC mapping table. */
sq_midi_cc_map_t *sq_midi_get_cc_map(sq_midi_t *midi);

/* MIDI learn: start learning for a parameter, cancel, or check active. */
void sq_midi_learn_start(sq_midi_t *midi, sq_param_id_t param);
void sq_midi_learn_cancel(sq_midi_t *midi);
sq_param_id_t sq_midi_learn_active(sq_midi_t *midi);

/* MIDI input mode */
typedef enum {
    SQ_MIDI_MODE_SYNTH = 0,  /* notes → synth (default) */
    SQ_MIDI_MODE_DRUM_PADS,  /* notes → GM drum map → sampler tracks */
} sq_midi_input_mode_t;

void sq_midi_set_input_mode(sq_midi_t *midi, sq_midi_input_mode_t mode);
sq_midi_input_mode_t sq_midi_get_input_mode(sq_midi_t *midi);

/* ─── MIDI Output ────────────────────────────────────────────────────────── */

/* Get number of available MIDI output ports. */
int sq_midi_get_output_port_count(sq_midi_t *midi);

/* Get name of a MIDI output port. */
const char *sq_midi_get_output_port_name(sq_midi_t *midi, int index);

/* Open/close a MIDI output port. */
int sq_midi_open_output_port(sq_midi_t *midi, int index);
void sq_midi_close_output_port(sq_midi_t *midi);
int sq_midi_get_open_output_port(sq_midi_t *midi);

/* Send MIDI note on/off to output. Called from sequencer. */
void sq_midi_send_note_on(sq_midi_t *midi, uint8_t channel, uint8_t note, uint8_t velocity);
void sq_midi_send_note_off(sq_midi_t *midi, uint8_t channel, uint8_t note);

#ifdef __cplusplus
}
#endif

#endif /* SQ_MIDI_H */
