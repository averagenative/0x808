/*
 * sq_midi.cpp — MIDI input wrapper using RtMidi's C API.
 *
 * Handles note on/off, CC messages (mapped to engine parameters),
 * pitch bend, program change, and sustain pedal. Supports MIDI learn
 * mode and switchable input modes (synth vs drum pads).
 *
 * This file is C++ because RtMidi is C++, but exposes a pure C API
 * via sq_midi.h.
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

/* ─── GM drum note → sample index mapping ────────────────────────────────── */

/* Standard GM drum map: MIDI note → sample track index.
 * -1 = unmapped. Covers the most common percussion notes. */
static int gm_drum_map_lookup(uint8_t note)
{
    switch (note) {
    case 36: return 0;  /* C2  = Bass Drum 1 (kick) */
    case 38: return 1;  /* D2  = Snare */
    case 39: return 3;  /* D#2 = Hand Clap */
    case 40: return 1;  /* E2  = Electric Snare (→ snare) */
    case 42: return 2;  /* F#2 = Closed Hi-Hat */
    case 44: return 2;  /* G#2 = Pedal Hi-Hat (→ closed hat) */
    case 46: return 4;  /* A#2 = Open Hi-Hat */
    case 49: return 5;  /* C#3 = Crash Cymbal */
    case 51: return 5;  /* D#3 = Ride Cymbal */
    case 56: return 5;  /* G#3 = Cowbell */
    default: return -1;
    }
}

struct sq_midi {
    RtMidiInPtr           midiin;
    RtMidiOutPtr          midiout;
    sq_command_queue_t   *cmd_queue;
    int                   preset;
    int                   open_port;
    int                   open_out_port;
    char                  port_names[32][128];
    int                   port_count;
    char                  out_port_names[32][128];
    int                   out_port_count;

    /* CC mapping & learn */
    sq_midi_cc_map_t      cc_map;
    sq_param_id_t         learn_param;   /* SQ_PARAM_NONE when not learning */

    /* Input mode */
    sq_midi_input_mode_t  input_mode;

    /* Sustain pedal state */
    bool                  sustain_held;
};

/* ─── Factory CC map defaults ────────────────────────────────────────────── */

extern "C" void sq_midi_cc_map_init(sq_midi_cc_map_t *m)
{
    memset(m->map, SQ_MIDI_CC_UNASSIGNED, sizeof(m->map));

    /* GM2 / universal standard CCs */
    m->map[1]  = SQ_PARAM_FILTER_CUTOFF;    /* Mod wheel → filter sweep */
    m->map[7]  = SQ_PARAM_MASTER_VOLUME;    /* Volume */
    m->map[71] = SQ_PARAM_FILTER_RESONANCE; /* GM2 Timbre */
    m->map[72] = SQ_PARAM_AMP_RELEASE;      /* GM2 Release Time */
    m->map[73] = SQ_PARAM_AMP_ATTACK;       /* GM2 Attack Time */
    m->map[74] = SQ_PARAM_FILTER_CUTOFF;    /* GM2 Brightness */
    m->map[91] = SQ_PARAM_REVERB_WET;       /* Effects 1 Depth */
    m->map[93] = SQ_PARAM_DELAY_WET;        /* Effects 3 Depth */

    /* Akai MPK Mini MK3 defaults (CC 70-77) */
    m->map[70] = SQ_PARAM_FILTER_CUTOFF;
    /* 71 already set above */
    /* 72 already set above */
    /* 73 already set above */
    /* 74 already set above */
    m->map[75] = SQ_PARAM_AMP_DECAY;
    m->map[76] = SQ_PARAM_AMP_SUSTAIN;
    m->map[77] = SQ_PARAM_DELAY_WET;

    /* Novation Launchkey Mini (CC 21-28) */
    m->map[21] = SQ_PARAM_FILTER_CUTOFF;
    m->map[22] = SQ_PARAM_FILTER_RESONANCE;
    m->map[23] = SQ_PARAM_AMP_ATTACK;
    m->map[24] = SQ_PARAM_AMP_DECAY;
    m->map[25] = SQ_PARAM_AMP_SUSTAIN;
    m->map[26] = SQ_PARAM_AMP_RELEASE;
    m->map[27] = SQ_PARAM_REVERB_WET;
    m->map[28] = SQ_PARAM_DELAY_WET;
}

