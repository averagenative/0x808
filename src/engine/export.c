/*
 * export.c — Offline audio rendering and WAV export.
 */

#define LOG_TAG "export"
#include "core/log.h"
#include "engine/export.h"

#include "dr_wav.h"
#ifdef SQ_HAVE_MP3
#include "layer3.h"
#endif

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

    drwav_uint64 written;
    uint32_t total_samples = result->num_frames * 2; /* stereo */

    if (bit_depth == 32) {
        /* 32-bit float: write float data directly */
        written = drwav_write_pcm_frames(&wav, result->num_frames, result->data);
    } else if (bit_depth == 16) {
        /* Convert float -> int16 */
        int16_t *pcm16 = malloc(total_samples * sizeof(int16_t));
        if (!pcm16) {
            drwav_uninit(&wav);
            return -1;
        }
        for (uint32_t i = 0; i < total_samples; i++) {
            float v = result->data[i];
            if (v > 1.0f) v = 1.0f;
            if (v < -1.0f) v = -1.0f;
            pcm16[i] = (int16_t)(v * 32767.0f);
        }
        written = drwav_write_pcm_frames(&wav, result->num_frames, pcm16);
        free(pcm16);
    } else {
        /* 24-bit: convert float -> int32 (dr_wav reads 32-bit ints for 24-bit PCM) */
        int32_t *pcm32 = malloc(total_samples * sizeof(int32_t));
        if (!pcm32) {
            drwav_uninit(&wav);
            return -1;
        }
        for (uint32_t i = 0; i < total_samples; i++) {
            float v = result->data[i];
            if (v > 1.0f) v = 1.0f;
            if (v < -1.0f) v = -1.0f;
            pcm32[i] = (int32_t)(v * 8388607.0f); /* 2^23 - 1 */
        }
        written = drwav_write_pcm_frames(&wav, result->num_frames, pcm32);
        free(pcm32);
    }

    drwav_uninit(&wav);

    LOG_INFO("Wrote %llu frames to %s (%d-bit)", (unsigned long long)written,
             filepath, bit_depth);
    return 0;
}

#ifndef SQ_HAVE_MP3
int sq_export_write_mp3(const char *filepath, const sq_export_result_t *result,
                        int bitrate)
{
    (void)filepath; (void)result; (void)bitrate;
    LOG_ERROR("MP3 export not available (build with -DENABLE_MP3=ON)");
    return -1;
}
#else
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
#endif /* SQ_HAVE_MP3 */

/* ─── FLAC export (minimal verbatim encoder, no external deps) ───────────── */

/*
 * Minimal FLAC encoder using verbatim (uncompressed) subframes.
 * Produces valid FLAC files with no compression — samples are stored as-is
 * with only the FLAC framing overhead. No external library needed.
 *
 * Reference: https://xiph.org/flac/format.html
 */

