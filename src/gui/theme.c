/*
 * theme.c — Dark/light theme implementation using Nuklear style tables.
 */

#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#include "nuklear.h"

#include "gui/theme.h"

#define LOG_TAG "theme"
#include "core/log.h"

static sq_theme_t s_current_theme = THEME_DARK;

sq_theme_t theme_current(void)
{
    return s_current_theme;
}

void theme_toggle(void)
{
    s_current_theme = (s_current_theme == THEME_DARK) ? THEME_LIGHT : THEME_DARK;
    LOG_INFO("Theme toggled to %s", s_current_theme == THEME_DARK ? "dark" : "light");
}

void theme_get_clear_color(float out[4])
{
    if (s_current_theme == THEME_LIGHT) {
        out[0] = 0.88f; out[1] = 0.88f; out[2] = 0.90f; out[3] = 1.0f;
    } else {
        out[0] = 0.12f; out[1] = 0.12f; out[2] = 0.13f; out[3] = 1.0f;
    }
}

void theme_apply(struct nk_context *ctx, sq_theme_t theme)
{
    struct nk_color table[NK_COLOR_COUNT];
    s_current_theme = theme;

    if (theme == THEME_DARK) {
        table[NK_COLOR_TEXT]                   = nk_rgba(210, 210, 210, 255);
        table[NK_COLOR_WINDOW]                 = nk_rgba(35,  35,  38,  255);
        table[NK_COLOR_HEADER]                 = nk_rgba(45,  45,  48,  255);
        table[NK_COLOR_BORDER]                 = nk_rgba(60,  60,  65,  255);
        table[NK_COLOR_BUTTON]                 = nk_rgba(55,  55,  60,  255);
        table[NK_COLOR_BUTTON_HOVER]           = nk_rgba(70,  70,  75,  255);
        table[NK_COLOR_BUTTON_ACTIVE]          = nk_rgba(80,  80,  85,  255);
        table[NK_COLOR_TOGGLE]                 = nk_rgba(50,  50,  55,  255);
        table[NK_COLOR_TOGGLE_HOVER]           = nk_rgba(60,  60,  65,  255);
        table[NK_COLOR_TOGGLE_CURSOR]          = nk_rgba(100, 180, 255, 255);
        table[NK_COLOR_SELECT]                 = nk_rgba(45,  45,  48,  255);
        table[NK_COLOR_SELECT_ACTIVE]          = nk_rgba(100, 180, 255, 255);
        table[NK_COLOR_SLIDER]                 = nk_rgba(45,  45,  48,  255);
        table[NK_COLOR_SLIDER_CURSOR]          = nk_rgba(100, 180, 255, 255);
        table[NK_COLOR_SLIDER_CURSOR_HOVER]    = nk_rgba(120, 200, 255, 255);
        table[NK_COLOR_SLIDER_CURSOR_ACTIVE]   = nk_rgba(140, 220, 255, 255);
        table[NK_COLOR_PROPERTY]               = nk_rgba(45,  45,  48,  255);
        table[NK_COLOR_EDIT]                   = nk_rgba(40,  40,  43,  255);
        table[NK_COLOR_EDIT_CURSOR]            = nk_rgba(210, 210, 210, 255);
        table[NK_COLOR_COMBO]                  = nk_rgba(45,  45,  48,  255);
        table[NK_COLOR_CHART]                  = nk_rgba(45,  45,  48,  255);
        table[NK_COLOR_CHART_COLOR]            = nk_rgba(100, 180, 255, 255);
        table[NK_COLOR_CHART_COLOR_HIGHLIGHT]  = nk_rgba(255, 100, 100, 255);
        table[NK_COLOR_SCROLLBAR]              = nk_rgba(40,  40,  43,  255);
        table[NK_COLOR_SCROLLBAR_CURSOR]       = nk_rgba(70,  70,  75,  255);
        table[NK_COLOR_SCROLLBAR_CURSOR_HOVER] = nk_rgba(90,  90,  95,  255);
        table[NK_COLOR_SCROLLBAR_CURSOR_ACTIVE]= nk_rgba(100, 100, 105, 255);
        table[NK_COLOR_TAB_HEADER]             = nk_rgba(45,  45,  48,  255);
    } else {
        /* Light theme */
        table[NK_COLOR_TEXT]                   = nk_rgba(30,  30,  35,  255);
        table[NK_COLOR_WINDOW]                 = nk_rgba(230, 230, 234, 255);
        table[NK_COLOR_HEADER]                 = nk_rgba(210, 210, 215, 255);
        table[NK_COLOR_BORDER]                 = nk_rgba(170, 170, 180, 255);
        table[NK_COLOR_BUTTON]                 = nk_rgba(200, 200, 206, 255);
        table[NK_COLOR_BUTTON_HOVER]           = nk_rgba(185, 185, 192, 255);
        table[NK_COLOR_BUTTON_ACTIVE]          = nk_rgba(170, 170, 178, 255);
        table[NK_COLOR_TOGGLE]                 = nk_rgba(200, 200, 206, 255);
        table[NK_COLOR_TOGGLE_HOVER]           = nk_rgba(185, 185, 192, 255);
        table[NK_COLOR_TOGGLE_CURSOR]          = nk_rgba(40,  120, 200, 255);
        table[NK_COLOR_SELECT]                 = nk_rgba(220, 220, 225, 255);
        table[NK_COLOR_SELECT_ACTIVE]          = nk_rgba(40,  120, 200, 255);
        table[NK_COLOR_SLIDER]                 = nk_rgba(200, 200, 206, 255);
        table[NK_COLOR_SLIDER_CURSOR]          = nk_rgba(40,  120, 200, 255);
        table[NK_COLOR_SLIDER_CURSOR_HOVER]    = nk_rgba(50,  140, 220, 255);
        table[NK_COLOR_SLIDER_CURSOR_ACTIVE]   = nk_rgba(30,  100, 180, 255);
        table[NK_COLOR_PROPERTY]               = nk_rgba(210, 210, 215, 255);
        table[NK_COLOR_EDIT]                   = nk_rgba(240, 240, 244, 255);
        table[NK_COLOR_EDIT_CURSOR]            = nk_rgba(30,  30,  35,  255);
        table[NK_COLOR_COMBO]                  = nk_rgba(210, 210, 215, 255);
        table[NK_COLOR_CHART]                  = nk_rgba(220, 220, 225, 255);
        table[NK_COLOR_CHART_COLOR]            = nk_rgba(40,  120, 200, 255);
        table[NK_COLOR_CHART_COLOR_HIGHLIGHT]  = nk_rgba(200, 60,  60,  255);
        table[NK_COLOR_SCROLLBAR]              = nk_rgba(215, 215, 220, 255);
        table[NK_COLOR_SCROLLBAR_CURSOR]       = nk_rgba(170, 170, 180, 255);
        table[NK_COLOR_SCROLLBAR_CURSOR_HOVER] = nk_rgba(150, 150, 160, 255);
        table[NK_COLOR_SCROLLBAR_CURSOR_ACTIVE]= nk_rgba(130, 130, 140, 255);
        table[NK_COLOR_TAB_HEADER]             = nk_rgba(210, 210, 215, 255);
    }

    nk_style_from_table(ctx, table);
    LOG_INFO("Applied %s theme", theme == THEME_DARK ? "dark" : "light");
}