/* ─── MIDI callback ──────────────────────────────────────────────────────── */

static void midi_callback(double /*timestamp*/, const unsigned char *message,
                          size_t message_size, void *userdata)
{
    sq_midi_t *midi = (sq_midi_t *)userdata;
    if (!midi || !midi->cmd_queue || message_size < 1) return;

    unsigned char status = message[0] & 0xF0;
    unsigned char data1  = (message_size >= 2) ? message[1] : 0;
    unsigned char data2  = (message_size >= 3) ? message[2] : 0;

    switch (status) {
    case 0x90: /* Note On */
        if (data2 > 0) {
            if (midi->input_mode == SQ_MIDI_MODE_DRUM_PADS) {
                /* GM drum map: trigger sampler track */
                int track = gm_drum_map_lookup(data1);
                /* Fallback: if GM map misses, map sequentially from note 36 or 48 */
                if (track < 0) {
                    if (data1 >= 36 && data1 < 44)
                        track = data1 - 36;  /* notes 36-43 → tracks 0-7 */
                    else if (data1 >= 48 && data1 < 56)
                        track = data1 - 48;  /* notes 48-55 → tracks 0-7 */
                }
                LOG_WARN("Drum pad: note=%d vel=%d -> track=%d", data1, data2, track);
                if (track >= 0) {
                    sq_command_t cmd;
                    memset(&cmd, 0, sizeof(cmd));
                    /* Use CMD_TRIGGER_NOTE with a negative preset to signal
                     * "trigger sampler track N" to the engine */
                    cmd.type = CMD_TRIGGER_NOTE;
                    cmd.note.preset    = -(track + 1); /* negative = sampler track */
                    cmd.note.midi_note = data1;
                    /* Floor velocity at 0.5 — cheap pads send very low values */
                    float pad_vel = (float)data2 / 127.0f;
                    if (pad_vel < 0.5f) pad_vel = 0.5f;
                    cmd.note.velocity  = pad_vel;
                    cmd.note.volume    = 0.8f;
                    cmd.note.pan       = 0.0f;
                    cmd_queue_push(midi->cmd_queue, &cmd);
                }
            } else {
                /* Synth mode */
                sq_command_t cmd;
                memset(&cmd, 0, sizeof(cmd));
                cmd.type = CMD_TRIGGER_NOTE;
                cmd.note.preset    = midi->preset;
                cmd.note.midi_note = data1;
                cmd.note.velocity  = (float)data2 / 127.0f;
                cmd.note.volume    = 0.7f;
                cmd.note.pan       = 0.0f;
                cmd_queue_push(midi->cmd_queue, &cmd);
            }
            break;
        }
        /* vel=0 is note off, fall through */
        /* fallthrough */
    case 0x80: /* Note Off */
        if (midi->input_mode == SQ_MIDI_MODE_SYNTH) {
            sq_command_t cmd;
            memset(&cmd, 0, sizeof(cmd));
            cmd.type = CMD_RELEASE_NOTE;
            cmd.note.midi_note = data1;
            cmd_queue_push(midi->cmd_queue, &cmd);
        }
        break;

    case 0xB0: /* Control Change */
    {
        uint8_t cc  = data1;
        uint8_t val = data2;

        /* Sustain pedal (CC64) */
        if (cc == 64) {
            midi->sustain_held = (val >= 64);
            /* TODO: hold/release synth notes based on pedal state */
            break;
        }

        /* MIDI learn mode: assign this CC to the learning parameter */
        if (midi->learn_param != SQ_PARAM_NONE) {
            /* Clear any existing mapping for this CC */
            midi->cc_map.map[cc] = (int8_t)midi->learn_param;
            LOG_INFO("MIDI Learn: CC%d → param %d", cc, midi->learn_param);
            midi->learn_param = SQ_PARAM_NONE;
            break;
        }

        /* Look up CC in map and push command */
        int8_t param = midi->cc_map.map[cc];
        if (param != SQ_MIDI_CC_UNASSIGNED) {
            sq_command_t cmd;
            memset(&cmd, 0, sizeof(cmd));
            cmd.type = CMD_MIDI_CC;
            cmd.midi_cc.cc    = cc;
            cmd.midi_cc.value = val;
            cmd_queue_push(midi->cmd_queue, &cmd);
        }
        break;
    }

    case 0xE0: /* Pitch Bend */
    {
        int16_t bend = (int16_t)(((uint16_t)data2 << 7) | (uint16_t)data1) - 8192;
        sq_command_t cmd;
        memset(&cmd, 0, sizeof(cmd));
        cmd.type = CMD_PITCH_BEND;
        cmd.pitch_bend.value = bend;
        cmd_queue_push(midi->cmd_queue, &cmd);
        break;
    }

    case 0xC0: /* Program Change */
        /* Map program 0-3 to kit select, 4+ to pattern select */
        if (data1 < 4) {
            /* Kit select — push as a pattern change for now.
             * TODO: proper kit switching via command queue */
            LOG_INFO("MIDI Program Change: %d (kit select)", data1);
        } else {
            sq_command_t cmd;
            memset(&cmd, 0, sizeof(cmd));
            cmd.type = CMD_SET_PATTERN;
            cmd.int_val = data1 - 4;
            cmd_queue_push(midi->cmd_queue, &cmd);
        }
        break;

    default:
        break;
    }
}

