/*
 * theme.cpp — Theme system for the 0x808 GUI.
 *
 * Built-in themes: Dark, Light, Hacker (green CRT), Midnight (blue/purple),
 *                  Amber (warm CRT).
 * User themes: loaded from JSON files in {base_dir}/themes/
 */

#include "imgui.h"
#include "gui/theme.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#endif

extern "C" {
#define LOG_TAG "theme"
#include "core/log.h"
}

static sq_theme_t s_current_theme = THEME_DARK;
static int        s_active_user_theme = -1;  /* -1 = built-in theme active */

static const char *s_theme_names[] = {
    "Dark", "Light", "Hacker", "Midnight", "Amber", "Vaporwave", "Neon"
};

/* --- User theme storage --------------------------------------------------- */

static sq_user_theme_t s_user_themes[SQ_MAX_USER_THEMES];
static int             s_num_user_themes = 0;

/* --- Built-in theme API --------------------------------------------------- */

const char *theme_name(sq_theme_t theme)
{
    if (theme >= 0 && theme < THEME_COUNT)
        return s_theme_names[theme];
    return "Unknown";
}

sq_theme_t theme_current(void)
{
    return s_current_theme;
}

void theme_toggle(void)
{
    s_active_user_theme = -1;
    s_current_theme = (sq_theme_t)((s_current_theme + 1) % THEME_COUNT);
    theme_apply(s_current_theme);
}

void theme_get_clear_color(float out[4])
{
    /* If a user theme is active, use its clear color */
    if (s_active_user_theme >= 0 && s_active_user_theme < s_num_user_themes) {
        const sq_user_theme_t &ut = s_user_themes[s_active_user_theme];
        out[0] = ut.clear_color[0];
        out[1] = ut.clear_color[1];
        out[2] = ut.clear_color[2];
        out[3] = 1.0f;
        return;
    }

    switch (s_current_theme) {
    case THEME_LIGHT:
        out[0] = 0.72f; out[1] = 0.72f; out[2] = 0.74f; out[3] = 1.0f;
        break;
    case THEME_HACKER:
        out[0] = 0.02f; out[1] = 0.04f; out[2] = 0.02f; out[3] = 1.0f;
        break;
    case THEME_MIDNIGHT:
        out[0] = 0.04f; out[1] = 0.03f; out[2] = 0.08f; out[3] = 1.0f;
        break;
    case THEME_AMBER:
        out[0] = 0.06f; out[1] = 0.04f; out[2] = 0.02f; out[3] = 1.0f;
        break;
    case THEME_VAPORWAVE:
        out[0] = 0.04f; out[1] = 0.02f; out[2] = 0.06f; out[3] = 1.0f;
        break;
    case THEME_NEON:
        out[0] = 0.02f; out[1] = 0.02f; out[2] = 0.05f; out[3] = 1.0f;
        break;
    default: /* THEME_DARK */
        out[0] = 0.12f; out[1] = 0.12f; out[2] = 0.13f; out[3] = 1.0f;
        break;
    }
}

/* --- Helper: set common style properties ---------------------------------- */

static void style_common(ImGuiStyle &style)
{
    style.WindowRounding    = 0.0f;
    style.FrameRounding     = 2.0f;
    style.GrabRounding      = 2.0f;
    style.ScrollbarRounding = 2.0f;
    style.WindowBorderSize  = 1.0f;
    style.FrameBorderSize   = 0.0f;
    style.WindowPadding     = ImVec2(8, 8);
    style.FramePadding      = ImVec2(4, 3);
    style.ItemSpacing       = ImVec2(6, 4);
}

/* --- Individual theme implementations ------------------------------------- */

static void apply_dark(ImVec4 *c)
{
    c[ImGuiCol_Text]                  = ImVec4(0.82f, 0.82f, 0.82f, 1.00f);
    c[ImGuiCol_TextDisabled]          = ImVec4(0.42f, 0.42f, 0.42f, 1.00f);
    c[ImGuiCol_WindowBg]              = ImVec4(0.14f, 0.14f, 0.15f, 1.00f);
    c[ImGuiCol_ChildBg]               = ImVec4(0.14f, 0.14f, 0.15f, 1.00f);
    c[ImGuiCol_PopupBg]               = ImVec4(0.16f, 0.16f, 0.17f, 0.95f);
    c[ImGuiCol_Border]                = ImVec4(0.24f, 0.24f, 0.25f, 1.00f);
    c[ImGuiCol_BorderShadow]          = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    c[ImGuiCol_FrameBg]               = ImVec4(0.18f, 0.18f, 0.19f, 1.00f);
    c[ImGuiCol_FrameBgHovered]        = ImVec4(0.24f, 0.24f, 0.25f, 1.00f);
    c[ImGuiCol_FrameBgActive]         = ImVec4(0.28f, 0.28f, 0.30f, 1.00f);
    c[ImGuiCol_TitleBg]               = ImVec4(0.14f, 0.14f, 0.15f, 1.00f);
    c[ImGuiCol_TitleBgActive]         = ImVec4(0.14f, 0.14f, 0.15f, 1.00f);
    c[ImGuiCol_TitleBgCollapsed]      = ImVec4(0.14f, 0.14f, 0.15f, 1.00f);
    c[ImGuiCol_MenuBarBg]             = ImVec4(0.18f, 0.18f, 0.19f, 1.00f);
    c[ImGuiCol_ScrollbarBg]           = ImVec4(0.16f, 0.16f, 0.17f, 1.00f);
    c[ImGuiCol_ScrollbarGrab]         = ImVec4(0.27f, 0.27f, 0.29f, 1.00f);
    c[ImGuiCol_ScrollbarGrabHovered]  = ImVec4(0.35f, 0.35f, 0.37f, 1.00f);
    c[ImGuiCol_ScrollbarGrabActive]   = ImVec4(0.39f, 0.39f, 0.41f, 1.00f);
    c[ImGuiCol_CheckMark]             = ImVec4(0.39f, 0.71f, 1.00f, 1.00f);
    c[ImGuiCol_SliderGrab]            = ImVec4(0.39f, 0.71f, 1.00f, 1.00f);
    c[ImGuiCol_SliderGrabActive]      = ImVec4(0.55f, 0.86f, 1.00f, 1.00f);
    c[ImGuiCol_Button]                = ImVec4(0.22f, 0.22f, 0.24f, 1.00f);
    c[ImGuiCol_ButtonHovered]         = ImVec4(0.27f, 0.27f, 0.29f, 1.00f);
    c[ImGuiCol_ButtonActive]          = ImVec4(0.31f, 0.31f, 0.33f, 1.00f);
    c[ImGuiCol_Header]                = ImVec4(0.22f, 0.22f, 0.24f, 1.00f);
    c[ImGuiCol_HeaderHovered]         = ImVec4(0.27f, 0.27f, 0.29f, 1.00f);
    c[ImGuiCol_HeaderActive]          = ImVec4(0.31f, 0.31f, 0.33f, 1.00f);
    c[ImGuiCol_Separator]             = ImVec4(0.24f, 0.24f, 0.25f, 1.00f);
    c[ImGuiCol_SeparatorHovered]      = ImVec4(0.39f, 0.71f, 1.00f, 1.00f);
    c[ImGuiCol_SeparatorActive]       = ImVec4(0.39f, 0.71f, 1.00f, 1.00f);
    c[ImGuiCol_Tab]                   = ImVec4(0.18f, 0.18f, 0.19f, 1.00f);
    c[ImGuiCol_TabHovered]            = ImVec4(0.27f, 0.27f, 0.29f, 1.00f);
    c[ImGuiCol_TabSelected]           = ImVec4(0.24f, 0.24f, 0.25f, 1.00f);
    c[ImGuiCol_PlotLines]             = ImVec4(0.39f, 0.71f, 1.00f, 1.00f);
    c[ImGuiCol_PlotHistogram]         = ImVec4(0.39f, 0.71f, 1.00f, 1.00f);
}

