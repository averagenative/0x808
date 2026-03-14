/*
 * gtk_theme.c — Theme system for the GTK 4.0 frontend.
 *
 * 7 built-in themes matching the ImGui frontend:
 * Dark, Light, Hacker, Midnight, Amber, Vaporwave, Neon
 *
 * User themes: loaded from JSON files in {base_dir}/themes/
 * Supports both hex color format (#rrggbb) and ImGui float format (_r, _g, _b).
 */

#include "gtk_gui.h"
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>

#define LOG_TAG "gtk_theme"
#include "core/log.h"

#define CSS_COMMON \
    "  font-family: 'DejaVu Sans Mono', monospace;" \
    "  font-size: 13px;" \
    "}"

/* Custom flat button styling — subtle filled bg like ImGui buttons */
#define CSS_THIN_CONTROLS \
    ".sq-btn { padding: 5px 12px; border-radius: 4px;" \
    "  background-color: rgba(128,128,140,0.15); }" \
    ".sq-btn:hover { background-color: rgba(128,128,140,0.25); }" \
    ".sq-btn.active { background-color: rgba(100,160,210,0.3); }" \
    ".sq-btn.recording { background-color: rgba(200,30,30,0.7); color: #fff; }" \
    ".strip-even { background-color: rgba(128,128,140,0.10); padding: 1px; }" \
    ".strip-odd { background-color: rgba(128,128,140,0.25); padding: 1px; }" \
    ".strip-label { font-size: 9px; margin: 0; padding: 0; }" \
    ".synth-label { font-size: 10px; margin: 0; padding: 0; }" \
    ".synth-section { font-size: 10px; font-weight: bold; margin: 1px 0; padding: 0; }" \
    "button.flat { padding: 4px 8px; }" \
    "dropdown { background-image: none; }" \
    "button.section-color-0 { background-color: rgba(100,161,219,0.35); }" \
    "button.section-color-0:hover { background-color: rgba(100,161,219,0.50); }" \
    "button.section-color-1 { background-color: rgba(219,130,61,0.35); }" \
    "button.section-color-1:hover { background-color: rgba(219,130,61,0.50); }" \
    "button.section-color-2 { background-color: rgba(79,199,120,0.35); }" \
    "button.section-color-2:hover { background-color: rgba(79,199,120,0.50); }" \
    "button.section-color-3 { background-color: rgba(199,79,181,0.35); }" \
    "button.section-color-3:hover { background-color: rgba(199,79,181,0.50); }" \
    "button.section-color-4 { background-color: rgba(219,199,61,0.35); }" \
    "button.section-color-4:hover { background-color: rgba(219,199,61,0.50); }" \
    "button.section-color-5 { background-color: rgba(120,79,199,0.35); }" \
    "button.section-color-5:hover { background-color: rgba(120,79,199,0.50); }" \
    "button.section-color-6 { background-color: rgba(61,199,199,0.35); }" \
    "button.section-color-6:hover { background-color: rgba(61,199,199,0.50); }" \
    "button.section-color-7 { background-color: rgba(199,100,100,0.35); }" \
    "button.section-color-7:hover { background-color: rgba(199,100,100,0.50); }" \
    "button.section-color-0.active, button.section-color-1.active," \
    "button.section-color-2.active, button.section-color-3.active," \
    "button.section-color-4.active, button.section-color-5.active," \
    "button.section-color-6.active, button.section-color-7.active {" \
    "  border: 2px solid rgba(255,255,255,0.6); }" \
    "label.status-flash { color: #fff; font-weight: bold; font-size: 12px; }"

/* ─── Theme CSS strings ───────────────────────────────────────────────────── */