/* ─── Public API ─────────────────────────────────────────────────────────── */

extern "C" sq_midi_t *sq_midi_init(sq_command_queue_t *cmd_queue, int preset)
{
    sq_midi_t *midi = (sq_midi_t *)calloc(1, sizeof(sq_midi_t));
    if (!midi) return NULL;

    midi->cmd_queue    = cmd_queue;
    midi->preset       = preset;
    midi->open_port    = -1;
    midi->open_out_port = -1;
    midi->learn_param  = SQ_PARAM_NONE;
    midi->input_mode   = SQ_MIDI_MODE_SYNTH;

    /* Initialize factory CC defaults */
    sq_midi_cc_map_init(&midi->cc_map);

    midi->midiin = rtmidi_in_create_default();
    if (!midi->midiin || !midi->midiin->ok) {
        LOG_ERROR("Failed to create RtMidi input: %s",
                  (midi->midiin && midi->midiin->msg) ? midi->midiin->msg : "alloc failed");
        if (midi->midiin) rtmidi_in_free(midi->midiin);
        free(midi);
        return NULL;
    }

    /* Ignore sysex/timing/active-sensing */
    rtmidi_in_ignore_types(midi->midiin, true, true, true);

    /* Create MIDI output (optional — may fail without crashing) */
    midi->midiout = rtmidi_out_create_default();
    if (!midi->midiout || !midi->midiout->ok) {
        LOG_WARN("MIDI output not available");
        if (midi->midiout) { rtmidi_out_free(midi->midiout); midi->midiout = NULL; }
    }

    LOG_INFO("MIDI initialized (in: %d ports, out: %d ports)",
             rtmidi_get_port_count(midi->midiin),
             midi->midiout ? (int)rtmidi_get_port_count(midi->midiout) : 0);
    return midi;
}

extern "C" void sq_midi_shutdown(sq_midi_t *midi)
{
    if (!midi) return;
    if (midi->open_port >= 0)
        rtmidi_close_port(midi->midiin);
    if (midi->open_out_port >= 0 && midi->midiout)
        rtmidi_close_port(midi->midiout);
    if (midi->midiin)
        rtmidi_in_free(midi->midiin);
    if (midi->midiout)
        rtmidi_out_free(midi->midiout);
    free(midi);
    LOG_INFO("MIDI shut down");
}

extern "C" int sq_midi_get_port_count(sq_midi_t *midi)
{
    if (!midi || !midi->midiin) return 0;
    int count = (int)rtmidi_get_port_count(midi->midiin);
    midi->port_count = count > 32 ? 32 : count;
    for (int i = 0; i < midi->port_count; i++) {
        int buflen = (int)sizeof(midi->port_names[i]);
        rtmidi_get_port_name(midi->midiin, (unsigned int)i,
                             midi->port_names[i], &buflen);
    }
    return midi->port_count;
}

extern "C" const char *sq_midi_get_port_name(sq_midi_t *midi, int index)
{
    if (!midi || index < 0 || index >= midi->port_count) return "";
    return midi->port_names[index];
}

