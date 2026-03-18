/*
 * session.h — Application session persistence.
 *
 * Auto-saves UI and app state on quit, auto-loads on startup.
 * State is stored in JSON at a platform-specific location:
 *   Linux:   ~/.local/share/0x808/session.json
 *   Windows: %APPDATA%\0x808\session.json
 *   macOS:   ~/Library/Application Support/0x808/session.json
 */

#ifndef SQ_SESSION_H
#define SQ_SESSION_H

#ifdef __cplusplus
extern "C" {
#endif

#include "app/sq_app.h"
#include "engine/engine.h"

/*
 * Get the platform-specific session directory path.
 * Creates the directory if it doesn't exist.
 * Returns pointer to static buffer.
 */
const char *sq_session_dir(void);

/*
 * Get the full path to session.json.
 * Returns pointer to static buffer.
 */
const char *sq_session_path(void);

/*
 * Save session state to disk.
 * Saves: audio device, MIDI config, window size, theme, panels,
 * last project path, CC map overrides.
 */
int sq_session_save(const sq_app_t *app, const sq_engine_t *engine,
                    int win_w, int win_h, int theme_id,
                    const char *last_project_path);

/*
 * Load session state from disk.
 * Restores app state fields. Returns 0 on success, -1 on failure.
 * win_w/win_h/theme_id are output parameters (set to loaded values).
 * last_project_path buffer receives the last project path (if any).
 */
int sq_session_load(sq_app_t *app, sq_engine_t *engine,
                    int *win_w, int *win_h, int *theme_id,
                    char *last_project_path, int path_bufsize);

/*
 * Get the path for the autosave project file.
 * Located in the session directory (e.g., ~/.local/share/0x808/autosave.sqproj).
 */
const char *sq_session_autosave_path(void);

#ifdef __cplusplus
}
#endif

#endif /* SQ_SESSION_H */
