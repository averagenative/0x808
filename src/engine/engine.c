/*
 * engine.c — Top-level engine: init, shutdown, and the main process function.
 *
 * sq_engine_process() is the single entry point called by both the standalone
 * audio callback and the plugin process callback. It coordinates:
 *   transport → sequencer → sampler → mixer → output
 *
 * The pipeline per audio buffer:
 * 1. Advance the transport clock by buffer size
 * 2. If we crossed a step boundary, tell the sequencer to trigger voices
 * 3. Render all active voices through the mixer into the output buffer
 */

#include "engine/engine.h"
#include "engine/command_queue.h"
#include "engine/mixer.h"
#include "engine/transport.h"
#include "engine/sequencer.h"
#include "engine/synth.h"
#include "engine/envelope.h"
#include "engine/sampler.h"
#include "formats/sf2.h"
#include "formats/sample_io.h"
#include "formats/project.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>

#ifdef _WIN32
#include <windows.h>
#define sq_sleep_ms(ms) Sleep(ms)
#else
#include <unistd.h>
#define sq_sleep_ms(ms) usleep((ms) * 1000)
#endif

#define LOG_TAG "engine"
#include "core/log.h"

void sq_engine_init(sq_engine_t *engine, uint32_t sample_rate)
{
    /* Zero everything first */
    memset(engine, 0, sizeof(sq_engine_t));

    engine->sample_rate   = sample_rate;
    engine->master_volume = 1.0f;
    engine->rng_state     = 0x12345678; /* deterministic seed for PRNG */

    /* Initialize lock-free command queue */
    cmd_queue_init(&engine->cmd_queue);

    /* Initialize MIDI CC map with factory defaults */
    memset(engine->cc_map.map, -1, sizeof(engine->cc_map.map));
    engine->cc_map.map[1]  = SQ_PARAM_FILTER_CUTOFF;    /* Mod wheel */
    engine->cc_map.map[7]  = SQ_PARAM_MASTER_VOLUME;
    engine->cc_map.map[70] = SQ_PARAM_FILTER_CUTOFF;    /* MPK Mini K1 */
    engine->cc_map.map[71] = SQ_PARAM_FILTER_RESONANCE; /* GM2 Timbre */
    engine->cc_map.map[72] = SQ_PARAM_AMP_RELEASE;      /* GM2 Release */
    engine->cc_map.map[73] = SQ_PARAM_AMP_ATTACK;       /* GM2 Attack */
    engine->cc_map.map[74] = SQ_PARAM_FILTER_CUTOFF;    /* GM2 Brightness */
    engine->cc_map.map[75] = SQ_PARAM_AMP_DECAY;        /* MPK Mini K6 */
    engine->cc_map.map[76] = SQ_PARAM_AMP_SUSTAIN;      /* MPK Mini K7 */
    engine->cc_map.map[77] = SQ_PARAM_DELAY_WET;        /* MPK Mini K8 */
    engine->cc_map.map[91] = SQ_PARAM_REVERB_WET;       /* Effects 1 */
    engine->cc_map.map[93] = SQ_PARAM_DELAY_WET;        /* Effects 3 */
    /* Novation Launchkey Mini CC 21-28 */
    engine->cc_map.map[21] = SQ_PARAM_FILTER_CUTOFF;
    engine->cc_map.map[22] = SQ_PARAM_FILTER_RESONANCE;
    engine->cc_map.map[23] = SQ_PARAM_AMP_ATTACK;
    engine->cc_map.map[24] = SQ_PARAM_AMP_DECAY;
    engine->cc_map.map[25] = SQ_PARAM_AMP_SUSTAIN;
    engine->cc_map.map[26] = SQ_PARAM_AMP_RELEASE;
    engine->cc_map.map[27] = SQ_PARAM_REVERB_WET;
    engine->cc_map.map[28] = SQ_PARAM_DELAY_WET;

    /* Initialize transport to defaults (120 BPM, stopped) */
    transport_init(&engine->transport);

    /* Initialize synth wavetables and presets */
    synth_init_wavetables(&engine->wavetables);
    synth_init_presets(engine);

    /* Allocate and generate wavetable banks */
    engine->wt_banks = calloc(SQ_WT_MAX_BANKS, sizeof(sq_wt_bank_t));
    if (!engine->wt_banks) {
        LOG_ERROR("Failed to allocate wavetable banks (%zu bytes)",
                  SQ_WT_MAX_BANKS * sizeof(sq_wt_bank_t));
        engine->num_wt_banks = 0;
    } else {
        engine->num_wt_banks = 0;
        synth_init_wt_banks(engine);
    }

    /* Create 5 default patterns */
    engine->num_patterns = 5;
    for (uint32_t pi = 0; pi < engine->num_patterns; pi++) {
        sq_pattern_t *p = &engine->patterns[pi];
        snprintf(p->name, SQ_PATTERN_NAME_LEN, "Pattern %u", pi + 1);
        p->num_tracks = 6;  /* 4 sampler + 2 synth for all patterns */

        for (uint32_t t = 0; t < p->num_tracks; t++) {
            p->tracks[t].type   = TRACK_SAMPLER;
            p->tracks[t].length = 16;
            p->tracks[t].volume = 0.8f;
            p->tracks[t].pan    = 0.0f;
            p->tracks[t].mute   = false;
            p->tracks[t].solo   = false;
            p->tracks[t].sample_index = -1;
            p->tracks[t].synth_preset = -1;
        }

        /* Tracks 4-5: synth tracks */
        p->tracks[4].type = TRACK_SYNTH;
        p->tracks[4].synth_preset = 0; /* Bass preset */
        p->tracks[4].volume = 0.6f;

        p->tracks[5].type = TRACK_SYNTH;
        p->tracks[5].synth_preset = 52; /* Dark Pad */
        p->tracks[5].volume = 0.5f;
    }
}