static const char *THEME_CSS[] = {
    /* 0: DARK */
    "window, box, button, label, scale, scrolledwindow, separator, checkbutton, dropdown, entry, popover, popover > contents {"
    "  background-color: #24242a;"
    "  color: #d0d0d0;"
    CSS_COMMON
    "button { background-color: #38383e; color: #d0d0d0;"
    "  padding: 4px 8px; }"
    "button:hover { background-color: #45454c; }"
    "button:active, button.active { background-color: #6499cc44; color: #fff; }"
    "dropdown { background-color: #30303a; color: #d0d0d0; }"
    "popover, popover > contents { background-color: #2a2a32; color: #d0d0d0; }"
    "scale trough { background-color: #2a2a30; min-height: 6px; }"
    "scale highlight { background-color: #6499cc; }"
    "scale slider { background-color: #6499cc; min-width: 16px; min-height: 16px;"
    "  border-radius: 8px; margin: 0; padding: 0; }"
    "label.status { color: #6499cc; font-size: 11px; }"
    "drawingarea { background-color: #1a1a20; }"
    "separator { background-color: #40404a; min-height: 1px; }",

    /* 1: LIGHT */
    "window, box, button, label, scale, scrolledwindow, separator, checkbutton, dropdown, entry, popover, popover > contents {"
    "  background-color: #c2c2c7;"
    "  color: #262630;"
    CSS_COMMON
    "button { background-color: #a6a6ad; color: #262630; padding: 4px 8px; }"
    "button:hover { background-color: #b0b0b8; }"
    "button:active, button.active { background-color: #3373b344; }"
    "dropdown { background-color: #b0b0b8; color: #262630; }"
    "popover, popover > contents { background-color: #c8c8cd; color: #262630; }"
    "scale trough { background-color: #aaa; min-height: 6px; }"
    "scale highlight { background-color: #3373b3; }"
    "scale slider { background-color: #3373b3; min-width: 16px; min-height: 16px;"
    "  border-radius: 8px; margin: 0; padding: 0; }"
    "label.status { color: #555; font-size: 11px; }"
    "drawingarea { background-color: #b8b8bd; }"
    "separator { background-color: #999; min-height: 1px; }",

    /* 2: HACKER */
    "window, box, button, label, scale, scrolledwindow, separator, checkbutton, dropdown, entry, popover, popover > contents {"
    "  background-color: #0a0f0a;"
    "  color: #00aa38;"
    CSS_COMMON
    "button { background-color: #0f1a0f; color: #00aa38; padding: 4px 8px; }"
    "button:hover { background-color: #1a2a1a; }"
    "button:active, button.active { background-color: #00aa3833; }"
    "dropdown { background-color: #0f180f; color: #00aa38; }"
    "popover, popover > contents { background-color: #0f1a0f; color: #00aa38; }"
    "scale trough { background-color: #0f1a0f; min-height: 6px; }"
    "scale highlight { background-color: #00aa38; }"
    "scale slider { background-color: #00aa38; min-width: 16px; min-height: 16px;"
    "  border-radius: 8px; margin: 0; padding: 0; }"
    "label.status { color: #00aa38aa; font-size: 11px; }"
    "drawingarea { background-color: #060a06; }"
    "separator { background-color: #00aa3844; min-height: 1px; }",

    /* 3: MIDNIGHT */
    "window, box, button, label, scale, scrolledwindow, separator, checkbutton, dropdown, entry, popover, popover > contents {"
    "  background-color: #0d0a17;"
    "  color: #ccc8e0;"
    CSS_COMMON
    "button { background-color: #1a1528; color: #ccc8e0; padding: 4px 8px; }"
    "button:hover { background-color: #252035; }"
    "button:active, button.active { background-color: #33b3e644; }"
    "dropdown { background-color: #161220; color: #ccc8e0; }"
    "popover, popover > contents { background-color: #1a1528; color: #ccc8e0; }"
    "scale trough { background-color: #1a1528; min-height: 6px; }"
    "scale highlight { background-color: #33b3e6; }"
    "scale slider { background-color: #33b3e6; min-width: 16px; min-height: 16px;"
    "  border-radius: 8px; margin: 0; padding: 0; }"
    "label.status { color: #33b3e6aa; font-size: 11px; }"
    "drawingarea { background-color: #08060f; }"
    "separator { background-color: #33b3e644; min-height: 1px; }",

    /* 4: AMBER */
    "window, box, button, label, scale, scrolledwindow, separator, checkbutton, dropdown, entry, popover, popover > contents {"
    "  background-color: #0f0a05;"
    "  color: #e6cc8c;"
    CSS_COMMON
    "button { background-color: #1a1208; color: #e6cc8c; padding: 4px 8px; }"
    "button:hover { background-color: #2a1e10; }"
    "button:active, button.active { background-color: #cc8c1a33; }"
    "dropdown { background-color: #161008; color: #e6cc8c; }"
    "popover, popover > contents { background-color: #1a1208; color: #e6cc8c; }"
    "scale trough { background-color: #1a1208; min-height: 6px; }"
    "scale highlight { background-color: #cc8c1a; }"
    "scale slider { background-color: #cc8c1a; min-width: 16px; min-height: 16px;"
    "  border-radius: 8px; margin: 0; padding: 0; }"
    "label.status { color: #cc8c1aaa; font-size: 11px; }"
    "drawingarea { background-color: #0a0804; }"
    "separator { background-color: #cc8c1a44; min-height: 1px; }",

    /* 5: VAPORWAVE — pink/cyan/purple synthwave aesthetic */
    "window, box, button, label, scale, scrolledwindow, separator, checkbutton, dropdown, entry, popover, popover > contents {"
    "  background-color: #140a1f;"
    "  color: #d9bff2;"
    CSS_COMMON
    "button { background-color: #331452; color: #d9bff2; padding: 4px 8px; }"
    "button:hover { background-color: #4d1f73; }"
    "button:active, button.active { background-color: #662e9944; }"
    "dropdown { background-color: #261045; color: #d9bff2; }"
    "popover, popover > contents { background-color: #1f0d33; color: #d9bff2; }"
    "scale trough { background-color: #1f0d33; min-height: 6px; }"
    "scale highlight { background-color: #f266b3; }"
    "scale slider { background-color: #f266b3; min-width: 16px; min-height: 16px;"
    "  border-radius: 8px; margin: 0; padding: 0; }"
    "label.status { color: #4dd9f2; font-size: 11px; }"
    "drawingarea { background-color: #0a0510; }"
    "separator { background-color: #59268c44; min-height: 1px; }",

    /* 6: NEON — hot pink/magenta on dark, cyan accents */
    "window, box, button, label, scale, scrolledwindow, separator, checkbutton, dropdown, entry, popover, popover > contents {"
    "  background-color: #0f0a1a;"
    "  color: #f24de6;"
    CSS_COMMON
    "button { background-color: #261440; color: #f24de6; padding: 4px 8px; }"
    "button:hover { background-color: #401f66; }"
    "button:active, button.active { background-color: #592e8c44; }"
    "dropdown { background-color: #1f0d33; color: #f24de6; }"
    "popover, popover > contents { background-color: #1a0d2e; color: #f24de6; }"
    "scale trough { background-color: #1a0d2e; min-height: 6px; }"
    "scale highlight { background-color: #e633cc; }"
    "scale slider { background-color: #e633cc; min-width: 16px; min-height: 16px;"
    "  border-radius: 8px; margin: 0; padding: 0; }"
    "label.status { color: #00e6f2; font-size: 11px; }"
    "drawingarea { background-color: #05050d; }"
    "separator { background-color: #4d268044; min-height: 1px; }",
};

