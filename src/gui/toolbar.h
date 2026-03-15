/*
 * toolbar.h — Shared toolbar drawing for standalone and plugin GUIs.
 *
 * Extracts the common toolbar code (logo, transport, knobs, panel toggles,
 * theme popup, pattern selector, status) into a single shared function.
 * Standalone-only features (window controls, drag) are conditionally enabled.
 */

#ifndef SQ_TOOLBAR_H
#define SQ_TOOLBAR_H

#include "engine/engine.h"
#include "app/sq_app.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct SDL_Window;

/*
 * Parameters for the shared toolbar drawing function.
 * Callers fill this struct to control standalone vs plugin differences.
 */
typedef struct {
    sq_engine_t *engine;
    float        toolbar_h;       /* 80 for standalone, 70 for plugin */
    bool         is_plugin;       /* true = hide window controls & drag */
    int         *quit;            /* standalone quit flag, NULL for plugin */
    struct SDL_Window *window;    /* standalone SDL_Window, NULL for plugin */

    /* Panel toggle state — caller owns these */
    bool        *show_browser;
    bool        *show_mixer;
    bool        *show_piano_roll;
    bool        *show_keyboard;

    /* Status message — caller owns these */
    char        *save_status;
    int          save_status_size;
    uint32_t    *status_timer;

    /* Play start ticks for wall-clock playhead (caller-owned) */
    uint64_t    *play_start_ticks;

    /* Recording configuration (caller-owned) */
    sq_rec_config_t *rec_config;

    /* Settings panel toggle (caller-owned) */
    bool        *show_settings;
} sq_toolbar_params_t;

/*
 * Draw the toolbar. Call once per frame between ImGui::NewFrame() and
 * ImGui::Render(). Returns the export_x cursor position for pattern row alignment.
 */
void toolbar_draw(const sq_toolbar_params_t *params);

#ifdef __cplusplus
}
#endif

#endif /* SQ_TOOLBAR_H */
