/*
 * theme.h — Dark/light theme system for the 0x808 GUI.
 *           Includes user-defined JSON themes.
 */

#ifndef SQ_THEME_H
#define SQ_THEME_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    THEME_DARK,
    THEME_LIGHT,
    THEME_HACKER,
    THEME_MIDNIGHT,
    THEME_AMBER,
    THEME_VAPORWAVE,
    THEME_NEON,
    THEME_COUNT
} sq_theme_t;

/* --- Built-in theme API --------------------------------------------------- */

/* Theme name for display */
const char *theme_name(sq_theme_t theme);

/* Apply the given theme to ImGui */
void theme_apply(sq_theme_t theme);

/* Get current theme */
sq_theme_t theme_current(void);

/* Toggle between dark and light */
void theme_toggle(void);

/* Get the current theme index */
int theme_get_current(void);

/* Get the GL clear color for the current theme */
void theme_get_clear_color(float out[4]);

/* --- User (JSON) theme API ------------------------------------------------ */

#define SQ_MAX_USER_THEMES 16

typedef struct {
    char name[64];
    char path[512];
    float clear_color[3];
    float text[3], text_disabled[3];
    float window_bg[3], child_bg[3], popup_bg[3];
    float border[3];
    float frame_bg[3], frame_bg_hover[3], frame_bg_active[3];
    float button[3], button_hover[3], button_active[3];
    float slider[3], slider_active[3];
    float accent[3];        /* checkmark, separator hover */
    float header[3], header_hover[3], header_active[3];
    float tab[3], tab_hover[3], tab_selected[3];
    float plot_lines[3], plot_histogram[3];
    float pad[3];           /* sequencer pad base color */
    float pad_glow[3];      /* sequencer pad glow color */
    float frame_border;     /* 0 or 1 */
    bool loaded;
} sq_user_theme_t;

/* Scan a directory for .json theme files and load them */
void theme_scan_user_themes(const char *dir);

/* Number of successfully loaded user themes */
int theme_num_user_themes(void);

/* Display name for user theme at index (0-based) */
const char *theme_user_name(int index);

/* Apply user theme at index to ImGui */
void theme_apply_user(int index);

/* Get pad/glow colors for the currently active theme (built-in or user).
   If the active theme has no pad colors, sensible defaults are returned. */
void theme_get_pad_colors(float pad_rgb[3], float glow_rgb[3]);

#ifdef __cplusplus
}
#endif

#endif /* SQ_THEME_H */