static const char *THEME_NAMES[] = {
    "Dark", "Light", "Hacker", "Midnight", "Amber", "Vaporwave", "Neon"
};
#define NUM_THEMES 7

static int s_current_theme = 0;
static int s_active_user_theme = -1;  /* -1 = built-in theme active */
static GtkCssProvider *s_provider = NULL;

/* ─── User theme storage ─────────────────────────────────────────────────── */

#define GTK_MAX_USER_THEMES 16

typedef struct {
    char name[64];
    char background[8];     /* #rrggbb */
    char text[8];
    char accent[8];
    char button[8];
    char button_hover[8];
    char button_active[8];
    char popup_bg[8];
    char draw_bg[8];
    char separator[8];
    char status_color[8];
    int  loaded;
} gtk_user_theme_t;

static gtk_user_theme_t s_user_themes[GTK_MAX_USER_THEMES];
static int              s_num_user_themes = 0;

/* ─── Hex color helpers ──────────────────────────────────────────────────── */

/* Parse "#rrggbb" into r, g, b bytes (0-255). Returns 0 on success. */
static int parse_hex_color(const char *hex, int *r, int *g, int *b)
{
    if (!hex || hex[0] != '#' || strlen(hex) < 7) return -1;
    unsigned int val = 0;
    if (sscanf(hex + 1, "%06x", &val) != 1) return -1;
    *r = (val >> 16) & 0xFF;
    *g = (val >> 8) & 0xFF;
    *b = val & 0xFF;
    return 0;
}

