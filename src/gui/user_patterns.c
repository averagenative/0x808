/*
 * user_patterns.c — User-saved drum pattern library.
 */

#include "gui/user_patterns.h"

#include "cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#define LOG_TAG "userpat"
#include "core/log.h"

static sq_user_pattern_entry_t s_entries[SQ_USER_PATTERN_MAX];
static int s_count = 0;

/* Cached dir path (built once). */
static char s_dir[512] = {0};

static void mkdir_p(const char *path)
{
#ifdef _WIN32
    CreateDirectoryA(path, NULL);
#else
    mkdir(path, 0755);
#endif
}

static const char *patterns_dir(void)
{
    if (s_dir[0]) return s_dir;
#ifdef _WIN32
    char appdata[MAX_PATH];
    if (SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, appdata) == S_OK) {
        snprintf(s_dir, sizeof(s_dir), "%s\\0x808\\patterns", appdata);
    } else {
        snprintf(s_dir, sizeof(s_dir), "patterns");
    }
    char base[512];
    snprintf(base, sizeof(base), "%s\\0x808", appdata);
    mkdir_p(base);
#else
    const char *home = getenv("HOME");
    if (home)
        snprintf(s_dir, sizeof(s_dir), "%s/.local/share/0x808/patterns", home);
    else
        snprintf(s_dir, sizeof(s_dir), "patterns");
    char base[512];
    if (home) {
        snprintf(base, sizeof(base), "%s/.local/share/0x808", home);
        mkdir_p(base);
    }
#endif
    mkdir_p(s_dir);
    return s_dir;
}

/* Alphabetical string compare for qsort. */
static int entry_cmp(const void *a, const void *b)
{
    const sq_user_pattern_entry_t *ea = (const sq_user_pattern_entry_t *)a;
    const sq_user_pattern_entry_t *eb = (const sq_user_pattern_entry_t *)b;
    return strcmp(ea->name, eb->name);
}

/* Strip ".drumpat" and copy into `name` (size SQ_USER_PATTERN_NAME_LEN). */
static void filename_to_name(const char *filename, char *name)
{
    const char *dot = strstr(filename, ".drumpat");
    size_t n = dot ? (size_t)(dot - filename) : strlen(filename);
    if (n >= SQ_USER_PATTERN_NAME_LEN) n = SQ_USER_PATTERN_NAME_LEN - 1;
    memcpy(name, filename, n);
    name[n] = '\0';
}

int user_patterns_refresh(void)
{
    s_count = 0;
    const char *dir = patterns_dir();
#ifdef _WIN32
    char search[600];
    snprintf(search, sizeof(search), "%s\\*.drumpat", dir);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(search, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do {
        if (s_count >= SQ_USER_PATTERN_MAX) break;
        sq_user_pattern_entry_t *e = &s_entries[s_count++];
        snprintf(e->filename, sizeof(e->filename), "%s", fd.cFileName);
        filename_to_name(fd.cFileName, e->name);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR *d = opendir(dir);
    if (!d) return 0;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (s_count >= SQ_USER_PATTERN_MAX) break;
        size_t nlen = strlen(ent->d_name);
        if (nlen < 9) continue;
        if (strcmp(ent->d_name + nlen - 8, ".drumpat") != 0) continue;
        sq_user_pattern_entry_t *e = &s_entries[s_count++];
        snprintf(e->filename, sizeof(e->filename), "%s", ent->d_name);
        filename_to_name(ent->d_name, e->name);
    }
    closedir(d);
#endif
    qsort(s_entries, (size_t)s_count, sizeof(s_entries[0]), entry_cmp);
    return s_count;
}

int user_patterns_count(void) { return s_count; }

const sq_user_pattern_entry_t *user_patterns_get(int index)
{
    if (index < 0 || index >= s_count) return NULL;
    return &s_entries[index];
}

/* Replace whitespace and path separators with underscores so the saved
 * filename is safe on every OS and easy to eyeball in a file manager. */
static void sanitize_name(const char *in, char *out, size_t outsize)
{
    size_t j = 0;
    for (size_t i = 0; in[i] && j + 1 < outsize; i++) {
        char c = in[i];
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' ||
            c == '"' || c == '<' || c == '>' || c == '|') {
            c = '_';
        }
        out[j++] = c;
    }
    out[j] = '\0';
    /* Reject empty or dot-only names */
    if (j == 0 || (j == 1 && out[0] == '.')) {
        snprintf(out, outsize, "Untitled");
    }
}

