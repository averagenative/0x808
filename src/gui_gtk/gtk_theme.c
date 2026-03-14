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
static GtkCssProvider *s_provider = NULL;

static void apply_theme_css(GtkWidget *widget, int theme_idx)
{
    if (theme_idx < 0 || theme_idx >= NUM_THEMES) theme_idx = 0;
    s_current_theme = theme_idx;

    GdkDisplay *display = gtk_widget_get_display(widget);

    /* Remove old provider */
    if (s_provider) {
        gtk_style_context_remove_provider_for_display(display,
            GTK_STYLE_PROVIDER(s_provider));
        g_object_unref(s_provider);
    }

    /* Reset first (nuke Adwaita defaults), then apply theme on top */
    char *full_css = g_strconcat(CSS_THIN_CONTROLS, THEME_CSS[theme_idx], NULL);

    s_provider = gtk_css_provider_new();
    gtk_css_provider_load_from_string(s_provider, full_css);
    g_free(full_css);
    gtk_style_context_add_provider_for_display(display,
        GTK_STYLE_PROVIDER(s_provider),
        GTK_STYLE_PROVIDER_PRIORITY_USER);
}

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
    int next = (s_current_theme + 1) % NUM_THEMES;
    apply_theme_css(widget, next);

    char msg[32];
    snprintf(msg, sizeof(msg), "Theme: %s", THEME_NAMES[next]);
    sq_app_set_status(&g_gtk.app, msg, 90);
}

int gtk_theme_current(void)
{
    return s_current_theme;
}

const char *gtk_theme_current_name(void)
{
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