static void apply_light(ImVec4 *c)
{
    /* Softer light theme — warm gray, not stark white */
    c[ImGuiCol_Text]                  = ImVec4(0.15f, 0.15f, 0.18f, 1.00f);
    c[ImGuiCol_TextDisabled]          = ImVec4(0.50f, 0.50f, 0.52f, 1.00f);
    c[ImGuiCol_WindowBg]              = ImVec4(0.76f, 0.76f, 0.78f, 1.00f);
    c[ImGuiCol_ChildBg]               = ImVec4(0.76f, 0.76f, 0.78f, 1.00f);
    c[ImGuiCol_PopupBg]               = ImVec4(0.78f, 0.78f, 0.80f, 0.98f);
    c[ImGuiCol_Border]                = ImVec4(0.58f, 0.58f, 0.62f, 1.00f);
    c[ImGuiCol_FrameBg]               = ImVec4(0.68f, 0.68f, 0.71f, 1.00f);
    c[ImGuiCol_FrameBgHovered]        = ImVec4(0.63f, 0.63f, 0.66f, 1.00f);
    c[ImGuiCol_FrameBgActive]         = ImVec4(0.58f, 0.58f, 0.62f, 1.00f);
    c[ImGuiCol_TitleBg]               = ImVec4(0.70f, 0.70f, 0.72f, 1.00f);
    c[ImGuiCol_TitleBgActive]         = ImVec4(0.70f, 0.70f, 0.72f, 1.00f);
    c[ImGuiCol_Button]                = ImVec4(0.65f, 0.65f, 0.68f, 1.00f);
    c[ImGuiCol_ButtonHovered]         = ImVec4(0.60f, 0.60f, 0.63f, 1.00f);
    c[ImGuiCol_ButtonActive]          = ImVec4(0.55f, 0.55f, 0.58f, 1.00f);
    c[ImGuiCol_SliderGrab]            = ImVec4(0.20f, 0.45f, 0.70f, 1.00f);
    c[ImGuiCol_SliderGrabActive]      = ImVec4(0.15f, 0.38f, 0.65f, 1.00f);
    c[ImGuiCol_CheckMark]             = ImVec4(0.20f, 0.45f, 0.70f, 1.00f);
    c[ImGuiCol_Header]                = ImVec4(0.65f, 0.65f, 0.68f, 1.00f);
    c[ImGuiCol_HeaderHovered]         = ImVec4(0.60f, 0.60f, 0.63f, 1.00f);
    c[ImGuiCol_HeaderActive]          = ImVec4(0.55f, 0.55f, 0.58f, 1.00f);
    c[ImGuiCol_Separator]             = ImVec4(0.58f, 0.58f, 0.62f, 1.00f);
    c[ImGuiCol_Tab]                   = ImVec4(0.70f, 0.70f, 0.72f, 1.00f);
    c[ImGuiCol_TabHovered]            = ImVec4(0.60f, 0.60f, 0.63f, 1.00f);
    c[ImGuiCol_TabSelected]           = ImVec4(0.65f, 0.65f, 0.68f, 1.00f);
    c[ImGuiCol_PlotLines]             = ImVec4(0.20f, 0.45f, 0.70f, 1.00f);
    c[ImGuiCol_PlotHistogram]         = ImVec4(0.20f, 0.45f, 0.70f, 1.00f);
}

static void apply_hacker(ImGuiStyle &style, ImVec4 *c)
{
    style.FrameBorderSize = 1.0f;
    style.FrameRounding   = 1.0f;
    style.GrabRounding    = 1.0f;

    ImVec4 green      = ImVec4(0.00f, 0.67f, 0.22f, 1.00f);
    ImVec4 dim_green  = ImVec4(0.00f, 0.40f, 0.13f, 1.00f);
    ImVec4 dark_green = ImVec4(0.00f, 0.17f, 0.05f, 1.00f);
    ImVec4 bg_black   = ImVec4(0.04f, 0.06f, 0.04f, 1.00f);
    ImVec4 bg_panel   = ImVec4(0.06f, 0.09f, 0.06f, 1.00f);
    ImVec4 cyan       = ImVec4(0.00f, 0.60f, 0.60f, 1.00f);
    ImVec4 amber      = ImVec4(0.67f, 0.47f, 0.00f, 1.00f);

    c[ImGuiCol_Text]                  = green;
    c[ImGuiCol_TextDisabled]          = dim_green;
    c[ImGuiCol_WindowBg]              = bg_black;
    c[ImGuiCol_ChildBg]               = bg_panel;
    c[ImGuiCol_PopupBg]               = ImVec4(0.05f, 0.08f, 0.05f, 0.97f);
    c[ImGuiCol_Border]                = dim_green;
    c[ImGuiCol_BorderShadow]          = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    c[ImGuiCol_FrameBg]               = ImVec4(0.08f, 0.12f, 0.08f, 1.00f);
    c[ImGuiCol_FrameBgHovered]        = ImVec4(0.10f, 0.18f, 0.10f, 1.00f);
    c[ImGuiCol_FrameBgActive]         = ImVec4(0.12f, 0.22f, 0.12f, 1.00f);
    c[ImGuiCol_TitleBg]               = bg_black;
    c[ImGuiCol_TitleBgActive]         = dark_green;
    c[ImGuiCol_TitleBgCollapsed]      = bg_black;
    c[ImGuiCol_MenuBarBg]             = bg_panel;
    c[ImGuiCol_ScrollbarBg]           = bg_black;
    c[ImGuiCol_ScrollbarGrab]         = dark_green;
    c[ImGuiCol_ScrollbarGrabHovered]  = dim_green;
    c[ImGuiCol_ScrollbarGrabActive]   = green;
    c[ImGuiCol_CheckMark]             = cyan;
    c[ImGuiCol_SliderGrab]            = green;
    c[ImGuiCol_SliderGrabActive]      = cyan;
    c[ImGuiCol_Button]                = dark_green;
    c[ImGuiCol_ButtonHovered]         = dim_green;
    c[ImGuiCol_ButtonActive]          = green;
    c[ImGuiCol_Header]                = dark_green;
    c[ImGuiCol_HeaderHovered]         = dim_green;
    c[ImGuiCol_HeaderActive]          = ImVec4(0.00f, 0.40f, 0.13f, 1.00f);
    c[ImGuiCol_Separator]             = dim_green;
    c[ImGuiCol_SeparatorHovered]      = cyan;
    c[ImGuiCol_SeparatorActive]       = cyan;
    c[ImGuiCol_Tab]                   = dark_green;
    c[ImGuiCol_TabHovered]            = dim_green;
    c[ImGuiCol_TabSelected]           = ImVec4(0.00f, 0.35f, 0.12f, 1.00f);
    c[ImGuiCol_PlotLines]             = green;
    c[ImGuiCol_PlotHistogram]         = amber;
}