/* CRC-8 lookup table (polynomial 0x07) for FLAC frame headers */
static const uint8_t flac_crc8_table[256] = {
    0x00,0x07,0x0E,0x09,0x1C,0x1B,0x12,0x15,0x38,0x3F,0x36,0x31,0x24,0x23,0x2A,0x2D,
    0x70,0x77,0x7E,0x79,0x6C,0x6B,0x62,0x65,0x48,0x4F,0x46,0x41,0x54,0x53,0x5A,0x5D,
    0xE0,0xE7,0xEE,0xE9,0xFC,0xFB,0xF2,0xF5,0xD8,0xDF,0xD6,0xD1,0xC4,0xC3,0xCA,0xCD,
    0x90,0x97,0x9E,0x99,0x8C,0x8B,0x82,0x85,0xA8,0xAF,0xA6,0xA1,0xB4,0xB3,0xBA,0xBD,
    0xC7,0xC0,0xC9,0xCE,0xDB,0xDC,0xD5,0xD2,0xFF,0xF8,0xF1,0xF6,0xE3,0xE4,0xED,0xEA,
    0xB7,0xB0,0xB9,0xBE,0xAB,0xAC,0xA5,0xA2,0x8F,0x88,0x81,0x86,0x93,0x94,0x9D,0x9A,
    0x27,0x20,0x29,0x2E,0x3B,0x3C,0x35,0x32,0x1F,0x18,0x11,0x16,0x03,0x04,0x0D,0x0A,
    0x57,0x50,0x59,0x5E,0x4B,0x4C,0x45,0x42,0x6F,0x68,0x61,0x66,0x73,0x74,0x7D,0x7A,
    0x89,0x8E,0x87,0x80,0x95,0x92,0x9B,0x9C,0xB1,0xB6,0xBF,0xB8,0xAD,0xAA,0xA3,0xA4,
    0xF9,0xFE,0xF7,0xF0,0xE5,0xE2,0xEB,0xEC,0xC1,0xC6,0xCF,0xC8,0xDD,0xDA,0xD3,0xD4,
    0x69,0x6E,0x67,0x60,0x75,0x72,0x7B,0x7C,0x51,0x56,0x5F,0x58,0x4D,0x4A,0x43,0x44,
    0x19,0x1E,0x17,0x10,0x05,0x02,0x0B,0x0C,0x21,0x26,0x2F,0x28,0x3D,0x3A,0x33,0x34,
    0x4E,0x49,0x40,0x47,0x52,0x55,0x5C,0x5B,0x76,0x71,0x78,0x7F,0x6A,0x6D,0x64,0x63,
    0x3E,0x39,0x30,0x37,0x22,0x25,0x2C,0x2B,0x06,0x01,0x08,0x0F,0x1A,0x1D,0x14,0x13,
    0xAE,0xA9,0xA0,0xA7,0xB2,0xB5,0xBC,0xBB,0x96,0x91,0x98,0x9F,0x8A,0x8D,0x84,0x83,
    0xDE,0xD9,0xD0,0xD7,0xC2,0xC5,0xCC,0xCB,0xE6,0xE1,0xE8,0xEF,0xFA,0xFD,0xF4,0xF3,
};

/* CRC-16 lookup table (polynomial 0x8005) for FLAC frame footers */
static const uint16_t flac_crc16_table[256] = {
    0x0000,0x8005,0x800F,0x000A,0x801B,0x001E,0x0014,0x8011,
    0x8033,0x0036,0x003C,0x8039,0x0028,0x802D,0x8027,0x0022,
    0x8063,0x0066,0x006C,0x8069,0x0078,0x807D,0x8077,0x0072,
    0x0050,0x8055,0x805F,0x005A,0x804B,0x004E,0x0044,0x8041,
    0x80C3,0x00C6,0x00CC,0x80C9,0x00D8,0x80DD,0x80D7,0x00D2,
    0x00F0,0x80F5,0x80FF,0x00FA,0x80EB,0x00EE,0x00E4,0x80E1,
    0x00A0,0x80A5,0x80AF,0x00AA,0x80BB,0x00BE,0x00B4,0x80B1,
    0x8093,0x0096,0x009C,0x8099,0x0088,0x808D,0x8087,0x0082,
    0x8183,0x0186,0x018C,0x8189,0x0198,0x819D,0x8197,0x0192,
    0x01B0,0x81B5,0x81BF,0x01BA,0x81AB,0x01AE,0x01A4,0x81A1,
    0x01E0,0x81E5,0x81EF,0x01EA,0x81FB,0x01FE,0x01F4,0x81F1,
    0x81D3,0x01D6,0x01DC,0x81D9,0x01C8,0x81CD,0x81C7,0x01C2,
    0x0140,0x8145,0x814F,0x014A,0x815B,0x015E,0x0154,0x8151,
    0x8173,0x0176,0x017C,0x8179,0x0168,0x816D,0x8167,0x0162,
    0x8123,0x0126,0x012C,0x8129,0x0138,0x813D,0x8137,0x0132,
    0x0110,0x8115,0x811F,0x011A,0x810B,0x010E,0x0104,0x8101,
    0x8303,0x0306,0x030C,0x8309,0x0318,0x831D,0x8317,0x0312,
    0x0330,0x8335,0x833F,0x033A,0x832B,0x032E,0x0324,0x8321,
    0x0360,0x8365,0x836F,0x036A,0x837B,0x037E,0x0374,0x8371,
    0x8353,0x0356,0x035C,0x8359,0x0348,0x834D,0x8347,0x0342,
    0x0300,0x8305,0x830F,0x030A,0x831B,0x031E,0x0314,0x8311,
    0x8333,0x0336,0x033C,0x8339,0x0328,0x832D,0x8327,0x0322,
    0x8363,0x0366,0x036C,0x8369,0x0378,0x837D,0x8377,0x0372,
    0x0350,0x8355,0x835F,0x035A,0x834B,0x034E,0x0344,0x8341,
    0x0280,0x8285,0x828F,0x028A,0x829B,0x029E,0x0294,0x8291,
    0x82B3,0x02B6,0x02BC,0x82B9,0x02A8,0x82AD,0x82A7,0x02A2,
    0x82E3,0x02E6,0x02EC,0x82E9,0x02F8,0x82FD,0x82F7,0x02F2,
    0x02D0,0x82D5,0x82DF,0x02DA,0x82CB,0x02CE,0x02C4,0x82C1,
    0x8243,0x0246,0x024C,0x8249,0x0258,0x825D,0x8257,0x0252,
    0x0270,0x8275,0x827F,0x027A,0x826B,0x026E,0x0264,0x8261,
    0x0220,0x8225,0x822F,0x022A,0x823B,0x023E,0x0234,0x8231,
    0x8213,0x0216,0x021C,0x8219,0x0208,0x820D,0x8207,0x0202,
};

