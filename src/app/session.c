/*
 * session.c — Application session persistence.
 *
 * Saves/loads UI and app state as JSON. Auto-called on startup/shutdown
 * by standalone frontends. Plugin builds don't use this.
 */

#define LOG_TAG "session"
#include "core/log.h"
#include "app/session.h"
#include "engine/kits.h"
#include "cJSON.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#define mkdir_p(path) _mkdir(path)
#else
#include <unistd.h>
#define mkdir_p(path) mkdir(path, 0755)
#endif

/* ─── Platform-specific paths ────────────────────────────────────────────── */

const char *sq_session_dir(void)
{
    static char dir[512] = {0};
    if (dir[0]) return dir;

#ifdef _WIN32
    const char *appdata = getenv("APPDATA");
    if (appdata)
        snprintf(dir, sizeof(dir), "%s\\0x808", appdata);
    else
        snprintf(dir, sizeof(dir), "0x808");
#elif defined(__APPLE__)
    const char *home = getenv("HOME");
    if (home)
        snprintf(dir, sizeof(dir), "%s/Library/Application Support/0x808", home);
    else
        snprintf(dir, sizeof(dir), "0x808");
#else
    const char *xdg = getenv("XDG_DATA_HOME");
    if (xdg)
        snprintf(dir, sizeof(dir), "%s/0x808", xdg);
    else {
        const char *home = getenv("HOME");
        if (home)
            snprintf(dir, sizeof(dir), "%s/.local/share/0x808", home);
        else
            snprintf(dir, sizeof(dir), "0x808");
    }
#endif

    /* Create directory if it doesn't exist */
    mkdir_p(dir);
    return dir;
}

const char *sq_session_path(void)
{
    static char path[600] = {0};
    if (path[0]) return path;
    snprintf(path, sizeof(path), "%s%csession.json",
             sq_session_dir(),
#ifdef _WIN32
             '\\'
#else
             '/'
#endif
    );
    return path;
}

const char *sq_session_autosave_path(void)
{
    static char path[600] = {0};
    if (path[0]) return path;
    snprintf(path, sizeof(path), "%s%cautosave.sqproj",
             sq_session_dir(),
#ifdef _WIN32
             '\\'
#else
             '/'
#endif
    );
    return path;
}

/* ─── Save ───────────────────────────────────────────────────────────────── */