extern "C" int sq_midi_open_port(sq_midi_t *midi, int index)
{
    if (!midi || !midi->midiin) return -1;

    if (midi->open_port >= 0) {
        rtmidi_close_port(midi->midiin);
        rtmidi_in_cancel_callback(midi->midiin);
        midi->open_port = -1;
    }

    if (index < 0) return 0;

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

extern "C" void sq_midi_close_port(sq_midi_t *midi)
{
    if (!midi || midi->open_port < 0) return;
    rtmidi_close_port(midi->midiin);
    rtmidi_in_cancel_callback(midi->midiin);
    LOG_INFO("MIDI port closed");
    midi->open_port = -1;
}

extern "C" void sq_midi_set_preset(sq_midi_t *midi, int preset)
{
    if (midi) midi->preset = preset;
}

extern "C" int sq_midi_get_open_port(sq_midi_t *midi)
{
    return midi ? midi->open_port : -1;
}

extern "C" sq_midi_cc_map_t *sq_midi_get_cc_map(sq_midi_t *midi)
{
    return midi ? &midi->cc_map : NULL;
}

extern "C" void sq_midi_learn_start(sq_midi_t *midi, sq_param_id_t param)
{
    if (midi) midi->learn_param = param;
}

extern "C" void sq_midi_learn_cancel(sq_midi_t *midi)
{
    if (midi) midi->learn_param = SQ_PARAM_NONE;
}

extern "C" sq_param_id_t sq_midi_learn_active(sq_midi_t *midi)
{
    return midi ? midi->learn_param : SQ_PARAM_NONE;
}

extern "C" void sq_midi_set_input_mode(sq_midi_t *midi, sq_midi_input_mode_t mode)
{
    if (midi) midi->input_mode = mode;
}

extern "C" sq_midi_input_mode_t sq_midi_get_input_mode(sq_midi_t *midi)
{
    return midi ? midi->input_mode : SQ_MIDI_MODE_SYNTH;
}

/* ─── MIDI Output ────────────────────────────────────────────────────────── */

extern "C" int sq_midi_get_output_port_count(sq_midi_t *midi)
{
    if (!midi || !midi->midiout) return 0;
    int count = (int)rtmidi_get_port_count(midi->midiout);
    midi->out_port_count = count > 32 ? 32 : count;
    for (int i = 0; i < midi->out_port_count; i++) {
        int buflen = (int)sizeof(midi->out_port_names[i]);
        rtmidi_get_port_name(midi->midiout, (unsigned int)i,
                             midi->out_port_names[i], &buflen);
    }
    return midi->out_port_count;
}

extern "C" const char *sq_midi_get_output_port_name(sq_midi_t *midi, int index)
{
    if (!midi || index < 0 || index >= midi->out_port_count) return "";
    return midi->out_port_names[index];
}

extern "C" int sq_midi_open_output_port(sq_midi_t *midi, int index)
{
    if (!midi || !midi->midiout) return -1;
    if (midi->open_out_port >= 0) {
        rtmidi_close_port(midi->midiout);
        midi->open_out_port = -1;
    }
    if (index < 0) return 0;
    rtmidi_open_port(midi->midiout, (unsigned int)index, "0x808 MIDI Out");
    if (!midi->midiout->ok) {
        LOG_ERROR("Failed to open MIDI output port %d", index);
        return -1;
    }
    midi->open_out_port = index;
    LOG_INFO("MIDI output opened: %d (%s)", index,
             index < midi->out_port_count ? midi->out_port_names[index] : "?");
    return 0;
}

extern "C" void sq_midi_close_output_port(sq_midi_t *midi)
{
    if (!midi || midi->open_out_port < 0) return;
    rtmidi_close_port(midi->midiout);
    midi->open_out_port = -1;
}

extern "C" int sq_midi_get_open_output_port(sq_midi_t *midi)
{
    return midi ? midi->open_out_port : -1;
}

extern "C" void sq_midi_send_note_on(sq_midi_t *midi, uint8_t channel,
                                      uint8_t note, uint8_t velocity)
{
    if (!midi || !midi->midiout || midi->open_out_port < 0) return;
    unsigned char msg[3] = { (unsigned char)(0x90 | (channel & 0x0F)), note, velocity };
    rtmidi_out_send_message(midi->midiout, msg, 3);
}

extern "C" void sq_midi_send_note_off(sq_midi_t *midi, uint8_t channel, uint8_t note)
{
    if (!midi || !midi->midiout || midi->open_out_port < 0) return;
    unsigned char msg[3] = { (unsigned char)(0x80 | (channel & 0x0F)), note, 0 };
    rtmidi_out_send_message(midi->midiout, msg, 3);
}
