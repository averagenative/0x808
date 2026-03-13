/*
 * sample_browser.c — File browser for loading audio samples.
 *
 * Layout:
 * ┌──────────────────────────────────────────────────┐
 * │ Sample Browser                                    │
 * │ Path: /home/user/samples                         │
 * │ [..] [samples/] [Load] [Assign Track X]          │
 * ├──────────────────────────────────────────────────┤
 * │ kick_01.wav           44100 Hz  1.2s             │
 * │ snare_tight.wav       44100 Hz  0.8s             │
 * │ hihat_closed.flac     48000 Hz  0.3s             │
 * │ > subdirectory/                                   │
 * └──────────────────────────────────────────────────┘
 */

#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#include "nuklear.h"

#include "gui/sample_browser.h"
#include "gui/gui.h"
#include "formats/sample_io.h"

#define LOG_TAG "browser"
#include "core/log.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* Platform-specific directory/file handling */
#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#include <io.h>
/* Minimal dirent emulation for Windows */
typedef struct DIR DIR;
struct dirent { char d_name[260]; };
static DIR *opendir(const char *path);
static struct dirent *readdir(DIR *dir);
static void closedir(DIR *dir);
struct _dir_impl { HANDLE h; WIN32_FIND_DATAA fdata; int first; };
static DIR *opendir(const char *path) {
    char buf[MAX_PATH]; snprintf(buf, sizeof(buf), "%s\\*", path);
    struct _dir_impl *d = calloc(1, sizeof(*d));
    if (!d) return NULL;
    d->h = FindFirstFileA(buf, &d->fdata); d->first = 1;
    if (d->h == INVALID_HANDLE_VALUE) { free(d); return NULL; }
    return (DIR *)d;
}
static struct dirent *readdir(DIR *dir) {
    static struct dirent de;
    struct _dir_impl *d = (struct _dir_impl *)dir;
    if (d->first) { d->first = 0; }
    else if (!FindNextFileA(d->h, &d->fdata)) return NULL;
    strncpy(de.d_name, d->fdata.cFileName, sizeof(de.d_name) - 1);
    de.d_name[sizeof(de.d_name) - 1] = '\0';
    return &de;
}
static void closedir(DIR *dir) {
    struct _dir_impl *d = (struct _dir_impl *)dir;
    FindClose(d->h); free(d);
}
#define getcwd _getcwd
#define strcasecmp _stricmp
#include <sys/stat.h>
#define S_ISDIR(m) (((m) & _S_IFMT) == _S_IFDIR)
static char *realpath(const char *path, char *resolved) {
    return _fullpath(resolved, path, MAX_PATH);
}
#else
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

/* ─── Constants ──────────────────────────────────────────────────────────── */

#define MAX_DIR_ENTRIES 256
#define MAX_PATH_LEN    1024

/* ─── State ──────────────────────────────────────────────────────────────── */

typedef struct {
    char name[256];
    bool is_dir;
    off_t size;
} dir_entry_t;

static char s_current_path[MAX_PATH_LEN] = "";
static dir_entry_t s_entries[MAX_DIR_ENTRIES];
static int s_num_entries = 0;
static int s_selected_entry = -1;
static bool s_needs_refresh = true;
static int s_last_loaded_sample = -1;

/* Preview sample state: decoded for waveform display + audition */
static sq_sample_t s_preview_sample = {0};
static bool s_preview_loaded = false;
static char s_preview_path[MAX_PATH_LEN] = "";

/* Dedicated audition slot — persists while a voice plays the preview.
 * This avoids overwriting engine->samples[SQ_MAX_SAMPLES-1] permanently. */
static sq_sample_t s_audition_sample = {0};
static bool s_audition_active = false;
static sq_sample_t s_saved_slot = {0};

/* Close request flag — set by the "X" button, read by gui.c */
static bool s_close_requested = false;

/* ─── Helpers ────────────────────────────────────────────────────────────── */

