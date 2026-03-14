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
#include "formats/sf2.h"
#include "formats/sample_io.h"
#include "formats/project.h"
#include <string.h>
#include <stdlib.h>

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
        p->tracks[5].synth_preset = 3; /* Pluck preset */
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

    /* Free recording buffer */
    if (engine->rec_buffer) {
        free(engine->rec_buffer);
        engine->rec_buffer = NULL;
    }
    engine->rec_frames = 0;
    engine->rec_capacity = 0;
    engine->recording = false;

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
            case CMD_STOP:  engine->transport.playing = false; break;
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
        if (p->num_tracks > 0) {
            pattern_length = p->tracks[0].length;
        }
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
     * Step 4: Capture output to pre-allocated recording buffer.
     * No realloc in the audio path — buffer is pre-allocated by
     * sq_engine_start_recording() before playback begins.
     */
    if (engine->recording && output) {
        if (engine->rec_frames + num_frames <= engine->rec_capacity) {
            memcpy(engine->rec_buffer + engine->rec_frames * 2,
                   output, num_frames * 2 * sizeof(float));
            engine->rec_frames += num_frames;
        } else {
            engine->recording = false; /* buffer full, stop */
        }
    }
}

void sq_engine_start_recording(sq_engine_t *engine)
{
    /* Pre-allocate for 10 minutes at current sample rate */
    uint32_t max_frames = engine->sample_rate * 600;
    if (engine->rec_buffer) free(engine->rec_buffer);
    engine->rec_buffer = calloc((size_t)max_frames * 2, sizeof(float));
    engine->rec_capacity = engine->rec_buffer ? max_frames : 0;
    engine->rec_frames = 0;
    engine->recording = true;
    LOG_INFO("Recording started (pre-allocated %u frames, %.1f MB)",
             engine->rec_capacity,
             (double)engine->rec_capacity * 2 * sizeof(float) / (1024.0 * 1024.0));
}

void sq_engine_stop_recording(sq_engine_t *engine)
{
    engine->recording = false;
    LOG_INFO("Recording stopped (%u frames captured)", engine->rec_frames);
}

int sq_engine_safe_load(sq_engine_t *engine, const char *filepath)
{
    /* Stop playback and recording before modifying engine state */
    engine->transport.playing = false;
    engine->recording = false;

    /*
     * The audio callback checks engine->transport.playing at the start of
     * each buffer. Setting it false means the next buffer won't trigger any
     * voices or read pattern data. Since the audio buffer is typically
     * 256-1024 frames (5-23ms), there's an inherent small window, but this
     * is vastly better than the current no-protection approach.
     */

    LOG_INFO("Safe load: playback stopped, loading %s", filepath);
    return project_load(engine, filepath);
}
