/*
 * theme.h — Dark/light theme system for the 0x808 GUI.
 */

#ifndef SQ_THEME_H
#define SQ_THEME_H

/* Forward declaration — avoids re-including nuklear.h with NK_IMPLEMENTATION */
struct nk_context;

typedef enum {
    THEME_DARK,
    THEME_LIGHT
} sq_theme_t;

/* Apply the given theme to the Nuklear context */
void theme_apply(struct nk_context *ctx, sq_theme_t theme);

/* Get current theme */
sq_theme_t theme_current(void);

/* Toggle between dark and light */
void theme_toggle(void);

/* Get the GL clear color for the current theme */
void theme_get_clear_color(float out[4]);

#endif /* SQ_THEME_H */