static void apply_midnight(ImGuiStyle &style, ImVec4 *c)
{
    style.FrameBorderSize = 1.0f;
    style.FrameRounding   = 1.0f;
    style.GrabRounding    = 1.0f;

    ImVec4 purple     = ImVec4(0.55f, 0.35f, 0.85f, 1.00f);
    ImVec4 dim_purple = ImVec4(0.30f, 0.20f, 0.50f, 1.00f);
    ImVec4 dark_purp  = ImVec4(0.12f, 0.08f, 0.22f, 1.00f);
    ImVec4 bg_black   = ImVec4(0.05f, 0.04f, 0.09f, 1.00f);
    ImVec4 bg_panel   = ImVec4(0.07f, 0.06f, 0.12f, 1.00f);
    ImVec4 cyan       = ImVec4(0.20f, 0.70f, 0.90f, 1.00f);
    ImVec4 pink       = ImVec4(0.90f, 0.30f, 0.60f, 1.00f);

    c[ImGuiCol_Text]                  = ImVec4(0.80f, 0.78f, 0.88f, 1.00f);
    c[ImGuiCol_TextDisabled]          = dim_purple;
    c[ImGuiCol_WindowBg]              = bg_black;
    c[ImGuiCol_ChildBg]               = bg_panel;
    c[ImGuiCol_PopupBg]               = ImVec4(0.06f, 0.05f, 0.11f, 0.97f);
    c[ImGuiCol_Border]                = dim_purple;
    c[ImGuiCol_BorderShadow]          = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    c[ImGuiCol_FrameBg]               = ImVec4(0.10f, 0.08f, 0.16f, 1.00f);
    c[ImGuiCol_FrameBgHovered]        = ImVec4(0.14f, 0.11f, 0.22f, 1.00f);
    c[ImGuiCol_FrameBgActive]         = ImVec4(0.18f, 0.14f, 0.28f, 1.00f);
    c[ImGuiCol_TitleBg]               = bg_black;
    c[ImGuiCol_TitleBgActive]         = dark_purp;
    c[ImGuiCol_TitleBgCollapsed]      = bg_black;
    c[ImGuiCol_MenuBarBg]             = bg_panel;
    c[ImGuiCol_ScrollbarBg]           = bg_black;
    c[ImGuiCol_ScrollbarGrab]         = dark_purp;
    c[ImGuiCol_ScrollbarGrabHovered]  = dim_purple;
    c[ImGuiCol_ScrollbarGrabActive]   = purple;
    c[ImGuiCol_CheckMark]             = cyan;
    c[ImGuiCol_SliderGrab]            = purple;
    c[ImGuiCol_SliderGrabActive]      = cyan;
    c[ImGuiCol_Button]                = dark_purp;
    c[ImGuiCol_ButtonHovered]         = dim_purple;
    c[ImGuiCol_ButtonActive]          = purple;
    c[ImGuiCol_Header]                = dark_purp;
    c[ImGuiCol_HeaderHovered]         = dim_purple;
    c[ImGuiCol_HeaderActive]          = ImVec4(0.22f, 0.16f, 0.38f, 1.00f);
    c[ImGuiCol_Separator]             = dim_purple;
    c[ImGuiCol_SeparatorHovered]      = pink;
    c[ImGuiCol_SeparatorActive]       = pink;
    c[ImGuiCol_Tab]                   = dark_purp;
    c[ImGuiCol_TabHovered]            = dim_purple;
    c[ImGuiCol_TabSelected]           = ImVec4(0.18f, 0.13f, 0.30f, 1.00f);
    c[ImGuiCol_PlotLines]             = cyan;
    c[ImGuiCol_PlotHistogram]         = pink;
}

static void apply_amber(ImGuiStyle &style, ImVec4 *c)
{
    style.FrameBorderSize = 1.0f;
    style.FrameRounding   = 1.0f;
    style.GrabRounding    = 1.0f;

    ImVec4 amber      = ImVec4(0.80f, 0.55f, 0.10f, 1.00f);
    ImVec4 dim_amber  = ImVec4(0.45f, 0.30f, 0.06f, 1.00f);
    ImVec4 dark_amber = ImVec4(0.18f, 0.12f, 0.03f, 1.00f);
    ImVec4 bg_black   = ImVec4(0.06f, 0.04f, 0.02f, 1.00f);
    ImVec4 bg_panel   = ImVec4(0.09f, 0.06f, 0.03f, 1.00f);
    ImVec4 warm_white = ImVec4(0.90f, 0.80f, 0.55f, 1.00f);

    c[ImGuiCol_Text]                  = warm_white;
    c[ImGuiCol_TextDisabled]          = dim_amber;
    c[ImGuiCol_WindowBg]              = bg_black;
    c[ImGuiCol_ChildBg]               = bg_panel;
    c[ImGuiCol_PopupBg]               = ImVec4(0.08f, 0.05f, 0.03f, 0.97f);
    c[ImGuiCol_Border]                = dim_amber;
    c[ImGuiCol_BorderShadow]          = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    c[ImGuiCol_FrameBg]               = ImVec4(0.12f, 0.08f, 0.04f, 1.00f);
    c[ImGuiCol_FrameBgHovered]        = ImVec4(0.16f, 0.11f, 0.05f, 1.00f);
    c[ImGuiCol_FrameBgActive]         = ImVec4(0.22f, 0.15f, 0.06f, 1.00f);
    c[ImGuiCol_TitleBg]               = bg_black;
    c[ImGuiCol_TitleBgActive]         = dark_amber;
    c[ImGuiCol_TitleBgCollapsed]      = bg_black;
    c[ImGuiCol_MenuBarBg]             = bg_panel;
    c[ImGuiCol_ScrollbarBg]           = bg_black;
    c[ImGuiCol_ScrollbarGrab]         = dark_amber;
    c[ImGuiCol_ScrollbarGrabHovered]  = dim_amber;
    c[ImGuiCol_ScrollbarGrabActive]   = amber;
    c[ImGuiCol_CheckMark]             = amber;
    c[ImGuiCol_SliderGrab]            = amber;
    c[ImGuiCol_SliderGrabActive]      = warm_white;
    c[ImGuiCol_Button]                = dark_amber;
    c[ImGuiCol_ButtonHovered]         = dim_amber;
    c[ImGuiCol_ButtonActive]          = amber;
    c[ImGuiCol_Header]                = dark_amber;
    c[ImGuiCol_HeaderHovered]         = dim_amber;
    c[ImGuiCol_HeaderActive]          = ImVec4(0.30f, 0.20f, 0.05f, 1.00f);
    c[ImGuiCol_Separator]             = dim_amber;
    c[ImGuiCol_SeparatorHovered]      = amber;
    c[ImGuiCol_SeparatorActive]       = amber;
    c[ImGuiCol_Tab]                   = dark_amber;
    c[ImGuiCol_TabHovered]            = dim_amber;
    c[ImGuiCol_TabSelected]           = ImVec4(0.25f, 0.17f, 0.04f, 1.00f);
    c[ImGuiCol_PlotLines]             = amber;
    c[ImGuiCol_PlotHistogram]         = warm_white;
}

