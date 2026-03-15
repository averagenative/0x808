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
#include "engine/command_queue.h"

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

#ifdef __cplusplus
}
#endif

#endif /* SQ_MIDI_H */
