/*
 * midi_out.c — MIDI note layout, and the sequencer's MIDI output queue.
 *
 * Events go into engine->midi_out[] with their frame offset in the current
 * buffer. A note-off waits in midi_pending_offs[] until its frame comes up.
 * An event due exactly at the end of a buffer can't be reported in that
 * buffer, so sq_midi_out_end() keeps it and the next buffer sends it at
 * frame 0.
 */

#include "engine/midi_out.h"

/* GM drum notes for the 8 kit slots (see kits.h for the slot order) */
static const uint8_t KIT_SLOT_NOTES[8] = {
    36,  /* kick           -> Bass Drum 1    */
    38,  /* snare          -> Acoustic Snare */
    42,  /* closed hihat   -> Closed Hi-Hat  */
    39,  /* clap / rimshot -> Hand Clap      */
    46,  /* open hihat     -> Open Hi-Hat    */
    56,  /* cowbell / perc -> Cowbell        */
    37,  /* tom / rimshot  -> Side Stick     */
    50,  /* hi tom / extra -> High Tom       */
};

/* Drum tracks playing samples outside the kit get C4 + track number */
#define CUSTOM_SAMPLE_BASE_NOTE 60

int sq_midi_track_channel(const sq_pattern_t *pattern, int track)
{
    if (pattern->tracks[track].type == TRACK_SAMPLER)
        return SQ_MIDI_DRUM_CHANNEL;

    int channel = 0;
    for (int t = 0; t < track; t++) {
        if (pattern->tracks[t].type != TRACK_SAMPLER)
            channel++;
    }
    if (channel >= SQ_MIDI_DRUM_CHANNEL) channel++;
    if (channel > 15) channel = 15;
    return channel;
}

int sq_midi_track_note(const sq_track_t *track, int track_idx,
                       uint8_t note, int8_t pitch_offset)
{
    if (track->type == TRACK_SAMPLER) {
        if (track->sample_index >= 0 && track->sample_index < 8)
            return KIT_SLOT_NOTES[track->sample_index];
        return CUSTOM_SAMPLE_BASE_NOTE + track_idx;
    }

    /* Same note the synth plays (synth_trigger() defaults to C4) */
    int key = (note > 0 ? (int)note : 60) + pitch_offset;
    if (key < 0) key = 0;
    if (key > 127) key = 127;
    return key;
}

int sq_midi_find_track(const sq_pattern_t *pattern, int channel, int note)
{
    if (channel == SQ_MIDI_DRUM_CHANNEL) {
        for (uint32_t t = 0; t < pattern->num_tracks; t++) {
            const sq_track_t *trk = &pattern->tracks[t];
            if (trk->type == TRACK_SAMPLER &&
                sq_midi_track_note(trk, (int)t, 0, 0) == note)
                return (int)t;
        }
        return -1;
    }

    int first_melodic = -1;
    for (uint32_t t = 0; t < pattern->num_tracks; t++) {
        if (pattern->tracks[t].type == TRACK_SAMPLER) continue;
        if (first_melodic < 0) first_melodic = (int)t;
        if (sq_midi_track_channel(pattern, (int)t) == channel)
            return (int)t;
    }
    return first_melodic;
}

/* ─── Output queue ───────────────────────────────────────────────────────── */

static bool push_event(sq_engine_t *engine, uint32_t limit, uint32_t frame,
                       uint8_t status, uint8_t data1, uint8_t data2)
{
    if (engine->midi_out_count >= limit) return false;
    sq_midi_event_t *ev = &engine->midi_out[engine->midi_out_count++];
    ev->frame  = frame;
    ev->status = status;
    ev->data1  = data1;
    ev->data2  = data2;
    return true;
}

/* Note-ons leave room for every pending note-off, so no note can hang */
#define NOTE_ON_LIMIT (SQ_MAX_MIDI_OUT - SQ_MAX_MIDI_PENDING)

static void push_note_off(sq_engine_t *engine, uint32_t frame,
                          sq_midi_pending_off_t *off)
{
    push_event(engine, SQ_MAX_MIDI_OUT, frame,
               (uint8_t)(0x80 | off->channel), off->note, 0);
    off->remaining = 0;
}