static uint8_t flac_crc8(const uint8_t *data, size_t len)
{
    uint8_t crc = 0;
    for (size_t i = 0; i < len; i++)
        crc = flac_crc8_table[crc ^ data[i]];
    return crc;
}

static uint16_t flac_crc16(const uint8_t *data, size_t len)
{
    uint16_t crc = 0;
    for (size_t i = 0; i < len; i++)
        crc = (crc << 8) ^ flac_crc16_table[(crc >> 8) ^ data[i]];
    return crc;
}

/* Write big-endian helpers */
static void flac_write_be16(FILE *fp, uint16_t v)
{
    uint8_t b[2] = { (uint8_t)(v >> 8), (uint8_t)(v & 0xFF) };
    fwrite(b, 1, 2, fp);
}

static void flac_write_be24(FILE *fp, uint32_t v)
{
    uint8_t b[3] = { (uint8_t)((v >> 16) & 0xFF), (uint8_t)((v >> 8) & 0xFF),
                      (uint8_t)(v & 0xFF) };
    fwrite(b, 1, 3, fp);
}

static void flac_write_be32(FILE *fp, uint32_t v)
{
    uint8_t b[4] = { (uint8_t)(v >> 24), (uint8_t)((v >> 16) & 0xFF),
                      (uint8_t)((v >> 8) & 0xFF), (uint8_t)(v & 0xFF) };
    fwrite(b, 1, 4, fp);
}

/* Encode a UTF-8 coded frame number (FLAC uses UTF-8 encoding for frame numbers) */
static size_t flac_encode_utf8(uint32_t val, uint8_t *buf)
{
    if (val < 0x80) {
        buf[0] = (uint8_t)val;
        return 1;
    } else if (val < 0x800) {
        buf[0] = (uint8_t)(0xC0 | (val >> 6));
        buf[1] = (uint8_t)(0x80 | (val & 0x3F));
        return 2;
    } else if (val < 0x10000) {
        buf[0] = (uint8_t)(0xE0 | (val >> 12));
        buf[1] = (uint8_t)(0x80 | ((val >> 6) & 0x3F));
        buf[2] = (uint8_t)(0x80 | (val & 0x3F));
        return 3;
    } else if (val < 0x200000) {
        buf[0] = (uint8_t)(0xF0 | (val >> 18));
        buf[1] = (uint8_t)(0x80 | ((val >> 12) & 0x3F));
        buf[2] = (uint8_t)(0x80 | ((val >> 6) & 0x3F));
        buf[3] = (uint8_t)(0x80 | (val & 0x3F));
        return 4;
    } else if (val < 0x4000000) {
        buf[0] = (uint8_t)(0xF8 | (val >> 24));
        buf[1] = (uint8_t)(0x80 | ((val >> 18) & 0x3F));
        buf[2] = (uint8_t)(0x80 | ((val >> 12) & 0x3F));
        buf[3] = (uint8_t)(0x80 | ((val >> 6) & 0x3F));
        buf[4] = (uint8_t)(0x80 | (val & 0x3F));
        return 5;
    } else {
        buf[0] = (uint8_t)(0xFC | (val >> 30));
        buf[1] = (uint8_t)(0x80 | ((val >> 24) & 0x3F));
        buf[2] = (uint8_t)(0x80 | ((val >> 18) & 0x3F));
        buf[3] = (uint8_t)(0x80 | ((val >> 12) & 0x3F));
        buf[4] = (uint8_t)(0x80 | ((val >> 6) & 0x3F));
        buf[5] = (uint8_t)(0x80 | (val & 0x3F));
        return 6;
    }
}