/* Blend a color toward black (darken) or white (lighten) by a factor.
   factor < 0 darkens, factor > 0 lightens.  Writes "#rrggbb" into out. */
static void adjust_color(const char *hex, double factor, char out[8])
{
    int r, g, b;
    if (parse_hex_color(hex, &r, &g, &b) != 0) {
        snprintf(out, 8, "%s", hex);
        return;
    }
    if (factor > 0) {
        r = r + (int)((255 - r) * factor);
        g = g + (int)((255 - g) * factor);
        b = b + (int)((255 - b) * factor);
    } else {
        double f = 1.0 + factor; /* factor is negative */
        r = (int)(r * f);
        g = (int)(g * f);
        b = (int)(b * f);
    }
    if (r < 0) r = 0; if (r > 255) r = 255;
    if (g < 0) g = 0; if (g > 255) g = 255;
    if (b < 0) b = 0; if (b > 255) b = 255;
    snprintf(out, 8, "#%02x%02x%02x", r, g, b);
}

/* Convert float [0..1] RGB to "#rrggbb" */
static void float_to_hex(float rf, float gf, float bf, char out[8])
{
    int r = (int)(rf * 255.0f + 0.5f);
    int g = (int)(gf * 255.0f + 0.5f);
    int b = (int)(bf * 255.0f + 0.5f);
    if (r < 0) r = 0; if (r > 255) r = 255;
    if (g < 0) g = 0; if (g > 255) g = 255;
    if (b < 0) b = 0; if (b > 255) b = 255;
    snprintf(out, 8, "#%02x%02x%02x", r, g, b);
}

/* Generate full CSS string from a user theme's color values */
static char *gtk_user_theme_to_css(const gtk_user_theme_t *t)
{
    return g_strdup_printf(
        "window, box, button, label, scale, scrolledwindow, separator,"
        " checkbutton, dropdown, entry, popover, popover > contents {"
        "  background-color: %s;"
        "  color: %s;"
        "  font-family: 'DejaVu Sans Mono', monospace;"
        "  font-size: 13px;"
        "}"
        "button { background-color: %s; color: %s; padding: 4px 8px; }"
        "button:hover { background-color: %s; }"
        "button:active, button.active { background-color: %s; color: #fff; }"
        "dropdown { background-color: %s; color: %s; }"
        "popover, popover > contents { background-color: %s; color: %s; }"
        "scale trough { background-color: %s; min-height: 6px; }"
        "scale highlight { background-color: %s; }"
        "scale slider { background-color: %s; min-width: 16px; min-height: 16px;"
        "  border-radius: 8px; margin: 0; padding: 0; }"
        "label.status { color: %s; font-size: 11px; }"
        "drawingarea { background-color: %s; }"
        "separator { background-color: %s; min-height: 1px; }",
        t->background, t->text,
        t->button, t->text,
        t->button_hover,
        t->button_active,
        t->popup_bg, t->text,
        t->popup_bg, t->text,
        t->popup_bg,
        t->accent,
        t->accent,
        t->status_color,
        t->draw_bg,
        t->separator
    );
}

/* ─── CSS application (built-in or user) ─────────────────────────────────── */

static void apply_css_string(GtkWidget *widget, const char *css)
{
    GdkDisplay *display = gtk_widget_get_display(widget);

    if (s_provider) {
        gtk_style_context_remove_provider_for_display(display,
            GTK_STYLE_PROVIDER(s_provider));
        g_object_unref(s_provider);
    }

    char *full_css = g_strconcat(CSS_THIN_CONTROLS, css, NULL);

    s_provider = gtk_css_provider_new();
    gtk_css_provider_load_from_string(s_provider, full_css);
    g_free(full_css);
    gtk_style_context_add_provider_for_display(display,
        GTK_STYLE_PROVIDER(s_provider),
        GTK_STYLE_PROVIDER_PRIORITY_USER);
}

