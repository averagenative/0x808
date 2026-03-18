/*
 * sample_browser.cpp — File browser for loading audio samples (Dear ImGui port).
 *
 * Layout:
 * +--------------------------------------------------+
 * | Sample Browser                                    |
 * | Path: /home/user/samples                         |
 * | [Up (..)] [Refresh] [X] [Load Sample] Samples:n  |
 * +--------------------------------------------------+
 * | kick_01.wav           44100 Hz  1.2s             |
 * | snare_tight.wav       44100 Hz  0.8s             |
 * | hihat_closed.flac     48000 Hz  0.3s             |
 * | > subdirectory/                                   |
 * +--------------------------------------------------+
 * | Preview: kick_01.wav   1.2s 44100Hz              |
 * | [waveform]                                        |
 * | [Audition]                                        |
 * +--------------------------------------------------+
 */

#include "imgui.h"

extern "C" {
#include "engine/engine.h"
#include "formats/sample_io.h"
}

extern "C" {
#define LOG_TAG "browser"
#include "core/log.h"
}

#include <cstdio>
#include <cstring>
#include <cstdlib>

/* Platform-specific directory/file handling */
#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#include <io.h>
#include <sys/stat.h>

/* Minimal POSIX-like dirent emulation for Windows */
struct dirent { char d_name[260]; };
typedef struct { HANDLE h; WIN32_FIND_DATAA fdata; int first; } DIR;

static DIR *opendir(const char *path) {
    char buf[MAX_PATH]; snprintf(buf, sizeof(buf), "%s\\*", path);
    DIR *d = (DIR *)calloc(1, sizeof(*d));
    if (!d) return NULL;
    d->h = FindFirstFileA(buf, &d->fdata); d->first = 1;
    if (d->h == INVALID_HANDLE_VALUE) { free(d); return NULL; }
    return d;
}
static struct dirent *readdir(DIR *dir) {
    static struct dirent de;
    if (dir->first) { dir->first = 0; }
    else if (!FindNextFileA(dir->h, &dir->fdata)) return NULL;
    strncpy(de.d_name, dir->fdata.cFileName, sizeof(de.d_name) - 1);
    de.d_name[sizeof(de.d_name) - 1] = '\0';
    return &de;
}
static void closedir(DIR *dir) { FindClose(dir->h); free(dir); }
#define getcwd _getcwd
#define strcasecmp _stricmp
#define S_ISDIR(m) (((m) & _S_IFMT) == _S_IFDIR)
static char *realpath(const char *path, char *resolved) {
    return _fullpath(resolved, path, MAX_PATH);
}
#else
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

/* --- Constants --- */

#define MAX_DIR_ENTRIES 256
#define MAX_PATH_LEN    1024

/* --- State --- */

struct dir_entry_t {
    char name[256];
    bool is_dir;
    off_t size;
};

static char s_current_path[MAX_PATH_LEN] = "";
static dir_entry_t s_entries[MAX_DIR_ENTRIES];
static int s_num_entries = 0;
static int s_selected_entry = -1;
static bool s_needs_refresh = true;
static int s_last_loaded_sample = -1;

/* Preview sample state */
static sq_sample_t s_preview_sample = {};
static bool s_preview_loaded = false;
static char s_preview_path[MAX_PATH_LEN] = "";

/* Audition slot */
static sq_sample_t s_audition_sample = {};
static bool s_audition_active = false;
static sq_sample_t s_saved_slot = {};

/* Close request flag */
static bool s_close_requested = false;

/* --- Helpers --- */

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
    if (ea->is_dir && !eb->is_dir) return -1;
    if (!ea->is_dir && eb->is_dir) return 1;
    return strcasecmp(ea->name, eb->name);
}

