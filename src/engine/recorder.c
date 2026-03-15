/*
 * recorder.c — Streaming WAV recorder.
 *
 * Writes audio directly to disk during recording via dr_wav streaming API.
 * No large in-memory buffer — audio is flushed to the OS page cache each
 * callback (~256-512 frames = 1-2 KB per write).
 */

#define LOG_TAG "recorder"
#include "core/log.h"

#include "dr_wav.h"
#include "engine/engine.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#define sq_mkdir(p) _mkdir(p)
#else
#include <sys/statvfs.h>
#include <sys/stat.h>
#include <dirent.h>
#define sq_mkdir(p) mkdir(p, 0755)
#endif

/* Helper: create directory and all parents */
static void ensure_directory(const char *dir)
{
    if (!dir || !dir[0]) return;
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", dir);
    size_t len = strlen(tmp);
    if (len > 0 && (tmp[len - 1] == '/' || tmp[len - 1] == '\\'))
        tmp[--len] = '\0';
    for (size_t i = 1; i < len; i++) {
        if (tmp[i] == '/' || tmp[i] == '\\') {
            char c = tmp[i];
            tmp[i] = '\0';
            sq_mkdir(tmp);
            tmp[i] = c;
        }
    }
    sq_mkdir(tmp);
}

/* Helper: extract directory from a filepath */
static void extract_dir(const char *filepath, char *dir, size_t dir_size)
{
    snprintf(dir, dir_size, "%s", filepath);
    char *sep = strrchr(dir, '/');
#ifdef _WIN32
    char *sep2 = strrchr(dir, '\\');
    if (sep2 > sep) sep = sep2;
#endif
    if (sep) *(sep + 1) = '\0';
    else snprintf(dir, dir_size, ".");
}

/* ─── Start recording ────────────────────────────────────────────────────── */

int sq_recorder_start(sq_recorder_t *rec, const char *filepath,
                      uint32_t sample_rate, uint32_t bit_depth)
{
    if (!rec || !filepath) return -1;

    /* Stop any existing recording first */
    if (rec->state == SQ_REC_ACTIVE)
        sq_recorder_stop(rec);

    if (bit_depth != 16 && bit_depth != 24 && bit_depth != 32)
        bit_depth = 16;

    /* Ensure output directory exists */
    char dir[512];
    extract_dir(filepath, dir, sizeof(dir));
    ensure_directory(dir);

    /* Allocate drwav on heap */
    drwav *wav = (drwav *)calloc(1, sizeof(drwav));
    if (!wav) {
        LOG_ERROR("Failed to allocate drwav struct");
        return -1;
    }

    drwav_data_format format;
    format.container = drwav_container_riff;
    format.channels = 2;
    format.sampleRate = sample_rate;
    if (bit_depth == 32) {
        format.format = DR_WAVE_FORMAT_IEEE_FLOAT;
        format.bitsPerSample = 32;
    } else {
        format.format = DR_WAVE_FORMAT_PCM;
        format.bitsPerSample = (drwav_uint32)bit_depth;
    }

    if (!drwav_init_file_write(wav, filepath, &format, NULL)) {
        LOG_ERROR("Failed to open %s for streaming WAV write", filepath);
        free(wav);
        return -1;
    }

    rec->wav = wav;
    rec->state = SQ_REC_ACTIVE;
    rec->bit_depth = bit_depth;
    rec->sample_rate = sample_rate;
    rec->frames_written = 0;
    rec->disk_low = false;
    rec->disk_check_countdown = 0;
    snprintf(rec->filepath, sizeof(rec->filepath), "%s", filepath);

    LOG_INFO("Recording started: %s (%u-bit, %u Hz)",
             filepath, bit_depth, sample_rate);
    return 0;
}

/* Helper: handle write failure */
static void rec_handle_error(sq_recorder_t *rec, drwav_uint64 partial)
{
    rec->state = SQ_REC_ERROR;
    rec->frames_written += (uint64_t)partial;
    LOG_ERROR("Recording write failed at frame %llu",
              (unsigned long long)rec->frames_written);
    drwav *wav = (drwav *)rec->wav;
    if (wav) {
        drwav_uninit(wav);
        free(wav);
        rec->wav = NULL;
    }
}

/* ─── Write audio frames ─────────────────────────────────────────────────── */

