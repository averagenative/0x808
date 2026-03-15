/*
 * sq_midi.cpp — MIDI input wrapper using RtMidi's C API.
 *
 * MIDI note-on/off messages are pushed to the engine's lock-free command
 * queue for processing in the audio thread. This file is C++ because
 * RtMidi is C++, but exposes a pure C API via sq_midi.h.
 */

#include "sq_midi.h"
#include "rtmidi_c.h"

#include <cstdlib>
#include <cstring>
#include <cstdio>

extern "C" {
#define LOG_TAG "midi"
#include "core/log.h"
}

struct sq_midi {
    RtMidiInPtr         midiin;
    sq_command_queue_t *cmd_queue;
    int                 preset;
    int                 open_port;
    char                port_names[32][128];
    int                 port_count;
};

/* ─── MIDI callback ──────────────────────────────────────────────────────── */

static void midi_callback(double /*timestamp*/, const unsigned char *message,
                          size_t message_size, void *userdata)
{
    sq_midi_t *midi = (sq_midi_t *)userdata;
    if (!midi || !midi->cmd_queue || message_size < 2) return;

    unsigned char status = message[0] & 0xF0;
    unsigned char note   = message[1];
    unsigned char vel    = (message_size >= 3) ? message[2] : 0;

    if (status == 0x90 && vel > 0) {
        /* Note On */
        sq_command_t cmd;
        memset(&cmd, 0, sizeof(cmd));
        cmd.type = CMD_TRIGGER_NOTE;
        cmd.note.preset    = midi->preset;
        cmd.note.midi_note = note;
        cmd.note.velocity  = (float)vel / 127.0f;
        cmd.note.volume    = 0.7f;
        cmd.note.pan       = 0.0f;
        cmd_queue_push(midi->cmd_queue, &cmd);
    } else if (status == 0x80 || (status == 0x90 && vel == 0)) {
        /* Note Off */
        sq_command_t cmd;
        memset(&cmd, 0, sizeof(cmd));
        cmd.type = CMD_RELEASE_NOTE;
        cmd.note.midi_note = note;
        cmd_queue_push(midi->cmd_queue, &cmd);
    }
}

/* ─── Public API ─────────────────────────────────────────────────────────── */

sq_midi_t *sq_midi_init(sq_command_queue_t *cmd_queue, int preset)
{
    sq_midi_t *midi = (sq_midi_t *)calloc(1, sizeof(sq_midi_t));
    if (!midi) return NULL;

    midi->cmd_queue = cmd_queue;
    midi->preset    = preset;
    midi->open_port = -1;

    midi->midiin = rtmidi_in_create_default();
    if (!midi->midiin || !midi->midiin->ok) {
        LOG_ERROR("Failed to create RtMidi input: %s",
                  (midi->midiin && midi->midiin->msg) ? midi->midiin->msg : "alloc failed");
        if (midi->midiin) rtmidi_in_free(midi->midiin);
        free(midi);
        return NULL;
    }

    /* Don't ignore sysex/timing/active-sensing — we only care about notes
     * but let RtMidi filter at the callback level */
    rtmidi_in_ignore_types(midi->midiin, true, true, true);

    LOG_INFO("MIDI initialized (%d ports available)",
             rtmidi_get_port_count(midi->midiin));
    return midi;
}

void sq_midi_shutdown(sq_midi_t *midi)
{
    if (!midi) return;
    if (midi->open_port >= 0) {
        rtmidi_close_port(midi->midiin);
    }
    if (midi->midiin) {
        rtmidi_in_free(midi->midiin);
    }
    free(midi);
    LOG_INFO("MIDI shut down");
}

int sq_midi_get_port_count(sq_midi_t *midi)
{
    if (!midi || !midi->midiin) return 0;
    int count = (int)rtmidi_get_port_count(midi->midiin);
    /* Cache names */
    midi->port_count = count > 32 ? 32 : count;
    for (int i = 0; i < midi->port_count; i++) {
        int buflen = (int)sizeof(midi->port_names[i]);
        rtmidi_get_port_name(midi->midiin, (unsigned int)i,
                             midi->port_names[i], &buflen);
    }
    return midi->port_count;
}

const char *sq_midi_get_port_name(sq_midi_t *midi, int index)
{
    if (!midi || index < 0 || index >= midi->port_count) return "";
    return midi->port_names[index];
}

int sq_midi_open_port(sq_midi_t *midi, int index)
{
    if (!midi || !midi->midiin) return -1;

    /* Close existing port first */
    if (midi->open_port >= 0) {
        rtmidi_close_port(midi->midiin);
        rtmidi_in_cancel_callback(midi->midiin);
        midi->open_port = -1;
    }

    if (index < 0) return 0; /* just close */

    /* Set callback before opening port */
    rtmidi_in_set_callback(midi->midiin, midi_callback, midi);

    rtmidi_open_port(midi->midiin, (unsigned int)index, "0x808 MIDI In");
    if (!midi->midiin->ok) {
        LOG_ERROR("Failed to open MIDI port %d: %s",
                  index, midi->midiin->msg ? midi->midiin->msg : "unknown error");
        rtmidi_in_cancel_callback(midi->midiin);
        return -1;
    }

    midi->open_port = index;
    LOG_INFO("MIDI port opened: %d (%s)", index,
             index < midi->port_count ? midi->port_names[index] : "?");
    return 0;
}

void sq_midi_close_port(sq_midi_t *midi)
{
    if (!midi || midi->open_port < 0) return;
    rtmidi_close_port(midi->midiin);
    rtmidi_in_cancel_callback(midi->midiin);
    LOG_INFO("MIDI port closed");
    midi->open_port = -1;
}

void sq_midi_set_preset(sq_midi_t *midi, int preset)
{
    if (midi) midi->preset = preset;
}

int sq_midi_get_open_port(sq_midi_t *midi)
{
    return midi ? midi->open_port : -1;
}