static void apply_theme_css(GtkWidget *widget, int theme_idx)
{
    if (theme_idx < 0 || theme_idx >= NUM_THEMES) theme_idx = 0;
    s_current_theme = theme_idx;
    s_active_user_theme = -1;

    apply_css_string(widget, THEME_CSS[theme_idx]);
}

/* ─── JSON theme loading ─────────────────────────────────────────────────── */

/* Get a string value from JSON, or NULL */
static const char *json_get_str(const cJSON *root, const char *key)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key);
    if (cJSON_IsString(item) && item->valuestring)
        return item->valuestring;
    return NULL;
}

/* Get a float value from JSON, or the default */
static double json_get_num(const cJSON *root, const char *key, double def)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key);
    if (cJSON_IsNumber(item))
        return item->valuedouble;
    return def;
}

/* Copy a hex color string if valid, otherwise keep the default */
static void copy_hex_if_valid(const char *src, char dst[8])
{
    if (src && src[0] == '#' && strlen(src) >= 7) {
        snprintf(dst, 8, "%.7s", src);
    }
}

/* Check if a user theme name conflicts with a built-in theme */
static int theme_name_conflicts_builtin(const char *name)
{
    for (int i = 0; i < NUM_THEMES; i++) {
        if (strcasecmp(name, THEME_NAMES[i]) == 0)
            return 1;
    }
    return 0;
}