static bool is_audio_file(const char *name)
{
    const char *ext = strrchr(name, '.');
    if (!ext) return false;
    return (strcasecmp(ext, ".wav") == 0 ||
            strcasecmp(ext, ".mp3") == 0 ||
            strcasecmp(ext, ".flac") == 0 ||
            strcasecmp(ext, ".sf2") == 0);
}

static int entry_cmp(const void *a, const void *b)
{
    const dir_entry_t *ea = (const dir_entry_t *)a;
    const dir_entry_t *eb = (const dir_entry_t *)b;
    /* Directories first, then alphabetical */
    if (ea->is_dir && !eb->is_dir) return -1;
    if (!ea->is_dir && eb->is_dir) return 1;
    return strcasecmp(ea->name, eb->name);
}

static void refresh_directory(void)
{
    s_num_entries = 0;
    s_selected_entry = -1;

    if (s_current_path[0] == '\0') {
        /* Default to project samples directory or home */
        if (getcwd(s_current_path, sizeof(s_current_path)) == NULL) {
            strncpy(s_current_path, "/", sizeof(s_current_path));
        }
    }

    DIR *dir = opendir(s_current_path);
    if (!dir) {
        LOG_ERROR("Cannot open directory: %s", s_current_path);
        return;
    }

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL && s_num_entries < MAX_DIR_ENTRIES) {
        /* Skip hidden files except ".." */
        if (ent->d_name[0] == '.' && strcmp(ent->d_name, "..") != 0)
            continue;

        char fullpath[MAX_PATH_LEN];
        snprintf(fullpath, sizeof(fullpath), "%s/%s", s_current_path, ent->d_name);

        struct stat st;
        if (stat(fullpath, &st) != 0) continue;

        bool is_dir = S_ISDIR(st.st_mode);

        /* Only show directories and audio files */
        if (!is_dir && !is_audio_file(ent->d_name))
            continue;

        dir_entry_t *e = &s_entries[s_num_entries];
        strncpy(e->name, ent->d_name, sizeof(e->name) - 1);
        e->name[sizeof(e->name) - 1] = '\0';
        e->is_dir = is_dir;
        e->size = is_dir ? 0 : st.st_size;
        s_num_entries++;
    }

    closedir(dir);

    /* Sort: directories first, then alphabetical */
    if (s_num_entries > 0) {
        qsort(s_entries, (size_t)s_num_entries, sizeof(dir_entry_t), entry_cmp);
    }

    s_needs_refresh = false;
    LOG_DEBUG("Refreshed: %s (%d entries)", s_current_path, s_num_entries);
}

static void navigate_to(const char *dirname)
{
    char newpath[MAX_PATH_LEN];

    if (strcmp(dirname, "..") == 0) {
        /* Go up one level */
        strncpy(newpath, s_current_path, sizeof(newpath) - 1);
        newpath[sizeof(newpath) - 1] = '\0';
        char *last_slash = strrchr(newpath, '/');
#ifdef _WIN32
        char *last_bslash = strrchr(newpath, '\\');
        if (last_bslash && (!last_slash || last_bslash > last_slash))
            last_slash = last_bslash;
#endif
        if (last_slash && last_slash != newpath) {
            *last_slash = '\0';
        } else {
            strncpy(newpath, "/", sizeof(newpath));
        }
    } else {
        snprintf(newpath, sizeof(newpath), "%s/%s", s_current_path, dirname);
    }

    /* Resolve to real path */
    char resolved[MAX_PATH_LEN];
    if (realpath(newpath, resolved)) {
        strncpy(s_current_path, resolved, sizeof(s_current_path) - 1);
        s_current_path[sizeof(s_current_path) - 1] = '\0';
    } else {
        strncpy(s_current_path, newpath, sizeof(s_current_path) - 1);
        s_current_path[sizeof(s_current_path) - 1] = '\0';
    }

    s_needs_refresh = true;
    LOG_INFO("Navigate to: %s", s_current_path);
}

