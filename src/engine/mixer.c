/*
 * mixer.c — Clear buffer, render voices, apply master volume, compute peaks.
 */

#include "engine/mixer.h"
#include "engine/sampler.h"
#include "engine/synth.h"
#include "engine/effects.h"
#include "formats/sf2.h"
#include <string.h>
#include <math.h>

void mixer_process(sq_engine_t *engine, float *output, uint32_t num_frames)
{
    /* Step 1: Clear the output buffer to silence (all zeros) */
    memset(output, 0, num_frames * 2 * sizeof(float));

    /* Figure out which tracks have at least one active (non-bypassed)
     * insert effect. Those tracks get rendered into the scratch buffer
     * individually so their FX stay on that track; all other voices go
     * straight to the master output. */
    uint16_t fx_track_mask = 0;
    int pat_idx = engine->transport.current_pattern;
    sq_pattern_t *pat = NULL;
    if (pat_idx >= 0 && (uint32_t)pat_idx < engine->num_patterns) {
        pat = &engine->patterns[pat_idx];
        for (uint32_t t = 0; t < pat->num_tracks && t < SQ_MAX_TRACKS; t++) {
            for (int e = 0; e < MAX_TRACK_EFFECTS; e++) {
                if (pat->tracks[t].effects[e].type != EFFECT_NONE &&
                    !pat->tracks[t].effects[e].bypass) {
                    fx_track_mask |= (uint16_t)(1u << t);
                    break;
                }
            }
        }
    }

    /* Step 2a: Per-track rendering for FX tracks (isolated mix + FX) */
    if (pat && fx_track_mask) {
        const uint32_t scratch_cap =
            (uint32_t)(sizeof(engine->fx_scratch) / sizeof(float)) / 2;
        uint32_t remaining = num_frames;
        uint32_t offset = 0;
        while (remaining > 0) {
            uint32_t chunk = remaining < scratch_cap ? remaining : scratch_cap;
            for (uint32_t t = 0; t < pat->num_tracks && t < SQ_MAX_TRACKS; t++) {
                if (!(fx_track_mask & (1u << t))) continue;

                memset(engine->fx_scratch, 0, chunk * 2 * sizeof(float));
                sampler_render_track(engine, engine->fx_scratch, chunk, (int)t);
                synth_render_track(engine, engine->fx_scratch, chunk, (int)t);

                effects_chain_process(pat->tracks[t].effects, MAX_TRACK_EFFECTS,
                                      engine->fx_scratch, chunk,
                                      engine->sample_rate, engine->transport.bpm);

                float *dst = output + offset * 2;
                for (uint32_t i = 0; i < chunk * 2; i++)
                    dst[i] += engine->fx_scratch[i];
            }
            offset += chunk;
            remaining -= chunk;
        }
    }

    /* Step 2b: Render voices that belong to NON-FX tracks (or are
     * untracked) straight into output. Voices on FX tracks have
     * already been rendered via fx_scratch above. */
    if (fx_track_mask == 0) {
        sampler_render(engine, output, num_frames);
        synth_render(engine, output, num_frames);
    } else {
        /* Each non-FX track */
        for (uint32_t t = 0; t < SQ_MAX_TRACKS; t++) {
            if (fx_track_mask & (1u << t)) continue;
            sampler_render_track(engine, output, num_frames, (int)t);
            synth_render_track(engine, output, num_frames, (int)t);
        }
        /* Untracked voices (MIDI/API path, track_index == -1) */
        sampler_render_track(engine, output, num_frames, -1);
        synth_render_track(engine, output, num_frames, -1);
    }

    /* SF2 always goes to master (TSF manages its own voice pool and has
     * no concept of our track index). */
    sf2_render(engine, output, num_frames);

    /* Step 3: Apply master volume to every sample in the buffer */
    {
        float vol = engine->master_volume;
        uint32_t total_samples = num_frames * 2;
        for (uint32_t i = 0; i < total_samples; i++) {
            output[i] *= vol;
        }
    }

    /* Step 4: Master bus effects */
    effects_chain_process(engine->master_effects, MAX_TRACK_EFFECTS,
                          output, num_frames,
                          engine->sample_rate, engine->transport.bpm);

    /* Step 5: Compute master peak levels from final output */
    {
        float peak_l = 0.0f, peak_r = 0.0f;
        for (uint32_t f = 0; f < num_frames; f++) {
            float abs_l = fabsf(output[f * 2]);
            float abs_r = fabsf(output[f * 2 + 1]);
            if (abs_l > peak_l) peak_l = abs_l;
            if (abs_r > peak_r) peak_r = abs_r;
        }
        /* Peak hold with decay: keep the higher of new peak or decayed old */
        float decay = 0.92f;
        engine->master_peak[0] = (peak_l > engine->master_peak[0] * decay)
                                 ? peak_l : engine->master_peak[0] * decay;
        engine->master_peak[1] = (peak_r > engine->master_peak[1] * decay)
                                 ? peak_r : engine->master_peak[1] * decay;
    }

    /* Scope ring-buffer capture (mono downmix). Kept in the same pass so
     * we don't do a second read of the output buffer. No malloc, no lock —
     * real-time safe. */
    {
        const uint32_t bufsize = (uint32_t)(sizeof(engine->scope_buffer) /
                                            sizeof(engine->scope_buffer[0]));
        uint32_t w = engine->scope_write_pos;
        for (uint32_t f = 0; f < num_frames; f++) {
            engine->scope_buffer[w] = 0.5f * (output[f * 2] + output[f * 2 + 1]);
            w = (w + 1) & (bufsize - 1);
        }
        engine->scope_write_pos = w;
    }

    /* Step 6: Estimate per-track peak levels.
     * Since voices don't carry track indices, we approximate by checking
     * which tracks have active steps at the current playback position
     * and scaling by track volume. */
    {
        int pat_idx = engine->transport.current_pattern;
        float decay = 0.92f;

        if (pat_idx >= 0 && (uint32_t)pat_idx < engine->num_patterns &&
            engine->transport.playing) {
            sq_pattern_t *pat = &engine->patterns[pat_idx];
            int step = engine->transport.current_step;

            /* Check solo state */
            bool any_solo = false;
            for (uint32_t t = 0; t < pat->num_tracks; t++) {
                if (pat->tracks[t].solo) { any_solo = true; break; }
            }

            for (uint32_t t = 0; t < pat->num_tracks && t < SQ_MAX_TRACKS; t++) {
                sq_track_t *track = &pat->tracks[t];
                int ts = step % (int)track->length;
                bool active = (track->steps[ts].velocity > 0);
                bool audible = !track->mute && (!any_solo || track->solo);
                float estimated = 0.0f;

                if (active && audible) {
                    float vel = (float)track->steps[ts].velocity / 127.0f;
                    estimated = vel * track->volume;
                }

                engine->track_peaks[t] = (estimated > engine->track_peaks[t] * decay)
                                         ? estimated : engine->track_peaks[t] * decay;
            }
        } else {
            /* Not playing: decay all track peaks */
            for (uint32_t t = 0; t < SQ_MAX_TRACKS; t++) {
                engine->track_peaks[t] *= decay;
            }
        }
    }

    /* Step 7: Add inaudible DC offset to prevent PulseAudio/WSLg from
     * auto-suspending (corking) the stream during silence.
     * 1e-4 is ~-80 dBFS — inaudible but enough to keep the stream alive. */
    {
        uint32_t total_samples = num_frames * 2;
        for (uint32_t i = 0; i < total_samples; i++) {
            output[i] += 1.0e-4f;
        }
    }
}
