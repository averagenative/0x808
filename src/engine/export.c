/*
 * export.c — Offline audio rendering and WAV export.
 */

#define LOG_TAG "export"
#include "core/log.h"
#include "engine/export.h"

#include "dr_wav.h"
#include "layer3.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define EXPORT_CHUNK_FRAMES 512

int sq_export_render(sq_engine_t *engine, const sq_export_config_t *config,
                     sq_export_result_t *result)
{
    if (!engine || !config || !result) return -1;

    memset(result, 0, sizeof(*result));

    uint32_t sr = config->sample_rate > 0 ? config->sample_rate : engine->sample_rate;

    /* Calculate total frames to render */
    int pat_idx = config->pattern_index >= 0
                  ? config->pattern_index
                  : engine->transport.current_pattern;
    if (pat_idx < 0) pat_idx = 0;

    int num_bars = config->num_bars > 0 ? config->num_bars : 4;
    bool export_arrangement = (config->pattern_index < 0 &&
                               engine->arrangement.num_sections > 0);

    /* Steps per bar = steps_per_beat * beats_per_bar (assume 4/4 time) */
    int steps_per_bar = 16; /* 4 beats * 4 steps_per_beat */
    if (pat_idx >= 0 && (uint32_t)pat_idx < engine->num_patterns &&
        engine->patterns[pat_idx].num_tracks > 0) {
        steps_per_bar = (int)engine->patterns[pat_idx].tracks[0].length;
    }

    double seconds;
    if (export_arrangement) {
        /* Calculate total length from arrangement sections */
        int total_bars = 0;
        for (uint32_t s = 0; s < engine->arrangement.num_sections; s++) {
            int pi = engine->arrangement.sections[s].pattern_index;
            int reps = engine->arrangement.sections[s].repeat_count;
            if (reps < 1) reps = 1;
            int sec_steps = 16;
            if (pi >= 0 && (uint32_t)pi < engine->num_patterns &&
                engine->patterns[pi].num_tracks > 0) {
                sec_steps = (int)engine->patterns[pi].tracks[0].length;
            }
            total_bars += reps; /* each repeat = one bar of sec_steps steps */
            (void)sec_steps;
        }
        /* Total steps across all sections */
        double total_beats = 0;
        for (uint32_t s = 0; s < engine->arrangement.num_sections; s++) {
            int pi = engine->arrangement.sections[s].pattern_index;
            int reps = engine->arrangement.sections[s].repeat_count;
            if (reps < 1) reps = 1;
            int sec_steps = 16;
            if (pi >= 0 && (uint32_t)pi < engine->num_patterns &&
                engine->patterns[pi].num_tracks > 0) {
                sec_steps = (int)engine->patterns[pi].tracks[0].length;
            }
            total_beats += (double)(reps * sec_steps) / 4.0;
        }
        seconds = total_beats * (60.0 / engine->transport.bpm);
        LOG_INFO("Rendering arrangement: %u sections, %.1f beats, %.1f sec",
                 engine->arrangement.num_sections, total_beats, seconds);
    } else {
        double beats = (double)(num_bars * steps_per_bar) / 4.0;
        seconds = beats * (60.0 / engine->transport.bpm);
    }

    uint32_t total_frames = (uint32_t)(seconds * sr) + sr; /* +1 sec for tail */

    /* Cap at 10 minutes to prevent overflow */
    uint32_t max_frames = sr * 600; /* 10 min */
    if (total_frames > max_frames) {
        LOG_WARN("Export capped at %u frames (10 min)", max_frames);
        total_frames = max_frames;
    }

    LOG_INFO("Rendering: %.1f sec, %u frames at %u Hz",
             seconds, total_frames, sr);

    /* Check allocation size won't overflow or exceed 500MB */
    uint64_t alloc_size = (uint64_t)total_frames * 2 * sizeof(float);
    if (alloc_size > 500ULL * 1024 * 1024) {
        LOG_ERROR("Export buffer too large: %llu bytes", (unsigned long long)alloc_size);
        return -1;
    }

    /* Allocate output buffer */
    float *data = calloc(total_frames * 2, sizeof(float));
    if (!data) {
        LOG_ERROR("Failed to allocate %u frames", total_frames);
        return -1;
    }

    /* Save and reset transport state */
    sq_transport_t saved_transport = engine->transport;
    uint32_t saved_sr = engine->sample_rate;

    engine->sample_rate = sr;
    engine->transport.playing = true;
    engine->transport.current_beat = 0.0;
    engine->transport.sample_position = 0;
    engine->transport.current_step = 0;
    engine->transport.current_section = 0;
    engine->transport.section_repeat = 0;

    if (export_arrangement) {
        engine->transport.mode = MODE_SONG;
        engine->transport.current_pattern =
            engine->arrangement.sections[0].pattern_index;
    } else {
        engine->transport.mode = MODE_PATTERN;
        engine->transport.current_pattern = pat_idx;
    }

    /* Render in chunks */
    float *ptr = data;
    uint32_t rendered = 0;
    while (rendered < total_frames) {
        uint32_t chunk = EXPORT_CHUNK_FRAMES;
        if (rendered + chunk > total_frames)
            chunk = total_frames - rendered;

        sq_engine_process(engine, ptr, chunk);
        ptr += chunk * 2;
        rendered += chunk;
    }

    /* Restore transport state */
    engine->transport = saved_transport;
    engine->sample_rate = saved_sr;

    /* Compute peak level */
    float peak = 0.0f;
    for (uint32_t i = 0; i < total_frames * 2; i++) {
        float v = fabsf(data[i]);
        if (v > peak) peak = v;
    }

    result->data = data;
    result->num_frames = total_frames;
    result->sample_rate = sr;
    result->peak_level = peak;

    LOG_INFO("Render complete: %u frames, peak=%.4f", total_frames, peak);
    return 0;
}