void sq_midi_out_begin(sq_engine_t *engine)
{
    /* Events held over from the end of the last buffer go out first */
    uint32_t carry = engine->midi_out_carry;
    for (uint32_t i = 0; i < carry; i++) {
        engine->midi_out[i] = engine->midi_out[engine->midi_out_count + i];
        engine->midi_out[i].frame = 0;
    }
    engine->midi_out_count = carry;
    engine->midi_out_carry = 0;
}

/* Note-offs sort before note-ons on the same frame */
static bool event_before(const sq_midi_event_t *a, const sq_midi_event_t *b)
{
    if (a->frame != b->frame) return a->frame < b->frame;
    return (a->status & 0xF0) == 0x80 && (b->status & 0xF0) != 0x80;
}

void sq_midi_out_end(sq_engine_t *engine, uint32_t num_frames)
{
    /* Stable insertion sort: few events, mostly in order already */
    sq_midi_event_t *ev = engine->midi_out;
    for (uint32_t i = 1; i < engine->midi_out_count; i++) {
        sq_midi_event_t cur = ev[i];
        uint32_t j = i;
        while (j > 0 && event_before(&cur, &ev[j - 1])) {
            ev[j] = ev[j - 1];
            j--;
        }
        ev[j] = cur;
    }

    /* Hold back events due at the very end of this buffer */
    while (engine->midi_out_count > 0 &&
           ev[engine->midi_out_count - 1].frame >= num_frames) {
        engine->midi_out_count--;
        engine->midi_out_carry++;
    }
}

void sq_midi_out_note(sq_engine_t *engine, int track, uint8_t note,
                      int8_t pitch_offset, float velocity,
                      uint32_t length_frames)
{
    if (!engine->midi_out_enabled) return;

    int pi = engine->transport.current_pattern;
    if (pi < 0 || (uint32_t)pi >= engine->num_patterns) return;
    const sq_pattern_t *pat = &engine->patterns[pi];
    if (track < 0 || (uint32_t)track >= pat->num_tracks) return;

    uint8_t channel = (uint8_t)sq_midi_track_channel(pat, track);
    uint8_t key = (uint8_t)sq_midi_track_note(&pat->tracks[track], track,
                                              note, pitch_offset);
    int vel = (int)(velocity * 127.0f + 0.5f);
    if (vel < 1) vel = 1;
    if (vel > 127) vel = 127;

    /* A new hit on a note that's still sounding ends the old one first */
    sq_midi_pending_off_t *slot = NULL;
    for (int i = 0; i < SQ_MAX_MIDI_PENDING; i++) {
        sq_midi_pending_off_t *off = &engine->midi_pending_offs[i];
        if (off->remaining > 0 && off->channel == channel && off->note == key)
            push_note_off(engine, engine->midi_frame, off);
        if (off->remaining == 0 && !slot)
            slot = off;
    }

    /* No room to schedule the note-off: skip the note rather than hang it */
    if (!slot) return;
    if (!push_event(engine, NOTE_ON_LIMIT, engine->midi_frame,
                    (uint8_t)(0x90 | channel), key, (uint8_t)vel))
        return;

    slot->remaining = length_frames > 0 ? length_frames : 1;
    slot->channel   = channel;
    slot->note      = key;
}

void sq_midi_out_advance(sq_engine_t *engine, uint32_t frame,
                         uint32_t num_frames)
{
    for (int i = 0; i < SQ_MAX_MIDI_PENDING; i++) {
        sq_midi_pending_off_t *off = &engine->midi_pending_offs[i];
        if (off->remaining == 0) continue;
        if (off->remaining <= num_frames)
            push_note_off(engine, frame + off->remaining, off);
        else
            off->remaining -= num_frames;
    }
}

void sq_midi_out_flush(sq_engine_t *engine, uint32_t frame)
{
    for (int i = 0; i < SQ_MAX_MIDI_PENDING; i++) {
        sq_midi_pending_off_t *off = &engine->midi_pending_offs[i];
        if (off->remaining > 0)
            push_note_off(engine, frame, off);
    }
}