static void refresh_directory(void)
{
    s_num_entries = 0;
    s_selected_entry = -1;

    if (s_current_path[0] == '\0') {
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
        if (ent->d_name[0] == '.' && strcmp(ent->d_name, "..") != 0)
            continue;

        char fullpath[MAX_PATH_LEN];
        snprintf(fullpath, sizeof(fullpath), "%s/%s", s_current_path, ent->d_name);

        struct stat st;
        if (stat(fullpath, &st) != 0) continue;

        bool is_dir = S_ISDIR(st.st_mode);
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

/* --- Draw --- */

extern "C" void sample_browser_draw(sq_engine_t *engine,
                                     float x, float y, float w, float h)
{
    if (s_needs_refresh) {
        refresh_directory();
    }

    ImGui::SetNextWindowPos(ImVec2(x, y));
    ImGui::SetNextWindowSize(ImVec2(w, h));
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar;

    if (!ImGui::Begin("SampleBrowser", nullptr, flags)) {
        ImGui::End();
        return;
    }

    /* Title */
    ImGui::Text("Sample Browser");
    ImGui::Separator();

    /* Current path */
    ImGui::Text("Path: %s", s_current_path);

    /* Navigation buttons */
    if (ImGui::Button("Up (..)")) {
        navigate_to("..");
    }
    ImGui::SameLine();
    if (ImGui::Button("Refresh")) {
        s_needs_refresh = true;
    }
    ImGui::SameLine();

    /* Close button (red) */
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.63f, 0.20f, 0.20f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.78f, 0.27f, 0.27f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.86f, 0.35f, 0.35f, 1.0f));
    if (ImGui::Button("X")) {
        s_close_requested = true;
    }
    ImGui::PopStyleColor(3);
    ImGui::SameLine();

    /* Load button */
    bool can_load = (s_selected_entry >= 0 &&
                     s_selected_entry < s_num_entries &&
                     !s_entries[s_selected_entry].is_dir);
    if (!can_load) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("Load Sample")) {
        if (can_load) {
            char filepath[MAX_PATH_LEN];
            snprintf(filepath, sizeof(filepath), "%s/%s",
                     s_current_path, s_entries[s_selected_entry].name);

            if (engine->num_samples < SQ_MAX_SAMPLES) {
                int idx = (int)engine->num_samples;
                if (sample_io_load(filepath, &engine->samples[idx]) == 0) {
                    engine->num_samples++;
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
    }
    if (!can_load) {
        ImGui::EndDisabled();
    }

    ImGui::SameLine();
    ImGui::Text("Samples: %u/%d", engine->num_samples, SQ_MAX_SAMPLES);

    ImGui::Separator();

    /* Waveform preview height */
    float waveform_h = 0;
    if (s_preview_loaded && s_preview_sample.data)
        waveform_h = 90.0f;

    /* File list in a scrollable child region */
    float list_h = ImGui::GetContentRegionAvail().y - waveform_h;
    if (list_h < 50.0f) list_h = 50.0f;

    if (ImGui::BeginChild("FileList", ImVec2(0, list_h), ImGuiChildFlags_Borders)) {
        /* Three columns: Name, Size, Action */
        if (ImGui::BeginTable("FileTable", 3,
                              ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY)) {
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 0.6f);
            ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthStretch, 0.2f);
            ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch, 0.2f);

            for (int i = 0; i < s_num_entries; i++) {
                dir_entry_t *e = &s_entries[i];

                ImGui::TableNextRow();

                /* Name column */
                ImGui::TableSetColumnIndex(0);
                char label[280];
                if (e->is_dir) {
                    snprintf(label, sizeof(label), "> %s/", e->name);
                } else {
                    snprintf(label, sizeof(label), "  %s", e->name);
                }

                bool selected = (i == s_selected_entry);
                ImGui::PushID(i);
                if (ImGui::Selectable(label, selected,
                                      ImGuiSelectableFlags_SpanAllColumns |
                                      ImGuiSelectableFlags_AllowDoubleClick)) {
                    if (ImGui::IsMouseDoubleClicked(0) && e->is_dir) {
                        navigate_to(e->name);
                    }
                    s_selected_entry = i;
                }
                ImGui::PopID();

                /* Size column */
                ImGui::TableSetColumnIndex(1);
                if (e->is_dir) {
                    ImGui::TextDisabled("<DIR>");
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
                    ImGui::TextUnformatted(size_str);
                }

                /* Action column */
                ImGui::TableSetColumnIndex(2);
                if (e->is_dir) {
                    ImGui::PushID(i + 10000);
                    if (ImGui::SmallButton("Open")) {
                        navigate_to(e->name);
                    }
                    ImGui::PopID();
                }
            }

            if (s_num_entries == 0) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextDisabled("(no audio files in this directory)");
            }

            ImGui::EndTable();
        }
    }
    ImGui::EndChild();

    /* Waveform preview */
    if (s_preview_loaded && s_preview_sample.data) {
        ImGui::Text("Preview: %.32s", s_preview_sample.name);
        ImGui::SameLine();
        ImGui::Text("  %.1fs %uHz",
                     (float)s_preview_sample.num_frames / s_preview_sample.sample_rate,
                     s_preview_sample.sample_rate);

        /* Draw waveform using ImGui DrawList */
        ImVec2 wf_pos = ImGui::GetCursorScreenPos();
        float wf_w = ImGui::GetContentRegionAvail().x;
        float wf_h = 40.0f;
        ImDrawList *dl = ImGui::GetWindowDrawList();

        /* Background */
        dl->AddRectFilled(wf_pos, ImVec2(wf_pos.x + wf_w, wf_pos.y + wf_h),
                          IM_COL32(25, 25, 28, 255));

        /* Center line */
        float cy = wf_pos.y + wf_h * 0.5f;
        dl->AddLine(ImVec2(wf_pos.x, cy), ImVec2(wf_pos.x + wf_w, cy),
                    IM_COL32(60, 60, 65, 255));

        /* Draw waveform as min/max vertical lines per pixel */
        float *sdata = s_preview_sample.data;
        uint32_t nframes = s_preview_sample.num_frames;
        uint32_t nch = s_preview_sample.num_channels;
        int pw = (int)wf_w;
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
                float half_h = wf_h * 0.5f;
                float y0 = cy - mx * half_h;
                float y1 = cy - mn * half_h;
                if (y1 - y0 < 1.0f) y1 = y0 + 1.0f;
                dl->AddLine(ImVec2(wf_pos.x + px, y0),
                            ImVec2(wf_pos.x + px, y1),
                            IM_COL32(80, 180, 220, 200));
            }
        }

        /* Draw slice markers if the selected track has sample_start set */
        /* Simple onset detection preview: draw vertical lines at energy peaks */
        if (nframes > 512 && pw > 0) {
            uint32_t win = 512;
            float prev_rms = 0.0f;
            float slice_thresh = 0.01f;
            for (uint32_t f = 0; f + win < nframes; f += win) {
                float sum = 0.0f;
                for (uint32_t i = f; i < f + win; i++) {
                    float v = sdata[i * nch];
                    sum += v * v;
                }
                float rms = sum / (float)win;
                if (rms > slice_thresh && prev_rms < slice_thresh * 0.3f && f > 0) {
                    float sx = wf_pos.x + ((float)f / (float)nframes) * wf_w;
                    dl->AddLine(ImVec2(sx, wf_pos.y),
                                ImVec2(sx, wf_pos.y + wf_h),
                                IM_COL32(255, 100, 50, 150), 1.0f);
                }
                prev_rms = rms;
            }
        }

        /* Advance cursor past the waveform rect */
        ImGui::Dummy(ImVec2(wf_w, wf_h));

        /* Audition button */
        if (ImGui::Button("Audition")) {
            int tmp_idx = SQ_MAX_SAMPLES - 1;

            /* Stop any voices still using the audition slot */
            if (s_audition_active) {
                for (int v = 0; v < SQ_MAX_VOICES; v++) {
                    if (engine->voices[v].active &&
                        engine->voices[v].sample_index == tmp_idx) {
                        engine->voices[v].active = false;
                    }
                }
                engine->samples[tmp_idx] = s_saved_slot;
                s_audition_active = false;
            }

            /* Copy preview into persistent audition slot */
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
    }

    ImGui::End();

    /* Auto-load preview when selection changes */
    if (s_selected_entry >= 0 && s_selected_entry < s_num_entries &&
        !s_entries[s_selected_entry].is_dir) {
        char filepath[MAX_PATH_LEN];
        snprintf(filepath, sizeof(filepath), "%s/%s",
                 s_current_path, s_entries[s_selected_entry].name);
        if (strcmp(filepath, s_preview_path) != 0) {
            /* Stop audition voices before freeing preview data */
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
}

extern "C" int sample_browser_close_requested(void)
{
    if (s_close_requested) {
        s_close_requested = false;
        return 1;
    }
    return 0;
}
