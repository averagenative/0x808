/*
 * sq_app.h — Shared application controller (C99).
 *
 * Non-rendering app logic shared by all frontends (ImGui standalone,
 * ImGui plugin, GTK). Owns keyboard shortcut dispatch, panel state,
 * visual playhead computation, status messages, and undo coordination.
 *
 * Frontends translate their native keycodes (SDL, GDK, etc.) into
 * SQ_KEY_* constants before calling sq_app functions.
 */

#ifndef SQ_APP_H
#define SQ_APP_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "engine/engine.h"

/* ─── Frontend-agnostic key constants ─────────────────────────────────────── */

enum {
    SQ_KEY_NONE = 0,
    SQ_KEY_SPACE,
    SQ_KEY_ESCAPE,
    SQ_KEY_1, SQ_KEY_2, SQ_KEY_3, SQ_KEY_4, SQ_KEY_5,
    SQ_KEY_6, SQ_KEY_7, SQ_KEY_8, SQ_KEY_9,
    SQ_KEY_C, SQ_KEY_O, SQ_KEY_S, SQ_KEY_T, SQ_KEY_V, SQ_KEY_Z,
    SQ_KEY_EQUALS,
    /* QWERTY piano keys */
    SQ_KEY_A, SQ_KEY_W, SQ_KEY_D, SQ_KEY_E, SQ_KEY_F,
    SQ_KEY_R, SQ_KEY_G, SQ_KEY_H, SQ_KEY_U, SQ_KEY_J,
    SQ_KEY_I, SQ_KEY_K, SQ_KEY_L, SQ_KEY_P,
    SQ_KEY_SEMICOLON, SQ_KEY_QUOTE,
    SQ_KEY_X,
    SQ_KEY_COUNT
};

/* ─── Modifier flags ──────────────────────────────────────────────────────── */

#define SQ_MOD_CTRL  0x01
#define SQ_MOD_SHIFT 0x02
#define SQ_MOD_ALT   0x04

/* ─── Actions returned by key handler ─────────────────────────────────────── */

typedef enum {
    SQ_ACTION_NONE = 0,
    SQ_ACTION_QUIT,
    SQ_ACTION_SAVE,
    SQ_ACTION_LOAD,
    SQ_ACTION_TOGGLE_THEME,
} sq_app_action_t;

/* ─── Panel identifiers ──────────────────────────────────────────────────── */

typedef enum {
    SQ_PANEL_BROWSER = 0,
    SQ_PANEL_MIXER,
    SQ_PANEL_PIANO_ROLL,
    SQ_PANEL_KEYBOARD,
    SQ_PANEL_SETTINGS,
    SQ_PANEL_COUNT
} sq_panel_t;

/* ─── Audio device configuration ─────────────────────────────────────────── */

#define SQ_DEVICE_NAME_LEN 128

typedef struct {
    char     device_name[SQ_DEVICE_NAME_LEN]; /* empty = "Default"          */
    uint32_t sample_rate;                      /* current sample rate        */
    int      device_index;                     /* SDL device index, -1=default */
} sq_audio_config_t;

/* ─── Recording configuration ────────────────────────────────────────────── */

#define SQ_REC_DIR_LEN  256
#define SQ_REC_PREFIX_LEN 64

typedef struct {
    char     output_dir[SQ_REC_DIR_LEN];   /* where recordings are saved     */
    char     prefix[SQ_REC_PREFIX_LEN];    /* filename prefix (default: "recording") */
    uint32_t bit_depth;                     /* 16, 24, or 32                  */
} sq_rec_config_t;

/* ─── App state struct ────────────────────────────────────────────────────── */

#define SQ_STATUS_LEN 128

typedef struct {
    /* Panel visibility */
    bool panels[SQ_PANEL_COUNT];

    /* Visual playhead */
    int  visual_step;
    int  selected_track;

    /* Status message */
    char     status_msg[SQ_STATUS_LEN];
    uint32_t status_timer;          /* frames remaining */

    /* Clipboard */
    sq_pattern_t clipboard;
    bool         clipboard_valid;

    /* Playhead tracking */
    uint64_t play_start_ticks;
    bool     was_playing;

    /* Recording configuration */
    sq_rec_config_t rec_config;

    /* Audio device configuration */
    sq_audio_config_t audio_config;

    /* Audio restart callback (frontend-specific) */
    void (*audio_restart_fn)(void *userdata);
    void  *audio_restart_userdata;

    /* MIDI config */
    char  midi_device_name[SQ_DEVICE_NAME_LEN];
    int   midi_port_index;  /* -1 = none */

} sq_app_t;

/* ─── Public API ──────────────────────────────────────────────────────────── */

/*
 * Initialize app state to defaults.
 * All panels hidden, pattern mode, no status message.
 */
void sq_app_init(sq_app_t *app);

/*
 * Handle a key press. Translates key + modifiers into engine actions
 * and app state changes. Returns an action the frontend must handle.
 *
 * key:  SQ_KEY_* constant
 * mod:  bitmask of SQ_MOD_* flags
 * down: true = key pressed, false = key released
 */
sq_app_action_t sq_app_handle_key(sq_app_t *app, sq_engine_t *engine,
                                   int key, int mod, bool down);

/*
 * Toggle a panel's visibility with mutual exclusion logic.
 */
void sq_app_toggle_panel(sq_app_t *app, sq_panel_t panel);

/*
 * Compute the visual playhead step from wall-clock ticks.
 * Call once per frame. freq = performance counter frequency.
 * now  = current performance counter value.
 */
void sq_app_update_playhead(sq_app_t *app, sq_engine_t *engine,
                            uint64_t now, uint64_t freq);

/*
 * Set a status message with auto-dismiss timer (in frames).
 */
void sq_app_set_status(sq_app_t *app, const char *msg, uint32_t frames);

/*
 * Tick the status timer (call once per frame). Clears message when expired.
 */
void sq_app_update_status(sq_app_t *app);

/*
 * Get the synth preset index to use for keyboard/piano playback.
 * Checks selected track first, then falls back to first synth track.
 * Returns -1 if no synth track found.
 */
int sq_app_get_keyboard_preset(const sq_app_t *app, const sq_engine_t *engine);

/*
 * Initialize a new pattern by copying track layout from pattern 0.
 */
void sq_app_init_new_pattern(sq_engine_t *engine, int idx);

/*
 * Initialize recording config with platform-appropriate defaults.
 */
void sq_app_init_rec_config(sq_rec_config_t *cfg);

#ifdef __cplusplus
}
#endif

#endif /* SQ_APP_H */