/* Load a single JSON theme file. Returns 1 on success. */
static int load_json_theme(const char *path, gtk_user_theme_t *t)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        LOG_WARN("Could not open theme file: %s", path);
        return 0;
    }

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (sz <= 0 || sz > 64 * 1024) {
        LOG_WARN("Theme file too large or empty: %s (%ld bytes)", path, sz);
        fclose(f);
        return 0;
    }

    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return 0; }
    fread(buf, 1, (size_t)sz, f);
    buf[sz] = '\0';
    fclose(f);

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) {
        LOG_WARN("Failed to parse theme JSON: %s", path);
        return 0;
    }

    /* Set defaults (dark theme colors) */
    memset(t, 0, sizeof(*t));
    snprintf(t->background,   8, "#24242a");
    snprintf(t->text,         8, "#d0d0d0");
    snprintf(t->accent,       8, "#6499cc");
    snprintf(t->button,       8, "#38383e");
    snprintf(t->button_hover, 8, "#45454c");
    snprintf(t->button_active,8, "#6499cc");
    snprintf(t->popup_bg,     8, "#2a2a32");
    snprintf(t->draw_bg,      8, "#1a1a20");
    snprintf(t->separator,    8, "#40404a");
    snprintf(t->status_color, 8, "#6499cc");

    /* Read "name" */
    const char *name = json_get_str(root, "name");
    if (name) {
        snprintf(t->name, sizeof(t->name), "%s", name);
    } else {
        /* Derive from filename */
        const char *slash = strrchr(path, '/');
        if (!slash) slash = strrchr(path, '\\');
        const char *base = slash ? slash + 1 : path;
        snprintf(t->name, sizeof(t->name), "%s", base);
        char *dot = strrchr(t->name, '.');
        if (dot) *dot = '\0';
    }

    /* --- Read hex color strings (simple format) --- */
    copy_hex_if_valid(json_get_str(root, "background"), t->background);
    copy_hex_if_valid(json_get_str(root, "text"),       t->text);
    copy_hex_if_valid(json_get_str(root, "accent"),     t->accent);
    copy_hex_if_valid(json_get_str(root, "button"),     t->button);
    copy_hex_if_valid(json_get_str(root, "button_hover"), t->button_hover);
    copy_hex_if_valid(json_get_str(root, "button_active"), t->button_active);
    copy_hex_if_valid(json_get_str(root, "popup_bg"),   t->popup_bg);
    copy_hex_if_valid(json_get_str(root, "draw_bg"),    t->draw_bg);
    copy_hex_if_valid(json_get_str(root, "separator"),  t->separator);
    copy_hex_if_valid(json_get_str(root, "status"),     t->status_color);

    /* --- Read ImGui float format (window_bg_r/g/b, text_r/g/b, etc.) --- */
    const cJSON *wbg_r = cJSON_GetObjectItemCaseSensitive(root, "window_bg_r");
    if (cJSON_IsNumber(wbg_r)) {
        float_to_hex(
            (float)json_get_num(root, "window_bg_r", 0.14),
            (float)json_get_num(root, "window_bg_g", 0.14),
            (float)json_get_num(root, "window_bg_b", 0.15),
            t->background);
    }
    const cJSON *txt_r = cJSON_GetObjectItemCaseSensitive(root, "text_r");
    if (cJSON_IsNumber(txt_r)) {
        float_to_hex(
            (float)json_get_num(root, "text_r", 0.82),
            (float)json_get_num(root, "text_g", 0.82),
            (float)json_get_num(root, "text_b", 0.82),
            t->text);
    }
    const cJSON *acc_r = cJSON_GetObjectItemCaseSensitive(root, "accent_r");
    if (cJSON_IsNumber(acc_r)) {
        float_to_hex(
            (float)json_get_num(root, "accent_r", 0.39),
            (float)json_get_num(root, "accent_g", 0.71),
            (float)json_get_num(root, "accent_b", 1.0),
            t->accent);
    }
    const cJSON *btn_r = cJSON_GetObjectItemCaseSensitive(root, "button_r");
    if (cJSON_IsNumber(btn_r)) {
        float_to_hex(
            (float)json_get_num(root, "button_r", 0.22),
            (float)json_get_num(root, "button_g", 0.22),
            (float)json_get_num(root, "button_b", 0.24),
            t->button);
    }
    const cJSON *btn_h_r = cJSON_GetObjectItemCaseSensitive(root, "button_hover_r");
    if (cJSON_IsNumber(btn_h_r)) {
        float_to_hex(
            (float)json_get_num(root, "button_hover_r", 0.27),
            (float)json_get_num(root, "button_hover_g", 0.27),
            (float)json_get_num(root, "button_hover_b", 0.29),
            t->button_hover);
    }
    const cJSON *btn_a_r = cJSON_GetObjectItemCaseSensitive(root, "button_active_r");
    if (cJSON_IsNumber(btn_a_r)) {
        float_to_hex(
            (float)json_get_num(root, "button_active_r", 0.31),
            (float)json_get_num(root, "button_active_g", 0.31),
            (float)json_get_num(root, "button_active_b", 0.33),
            t->button_active);
    }
    const cJSON *pop_r = cJSON_GetObjectItemCaseSensitive(root, "popup_bg_r");
    if (cJSON_IsNumber(pop_r)) {
        float_to_hex(
            (float)json_get_num(root, "popup_bg_r", 0.16),
            (float)json_get_num(root, "popup_bg_g", 0.16),
            (float)json_get_num(root, "popup_bg_b", 0.17),
            t->popup_bg);
    }

    /* Auto-derive missing colors from the ones we have:
       If button wasn't explicitly set, derive from background.
       If button_hover/active weren't set, derive from button. */
    if (!json_get_str(root, "button") && !cJSON_IsNumber(btn_r)) {
        adjust_color(t->background, 0.15, t->button);
    }
    if (!json_get_str(root, "button_hover") && !cJSON_IsNumber(btn_h_r)) {
        adjust_color(t->button, 0.15, t->button_hover);
    }
    if (!json_get_str(root, "button_active") && !cJSON_IsNumber(btn_a_r)) {
        /* Use accent with some transparency effect approximation */
        snprintf(t->button_active, 8, "%s", t->accent);
    }
    if (!json_get_str(root, "popup_bg") && !cJSON_IsNumber(pop_r)) {
        adjust_color(t->background, 0.08, t->popup_bg);
    }
    if (!json_get_str(root, "draw_bg")) {
        adjust_color(t->background, -0.15, t->draw_bg);
    }
    if (!json_get_str(root, "separator")) {
        adjust_color(t->background, 0.25, t->separator);
    }
    if (!json_get_str(root, "status")) {
        snprintf(t->status_color, 8, "%s", t->accent);
    }

    cJSON_Delete(root);
    t->loaded = 1;
    return 1;
}