/* --- Vaporwave: pink/cyan/purple synthwave -------------------------------- */

static void apply_vaporwave(ImGuiStyle &style, ImVec4 *c)
{
    style.FrameBorderSize = 1.0f;
    style.FrameRounding   = 1.0f;
    style.GrabRounding    = 1.0f;

    ImVec4 pink      = ImVec4(0.95f, 0.40f, 0.70f, 1.00f);
    ImVec4 dim_pink  = ImVec4(0.50f, 0.20f, 0.38f, 1.00f);
    ImVec4 dark_purp = ImVec4(0.20f, 0.08f, 0.30f, 1.00f);
    ImVec4 bg_black  = ImVec4(0.08f, 0.04f, 0.12f, 1.00f);
    ImVec4 bg_panel  = ImVec4(0.10f, 0.05f, 0.15f, 1.00f);
    ImVec4 text      = ImVec4(0.85f, 0.75f, 0.95f, 1.00f);
    ImVec4 cyan      = ImVec4(0.30f, 0.85f, 0.95f, 1.00f);

    c[ImGuiCol_Text]                  = text;
    c[ImGuiCol_TextDisabled]          = ImVec4(0.40f, 0.30f, 0.50f, 1.00f);
    c[ImGuiCol_WindowBg]              = bg_black;
    c[ImGuiCol_ChildBg]               = bg_panel;
    c[ImGuiCol_PopupBg]               = ImVec4(0.09f, 0.05f, 0.14f, 0.97f);
    c[ImGuiCol_Border]                = ImVec4(0.35f, 0.15f, 0.55f, 1.00f);
    c[ImGuiCol_BorderShadow]          = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    c[ImGuiCol_FrameBg]               = ImVec4(0.12f, 0.06f, 0.18f, 1.00f);
    c[ImGuiCol_FrameBgHovered]        = ImVec4(0.16f, 0.09f, 0.24f, 1.00f);
    c[ImGuiCol_FrameBgActive]         = ImVec4(0.20f, 0.12f, 0.30f, 1.00f);
    c[ImGuiCol_TitleBg]               = bg_black;
    c[ImGuiCol_TitleBgActive]         = dark_purp;
    c[ImGuiCol_TitleBgCollapsed]      = bg_black;
    c[ImGuiCol_MenuBarBg]             = bg_panel;
    c[ImGuiCol_ScrollbarBg]           = bg_black;
    c[ImGuiCol_ScrollbarGrab]         = dark_purp;
    c[ImGuiCol_ScrollbarGrabHovered]  = dim_pink;
    c[ImGuiCol_ScrollbarGrabActive]   = pink;
    c[ImGuiCol_CheckMark]             = cyan;
    c[ImGuiCol_SliderGrab]            = pink;
    c[ImGuiCol_SliderGrabActive]      = ImVec4(1.00f, 0.55f, 0.80f, 1.00f);
    c[ImGuiCol_Button]                = dark_purp;
    c[ImGuiCol_ButtonHovered]         = dim_pink;
    c[ImGuiCol_ButtonActive]          = pink;
    c[ImGuiCol_Header]                = dark_purp;
    c[ImGuiCol_HeaderHovered]         = dim_pink;
    c[ImGuiCol_HeaderActive]          = ImVec4(0.35f, 0.15f, 0.55f, 1.00f);
    c[ImGuiCol_Separator]             = dim_pink;
    c[ImGuiCol_SeparatorHovered]      = pink;
    c[ImGuiCol_SeparatorActive]       = pink;
    c[ImGuiCol_Tab]                   = dark_purp;
    c[ImGuiCol_TabHovered]            = dim_pink;
    c[ImGuiCol_TabSelected]           = ImVec4(0.28f, 0.12f, 0.42f, 1.00f);
    c[ImGuiCol_PlotLines]             = cyan;
    c[ImGuiCol_PlotHistogram]         = pink;
}

/* --- Neon: hot pink/magenta on dark, cyan accents ------------------------- */