/* ─── Draw ───────────────────────────────────────────────────────────────── */

int sample_browser_draw(struct nk_context *ctx, sq_engine_t *engine,
                        float x, float y, float w, float h)
{
    int loaded_sample = -1;

    if (s_needs_refresh) {
        refresh_directory();
    }

    if (nk_begin(ctx, "SampleBrowser",
                 nk_rect(x, y, w, h),
                 NK_WINDOW_BORDER | NK_WINDOW_TITLE))
    {
        /* Current path display */
        nk_layout_row_dynamic(ctx, 18, 1);
        nk_labelf(ctx, NK_TEXT_LEFT, "Path: %s", s_current_path);

        /* Navigation buttons */
        nk_layout_row_dynamic(ctx, 25, 5);
        if (nk_button_label(ctx, "Up (..)")) {
            navigate_to("..");
        }
        if (nk_button_label(ctx, "Refresh")) {
            s_needs_refresh = true;
        }
        /* Close button — allows closing browser without the toolbar */
        {
            struct nk_style_button close_style = ctx->style.button;
            close_style.normal = nk_style_item_color(nk_rgba(160, 50, 50, 255));
            close_style.hover  = nk_style_item_color(nk_rgba(200, 70, 70, 255));
            close_style.active = nk_style_item_color(nk_rgba(220, 90, 90, 255));
            close_style.text_normal = nk_rgb(255, 255, 255);
            close_style.text_hover  = nk_rgb(255, 255, 255);
            if (nk_button_label_styled(ctx, &close_style, "X")) {
                s_close_requested = true;
            }
        }

        /* Load button */
        bool can_load = (s_selected_entry >= 0 &&
                         s_selected_entry < s_num_entries &&
                         !s_entries[s_selected_entry].is_dir);
        if (can_load) {
            if (nk_button_label(ctx, "Load Sample")) {
                char filepath[MAX_PATH_LEN];
                snprintf(filepath, sizeof(filepath), "%s/%s",
                         s_current_path, s_entries[s_selected_entry].name);

                if (engine->num_samples < SQ_MAX_SAMPLES) {
                    int idx = (int)engine->num_samples;
                    if (sample_io_load(filepath, &engine->samples[idx]) == 0) {
                        engine->num_samples++;
                        loaded_sample = idx;
                        s_last_loaded_sample = idx;
                        LOG_INFO("Loaded sample [%d]: %s", idx,
                                 engine->samples[idx].name);
                    } else {
                        LOG_ERROR("Failed to load: %s", filepath);
                    }
                } else {
                    LOG_WARN("Sample slots full (%d/%d)",
                             engine->num_samples, SQ_MAX_SAMPLES);
                }
            }
        } else {
            /* Greyed out load button */
            struct nk_style_button disabled = ctx->style.button;
            disabled.normal = nk_style_item_color(nk_rgba(40, 40, 43, 255));
            disabled.text_normal = nk_rgba(100, 100, 100, 255);
            nk_button_label_styled(ctx, &disabled, "Load Sample");
        }

        /* Status */
        nk_labelf(ctx, NK_TEXT_RIGHT, "Samples: %u/%d",
                  engine->num_samples, SQ_MAX_SAMPLES);

        /* Waveform preview takes space from file list */
        float waveform_h = 0;
        if (s_preview_loaded && s_preview_sample.data)
            waveform_h = 70;

        /* File list */
        nk_layout_row_dynamic(ctx, h - 100 - waveform_h, 1);
        if (nk_group_begin(ctx, "FileList", NK_WINDOW_BORDER)) {
            for (int i = 0; i < s_num_entries; i++) {
                dir_entry_t *e = &s_entries[i];

                nk_layout_row_begin(ctx, NK_DYNAMIC, 18, 3);

                /* Name column (60%) */
                nk_layout_row_push(ctx, 0.60f);
                {
                    char label[280];
                    if (e->is_dir) {
                        snprintf(label, sizeof(label), "> %s/", e->name);
                    } else {
                        snprintf(label, sizeof(label), "  %s", e->name);
                    }

                    /* Highlight selected entry */
                    nk_bool selected = (i == s_selected_entry) ? 1 : 0;
                    if (nk_selectable_label(ctx, label, NK_TEXT_LEFT, &selected)) {
                        if (selected) {
                            if (s_selected_entry == i && e->is_dir) {
                                /* Double-click on directory: navigate */
                                navigate_to(e->name);
                            }
                            s_selected_entry = i;
                        } else {
                            s_selected_entry = -1;
                        }
                    }
                }

                /* Size column (20%) */
                nk_layout_row_push(ctx, 0.20f);
                if (e->is_dir) {
                    nk_label(ctx, "<DIR>", NK_TEXT_RIGHT);
                } else {
                    char size_str[32];
                    if (e->size < 1024)
                        snprintf(size_str, sizeof(size_str), "%ld B", (long)e->size);
                    else if (e->size < 1024 * 1024)
                        snprintf(size_str, sizeof(size_str), "%.1f KB",
                                 (double)e->size / 1024.0);
                    else
                        snprintf(size_str, sizeof(size_str), "%.1f MB",
                                 (double)e->size / (1024.0 * 1024.0));
                    nk_label(ctx, size_str, NK_TEXT_RIGHT);
                }

                /* Action column (20%) */
                nk_layout_row_push(ctx, 0.20f);
                if (e->is_dir) {
                    if (nk_button_label(ctx, "Open")) {
                        navigate_to(e->name);
                    }
                } else {
                    nk_label(ctx, "", NK_TEXT_LEFT); /* placeholder */
                }

                nk_layout_row_end(ctx);
            }

            if (s_num_entries == 0) {
                nk_layout_row_dynamic(ctx, 18, 1);
                nk_label(ctx, "(no audio files in this directory)", NK_TEXT_CENTERED);
            }

            nk_group_end(ctx);
        }
        /* Waveform preview */
        if (s_preview_loaded && s_preview_sample.data) {
            nk_layout_row_dynamic(ctx, 14, 2);
            nk_labelf(ctx, NK_TEXT_LEFT, "Preview: %.32s", s_preview_sample.name);
            nk_labelf(ctx, NK_TEXT_RIGHT, "%.1fs %uHz",
                      (float)s_preview_sample.num_frames / s_preview_sample.sample_rate,
                      s_preview_sample.sample_rate);

            /* Draw waveform using Nuklear canvas */
            nk_layout_row_dynamic(ctx, 40, 1);
            struct nk_rect wf_bounds = nk_widget_bounds(ctx);
            nk_spacing(ctx, 1);
            struct nk_command_buffer *canvas = nk_window_get_canvas(ctx);

            /* Background */
            nk_fill_rect(canvas, wf_bounds, 0, nk_rgba(25, 25, 28, 255));

            /* Center line */
            float cy = wf_bounds.y + wf_bounds.h * 0.5f;
            nk_stroke_line(canvas, wf_bounds.x, cy,
                           wf_bounds.x + wf_bounds.w, cy,
                           1.0f, nk_rgba(60, 60, 65, 255));

            /* Draw waveform as min/max vertical lines per pixel */
            float *sdata = s_preview_sample.data;
            uint32_t nframes = s_preview_sample.num_frames;
            uint32_t nch = s_preview_sample.num_channels;
            int pw = (int)wf_bounds.w;
            if (pw > 0 && nframes > 0) {
                for (int px = 0; px < pw; px++) {
                    uint32_t start = (uint32_t)((uint64_t)px * nframes / pw);
                    uint32_t end   = (uint32_t)((uint64_t)(px + 1) * nframes / pw);
                    if (end > nframes) end = nframes;
                    float mn = 0, mx = 0;
                    for (uint32_t f = start; f < end; f++) {
                        float v = sdata[f * nch]; /* left channel */
                        if (v < mn) mn = v;
                        if (v > mx) mx = v;
                    }
                    float half_h = wf_bounds.h * 0.5f;
                    float y0 = cy - mx * half_h;
                    float y1 = cy - mn * half_h;
                    if (y1 - y0 < 1.0f) y1 = y0 + 1.0f;
                    nk_stroke_line(canvas, wf_bounds.x + px, y0,
                                   wf_bounds.x + px, y1,
                                   1.0f, nk_rgba(80, 180, 220, 200));
                }
            }

            /* Audition button */
            nk_layout_row_dynamic(ctx, 18, 2);
            if (nk_button_label(ctx, "Audition")) {
                int tmp_idx = SQ_MAX_SAMPLES - 1;

                /* Stop any voices still using the audition slot */
                if (s_audition_active) {
                    for (int v = 0; v < SQ_MAX_VOICES; v++) {
                        if (engine->voices[v].active &&
                            engine->voices[v].sample_index == tmp_idx) {
                            engine->voices[v].active = false;
                        }
                    }
                    /* Restore the original sample that was in this slot */
                    engine->samples[tmp_idx] = s_saved_slot;
                    s_audition_active = false;
                }

                /* Copy preview into persistent audition slot (shallow copy;
                 * s_preview_sample owns the data pointer) */
                s_audition_sample = s_preview_sample;

                /* Save whatever is currently in the engine slot */
                s_saved_slot = engine->samples[tmp_idx];

                /* Place audition sample into the engine slot */
                engine->samples[tmp_idx] = s_audition_sample;
                s_audition_active = true;

                /* Trigger on first free voice */
                for (int v = 0; v < SQ_MAX_VOICES; v++) {
                    if (!engine->voices[v].active) {
                        engine->voices[v].active = true;
                        engine->voices[v].sample_index = tmp_idx;
                        engine->voices[v].position = 0.0;
                        engine->voices[v].rate = 1.0;
                        engine->voices[v].velocity = 0.8f;
                        engine->voices[v].volume = 0.8f;
                        engine->voices[v].pan = 0.0f;
                        engine->voices[v].start_time = 0;
                        break;
                    }
                }
            }
            nk_spacing(ctx, 1);
        }
    }
    nk_end(ctx);

    /* Auto-load preview when selection changes */
    if (s_selected_entry >= 0 && s_selected_entry < s_num_entries &&
        !s_entries[s_selected_entry].is_dir) {
        char filepath[MAX_PATH_LEN];
        snprintf(filepath, sizeof(filepath), "%s/%s",
                 s_current_path, s_entries[s_selected_entry].name);
        if (strcmp(filepath, s_preview_path) != 0) {
            /* Stop audition voices before freeing preview data they share */
            if (s_audition_active) {
                int tmp_idx = SQ_MAX_SAMPLES - 1;
                for (int v = 0; v < SQ_MAX_VOICES; v++) {
                    if (engine->voices[v].active &&
                        engine->voices[v].sample_index == tmp_idx) {
                        engine->voices[v].active = false;
                    }
                }
                engine->samples[tmp_idx] = s_saved_slot;
                s_audition_active = false;
                memset(&s_audition_sample, 0, sizeof(s_audition_sample));
            }
            /* Free old preview */
            if (s_preview_sample.data) {
                free(s_preview_sample.data);
                s_preview_sample.data = NULL;
            }
            s_preview_loaded = false;
            /* Load new preview */
            if (sample_io_load(filepath, &s_preview_sample) == 0) {
                s_preview_loaded = true;
                strncpy(s_preview_path, filepath, sizeof(s_preview_path) - 1);
                s_preview_path[sizeof(s_preview_path) - 1] = '\0';
            } else {
                s_preview_path[0] = '\0';
            }
        }
    }

    return loaded_sample;
}

bool sample_browser_close_requested(void)
{
    if (s_close_requested) {
        s_close_requested = false;
        return true;
    }
    return false;
}