int sq_session_save(const sq_app_t *app, const sq_engine_t *engine,
                    int win_w, int win_h, int theme_id,
                    const char *last_project_path)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) return -1;

    cJSON_AddNumberToObject(root, "version", 1);

    /* Window */
    cJSON_AddNumberToObject(root, "win_w", win_w);
    cJSON_AddNumberToObject(root, "win_h", win_h);
    cJSON_AddNumberToObject(root, "theme", theme_id);

    /* Audio device */
    if (app->audio_config.device_name[0])
        cJSON_AddStringToObject(root, "audio_device", app->audio_config.device_name);
    cJSON_AddNumberToObject(root, "audio_device_index", app->audio_config.device_index);
    cJSON_AddNumberToObject(root, "sample_rate", app->audio_config.sample_rate);

    /* MIDI */
    if (app->midi_device_name[0])
        cJSON_AddStringToObject(root, "midi_device", app->midi_device_name);
    cJSON_AddNumberToObject(root, "midi_port", app->midi_port_index);

    /* Panels */
    cJSON *panels = cJSON_CreateArray();
    for (int i = 0; i < SQ_PANEL_COUNT; i++)
        cJSON_AddItemToArray(panels, cJSON_CreateBool(app->panels[i]));
    cJSON_AddItemToObject(root, "panels", panels);

    /* BPM + transport */
    cJSON_AddNumberToObject(root, "bpm", engine->transport.bpm);
    cJSON_AddNumberToObject(root, "swing", engine->transport.swing);
    cJSON_AddNumberToObject(root, "master_volume", engine->master_volume);

    /* Last project path */
    if (last_project_path && last_project_path[0])
        cJSON_AddStringToObject(root, "last_project", last_project_path);

    /* Recording config */
    cJSON_AddStringToObject(root, "rec_dir", app->rec_config.output_dir);
    cJSON_AddStringToObject(root, "rec_prefix", app->rec_config.prefix);
    cJSON_AddNumberToObject(root, "rec_bit_depth", app->rec_config.bit_depth);

    /* UI preferences */
    cJSON_AddBoolToObject(root, "show_tooltips", app->show_tooltips);

    /* Random options (TASK-227) — last-applied randomize settings. */
    {
        cJSON *r = cJSON_CreateObject();
        cJSON_AddBoolToObject(r, "steps",    app->random_options.steps);
        cJSON_AddBoolToObject(r, "velocity", app->random_options.velocity);
        cJSON_AddBoolToObject(r, "micro",    app->random_options.micro);
        cJSON_AddBoolToObject(r, "pitch",    app->random_options.pitch);
        cJSON_AddBoolToObject(r, "notes",    app->random_options.notes);
        cJSON_AddNumberToObject(r, "style",  app->random_options.style);
        cJSON_AddItemToObject(root, "random_options", r);
    }

    /* Current pattern + kit */
    cJSON_AddNumberToObject(root, "current_pattern", engine->transport.current_pattern);
    cJSON_AddNumberToObject(root, "current_kit", sq_current_kit);

    /* MIDI CC map overrides (sparse — only non-default) */
    cJSON *cc_map = cJSON_CreateObject();
    for (int i = 0; i < 128; i++) {
        if (engine->cc_map.map[i] != -1) {
            char key[8];
            snprintf(key, sizeof(key), "%d", i);
            cJSON_AddNumberToObject(cc_map, key, engine->cc_map.map[i]);
        }
    }
    cJSON_AddItemToObject(root, "cc_map", cc_map);

    /* Write to file */
    const char *path = sq_session_path();
    char *json = cJSON_Print(root);
    cJSON_Delete(root);
    if (!json) return -1;

    FILE *f = fopen(path, "w");
    if (!f) {
        LOG_ERROR("Failed to save session to %s", path);
        cJSON_free(json);
        return -1;
    }
    fputs(json, f);
    fclose(f);
    cJSON_free(json);

    LOG_INFO("Session saved to %s", path);
    return 0;
}

/* ─── Load ───────────────────────────────────────────────────────────────── */