void sq_engine_shutdown(sq_engine_t *engine)
{
    /* Free all loaded sample data using the correct dr_libs deallocator */
    for (uint32_t i = 0; i < engine->num_samples; i++) {
        sample_io_free(&engine->samples[i]);
    }
    engine->num_samples = 0;

    /* Free master effects (delay/reverb heap buffers) */
    for (int i = 0; i < MAX_TRACK_EFFECTS; i++) {
        effect_free(&engine->master_effects[i]);
    }

    /* Free per-track effects across all patterns */
    for (uint32_t p = 0; p < engine->num_patterns; p++) {
        sq_pattern_t *pat = &engine->patterns[p];
        for (uint32_t t = 0; t < pat->num_tracks; t++) {
            for (int e = 0; e < MAX_TRACK_EFFECTS; e++) {
                effect_free(&pat->tracks[t].effects[e]);
            }
        }
    }

    /* Stop any active recording */
    sq_recorder_stop(&engine->recorder);

    /* Free SoundFont */
    sf2_unload(engine);

    /* Free wavetable banks */
    if (engine->wt_banks) {
        free(engine->wt_banks);
        engine->wt_banks = NULL;
    }
    engine->num_wt_banks = 0;
}

void sq_engine_process(sq_engine_t *engine, float *output, uint32_t num_frames)
{
    /*
     * Step 0: Process pending commands from GUI thread (lock-free).
     */
    {
        sq_command_t cmd;
        while (cmd_queue_pop(&engine->cmd_queue, &cmd)) {
            switch (cmd.type) {
            case CMD_PLAY:  engine->transport.playing = true; break;
            case CMD_STOP:
                engine->transport.playing = false;
                /* Release all tracked notes */
                for (int an = 0; an < SQ_MAX_ACTIVE_NOTES; an++) {
                    if (engine->active_notes[an].remaining > 0) {
                        int vi = engine->active_notes[an].voice_index;
                        if (vi >= 0 && vi < SQ_MAX_SYNTH_VOICES &&
                            engine->synth_voices[vi].active) {
                            int pi = engine->synth_voices[vi].preset_index;
                            if (pi >= 0 && (uint32_t)pi < engine->num_synth_presets) {
                                envelope_release(&engine->synth_voices[vi].amp_env,
                                                 &engine->synth_presets[pi].amp_env,
                                                 engine->sample_rate);
                            }
                        }
                        engine->active_notes[an].remaining = 0;
                        engine->active_notes[an].voice_index = -1;
                    }
                }
                /* Kill ALL voices immediately on stop */
                for (int sv = 0; sv < SQ_MAX_VOICES; sv++)
                    engine->voices[sv].active = false;
                for (int sv = 0; sv < SQ_MAX_SYNTH_VOICES; sv++)
                    engine->synth_voices[sv].active = false;
                /* Clear retrigger queue */
                for (int ri = 0; ri < SQ_MAX_RETRIGGERS; ri++)
                    engine->retriggers[ri].count = 0;
                break;
            case CMD_SET_BPM: engine->transport.bpm = cmd.f64_val; break;
            case CMD_SET_VOLUME: engine->master_volume = cmd.f32_val; break;
            case CMD_SET_SWING: engine->transport.swing = cmd.f32_val; break;
            case CMD_SET_PATTERN: engine->transport.current_pattern = cmd.int_val; break;
            case CMD_SET_MODE: engine->transport.mode = (sq_play_mode_t)cmd.int_val; break;
            case CMD_QUEUE_SECTION: engine->transport.queued_section = cmd.int_val; break;
            case CMD_SET_STEP: {
                int pi = engine->transport.current_pattern;
                if (pi >= 0 && (uint32_t)pi < engine->num_patterns) {
                    sq_pattern_t *p = &engine->patterns[pi];
                    if (cmd.step.track < p->num_tracks && cmd.step.step < p->tracks[cmd.step.track].length) {
                        p->tracks[cmd.step.track].steps[cmd.step.step].velocity = cmd.step.velocity;
                        p->tracks[cmd.step.track].steps[cmd.step.step].pitch_offset = cmd.step.pitch;
                    }
                }
                break;
            }
            case CMD_SET_TRACK_VOLUME: {
                int pi = engine->transport.current_pattern;
                if (pi >= 0 && (uint32_t)pi < engine->num_patterns) {
                    sq_pattern_t *p = &engine->patterns[pi];
                    if (cmd.track_param.track < p->num_tracks)
                        p->tracks[cmd.track_param.track].volume = cmd.track_param.value;
                }
                break;
            }
            case CMD_SET_TRACK_PAN: {
                int pi = engine->transport.current_pattern;
                if (pi >= 0 && (uint32_t)pi < engine->num_patterns) {
                    sq_pattern_t *p = &engine->patterns[pi];
                    if (cmd.track_param.track < p->num_tracks)
                        p->tracks[cmd.track_param.track].pan = cmd.track_param.value;
                }
                break;
            }
            case CMD_SET_TRACK_MUTE: {
                int pi = engine->transport.current_pattern;
                if (pi >= 0 && (uint32_t)pi < engine->num_patterns) {
                    sq_pattern_t *p = &engine->patterns[pi];
                    if (cmd.track_param.track < p->num_tracks)
                        p->tracks[cmd.track_param.track].mute = cmd.bool_val;
                }
                break;
            }
            case CMD_SET_TRACK_SOLO: {
                int pi = engine->transport.current_pattern;
                if (pi >= 0 && (uint32_t)pi < engine->num_patterns) {
                    sq_pattern_t *p = &engine->patterns[pi];
                    if (cmd.track_param.track < p->num_tracks)
                        p->tracks[cmd.track_param.track].solo = cmd.bool_val;
                }
                break;
            }
            case CMD_TRIGGER_NOTE: {
                if (cmd.note.preset < 0) {
                    /* Negative preset = drum pad mode: trigger sampler track */
                    int track_idx = -(cmd.note.preset + 1);
                    if (track_idx >= 0 && (uint32_t)track_idx < engine->num_samples) {
                        sampler_trigger(engine, track_idx,
                                        cmd.note.velocity, 0,
                                        cmd.note.volume, cmd.note.pan);
                    }
                } else {
                    /* Synth mode: trigger synth voice */
                    synth_trigger(engine, cmd.note.preset,
                                  cmd.note.velocity, 0,
                                  cmd.note.volume, cmd.note.pan,
                                  cmd.note.midi_note);
                }
                break;
            }
            case CMD_RELEASE_NOTE: {
                /* MIDI / virtual keyboard note-off — match by frequency */
                float freq = 440.0f * powf(2.0f, ((float)cmd.note.midi_note - 69.0f) / 12.0f);
                for (int v = 0; v < SQ_MAX_SYNTH_VOICES; v++) {
                    sq_synth_voice_t *voice = &engine->synth_voices[v];
                    if (voice->active && fabsf(voice->frequency - freq) < 0.5f) {
                        int pi = voice->preset_index;
                        if (pi >= 0 && (uint32_t)pi < engine->num_synth_presets) {
                            envelope_release(&voice->amp_env,
                                             &engine->synth_presets[pi].amp_env,
                                             engine->sample_rate);
                        }
                    }
                }
                break;
            }
            case CMD_MIDI_CC: {
                /* Apply CC-mapped parameter change.
                 * The CC→param mapping was already resolved in sq_midi.cpp;
                 * we just receive the raw CC number + value and need the
                 * mapping table from the midi handle. For simplicity, we
                 * store a static CC map in the engine and apply here. */
                float normalized = (float)cmd.midi_cc.value / 127.0f;
                /* Decode which parameter based on a lookup.
                 * Since we don't have the midi handle here, the callback
                 * already filtered — we use a global CC map stored in engine. */
                uint8_t cc = cmd.midi_cc.cc;
                int8_t param = engine->cc_map.map[cc];
                if (param >= 0) {
                    switch ((sq_param_id_t)param) {
                    case SQ_PARAM_MASTER_VOLUME:
                        engine->master_volume = normalized;
                        break;
                    case SQ_PARAM_BPM:
                        engine->transport.bpm = 20.0 + normalized * 280.0;
                        break;
                    case SQ_PARAM_SWING:
                        engine->transport.swing = normalized;
                        break;
                    case SQ_PARAM_FILTER_CUTOFF:
                        for (uint32_t i = 0; i < engine->num_synth_presets; i++)
                            engine->synth_presets[i].filter_cutoff =
                                20.0f + normalized * 19980.0f;
                        break;
                    case SQ_PARAM_FILTER_RESONANCE:
                        for (uint32_t i = 0; i < engine->num_synth_presets; i++)
                            engine->synth_presets[i].filter_resonance =
                                0.5f + normalized * 19.5f;
                        break;
                    case SQ_PARAM_AMP_ATTACK:
                        for (uint32_t i = 0; i < engine->num_synth_presets; i++)
                            engine->synth_presets[i].amp_env.attack =
                                0.001f + normalized * 2.0f;
                        break;
                    case SQ_PARAM_AMP_DECAY:
                        for (uint32_t i = 0; i < engine->num_synth_presets; i++)
                            engine->synth_presets[i].amp_env.decay =
                                0.001f + normalized * 2.0f;
                        break;
                    case SQ_PARAM_AMP_SUSTAIN:
                        for (uint32_t i = 0; i < engine->num_synth_presets; i++)
                            engine->synth_presets[i].amp_env.sustain = normalized;
                        break;
                    case SQ_PARAM_AMP_RELEASE:
                        for (uint32_t i = 0; i < engine->num_synth_presets; i++)
                            engine->synth_presets[i].amp_env.release =
                                0.001f + normalized * 2.5f;
                        break;
                    case SQ_PARAM_DELAY_WET:
                        engine->master_effects[0].delay.wet = normalized * 0.5f;
                        break;
                    case SQ_PARAM_REVERB_WET:
                        if (engine->master_effects[1].type == EFFECT_COMPRESSOR) {
                            /* Reverb might be in a different slot — find it */
                            for (int e = 0; e < MAX_TRACK_EFFECTS; e++) {
                                if (engine->master_effects[e].type == EFFECT_REVERB)
                                    engine->master_effects[e].reverb.wet = normalized;
                                else if (engine->master_effects[e].type == EFFECT_SHIMMER)
                                    engine->master_effects[e].shimmer.mix = normalized;
                            }
                        }
                        break;
                    default:
                        break;
                    }
                }
                break;
            }
            case CMD_PITCH_BEND: {
                /* Apply pitch bend to all active synth voices */
                float bend_normalized = (float)cmd.pitch_bend.value / 8192.0f;
                float bend_semitones = bend_normalized * 2.0f; /* ±2 semitones */
                for (int v = 0; v < SQ_MAX_SYNTH_VOICES; v++) {
                    if (engine->synth_voices[v].active) {
                        engine->synth_voices[v].pitch_bend = bend_semitones;
                    }
                }
                break;
            }
            default: break;
            }
        }
    }

    /*
     * Step 1: Advance transport and check for step changes.
     *
     * We need to know the pattern length so the transport can wrap.
     * Get it from the current pattern's first track.
     */
    uint32_t pattern_length = 16; /* default */
    int pat_idx = engine->transport.current_pattern;
    if (pat_idx >= 0 && (uint32_t)pat_idx < engine->num_patterns) {
        sq_pattern_t *p = &engine->patterns[pat_idx];
        /* Use the longest track length for transport wrap (polymeter support) */
        for (uint32_t t = 0; t < p->num_tracks; t++) {
            if (p->tracks[t].length > pattern_length)
                pattern_length = p->tracks[t].length;
        }
    }

    /* Trigger step 0 immediately on play start */
    if (engine->transport.step0_pending) {
        engine->transport.step0_pending = false;
        sequencer_trigger_step(engine, 0);
    }

    int new_step = transport_advance(&engine->transport, engine->sample_rate,
                                     num_frames, pattern_length);

    /*
     * Step 1b: Handle song/perform mode pattern transitions.
     */
    if (engine->transport.pattern_completed) {
        engine->transport.pattern_completed = false;

        if (engine->transport.mode == MODE_SONG) {
            /* Song mode: advance through arrangement sections */
            sq_arrangement_t *arr = &engine->arrangement;
            if (arr->num_sections > 0) {
                int sec = engine->transport.current_section;
                if (sec >= 0 && (uint32_t)sec < arr->num_sections) {
                    engine->transport.section_repeat++;
                    int repeats = arr->sections[sec].repeat_count;
                    if (repeats < 1) repeats = 1;

                    if (engine->transport.section_repeat >= repeats) {
                        /* Move to next section */
                        engine->transport.section_repeat = 0;
                        engine->transport.current_section++;

                        if ((uint32_t)engine->transport.current_section >= arr->num_sections) {
                            /* End of arrangement — stop */
                            engine->transport.playing = false;
                            engine->transport.current_section = 0;
                        } else {
                            /* Switch to next section's pattern */
                            int next_pat = arr->sections[engine->transport.current_section].pattern_index;
                            if (next_pat >= 0 && (uint32_t)next_pat < engine->num_patterns) {
                                engine->transport.current_pattern = next_pat;
                                engine->transport.current_beat = 0.0;
                                engine->transport.current_step = 0;
                            }
                        }
                    }
                }
            }
        } else if (engine->transport.mode == MODE_PERFORM) {
            /* Perform mode: switch to queued section at pattern boundary */
            if (engine->transport.queued_section >= 0) {
                sq_arrangement_t *arr = &engine->arrangement;
                int queued = engine->transport.queued_section;
                if ((uint32_t)queued < arr->num_sections) {
                    engine->transport.current_section = queued;
                    int next_pat = arr->sections[queued].pattern_index;
                    if (next_pat >= 0 && (uint32_t)next_pat < engine->num_patterns) {
                        engine->transport.current_pattern = next_pat;
                        engine->transport.current_beat = 0.0;
                        engine->transport.current_step = 0;
                    }
                }
                engine->transport.queued_section = -1;
            }
        }
        /* MODE_PATTERN: just loops — no action needed */
    }

    /*
     * Step 2: If we crossed into a new step, trigger voices.
     */
    if (new_step >= 0) {
        sequencer_trigger_step(engine, new_step);
    }

    /*
     * Step 3: Render all active voices through the mixer.
     * mixer_process() clears the buffer, renders sampler voices,
     * and applies master volume.
     */
    mixer_process(engine, output, num_frames);

    /*
     * Step 3b: Process active note timers for sequencer note-off.
     * Decrement remaining samples; release voice when expired.
     */
    for (int i = 0; i < SQ_MAX_ACTIVE_NOTES; i++) {
        sq_active_note_t *an = &engine->active_notes[i];
        if (an->remaining == 0) continue;

        if (an->remaining <= num_frames) {
            /* Note expired — release the voice */
            an->remaining = 0;
            int vi = an->voice_index;
            if (vi >= 0 && vi < SQ_MAX_SYNTH_VOICES &&
                engine->synth_voices[vi].active) {
                int pi = engine->synth_voices[vi].preset_index;
                if (pi >= 0 && (uint32_t)pi < engine->num_synth_presets) {
                    envelope_release(&engine->synth_voices[vi].amp_env,
                                     &engine->synth_presets[pi].amp_env,
                                     engine->sample_rate);
                }
            }
            an->voice_index = -1;
        } else {
            an->remaining -= num_frames;
        }
    }

    /*
     * Step 3c: Process retrigger queue (note repeat / ratcheting).
     * Each retrigger fires a new voice at regular intervals within a step.
     */
    {
        int pat_idx = engine->transport.current_pattern;
        sq_pattern_t *pat = (pat_idx >= 0 && (uint32_t)pat_idx < engine->num_patterns)
                            ? &engine->patterns[pat_idx] : NULL;
        for (int ri = 0; ri < SQ_MAX_RETRIGGERS; ri++) {
            sq_retrigger_t *rt = &engine->retriggers[ri];
            if (rt->count == 0) continue;

            if (rt->remaining <= num_frames) {
                /* Fire retrigger */
                rt->velocity *= rt->vel_decay;
                if (pat && rt->track < pat->num_tracks) {
                    sq_track_t *trk = &pat->tracks[rt->track];
                    if (trk->type == TRACK_SAMPLER && trk->sample_index >= 0) {
                        sampler_trigger(engine, trk->sample_index,
                                        rt->velocity, rt->pitch_offset,
                                        trk->volume, trk->pan);
                    } else if (trk->type == TRACK_SYNTH && trk->synth_preset >= 0) {
                        synth_trigger(engine, trk->synth_preset,
                                      rt->velocity, rt->pitch_offset,
                                      trk->volume, trk->pan, rt->note);
                    }
                }
                rt->count--;
                rt->remaining = rt->interval;
            } else {
                rt->remaining -= num_frames;
            }
        }
    }

    /*
     * Step 4: Stream output to disk via the recorder.
     * Small writes (~1-2 KB) hit the OS page cache and return immediately.
     */
    if (engine->recorder.state == SQ_REC_ACTIVE && output) {
        sq_recorder_write(&engine->recorder, output, num_frames);
    }
}

/* Old sq_engine_start_recording / sq_engine_stop_recording removed.
 * Use sq_recorder_start() / sq_recorder_stop() via engine->recorder. */

int sq_engine_safe_load(sq_engine_t *engine, const char *filepath)
{
    /* Stop playback and recording before modifying engine state */
    engine->transport.playing = false;
    sq_recorder_stop(&engine->recorder);

    /*
     * The audio callback checks engine->transport.playing at the start of
     * each buffer. Setting it false means the next buffer won't trigger any
     * voices or read pattern data. Give the audio callback time to finish
     * its current buffer before we modify engine state.
     */
    sq_sleep_ms(50);

    LOG_INFO("Safe load: playback stopped, loading %s", filepath);
    return project_load(engine, filepath);
}