void gtk_theme_scan_user_themes(const char *dir)
{
    if (!dir || !dir[0]) return;

    s_num_user_themes = 0;
    LOG_INFO("Scanning for user themes in: %s", dir);

    DIR *d = opendir(dir);
    if (!d) {
        LOG_INFO("No themes directory: %s", dir);
        return;
    }

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (s_num_user_themes >= GTK_MAX_USER_THEMES) break;
        const char *name = ent->d_name;
        size_t len = strlen(name);
        if (len < 6) continue;
        if (strcmp(name + len - 5, ".json") != 0) continue;

        char full[600];
        snprintf(full, sizeof(full), "%s/%s", dir, name);

        if (load_json_theme(full, &s_user_themes[s_num_user_themes])) {
            if (theme_name_conflicts_builtin(s_user_themes[s_num_user_themes].name)) {
                LOG_INFO("Skipping user theme '%s' -- conflicts with built-in",
                         s_user_themes[s_num_user_themes].name);
                s_user_themes[s_num_user_themes].loaded = 0;
            } else {
                LOG_INFO("Loaded user theme: %s (%s)",
                         s_user_themes[s_num_user_themes].name, full);
                s_num_user_themes++;
            }
        }
    }
    closedir(d);

    LOG_INFO("Found %d user theme(s)", s_num_user_themes);
}

int gtk_theme_num_user_themes(void)
{
    return s_num_user_themes;
}

const char *gtk_theme_user_name(int index)
{
    if (index < 0 || index >= s_num_user_themes) return "?";
    return s_user_themes[index].name;
}

void gtk_theme_apply_user(GtkWidget *widget, int index)
{
    if (index < 0 || index >= s_num_user_themes) return;
    const gtk_user_theme_t *t = &s_user_themes[index];
    if (!t->loaded) return;

    s_active_user_theme = index;

    char *css = gtk_user_theme_to_css(t);
    apply_css_string(widget, css);
    g_free(css);

    char msg[64];
    snprintf(msg, sizeof(msg), "Theme: %s", t->name);
    sq_app_set_status(&g_gtk.app, msg, 90);

    LOG_INFO("Applied user theme: %s", t->name);
}

int gtk_theme_is_user_active(void)
{
    return s_active_user_theme >= 0;
}

int gtk_theme_active_user_index(void)
{
    return s_active_user_theme;
}

/* ─── Existing built-in theme API ────────────────────────────────────────── */

/* Recursively add "flat" CSS class to all buttons in the widget tree */
static void flatten_buttons_recursive(GtkWidget *widget)
{
    if (!widget) return;

    if (GTK_IS_BUTTON(widget)) {
        gtk_widget_add_css_class(widget, "flat");
    }

    /* Recurse into children */
    GtkWidget *child = gtk_widget_get_first_child(widget);
    while (child) {
        flatten_buttons_recursive(child);
        child = gtk_widget_get_next_sibling(child);
    }
}

void gtk_theme_flatten_buttons(GtkWidget *root)
{
    flatten_buttons_recursive(root);
}

void gtk_theme_apply_dark(GtkWidget *widget)
{
    apply_theme_css(widget, 0);
}

void gtk_theme_apply_light(GtkWidget *widget)
{
    apply_theme_css(widget, 1);
}

void gtk_theme_cycle(GtkWidget *widget)
{
    /* Cycle through built-in themes, then user themes */
    int total = NUM_THEMES + s_num_user_themes;
    int cur;
    if (s_active_user_theme >= 0)
        cur = NUM_THEMES + s_active_user_theme;
    else
        cur = s_current_theme;

    int next = (cur + 1) % total;

    if (next < NUM_THEMES) {
        apply_theme_css(widget, next);
        char msg[32];
        snprintf(msg, sizeof(msg), "Theme: %s", THEME_NAMES[next]);
        sq_app_set_status(&g_gtk.app, msg, 90);
    } else {
        gtk_theme_apply_user(widget, next - NUM_THEMES);
    }
}