static void apply_neon(ImGuiStyle &style, ImVec4 *c)
{
    style.FrameBorderSize = 1.0f;
    style.FrameRounding   = 1.0f;
    style.GrabRounding    = 1.0f;

    ImVec4 magenta     = ImVec4(0.90f, 0.20f, 0.80f, 1.00f);
    ImVec4 dim_magenta = ImVec4(0.45f, 0.15f, 0.40f, 1.00f);
    ImVec4 dark_purp   = ImVec4(0.15f, 0.08f, 0.25f, 1.00f);
    ImVec4 bg_black    = ImVec4(0.06f, 0.04f, 0.10f, 1.00f);
    ImVec4 bg_panel    = ImVec4(0.08f, 0.05f, 0.13f, 1.00f);
    ImVec4 text        = ImVec4(0.95f, 0.30f, 0.90f, 1.00f);
    ImVec4 cyan        = ImVec4(0.00f, 0.90f, 0.95f, 1.00f);

    c[ImGuiCol_Text]                  = text;
    c[ImGuiCol_TextDisabled]          = ImVec4(0.45f, 0.15f, 0.40f, 1.00f);
    c[ImGuiCol_WindowBg]              = bg_black;
    c[ImGuiCol_ChildBg]               = bg_panel;
    c[ImGuiCol_PopupBg]               = ImVec4(0.07f, 0.05f, 0.12f, 0.97f);
    c[ImGuiCol_Border]                = ImVec4(0.30f, 0.15f, 0.50f, 1.00f);
    c[ImGuiCol_BorderShadow]          = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    c[ImGuiCol_FrameBg]               = ImVec4(0.10f, 0.07f, 0.15f, 1.00f);
    c[ImGuiCol_FrameBgHovered]        = ImVec4(0.14f, 0.10f, 0.22f, 1.00f);
    c[ImGuiCol_FrameBgActive]         = ImVec4(0.18f, 0.13f, 0.28f, 1.00f);
    c[ImGuiCol_TitleBg]               = bg_black;
    c[ImGuiCol_TitleBgActive]         = dark_purp;
    c[ImGuiCol_TitleBgCollapsed]      = bg_black;
    c[ImGuiCol_MenuBarBg]             = bg_panel;
    c[ImGuiCol_ScrollbarBg]           = bg_black;
    c[ImGuiCol_ScrollbarGrab]         = dark_purp;
    c[ImGuiCol_ScrollbarGrabHovered]  = dim_magenta;
    c[ImGuiCol_ScrollbarGrabActive]   = magenta;
    c[ImGuiCol_CheckMark]             = cyan;
    c[ImGuiCol_SliderGrab]            = magenta;
    c[ImGuiCol_SliderGrabActive]      = ImVec4(0.95f, 0.40f, 0.90f, 1.00f);
    c[ImGuiCol_Button]                = dark_purp;
    c[ImGuiCol_ButtonHovered]         = dim_magenta;
    c[ImGuiCol_ButtonActive]          = magenta;
    c[ImGuiCol_Header]                = dark_purp;
    c[ImGuiCol_HeaderHovered]         = dim_magenta;
    c[ImGuiCol_HeaderActive]          = ImVec4(0.30f, 0.15f, 0.50f, 1.00f);
    c[ImGuiCol_Separator]             = dim_magenta;
    c[ImGuiCol_SeparatorHovered]      = magenta;
    c[ImGuiCol_SeparatorActive]       = magenta;
    c[ImGuiCol_Tab]                   = dark_purp;
    c[ImGuiCol_TabHovered]            = dim_magenta;
    c[ImGuiCol_TabSelected]           = ImVec4(0.22f, 0.10f, 0.38f, 1.00f);
    c[ImGuiCol_PlotLines]             = cyan;
    c[ImGuiCol_PlotHistogram]         = magenta;
}

/* --- Public built-in API -------------------------------------------------- */

void theme_apply(sq_theme_t theme)
{
    s_current_theme = theme;
    s_active_user_theme = -1;
    ImGuiStyle &style = ImGui::GetStyle();
    style_common(style);

    ImVec4 *c = style.Colors;

    switch (theme) {
    case THEME_LIGHT:      apply_light(c);              break;
    case THEME_HACKER:     apply_hacker(style, c);      break;
    case THEME_MIDNIGHT:   apply_midnight(style, c);    break;
    case THEME_AMBER:      apply_amber(style, c);       break;
    case THEME_VAPORWAVE:  apply_vaporwave(style, c);   break;
    case THEME_NEON:       apply_neon(style, c);        break;
    default:               apply_dark(c);               break;
    }

    LOG_INFO("Applied %s theme", theme_name(theme));
}

/* ========================================================================== */
/* Minimal JSON parser — flat objects only: {"key": value, ...}               */
/* Values are either quoted strings or floats.                                */
/* ========================================================================== */

/* Skip whitespace, return pointer past it */
static const char *json_skip_ws(const char *p)
{
    while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r'))
        ++p;
    return p;
}

/* Parse a quoted string. Returns pointer past closing quote.
   Copies up to max-1 chars into out, null-terminates. */
static const char *json_parse_string(const char *p, char *out, int max)
{
    if (*p != '"') return nullptr;
    ++p;
    int i = 0;
    while (*p && *p != '"') {
        if (*p == '\\' && *(p + 1)) { ++p; } /* skip escaped char */
        if (i < max - 1) out[i++] = *p;
        ++p;
    }
    out[i] = '\0';
    if (*p == '"') ++p;
    return p;
}

/* Parse a float value. Returns pointer past the number. */
static const char *json_parse_float(const char *p, float *out)
{
    char *end = nullptr;
    *out = strtof(p, &end);
    return end ? end : p;
}

/* Callback type for key-value pairs */
typedef void (*json_kv_cb)(const char *key, const char *str_val,
                           float num_val, bool is_string, void *user);

/* Parse a flat JSON object, calling cb for each key-value pair */
static bool json_parse_flat(const char *buf, json_kv_cb cb, void *user)
{
    const char *p = json_skip_ws(buf);
    if (*p != '{') return false;
    ++p;

    while (true) {
        p = json_skip_ws(p);
        if (*p == '}') return true;
        if (*p == ',') { ++p; p = json_skip_ws(p); }
        if (*p == '}') return true;

        /* Key */
        char key[128] = {};
        p = json_parse_string(p, key, sizeof(key));
        if (!p) return false;

        p = json_skip_ws(p);
        if (*p != ':') return false;
        ++p;
        p = json_skip_ws(p);

        /* Value */
        if (*p == '"') {
            char sval[128] = {};
            p = json_parse_string(p, sval, sizeof(sval));
            if (!p) return false;
            cb(key, sval, 0.0f, true, user);
        } else {
            float fval = 0.0f;
            p = json_parse_float(p, &fval);
            cb(key, nullptr, fval, false, user);
        }
    }
}

/* --- JSON to sq_user_theme_t mapping -------------------------------------- */