int user_patterns_save(const sq_pattern_t *pat, const char *name, double bpm)
{
    if (!pat || !name || !name[0]) return -1;

    char safe[SQ_USER_PATTERN_NAME_LEN];
    sanitize_name(name, safe, sizeof(safe));

    char path[640];
    snprintf(path, sizeof(path), "%s/%s.drumpat", patterns_dir(), safe);

    cJSON *root = cJSON_CreateObject();
    if (!root) return -1;
    cJSON_AddNumberToObject(root, "version", 1);
    cJSON_AddStringToObject(root, "name", safe);
    cJSON_AddNumberToObject(root, "bpm", bpm);
    cJSON_AddNumberToObject(root, "num_tracks", pat->num_tracks);

    cJSON *tracks = cJSON_CreateArray();
    for (uint32_t t = 0; t < pat->num_tracks; t++) {
        const sq_track_t *trk = &pat->tracks[t];
        cJSON *tj = cJSON_CreateObject();
        cJSON_AddNumberToObject(tj, "type", trk->type);
        cJSON_AddNumberToObject(tj, "length", trk->length);
        cJSON_AddNumberToObject(tj, "sample_index", trk->sample_index);
        cJSON_AddNumberToObject(tj, "synth_preset", trk->synth_preset);
        cJSON_AddNumberToObject(tj, "volume", trk->volume);
        cJSON_AddNumberToObject(tj, "pan", trk->pan);

        cJSON *steps = cJSON_CreateArray();
        for (uint32_t s = 0; s < trk->length && s < SQ_MAX_STEPS; s++) {
            cJSON *sj = cJSON_CreateObject();
            cJSON_AddNumberToObject(sj, "v",  trk->steps[s].velocity);
            cJSON_AddNumberToObject(sj, "n",  trk->steps[s].note);
            cJSON_AddNumberToObject(sj, "po", trk->steps[s].pitch_offset);
            cJSON_AddNumberToObject(sj, "pr", trk->steps[s].probability);
            cJSON_AddNumberToObject(sj, "l",  trk->steps[s].length);
            cJSON_AddItemToArray(steps, sj);
        }
        cJSON_AddItemToObject(tj, "steps", steps);
        cJSON_AddItemToArray(tracks, tj);
    }
    cJSON_AddItemToObject(root, "tracks", tracks);

    char *text = cJSON_Print(root);
    cJSON_Delete(root);
    if (!text) return -1;

    FILE *f = fopen(path, "w");
    if (!f) { free(text); return -1; }
    fputs(text, f);
    fclose(f);
    free(text);
    LOG_INFO("Saved user pattern: %s", path);
    user_patterns_refresh();
    return 0;
}

int user_patterns_load(int index, sq_pattern_t *out, double *out_bpm)
{
    if (index < 0 || index >= s_count || !out) return -1;
    char path[640];
    snprintf(path, sizeof(path), "%s/%s", patterns_dir(),
             s_entries[index].filename);

    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > 10 * 1024 * 1024) { fclose(f); return -1; }
    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return -1; }
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        free(buf); fclose(f); return -1;
    }
    buf[sz] = '\0';
    fclose(f);

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) return -1;

    memset(out, 0, sizeof(*out));
    cJSON *jname = cJSON_GetObjectItem(root, "name");
    if (jname && jname->valuestring) {
        strncpy(out->name, jname->valuestring, SQ_PATTERN_NAME_LEN - 1);
        out->name[SQ_PATTERN_NAME_LEN - 1] = '\0';
    }
    if (out_bpm) {
        cJSON *jbpm = cJSON_GetObjectItem(root, "bpm");
        if (jbpm) *out_bpm = jbpm->valuedouble;
    }
    cJSON *jnt = cJSON_GetObjectItem(root, "num_tracks");
    if (jnt) {
        out->num_tracks = (uint32_t)jnt->valuedouble;
        if (out->num_tracks > SQ_MAX_TRACKS) out->num_tracks = SQ_MAX_TRACKS;
    }

    cJSON *tracks = cJSON_GetObjectItem(root, "tracks");
    if (tracks && cJSON_IsArray(tracks)) {
        int n = cJSON_GetArraySize(tracks);
        if ((uint32_t)n > SQ_MAX_TRACKS) n = SQ_MAX_TRACKS;
        for (int t = 0; t < n; t++) {
            cJSON *tj = cJSON_GetArrayItem(tracks, t);
            sq_track_t *trk = &out->tracks[t];
            cJSON *v;
            v = cJSON_GetObjectItem(tj, "type");
            if (v) trk->type = (sq_track_type_t)(int)v->valuedouble;
            v = cJSON_GetObjectItem(tj, "length");
            if (v) trk->length = (uint32_t)v->valuedouble;
            v = cJSON_GetObjectItem(tj, "sample_index");
            if (v) trk->sample_index = (int)v->valuedouble;
            v = cJSON_GetObjectItem(tj, "synth_preset");
            if (v) trk->synth_preset = (int)v->valuedouble;
            v = cJSON_GetObjectItem(tj, "volume");
            if (v) trk->volume = (float)v->valuedouble;
            v = cJSON_GetObjectItem(tj, "pan");
            if (v) trk->pan = (float)v->valuedouble;

            cJSON *steps = cJSON_GetObjectItem(tj, "steps");
            if (steps && cJSON_IsArray(steps)) {
                int ns = cJSON_GetArraySize(steps);
                if (ns > SQ_MAX_STEPS) ns = SQ_MAX_STEPS;
                for (int s = 0; s < ns; s++) {
                    cJSON *sj = cJSON_GetArrayItem(steps, s);
                    v = cJSON_GetObjectItem(sj, "v");
                    if (v) trk->steps[s].velocity = (uint8_t)v->valuedouble;
                    v = cJSON_GetObjectItem(sj, "n");
                    if (v) trk->steps[s].note = (uint8_t)v->valuedouble;
                    v = cJSON_GetObjectItem(sj, "po");
                    if (v) trk->steps[s].pitch_offset = (int8_t)v->valuedouble;
                    v = cJSON_GetObjectItem(sj, "pr");
                    if (v) trk->steps[s].probability = (uint8_t)v->valuedouble;
                    v = cJSON_GetObjectItem(sj, "l");
                    if (v) trk->steps[s].length = (float)v->valuedouble;
                }
            }
        }
    }
    cJSON_Delete(root);
    LOG_INFO("Loaded user pattern: %s", path);
    return 0;
}

int user_patterns_delete(int index)
{
    if (index < 0 || index >= s_count) return -1;
    char path[640];
    snprintf(path, sizeof(path), "%s/%s", patterns_dir(),
             s_entries[index].filename);
    if (remove(path) != 0) return -1;
    LOG_INFO("Deleted user pattern: %s", path);
    user_patterns_refresh();
    return 0;
}