int gtk_theme_current(void)
{
    return s_current_theme;
}

const char *gtk_theme_current_name(void)
{
    if (s_active_user_theme >= 0 && s_active_user_theme < s_num_user_themes)
        return s_user_themes[s_active_user_theme].name;
    return THEME_NAMES[s_current_theme];
}

int gtk_theme_count(void)
{
    return NUM_THEMES;
}

const char *gtk_theme_name(int index)
{
    if (index < 0 || index >= NUM_THEMES) return "?";
    return THEME_NAMES[index];
}

void gtk_theme_apply_index(GtkWidget *widget, int index)
{
    apply_theme_css(widget, index);

    char msg[32];
    snprintf(msg, sizeof(msg), "Theme: %s", THEME_NAMES[index]);
    sq_app_set_status(&g_gtk.app, msg, 90);
}

/* ─── Theme-aware accent color for Cairo drawing areas ────────────────────── */

/* Accent colors for each built-in theme (index matches THEME_CSS order) */
static const struct { double r, g, b; } s_accent_colors[NUM_THEMES] = {
    { 0.392, 0.600, 0.800 },  /* 0 Dark:      #6499cc - blue           */
    { 0.200, 0.451, 0.702 },  /* 1 Light:     #3373b3 - blue           */
    { 0.000, 1.000, 0.000 },  /* 2 Hacker:    #00ff00 - green          */
    { 0.600, 0.400, 0.800 },  /* 3 Midnight:  #9966cc - purple         */
    { 0.800, 0.549, 0.102 },  /* 4 Amber:     #cc8c1a - orange/amber   */
    { 0.800, 0.200, 0.800 },  /* 5 Vaporwave: #cc33cc - pink/magenta   */
    { 0.200, 0.800, 1.000 },  /* 6 Neon:      #33ccff - cyan           */
};

void gtk_theme_get_accent_color(double *r, double *g, double *b)
{
    /* If a user theme is active, parse its accent hex color */
    if (s_active_user_theme >= 0 && s_active_user_theme < s_num_user_themes) {
        int ri, gi, bi;
        if (parse_hex_color(s_user_themes[s_active_user_theme].accent,
                            &ri, &gi, &bi) == 0) {
            *r = ri / 255.0;
            *g = gi / 255.0;
            *b = bi / 255.0;
            return;
        }
    }

    /* Built-in theme */
    int idx = s_current_theme;
    if (idx < 0 || idx >= NUM_THEMES) idx = 0;
    *r = s_accent_colors[idx].r;
    *g = s_accent_colors[idx].g;
    *b = s_accent_colors[idx].b;
}

/* ─── Flat button: GtkLabel + GtkGestureClick, no GtkButton chrome ────────── */

static void sq_flat_btn_click(GtkGestureClick *gesture, int n_press,
                               double x, double y, gpointer user_data)
{
    (void)gesture; (void)n_press; (void)x; (void)y;
    GCallback cb = (GCallback)g_object_get_data(G_OBJECT(user_data), "sq-callback");
    gpointer data = g_object_get_data(G_OBJECT(user_data), "sq-data");
    if (cb) {
        typedef void (*btn_cb)(GtkWidget*, gpointer);
        ((btn_cb)cb)(GTK_WIDGET(user_data), data);
    }
}

GtkWidget *sq_flat_button_new(const char *label, GCallback callback, gpointer data)
{
    GtkWidget *lbl = gtk_label_new(label);
    gtk_widget_add_css_class(lbl, "sq-btn");
    gtk_widget_set_cursor_from_name(lbl, "pointer");

    g_object_set_data(G_OBJECT(lbl), "sq-callback", (gpointer)callback);
    g_object_set_data(G_OBJECT(lbl), "sq-data", data);

    GtkGesture *click = gtk_gesture_click_new();
    g_signal_connect(click, "pressed", G_CALLBACK(sq_flat_btn_click), lbl);
    gtk_widget_add_controller(lbl, GTK_EVENT_CONTROLLER(click));

    return lbl;
}