static void theme_json_cb(const char *key, const char *str_val,
                          float num_val, bool is_string, void *user)
{
    sq_user_theme_t *t = (sq_user_theme_t *)user;

    if (is_string) {
        if (strcmp(key, "name") == 0)
            snprintf(t->name, sizeof(t->name), "%s", str_val);
        return;
    }

    /* Map "prefix_suffix" to the right float field.
       We use a table-driven approach for clarity. */
    struct mapping { const char *key; float *dst; };
    static const mapping map[] = {
        /* clear color */
        { "clear_r",          &t->clear_color[0] },
        { "clear_g",          &t->clear_color[1] },
        { "clear_b",          &t->clear_color[2] },
        /* text */
        { "text_r",           &t->text[0] },
        { "text_g",           &t->text[1] },
        { "text_b",           &t->text[2] },
        { "text_disabled_r",  &t->text_disabled[0] },
        { "text_disabled_g",  &t->text_disabled[1] },
        { "text_disabled_b",  &t->text_disabled[2] },
        /* backgrounds */
        { "window_bg_r",      &t->window_bg[0] },
        { "window_bg_g",      &t->window_bg[1] },
        { "window_bg_b",      &t->window_bg[2] },
        { "child_bg_r",       &t->child_bg[0] },
        { "child_bg_g",       &t->child_bg[1] },
        { "child_bg_b",       &t->child_bg[2] },
        { "popup_bg_r",       &t->popup_bg[0] },
        { "popup_bg_g",       &t->popup_bg[1] },
        { "popup_bg_b",       &t->popup_bg[2] },
        /* border */
        { "border_r",         &t->border[0] },
        { "border_g",         &t->border[1] },
        { "border_b",         &t->border[2] },
        /* frame */
        { "frame_bg_r",       &t->frame_bg[0] },
        { "frame_bg_g",       &t->frame_bg[1] },
        { "frame_bg_b",       &t->frame_bg[2] },
        { "frame_bg_hover_r", &t->frame_bg_hover[0] },
        { "frame_bg_hover_g", &t->frame_bg_hover[1] },
        { "frame_bg_hover_b", &t->frame_bg_hover[2] },
        { "frame_bg_active_r",&t->frame_bg_active[0] },
        { "frame_bg_active_g",&t->frame_bg_active[1] },
        { "frame_bg_active_b",&t->frame_bg_active[2] },
        /* button */
        { "button_r",         &t->button[0] },
        { "button_g",         &t->button[1] },
        { "button_b",         &t->button[2] },
        { "button_hover_r",   &t->button_hover[0] },
        { "button_hover_g",   &t->button_hover[1] },
        { "button_hover_b",   &t->button_hover[2] },
        { "button_active_r",  &t->button_active[0] },
        { "button_active_g",  &t->button_active[1] },
        { "button_active_b",  &t->button_active[2] },
        /* slider */
        { "slider_r",         &t->slider[0] },
        { "slider_g",         &t->slider[1] },
        { "slider_b",         &t->slider[2] },
        { "slider_active_r",  &t->slider_active[0] },
        { "slider_active_g",  &t->slider_active[1] },
        { "slider_active_b",  &t->slider_active[2] },
        /* accent (checkmark, separator hover) */
        { "accent_r",         &t->accent[0] },
        { "accent_g",         &t->accent[1] },
        { "accent_b",         &t->accent[2] },
        /* header */
        { "header_r",         &t->header[0] },
        { "header_g",         &t->header[1] },
        { "header_b",         &t->header[2] },
        { "header_hover_r",   &t->header_hover[0] },
        { "header_hover_g",   &t->header_hover[1] },
        { "header_hover_b",   &t->header_hover[2] },
        { "header_active_r",  &t->header_active[0] },
        { "header_active_g",  &t->header_active[1] },
        { "header_active_b",  &t->header_active[2] },
        /* tab */
        { "tab_r",            &t->tab[0] },
        { "tab_g",            &t->tab[1] },
        { "tab_b",            &t->tab[2] },
        { "tab_hover_r",      &t->tab_hover[0] },
        { "tab_hover_g",      &t->tab_hover[1] },
        { "tab_hover_b",      &t->tab_hover[2] },
        { "tab_selected_r",   &t->tab_selected[0] },
        { "tab_selected_g",   &t->tab_selected[1] },
        { "tab_selected_b",   &t->tab_selected[2] },
        /* plot */
        { "plot_lines_r",     &t->plot_lines[0] },
        { "plot_lines_g",     &t->plot_lines[1] },
        { "plot_lines_b",     &t->plot_lines[2] },
        { "plot_histogram_r", &t->plot_histogram[0] },
        { "plot_histogram_g", &t->plot_histogram[1] },
        { "plot_histogram_b", &t->plot_histogram[2] },
        /* pad colors */
        { "pad_r",            &t->pad[0] },
        { "pad_g",            &t->pad[1] },
        { "pad_b",            &t->pad[2] },
        { "pad_glow_r",       &t->pad_glow[0] },
        { "pad_glow_g",       &t->pad_glow[1] },
        { "pad_glow_b",       &t->pad_glow[2] },
        /* style */
        { "frame_border",     &t->frame_border },
    };

    for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); i++) {
        if (strcmp(key, map[i].key) == 0) {
            *map[i].dst = num_val;
            return;
        }
    }
    /* Unknown key — silently ignore for forward compatibility */
}

/* Load a single JSON theme file into a user theme slot.
   Returns true on success. */
static bool theme_load_json_into(const char *path, sq_user_theme_t *t)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        LOG_WARN("Could not open theme file: %s", path);
        return false;
    }

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (sz <= 0 || sz > 64 * 1024) {
        LOG_WARN("Theme file too large or empty: %s (%ld bytes)", path, sz);
        fclose(f);
        return false;
    }

    char *buf = (char *)malloc(sz + 1);
    if (!buf) { fclose(f); return false; }
    fread(buf, 1, sz, f);
    buf[sz] = '\0';
    fclose(f);

    memset(t, 0, sizeof(*t));
    snprintf(t->path, sizeof(t->path), "%s", path);

    /* Set sensible defaults before parsing (so missing keys don't zero out) */
    t->text[0] = t->text[1] = t->text[2] = 0.82f;
    t->text_disabled[0] = t->text_disabled[1] = t->text_disabled[2] = 0.42f;
    t->window_bg[0] = t->window_bg[1] = t->window_bg[2] = 0.14f;
    t->child_bg[0] = t->child_bg[1] = t->child_bg[2] = 0.14f;
    t->popup_bg[0] = t->popup_bg[1] = t->popup_bg[2] = 0.16f;
    t->border[0] = t->border[1] = t->border[2] = 0.24f;
    t->frame_bg[0] = t->frame_bg[1] = t->frame_bg[2] = 0.18f;
    t->frame_bg_hover[0] = t->frame_bg_hover[1] = t->frame_bg_hover[2] = 0.24f;
    t->frame_bg_active[0] = t->frame_bg_active[1] = t->frame_bg_active[2] = 0.28f;
    t->button[0] = t->button[1] = t->button[2] = 0.22f;
    t->button_hover[0] = t->button_hover[1] = t->button_hover[2] = 0.27f;
    t->button_active[0] = t->button_active[1] = t->button_active[2] = 0.31f;
    t->slider[0] = 0.39f; t->slider[1] = 0.71f; t->slider[2] = 1.00f;
    t->slider_active[0] = 0.55f; t->slider_active[1] = 0.86f; t->slider_active[2] = 1.00f;
    t->accent[0] = 0.39f; t->accent[1] = 0.71f; t->accent[2] = 1.00f;
    t->header[0] = t->header[1] = t->header[2] = 0.22f;
    t->header_hover[0] = t->header_hover[1] = t->header_hover[2] = 0.27f;
    t->header_active[0] = t->header_active[1] = t->header_active[2] = 0.31f;
    t->tab[0] = t->tab[1] = t->tab[2] = 0.18f;
    t->tab_hover[0] = t->tab_hover[1] = t->tab_hover[2] = 0.27f;
    t->tab_selected[0] = t->tab_selected[1] = t->tab_selected[2] = 0.24f;
    t->plot_lines[0] = 0.39f; t->plot_lines[1] = 0.71f; t->plot_lines[2] = 1.00f;
    t->plot_histogram[0] = 0.39f; t->plot_histogram[1] = 0.71f; t->plot_histogram[2] = 1.00f;
    t->pad[0] = 0.39f; t->pad[1] = 0.71f; t->pad[2] = 1.00f;
    t->pad_glow[0] = 0.55f; t->pad_glow[1] = 0.86f; t->pad_glow[2] = 1.00f;
    t->clear_color[0] = 0.12f; t->clear_color[1] = 0.12f; t->clear_color[2] = 0.13f;
    t->frame_border = 0.0f;

    bool ok = json_parse_flat(buf, theme_json_cb, t);
    free(buf);

    if (!ok) {
        LOG_WARN("Failed to parse theme JSON: %s", path);
        return false;
    }

    /* If no name was set, derive from filename */
    if (t->name[0] == '\0') {
        const char *slash = strrchr(path, '/');
        if (!slash) slash = strrchr(path, '\\');
        const char *base = slash ? slash + 1 : path;
        snprintf(t->name, sizeof(t->name), "%s", base);
        /* Strip .json extension */
        char *dot = strrchr(t->name, '.');
        if (dot) *dot = '\0';
    }

    t->loaded = true;
    return true;
}