int sq_export_write_wav(const char *filepath, const sq_export_result_t *result,
                        int bit_depth)
{
    if (!filepath || !result || !result->data) return -1;

    drwav wav;
    drwav_data_format format;
    format.container = drwav_container_riff;
    format.channels = 2;
    format.sampleRate = result->sample_rate;

    if (bit_depth == 16) {
        format.format = DR_WAVE_FORMAT_PCM;
        format.bitsPerSample = 16;
    } else if (bit_depth == 24) {
        format.format = DR_WAVE_FORMAT_PCM;
        format.bitsPerSample = 24;
    } else {
        /* Default to 32-bit float */
        format.format = DR_WAVE_FORMAT_IEEE_FLOAT;
        format.bitsPerSample = 32;
    }

    if (!drwav_init_file_write(&wav, filepath, &format, NULL)) {
        LOG_ERROR("Failed to open %s for writing", filepath);
        return -1;
    }

    drwav_uint64 written = drwav_write_pcm_frames(&wav, result->num_frames, result->data);
    drwav_uninit(&wav);

    LOG_INFO("Wrote %llu frames to %s (%d-bit)", (unsigned long long)written,
             filepath, bit_depth);
    return 0;
}

int sq_export_write_mp3(const char *filepath, const sq_export_result_t *result,
                        int bitrate)
{
    if (!filepath || !result || !result->data) return -1;

    /* Validate bitrate/samplerate combo */
    if (shine_check_config((int)result->sample_rate, bitrate) < 0) {
        LOG_ERROR("Unsupported MP3 config: %u Hz, %d kbps",
                  result->sample_rate, bitrate);
        return -1;
    }

    /* Configure shine encoder */
    shine_config_t config;
    shine_set_config_mpeg_defaults(&config.mpeg);
    config.mpeg.bitr = bitrate;
    config.mpeg.mode = STEREO;
    config.wave.channels = PCM_STEREO;
    config.wave.samplerate = (int)result->sample_rate;

    shine_t encoder = shine_initialise(&config);
    if (!encoder) {
        LOG_ERROR("Failed to initialize MP3 encoder");
        return -1;
    }

    int samples_per_pass = shine_samples_per_pass(encoder);

    /* Open output file */
    FILE *fp = fopen(filepath, "wb");
    if (!fp) {
        LOG_ERROR("Failed to open %s for writing", filepath);
        shine_close(encoder);
        return -1;
    }

    /* Convert float audio to interleaved int16 */
    uint32_t total_samples = result->num_frames * 2; /* stereo interleaved */
    int16_t *pcm = malloc(total_samples * sizeof(int16_t));
    if (!pcm) {
        LOG_ERROR("Failed to allocate PCM buffer");
        fclose(fp);
        shine_close(encoder);
        return -1;
    }

    for (uint32_t i = 0; i < total_samples; i++) {
        float v = result->data[i];
        if (v > 1.0f) v = 1.0f;
        if (v < -1.0f) v = -1.0f;
        pcm[i] = (int16_t)(v * 32767.0f);
    }

    /* Encode in chunks of samples_per_pass */
    uint32_t pos = 0;
    uint32_t total_written = 0;
    while (pos < result->num_frames) {
        uint32_t remaining = result->num_frames - pos;
        int16_t *chunk = pcm + pos * 2;

        /* Pad last chunk with silence if needed */
        int16_t *buf = chunk;
        int16_t *padded = NULL;
        if ((int)remaining < samples_per_pass) {
            padded = calloc((size_t)samples_per_pass * 2, sizeof(int16_t));
            if (padded) {
                memcpy(padded, chunk, remaining * 2 * sizeof(int16_t));
                buf = padded;
            }
        }

        int written = 0;
        unsigned char *mp3_data = shine_encode_buffer_interleaved(
            encoder, buf, &written);
        if (written > 0 && mp3_data) {
            fwrite(mp3_data, 1, (size_t)written, fp);
            total_written += (uint32_t)written;
        }

        free(padded);
        pos += (uint32_t)samples_per_pass;
    }

    /* Flush remaining encoder data */
    int flushed = 0;
    unsigned char *flush_data = shine_flush(encoder, &flushed);
    if (flushed > 0 && flush_data) {
        fwrite(flush_data, 1, (size_t)flushed, fp);
        total_written += (uint32_t)flushed;
    }

    fclose(fp);
    shine_close(encoder);
    free(pcm);

    LOG_INFO("Wrote MP3: %s (%u bytes, %d kbps)", filepath, total_written, bitrate);
    return 0;
}