void sq_recorder_write(sq_recorder_t *rec, const float *output,
                       uint32_t num_frames)
{
    if (!rec || rec->state != SQ_REC_ACTIVE || !rec->wav || !output)
        return;

    drwav *wav = (drwav *)rec->wav;

    if (rec->bit_depth == 32) {
        /* 32-bit float: write directly */
        drwav_uint64 written = drwav_write_pcm_frames(wav, num_frames, output);
        if (written < num_frames) {
            rec_handle_error(rec, written);
            return;
        }
        rec->frames_written += num_frames;
    } else if (rec->bit_depth == 16) {
        /* Convert float -> int16 on stack in small chunks */
        int16_t pcm16[2048]; /* 1024 stereo frames */
        uint32_t remaining = num_frames;
        const float *src = output;
        while (remaining > 0) {
            uint32_t chunk = remaining > 1024 ? 1024 : remaining;
            for (uint32_t i = 0; i < chunk * 2; i++) {
                float v = src[i];
                if (v > 1.0f) v = 1.0f;
                if (v < -1.0f) v = -1.0f;
                pcm16[i] = (int16_t)(v * 32767.0f);
            }
            drwav_uint64 w = drwav_write_pcm_frames(wav, chunk, pcm16);
            if (w < chunk) {
                rec_handle_error(rec, w);
                return;
            }
            rec->frames_written += chunk;
            src += chunk * 2;
            remaining -= chunk;
        }
    } else {
        /* 24-bit: convert float -> int32 */
        int32_t pcm32[2048];
        uint32_t remaining = num_frames;
        const float *src = output;
        while (remaining > 0) {
            uint32_t chunk = remaining > 1024 ? 1024 : remaining;
            for (uint32_t i = 0; i < chunk * 2; i++) {
                float v = src[i];
                if (v > 1.0f) v = 1.0f;
                if (v < -1.0f) v = -1.0f;
                pcm32[i] = (int32_t)(v * 8388607.0f);
            }
            drwav_uint64 w = drwav_write_pcm_frames(wav, chunk, pcm32);
            if (w < chunk) {
                rec_handle_error(rec, w);
                return;
            }
            rec->frames_written += chunk;
            src += chunk * 2;
            remaining -= chunk;
        }
    }

    /* Periodic disk space check (~every 10 seconds) */
    if (rec->disk_check_countdown == 0) {
        rec->disk_free_bytes = sq_recorder_disk_free(rec->filepath);
        rec->disk_low = (rec->disk_free_bytes > 0 &&
                         rec->disk_free_bytes < 500ULL * 1024 * 1024);
        rec->disk_check_countdown = rec->sample_rate * 10;
    } else if (rec->disk_check_countdown > num_frames) {
        rec->disk_check_countdown -= num_frames;
    } else {
        rec->disk_check_countdown = 0;
    }
}

/* ─── Stop recording ─────────────────────────────────────────────────────── */

void sq_recorder_stop(sq_recorder_t *rec)
{
    if (!rec) return;

    drwav *wav = (drwav *)rec->wav;
    if (wav) {
        drwav_uninit(wav);
        free(wav);
        rec->wav = NULL;
    }

    if (rec->state == SQ_REC_ACTIVE) {
        LOG_INFO("Recording stopped: %llu frames -> %s",
                 (unsigned long long)rec->frames_written, rec->filepath);
    }

    rec->state = SQ_REC_IDLE;
}

/* ─── Auto-incrementing filenames ────────────────────────────────────────── */

int sq_recorder_next_filename(const char *output_dir, const char *prefix,
                              int last_known, char *out_path, size_t out_path_size)
{
    if (!output_dir || !prefix || !out_path) return -1;

    /* Ensure output directory exists */
    ensure_directory(output_dir);

    int max_num = last_known > 0 ? last_known : 0;
    size_t prefix_len = strlen(prefix);

#ifdef _WIN32
    char search_pattern[600];
    snprintf(search_pattern, sizeof(search_pattern), "%s\\%s_*.wav",
             output_dir, prefix);
    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(search_pattern, &fd);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            const char *name = fd.cFileName;
            if (strncmp(name, prefix, prefix_len) == 0 && name[prefix_len] == '_') {
                int num = atoi(name + prefix_len + 1);
                if (num > max_num) max_num = num;
            }
        } while (FindNextFileA(hFind, &fd));
        FindClose(hFind);
    }
#else
    DIR *dir = opendir(output_dir);
    if (dir) {
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            const char *name = entry->d_name;
            if (strncmp(name, prefix, prefix_len) == 0 && name[prefix_len] == '_') {
                int num = atoi(name + prefix_len + 1);
                if (num > max_num) max_num = num;
            }
        }
        closedir(dir);
    }
#endif

    int next = max_num + 1;

    const char *sep = "/";
#ifdef _WIN32
    sep = "\\";
#endif
    size_t dirlen = strlen(output_dir);
    if (dirlen > 0 && (output_dir[dirlen - 1] == '/' || output_dir[dirlen - 1] == '\\'))
        sep = "";

    snprintf(out_path, out_path_size, "%s%s%s_%03d.wav",
             output_dir, sep, prefix, next);

    return next;
}

/* ─── Disk free space ────────────────────────────────────────────────────── */

uint64_t sq_recorder_disk_free(const char *path)
{
    if (!path || !path[0]) return 0;

#ifdef _WIN32
    ULARGE_INTEGER free_bytes;
    char dir[512];
    extract_dir(path, dir, sizeof(dir));
    if (GetDiskFreeSpaceExA(dir, &free_bytes, NULL, NULL))
        return (uint64_t)free_bytes.QuadPart;
    return 0;
#else
    char dir[512];
    extract_dir(path, dir, sizeof(dir));
    struct statvfs st;
    if (statvfs(dir, &st) == 0)
        return (uint64_t)st.f_bavail * (uint64_t)st.f_frsize;
    return 0;
#endif
}