int sq_session_load(sq_app_t *app, sq_engine_t *engine,
                    int *win_w, int *win_h, int *theme_id,
                    char *last_project_path, int path_bufsize)
{
    const char *path = sq_session_path();
    FILE *f = fopen(path, "rb");
    if (!f) {
        LOG_INFO("No session file at %s (first run)", path);
        return -1;
    }

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len <= 0 || len > 1024 * 1024) {
        fclose(f);
        return -1;
    }

    char *buf = (char *)malloc((size_t)len + 1);
    if (!buf) { fclose(f); return -1; }
    fread(buf, 1, (size_t)len, f);
    buf[len] = '\0';
    fclose(f);

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) {
        LOG_WARN("Failed to parse session.json");
        return -1;
    }

    cJSON *v;

    /* Window */
    v = cJSON_GetObjectItem(root, "win_w");
    if (v && win_w) *win_w = (int)v->valuedouble;
    v = cJSON_GetObjectItem(root, "win_h");
    if (v && win_h) *win_h = (int)v->valuedouble;
    v = cJSON_GetObjectItem(root, "theme");
    if (v && theme_id) *theme_id = (int)v->valuedouble;

    /* Clamp window size */
    if (win_w && *win_w < 640)  *win_w = 640;
    if (win_w && *win_w > 3840) *win_w = 3840;
    if (win_h && *win_h < 360)  *win_h = 360;
    if (win_h && *win_h > 2160) *win_h = 2160;

    /* Audio device */
    v = cJSON_GetObjectItem(root, "audio_device");
    if (v && v->valuestring)
        snprintf(app->audio_config.device_name, SQ_DEVICE_NAME_LEN, "%s", v->valuestring);
    v = cJSON_GetObjectItem(root, "audio_device_index");
    if (v) app->audio_config.device_index = (int)v->valuedouble;
    v = cJSON_GetObjectItem(root, "sample_rate");
    if (v) app->audio_config.sample_rate = (uint32_t)v->valuedouble;

    /* MIDI */
    v = cJSON_GetObjectItem(root, "midi_device");
    if (v && v->valuestring)
        snprintf(app->midi_device_name, SQ_DEVICE_NAME_LEN, "%s", v->valuestring);
    v = cJSON_GetObjectItem(root, "midi_port");
    if (v) app->midi_port_index = (int)v->valuedouble;

    /* Panels */
    cJSON *panels = cJSON_GetObjectItem(root, "panels");
    if (panels && cJSON_IsArray(panels)) {
        int n = cJSON_GetArraySize(panels);
        for (int i = 0; i < n && i < SQ_PANEL_COUNT; i++)
            app->panels[i] = cJSON_IsTrue(cJSON_GetArrayItem(panels, i));
    }

    /* BPM + transport */
    v = cJSON_GetObjectItem(root, "bpm");
    if (v) engine->transport.bpm = v->valuedouble;
    v = cJSON_GetObjectItem(root, "swing");
    if (v) engine->transport.swing = (float)v->valuedouble;
    v = cJSON_GetObjectItem(root, "master_volume");
    if (v) engine->master_volume = (float)v->valuedouble;

    /* Last project */
    v = cJSON_GetObjectItem(root, "last_project");
    if (v && v->valuestring && last_project_path)
        snprintf(last_project_path, path_bufsize, "%s", v->valuestring);

    /* Recording config */
    v = cJSON_GetObjectItem(root, "rec_dir");
    if (v && v->valuestring)
        snprintf(app->rec_config.output_dir, SQ_REC_DIR_LEN, "%s", v->valuestring);
    v = cJSON_GetObjectItem(root, "rec_prefix");
    if (v && v->valuestring)
        snprintf(app->rec_config.prefix, SQ_REC_PREFIX_LEN, "%s", v->valuestring);
    v = cJSON_GetObjectItem(root, "rec_bit_depth");
    if (v) app->rec_config.bit_depth = (uint32_t)v->valuedouble;

    /* UI preferences */
    v = cJSON_GetObjectItem(root, "show_tooltips");
    if (v) app->show_tooltips = cJSON_IsTrue(v);

    /* Random options */
    cJSON *ropts = cJSON_GetObjectItem(root, "random_options");
    if (ropts) {
        v = cJSON_GetObjectItem(ropts, "steps");
        if (v) app->random_options.steps = cJSON_IsTrue(v);
        v = cJSON_GetObjectItem(ropts, "velocity");
        if (v) app->random_options.velocity = cJSON_IsTrue(v);
        v = cJSON_GetObjectItem(ropts, "micro");
        if (v) app->random_options.micro = cJSON_IsTrue(v);
        v = cJSON_GetObjectItem(ropts, "pitch");
        if (v) app->random_options.pitch = cJSON_IsTrue(v);
        v = cJSON_GetObjectItem(ropts, "notes");
        if (v) app->random_options.notes = cJSON_IsTrue(v);
        v = cJSON_GetObjectItem(ropts, "style");
        if (v) app->random_options.style = (int)v->valuedouble;
    }

    /* Current pattern + kit */
    v = cJSON_GetObjectItem(root, "current_pattern");
    if (v) {
        int cp = (int)v->valuedouble;
        if (cp >= 0 && (uint32_t)cp < engine->num_patterns)
            engine->transport.current_pattern = cp;
    }
    v = cJSON_GetObjectItem(root, "current_kit");
    if (v) {
        int ck = (int)v->valuedouble;
        if (ck >= 0 && ck < SQ_NUM_KITS)
            sq_current_kit = ck;
    }

    /* MIDI CC map */
    cJSON *cc_map = cJSON_GetObjectItem(root, "cc_map");
    if (cc_map && cJSON_IsObject(cc_map)) {
        cJSON *entry = NULL;
        cJSON_ArrayForEach(entry, cc_map) {
            int cc = atoi(entry->string);
            if (cc >= 0 && cc < 128) {
                int param = (int)entry->valuedouble;
                if (param >= -1 && param < SQ_PARAM_COUNT)
                    engine->cc_map.map[cc] = (int8_t)param;
            }
        }
    }

    cJSON_Delete(root);
    LOG_INFO("Session loaded from %s", path);
    return 0;
}