/* --- User theme public API ------------------------------------------------ */

/* Check if a user theme name conflicts with a built-in theme */
#ifdef _WIN32
#define sq_strcasecmp _stricmp
#else
#define sq_strcasecmp strcasecmp
#endif

static bool theme_name_conflicts_builtin(const char *name)
{
    for (int i = 0; i < THEME_COUNT; i++) {
        if (sq_strcasecmp(name, s_theme_names[i]) == 0)
            return true;
    }
    return false;
}

void theme_scan_user_themes(const char *dir)
{
    if (!dir || !dir[0]) return;

    s_num_user_themes = 0;
    LOG_INFO("Scanning for user themes in: %s", dir);

#ifdef _WIN32
    /* Windows: FindFirstFile / FindNextFile */
    char pattern[600];
    snprintf(pattern, sizeof(pattern), "%s\\*.json", dir);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) {
        /* Also try forward slashes */
        snprintf(pattern, sizeof(pattern), "%s/*.json", dir);
        h = FindFirstFileA(pattern, &fd);
    }
    if (h == INVALID_HANDLE_VALUE) {
        LOG_INFO("No theme files found in %s", dir);
        return;
    }
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        if (s_num_user_themes >= SQ_MAX_USER_THEMES) break;

        char full[600];
        snprintf(full, sizeof(full), "%s\\%s", dir, fd.cFileName);

        if (theme_load_json_into(full, &s_user_themes[s_num_user_themes])) {
            if (theme_name_conflicts_builtin(s_user_themes[s_num_user_themes].name)) {
                LOG_INFO("Skipping user theme '%s' — conflicts with built-in",
                         s_user_themes[s_num_user_themes].name);
                s_user_themes[s_num_user_themes].loaded = false;
            } else {
                LOG_INFO("Loaded user theme: %s (%s)",
                         s_user_themes[s_num_user_themes].name, full);
                s_num_user_themes++;
            }
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    /* POSIX: opendir / readdir */
    DIR *d = opendir(dir);
    if (!d) {
        LOG_INFO("No themes directory: %s", dir);
        return;
    }
    struct dirent *ent;
    while ((ent = readdir(d)) != nullptr) {
        if (s_num_user_themes >= SQ_MAX_USER_THEMES) break;
        const char *name = ent->d_name;
        size_t len = strlen(name);
        if (len < 6) continue; /* must be at least "x.json" */
        if (strcmp(name + len - 5, ".json") != 0) continue;

        char full[600];
        snprintf(full, sizeof(full), "%s/%s", dir, name);

        if (theme_load_json_into(full, &s_user_themes[s_num_user_themes])) {
            if (theme_name_conflicts_builtin(s_user_themes[s_num_user_themes].name)) {
                LOG_INFO("Skipping user theme '%s' — conflicts with built-in",
                         s_user_themes[s_num_user_themes].name);
                s_user_themes[s_num_user_themes].loaded = false;
            } else {
                LOG_INFO("Loaded user theme: %s (%s)",
                         s_user_themes[s_num_user_themes].name, full);
                s_num_user_themes++;
            }
        }
    }
    closedir(d);
#endif

    LOG_INFO("Found %d user theme(s)", s_num_user_themes);
}

int theme_num_user_themes(void)
{
    return s_num_user_themes;
}

const char *theme_user_name(int index)
{
    if (index < 0 || index >= s_num_user_themes) return "?";
    return s_user_themes[index].name;
}