/*
 * FLAC block size code lookup. Returns the 4-bit code for the frame header.
 * We use 4096 samples per frame (code 0x0C = 12).
 */
#define FLAC_BLOCK_SIZE 4096

/* Sample rate codes for FLAC frame header */
static int flac_sample_rate_code(uint32_t sr)
{
    switch (sr) {
    case  8000: return 4;
    case 16000: return 5;
    case 22050: return 6;
    case 24000: return 7;
    case 32000: return 8;
    case 44100: return 9;
    case 48000: return 10;
    case 96000: return 11;
    default:    return 0; /* 0 = get from STREAMINFO */
    }
}

int sq_export_write_flac(const char *filepath, const sq_export_result_t *result,
                         int bit_depth)
{
    if (!filepath || !result || !result->data) return -1;
    if (bit_depth != 16 && bit_depth != 24) {
        LOG_WARN("FLAC: unsupported bit depth %d, using 16", bit_depth);
        bit_depth = 16;
    }

    const uint32_t channels = 2;
    const uint32_t sample_rate = result->sample_rate;
    const uint32_t total_frames = result->num_frames;
    const int bytes_per_sample = bit_depth / 8;

    FILE *fp = fopen(filepath, "wb");
    if (!fp) {
        LOG_ERROR("Failed to open %s for writing", filepath);
        return -1;
    }

    /* ── fLaC marker ── */
    fwrite("fLaC", 1, 4, fp);

    /* ── STREAMINFO metadata block (last metadata block, type=0) ── */
    /* Header: 1 bit last-block flag + 7 bits type + 24 bits length */
    uint8_t meta_header[4];
    meta_header[0] = 0x80 | 0x00; /* last block=1, type=STREAMINFO(0) */
    /* STREAMINFO is always 34 bytes */
    meta_header[1] = 0x00;
    meta_header[2] = 0x00;
    meta_header[3] = 34;
    fwrite(meta_header, 1, 4, fp);

    /* STREAMINFO block: 34 bytes */
    uint32_t last_block_size = total_frames % FLAC_BLOCK_SIZE;
    if (last_block_size == 0) last_block_size = FLAC_BLOCK_SIZE;

    flac_write_be16(fp, FLAC_BLOCK_SIZE);      /* min block size */
    flac_write_be16(fp, FLAC_BLOCK_SIZE);      /* max block size */
    flac_write_be24(fp, 0);                     /* min frame size (0=unknown) */
    flac_write_be24(fp, 0);                     /* max frame size (0=unknown) */

    /* 20 bits sample rate + 3 bits (channels-1) + 5 bits (bps-1) + 36 bits total samples */
    uint64_t si_packed = 0;
    si_packed |= ((uint64_t)sample_rate & 0xFFFFF) << 44;
    si_packed |= ((uint64_t)(channels - 1) & 0x7) << 41;
    si_packed |= ((uint64_t)(bit_depth - 1) & 0x1F) << 36;
    si_packed |= (uint64_t)total_frames & 0xFFFFFFFFFULL;

    uint8_t si_bytes[8];
    for (int i = 7; i >= 0; i--) {
        si_bytes[i] = (uint8_t)(si_packed & 0xFF);
        si_packed >>= 8;
    }
    fwrite(si_bytes, 1, 8, fp);

    /* MD5 signature: 16 bytes of zeros (we skip MD5 computation) */
    uint8_t md5[16] = {0};
    fwrite(md5, 1, 16, fp);

    /* ── Audio frames ── */
    uint32_t frames_written = 0;
    uint32_t frame_number = 0;

    /* Allocate a buffer for one frame's worth of raw sample data + header overhead */
    size_t max_frame_bytes = 16 + /* frame header max */
                             channels * (1 + (size_t)FLAC_BLOCK_SIZE * bytes_per_sample) + /* subframes */
                             2; /* CRC-16 footer */
    uint8_t *frame_buf = (uint8_t *)malloc(max_frame_bytes);
    if (!frame_buf) {
        LOG_ERROR("Failed to allocate FLAC frame buffer");
        fclose(fp);
        return -1;
    }

    while (frames_written < total_frames) {
        uint32_t block_size = FLAC_BLOCK_SIZE;
        if (frames_written + block_size > total_frames)
            block_size = total_frames - frames_written;

        size_t pos = 0;

        /* Frame header sync code: 0xFFF8 (14 bits sync + 1 bit reserved=0 + 1 bit blocking strategy=0) */
        frame_buf[pos++] = 0xFF;
        frame_buf[pos++] = 0xF8;

        /* Block size code (4 bits) + sample rate code (4 bits) */
        int bs_code;
        int need_bs16 = 0;
        if (block_size == FLAC_BLOCK_SIZE) {
            bs_code = 0x0C; /* 4096 */
        } else {
            /* Use 16-bit block size at end of stream */
            bs_code = 0x06; /* get 8-bit (blocksize-1) from end of header */
            if (block_size > 256) {
                bs_code = 0x07; /* get 16-bit (blocksize-1) from end of header */
                need_bs16 = 1;
            }
        }

        int sr_code = flac_sample_rate_code(sample_rate);
        frame_buf[pos++] = (uint8_t)((bs_code << 4) | sr_code);

        /* Channel assignment (4 bits) + sample size (3 bits) + reserved (1 bit) */
        /* Channel assignment: 0x01 = 2 channels (left, right), independent */
        int bps_code;
        switch (bit_depth) {
        case 16: bps_code = 4; break;
        case 24: bps_code = 6; break;
        default: bps_code = 4; break;
        }
        frame_buf[pos++] = (uint8_t)(0x10 | (bps_code << 1));

        /* UTF-8 coded frame number */
        pos += flac_encode_utf8(frame_number, frame_buf + pos);

        /* Block size value at end of header (if needed) */
        if (bs_code == 0x06) {
            frame_buf[pos++] = (uint8_t)(block_size - 1);
        } else if (bs_code == 0x07 || need_bs16) {
            frame_buf[pos++] = (uint8_t)((block_size - 1) >> 8);
            frame_buf[pos++] = (uint8_t)((block_size - 1) & 0xFF);
        }

        /* CRC-8 of frame header */
        frame_buf[pos] = flac_crc8(frame_buf, pos);
        pos++;

        /* ── Subframes (one per channel, verbatim encoding) ── */
        const float *src = result->data + (size_t)frames_written * channels;

        for (uint32_t ch = 0; ch < channels; ch++) {
            /* Subframe header: 1 bit zero-pad + 6 bits type + 1 bit wasted-bits flag */
            /* Type 0b000001 = VERBATIM */
            frame_buf[pos++] = 0x02; /* 0b00000010 = verbatim, no wasted bits */

            /* Raw samples */
            for (uint32_t s = 0; s < block_size; s++) {
                float v = src[s * channels + ch];
                if (v > 1.0f) v = 1.0f;
                if (v < -1.0f) v = -1.0f;

                if (bit_depth == 16) {
                    int16_t sample = (int16_t)(v * 32767.0f);
                    frame_buf[pos++] = (uint8_t)((uint16_t)sample >> 8);
                    frame_buf[pos++] = (uint8_t)((uint16_t)sample & 0xFF);
                } else { /* 24-bit */
                    int32_t sample = (int32_t)(v * 8388607.0f);
                    frame_buf[pos++] = (uint8_t)((uint32_t)sample >> 16);
                    frame_buf[pos++] = (uint8_t)(((uint32_t)sample >> 8) & 0xFF);
                    frame_buf[pos++] = (uint8_t)((uint32_t)sample & 0xFF);
                }
            }
        }

        /* Byte-align (already aligned since verbatim samples are byte-aligned) */

        /* CRC-16 of entire frame (header + subframes) */
        uint16_t crc16 = flac_crc16(frame_buf, pos);
        frame_buf[pos++] = (uint8_t)(crc16 >> 8);
        frame_buf[pos++] = (uint8_t)(crc16 & 0xFF);

        fwrite(frame_buf, 1, pos, fp);

        frames_written += block_size;
        frame_number++;
    }

    free(frame_buf);
    fclose(fp);

    LOG_INFO("Wrote FLAC: %s (%u frames, %d-bit, %u Hz)",
             filepath, total_frames, bit_depth, sample_rate);
    return 0;
}