void theme_apply_user(int index)
{
    if (index < 0 || index >= s_num_user_themes) return;
    const sq_user_theme_t &t = s_user_themes[index];
    if (!t.loaded) return;

    s_active_user_theme = index;

    ImGuiStyle &style = ImGui::GetStyle();
    style_common(style);
    style.FrameBorderSize = t.frame_border;
    if (t.frame_border > 0.0f) {
        style.FrameRounding = 1.0f;
        style.GrabRounding  = 1.0f;
    }

    ImVec4 *c = style.Colors;

    c[ImGuiCol_Text]                  = ImVec4(t.text[0], t.text[1], t.text[2], 1.0f);
    c[ImGuiCol_TextDisabled]          = ImVec4(t.text_disabled[0], t.text_disabled[1], t.text_disabled[2], 1.0f);
    c[ImGuiCol_WindowBg]              = ImVec4(t.window_bg[0], t.window_bg[1], t.window_bg[2], 1.0f);
    c[ImGuiCol_ChildBg]               = ImVec4(t.child_bg[0], t.child_bg[1], t.child_bg[2], 1.0f);
    c[ImGuiCol_PopupBg]               = ImVec4(t.popup_bg[0], t.popup_bg[1], t.popup_bg[2], 0.97f);
    c[ImGuiCol_Border]                = ImVec4(t.border[0], t.border[1], t.border[2], 1.0f);
    c[ImGuiCol_BorderShadow]          = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    c[ImGuiCol_FrameBg]               = ImVec4(t.frame_bg[0], t.frame_bg[1], t.frame_bg[2], 1.0f);
    c[ImGuiCol_FrameBgHovered]        = ImVec4(t.frame_bg_hover[0], t.frame_bg_hover[1], t.frame_bg_hover[2], 1.0f);
    c[ImGuiCol_FrameBgActive]         = ImVec4(t.frame_bg_active[0], t.frame_bg_active[1], t.frame_bg_active[2], 1.0f);
    c[ImGuiCol_TitleBg]               = ImVec4(t.window_bg[0], t.window_bg[1], t.window_bg[2], 1.0f);
    c[ImGuiCol_TitleBgActive]         = ImVec4(t.frame_bg[0], t.frame_bg[1], t.frame_bg[2], 1.0f);
    c[ImGuiCol_TitleBgCollapsed]      = ImVec4(t.window_bg[0], t.window_bg[1], t.window_bg[2], 1.0f);
    c[ImGuiCol_MenuBarBg]             = ImVec4(t.frame_bg[0], t.frame_bg[1], t.frame_bg[2], 1.0f);
    c[ImGuiCol_ScrollbarBg]           = ImVec4(t.window_bg[0], t.window_bg[1], t.window_bg[2], 1.0f);
    c[ImGuiCol_ScrollbarGrab]         = ImVec4(t.button[0], t.button[1], t.button[2], 1.0f);
    c[ImGuiCol_ScrollbarGrabHovered]  = ImVec4(t.button_hover[0], t.button_hover[1], t.button_hover[2], 1.0f);
    c[ImGuiCol_ScrollbarGrabActive]   = ImVec4(t.button_active[0], t.button_active[1], t.button_active[2], 1.0f);
    c[ImGuiCol_CheckMark]             = ImVec4(t.accent[0], t.accent[1], t.accent[2], 1.0f);
    c[ImGuiCol_SliderGrab]            = ImVec4(t.slider[0], t.slider[1], t.slider[2], 1.0f);
    c[ImGuiCol_SliderGrabActive]      = ImVec4(t.slider_active[0], t.slider_active[1], t.slider_active[2], 1.0f);
    c[ImGuiCol_Button]                = ImVec4(t.button[0], t.button[1], t.button[2], 1.0f);
    c[ImGuiCol_ButtonHovered]         = ImVec4(t.button_hover[0], t.button_hover[1], t.button_hover[2], 1.0f);
    c[ImGuiCol_ButtonActive]          = ImVec4(t.button_active[0], t.button_active[1], t.button_active[2], 1.0f);
    c[ImGuiCol_Header]                = ImVec4(t.header[0], t.header[1], t.header[2], 1.0f);
    c[ImGuiCol_HeaderHovered]         = ImVec4(t.header_hover[0], t.header_hover[1], t.header_hover[2], 1.0f);
    c[ImGuiCol_HeaderActive]          = ImVec4(t.header_active[0], t.header_active[1], t.header_active[2], 1.0f);
    c[ImGuiCol_Separator]             = ImVec4(t.border[0], t.border[1], t.border[2], 1.0f);
    c[ImGuiCol_SeparatorHovered]      = ImVec4(t.accent[0], t.accent[1], t.accent[2], 1.0f);
    c[ImGuiCol_SeparatorActive]       = ImVec4(t.accent[0], t.accent[1], t.accent[2], 1.0f);
    c[ImGuiCol_Tab]                   = ImVec4(t.tab[0], t.tab[1], t.tab[2], 1.0f);
    c[ImGuiCol_TabHovered]            = ImVec4(t.tab_hover[0], t.tab_hover[1], t.tab_hover[2], 1.0f);
    c[ImGuiCol_TabSelected]           = ImVec4(t.tab_selected[0], t.tab_selected[1], t.tab_selected[2], 1.0f);
    c[ImGuiCol_PlotLines]             = ImVec4(t.plot_lines[0], t.plot_lines[1], t.plot_lines[2], 1.0f);
    c[ImGuiCol_PlotHistogram]         = ImVec4(t.plot_histogram[0], t.plot_histogram[1], t.plot_histogram[2], 1.0f);

    LOG_INFO("Applied user theme: %s", t.name);
}

/* --- Pad color query ------------------------------------------------------ */

void theme_get_pad_colors(float pad_rgb[3], float glow_rgb[3])
{
    /* If a user theme with pad colors is active, use those */
    if (s_active_user_theme >= 0 && s_active_user_theme < s_num_user_themes) {
        const sq_user_theme_t &ut = s_user_themes[s_active_user_theme];
        pad_rgb[0]  = ut.pad[0];  pad_rgb[1]  = ut.pad[1];  pad_rgb[2]  = ut.pad[2];
        glow_rgb[0] = ut.pad_glow[0]; glow_rgb[1] = ut.pad_glow[1]; glow_rgb[2] = ut.pad_glow[2];
        return;
    }

    /* Built-in theme defaults */
    switch (s_current_theme) {
    case THEME_HACKER:
        pad_rgb[0] = 0.00f; pad_rgb[1] = 0.75f; pad_rgb[2] = 0.25f;
        glow_rgb[0] = 0.70f; glow_rgb[1] = 0.20f; glow_rgb[2] = 0.90f; /* neon purple */
        break;
    case THEME_MIDNIGHT:
        pad_rgb[0] = 0.55f; pad_rgb[1] = 0.35f; pad_rgb[2] = 0.85f;
        glow_rgb[0] = 0.70f; glow_rgb[1] = 0.45f; glow_rgb[2] = 0.95f;
        break;
    case THEME_AMBER:
        pad_rgb[0] = 0.80f; pad_rgb[1] = 0.55f; pad_rgb[2] = 0.10f;
        glow_rgb[0] = 0.95f; glow_rgb[1] = 0.70f; glow_rgb[2] = 0.15f;
        break;
    case THEME_LIGHT:
        pad_rgb[0] = 0.20f; pad_rgb[1] = 0.45f; pad_rgb[2] = 0.70f;
        glow_rgb[0] = 0.30f; glow_rgb[1] = 0.55f; glow_rgb[2] = 0.85f;
        break;
    case THEME_VAPORWAVE:
        pad_rgb[0] = 0.95f; pad_rgb[1] = 0.40f; pad_rgb[2] = 0.70f;
        glow_rgb[0] = 1.00f; glow_rgb[1] = 0.55f; glow_rgb[2] = 0.80f;
        break;
    case THEME_NEON:
        pad_rgb[0] = 0.80f; pad_rgb[1] = 0.20f; pad_rgb[2] = 0.90f;
        glow_rgb[0] = 0.90f; glow_rgb[1] = 0.30f; glow_rgb[2] = 0.95f;
        break;
    default: /* THEME_DARK */
        pad_rgb[0] = 0.39f; pad_rgb[1] = 0.71f; pad_rgb[2] = 1.00f;
        glow_rgb[0] = 0.55f; glow_rgb[1] = 0.86f; glow_rgb[2] = 1.00f;
        break;
    }
}
