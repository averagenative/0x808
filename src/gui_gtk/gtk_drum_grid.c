/*
 * gtk_drum_grid.c — Drum grid rendered with Cairo on a GtkDrawingArea.
 *
 * Layout: [track controls 200px | step grid]
 * Features: click toggle, drag paint, beat shading, velocity display,
 *           mute/solo buttons, synth separator, playhead highlight.
 */

#include "gtk_gui.h"
#include "gui/undo.h"
#include "engine/kits.h"
#include <math.h>
#include <string.h>
#include <stdio.h>

/* Track colors (matching ImGui frontend) */
static const double TRACK_COLORS[][3] = {
    {0.86, 0.31, 0.31},  /* red */
    {0.31, 0.71, 0.86},  /* blue */
    {0.31, 0.78, 0.47},  /* green */
    {0.86, 0.71, 0.24},  /* yellow */
    {0.71, 0.39, 0.86},  /* purple */
    {0.86, 0.55, 0.24},  /* orange */
    {0.39, 0.78, 0.78},  /* cyan */
    {0.78, 0.47, 0.63},  /* pink */
};
#define NUM_TRACK_COLORS 8

/* Layout constants */
#define CTRL_W   200.0
#define CELL_PAD 1.0
#define HEADER_H 22.0
#define MIN_CELL_H 28.0

/* Drag state */
static int  s_drag_track = -1;
static int  s_drag_step  = -1;
static int  s_drag_val   = -1;  /* 0=erasing, 100=painting */
static bool s_undo_pushed = false;

/* Scroll offset for vertical track scrolling */
static double s_scroll_offset = 0.0;

/* Hover state for cell highlight */
static int  s_hover_track = -1;
static int  s_hover_step  = -1;
static double s_hover_x = -1.0;
static double s_hover_y = -1.0;

/* Right-click popover state */
static GtkWidget *s_popover      = NULL;
static int        s_popup_track  = -1;
static int        s_popup_step   = -1;
static GtkWidget *s_vel_spin     = NULL;
static GtkWidget *s_pitch_spin   = NULL;

/* Clipboard for copy/paste */
static bool    s_clipboard_valid = false;
static uint8_t s_clipboard_vel   = 100;
static int8_t  s_clipboard_pitch = 0;

/* ─── Helper: get grid geometry ───────────────────────────────────────────── */

typedef struct {
    double grid_x, grid_y, grid_w, grid_h;
    double cell_w, cell_h;
    uint32_t max_steps;
    uint32_t num_tracks;
} grid_geom_t;

static bool get_geom(int width, int height, grid_geom_t *g)
{
    sq_engine_t *engine = g_gtk.engine;
    if (!engine) return false;
    int pi = engine->transport.current_pattern;
    if (pi < 0 || (uint32_t)pi >= engine->num_patterns) return false;
    sq_pattern_t *pat = &engine->patterns[pi];
    if (pat->num_tracks == 0) return false;

    g->grid_x = CTRL_W;
    g->grid_w = (double)width - CTRL_W;
    g->grid_y = HEADER_H;
    g->grid_h = (double)height - HEADER_H;
    g->num_tracks = pat->num_tracks;

    g->max_steps = pat->tracks[0].length;
    for (uint32_t t = 1; t < pat->num_tracks; t++) {
        if (pat->tracks[t].length > g->max_steps)
            g->max_steps = pat->tracks[t].length;
    }
    if (g->max_steps == 0) g->max_steps = 16;

    g->cell_w = g->grid_w / (double)g->max_steps;
    g->cell_h = g->grid_h / (double)g->num_tracks;
    if (g->cell_h < MIN_CELL_H) g->cell_h = MIN_CELL_H;

    /* Clamp scroll offset */
    double max_scroll = g->cell_h * g->num_tracks - g->grid_h;
    if (max_scroll < 0) max_scroll = 0;
    if (s_scroll_offset > max_scroll) s_scroll_offset = max_scroll;
    if (s_scroll_offset < 0) s_scroll_offset = 0;

    return true;
}

/* ─── Drawing ─────────────────────────────────────────────────────────────── */

static void on_draw(GtkDrawingArea *area, cairo_t *cr,
                    int width, int height, gpointer user_data)
{
    (void)area; (void)user_data;
    sq_engine_t *engine = g_gtk.engine;
    if (!engine) return;

    grid_geom_t g;
    if (!get_geom(width, height, &g)) return;

    int pi = engine->transport.current_pattern;
    sq_pattern_t *pat = &engine->patterns[pi];

    /* Background */
    cairo_set_source_rgb(cr, 0.04, 0.04, 0.06);
    cairo_rectangle(cr, 0, 0, width, height);
    cairo_fill(cr);

    cairo_select_font_face(cr, "monospace", CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_NORMAL);

    /* Step number headers */
    cairo_set_font_size(cr, 9.0);
    for (uint32_t s = 0; s < g.max_steps; s++) {
        /* Beat-aligned header shading */
        if (s % 4 == 0) {
            cairo_set_source_rgba(cr, 0.5, 0.5, 0.5, 0.9);
        } else {
            cairo_set_source_rgba(cr, 0.35, 0.35, 0.35, 0.7);
        }
        char num[4];
        snprintf(num, sizeof(num), "%d", s + 1);
        double sx = g.grid_x + s * g.cell_w + g.cell_w * 0.25;
        cairo_move_to(cr, sx, HEADER_H - 6);
        cairo_show_text(cr, num);
    }

    /* Track separator: find first synth track */
    int first_synth = -1;
    for (uint32_t t = 0; t < pat->num_tracks; t++) {
        if (pat->tracks[t].type == TRACK_SYNTH) { first_synth = (int)t; break; }
    }

    /* Clip to grid area */
    cairo_save(cr);
    cairo_rectangle(cr, 0, g.grid_y, width, g.grid_h);
    cairo_clip(cr);

    /* Draw tracks */
    for (uint32_t t = 0; t < pat->num_tracks; t++) {
        sq_track_t *track = &pat->tracks[t];
        double ty = g.grid_y + t * g.cell_h - s_scroll_offset;

        /* Skip tracks outside visible area */
        if (ty + g.cell_h < g.grid_y || ty > g.grid_y + g.grid_h) continue;
        int ci = track->color_index % NUM_TRACK_COLORS;

        /* ── Synth separator ──────────────────────────────────────── */
        if ((int)t == first_synth && first_synth > 0) {
            cairo_set_source_rgba(cr, 0.39, 0.71, 1.0, 0.3);
            cairo_set_line_width(cr, 1.0);
            cairo_move_to(cr, 0, ty - 1);
            cairo_line_to(cr, width, ty - 1);
            cairo_stroke(cr);
            cairo_set_source_rgba(cr, 0.39, 0.71, 1.0, 0.5);
            cairo_set_font_size(cr, 8.0);
            cairo_move_to(cr, CTRL_W - 60, ty - 3);
            cairo_show_text(cr, "SYNTH");
        }

        /* ── Selected track highlight ─────────────────────────────── */
        if ((int)t == g_gtk.app.selected_track) {
            cairo_set_source_rgba(cr, 0.2, 1.0, 0.2, 0.08);
            cairo_rectangle(cr, 0, ty, width, g.cell_h);
            cairo_fill(cr);
        }

        /* ── Track controls (left column) ─────────────────────────── */

        /* Track color bar */
        cairo_set_source_rgba(cr, TRACK_COLORS[ci][0], TRACK_COLORS[ci][1],
                              TRACK_COLORS[ci][2], 0.7);
        cairo_rectangle(cr, 0, ty + 2, 3, g.cell_h - 4);
        cairo_fill(cr);

        /* Track name */
        cairo_set_font_size(cr, 11.0);
        if ((int)t == g_gtk.app.selected_track)
            cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 1.0);
        else
            cairo_set_source_rgba(cr, 0.7, 0.7, 0.7, 1.0);

        char label[32];
        if (track->type == TRACK_SAMPLER && track->sample_index >= 0 &&
            (uint32_t)track->sample_index < engine->num_samples) {
            snprintf(label, sizeof(label), "%.12s",
                     engine->samples[track->sample_index].name);
        } else if (track->type == TRACK_SYNTH) {
            int pi2 = track->synth_preset;
            if (pi2 >= 0 && (uint32_t)pi2 < engine->num_synth_presets)
                snprintf(label, sizeof(label), "%.12s",
                         engine->synth_presets[pi2].name);
            else
                snprintf(label, sizeof(label), "Synth %d", pi2);
        } else {
            snprintf(label, sizeof(label), "Track %d", t + 1);
        }
        cairo_move_to(cr, 8, ty + 14);
        cairo_show_text(cr, label);

        /* Type badge (clickable — cycles track type) */
        {
            const char *badge_text;
            double br, bg2, bb;
            if (track->type == TRACK_SYNTH) {
                badge_text = "SYN"; br = 0.71; bg2 = 0.39; bb = 0.86;
            } else if (track->type == TRACK_SF2) {
                badge_text = "SF2"; br = 0.86; bg2 = 0.71; bb = 0.24;
            } else {
                badge_text = "SMP"; br = 0.31; bg2 = 0.78; bb = 0.47;
            }
            double badge_x = 6;
            double badge_y = ty + g.cell_h - 16;
            double badge_w = 28;
            double badge_h = 13;

            /* Badge background */
            cairo_set_source_rgba(cr, br, bg2, bb, 0.25);
            cairo_rectangle(cr, badge_x, badge_y, badge_w, badge_h);
            cairo_fill(cr);
            /* Badge border */
            cairo_set_source_rgba(cr, br, bg2, bb, 0.5);
            cairo_set_line_width(cr, 0.8);
            cairo_rectangle(cr, badge_x, badge_y, badge_w, badge_h);
            cairo_stroke(cr);
            /* Badge text */
            cairo_set_font_size(cr, 8.0);
            cairo_set_source_rgba(cr, br, bg2, bb, 0.9);
            cairo_move_to(cr, badge_x + 3, badge_y + 10);
            cairo_show_text(cr, badge_text);
        }

        /* Mute button */
        double btn_y = ty + 2;
        double btn_x = CTRL_W - 42;
        if (track->mute) {
            cairo_set_source_rgba(cr, 0.7, 0.12, 0.12, 0.9);
            cairo_rectangle(cr, btn_x, btn_y, 18, 14);
            cairo_fill(cr);
            cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
        } else {
            cairo_set_source_rgba(cr, 0.25, 0.25, 0.28, 0.8);
            cairo_rectangle(cr, btn_x, btn_y, 18, 14);
            cairo_fill(cr);
            cairo_set_source_rgba(cr, 0.5, 0.5, 0.5, 0.8);
        }
        cairo_set_font_size(cr, 9.0);
        cairo_move_to(cr, btn_x + 4, btn_y + 11);
        cairo_show_text(cr, "M");

        /* Solo button */
        btn_x = CTRL_W - 22;
        if (track->solo) {
            cairo_set_source_rgba(cr, 0.12, 0.6, 0.12, 0.9);
            cairo_rectangle(cr, btn_x, btn_y, 18, 14);
            cairo_fill(cr);
            cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
        } else {
            cairo_set_source_rgba(cr, 0.25, 0.25, 0.28, 0.8);
            cairo_rectangle(cr, btn_x, btn_y, 18, 14);
            cairo_fill(cr);
            cairo_set_source_rgba(cr, 0.5, 0.5, 0.5, 0.8);
        }
        cairo_move_to(cr, btn_x + 4, btn_y + 11);
        cairo_show_text(cr, "S");

        /* Volume bar */
        double vol_x = CTRL_W - 42;
        double vol_y = ty + 18;
        double vol_w = 38.0;
        double vol_h = 5.0;
        cairo_set_source_rgba(cr, 0.15, 0.15, 0.18, 0.8);
        cairo_rectangle(cr, vol_x, vol_y, vol_w, vol_h);
        cairo_fill(cr);
        cairo_set_source_rgba(cr, TRACK_COLORS[ci][0], TRACK_COLORS[ci][1],
                              TRACK_COLORS[ci][2], 0.6);
        cairo_rectangle(cr, vol_x, vol_y, vol_w * track->volume, vol_h);
        cairo_fill(cr);

        /* ── Step cells ───────────────────────────────────────────── */
        for (uint32_t s = 0; s < track->length; s++) {
            double sx = g.grid_x + s * g.cell_w + CELL_PAD;
            double sy = ty + CELL_PAD;
            double sw = g.cell_w - CELL_PAD * 2;
            double sh = g.cell_h - CELL_PAD * 2;

            uint8_t vel = track->steps[s].velocity;

            if (vel > 0) {
                /* Active step — colored with velocity opacity */
                double alpha = 0.35 + 0.65 * ((double)vel / 127.0);
                double r = TRACK_COLORS[ci][0];
                double gn = TRACK_COLORS[ci][1];
                double b = TRACK_COLORS[ci][2];

                /* Rounded rect fill */
                double rad = 4.0;
                cairo_new_sub_path(cr);
                cairo_arc(cr, sx + sw - rad, sy + rad, rad, -M_PI/2, 0);
                cairo_arc(cr, sx + sw - rad, sy + sh - rad, rad, 0, M_PI/2);
                cairo_arc(cr, sx + rad, sy + sh - rad, rad, M_PI/2, M_PI);
                cairo_arc(cr, sx + rad, sy + rad, rad, M_PI, 3*M_PI/2);
                cairo_close_path(cr);
                cairo_set_source_rgba(cr, r, gn, b, alpha);
                cairo_fill(cr);

                /* Inner glow */
                cairo_set_source_rgba(cr, r * 0.5 + 0.5, gn * 0.5 + 0.5,
                                      b * 0.5 + 0.5, alpha * 0.15);
                cairo_rectangle(cr, sx + 2, sy + 2, sw - 4, sh - 4);
                cairo_fill(cr);

                /* Velocity number */
                if (sw > 16) {
                    cairo_set_source_rgba(cr, 1, 1, 1, 0.7);
                    cairo_set_font_size(cr, 8.0);
                    char vstr[4];
                    snprintf(vstr, sizeof(vstr), "%d", vel);
                    cairo_move_to(cr, sx + 3, sy + sh - 3);
                    cairo_show_text(cr, vstr);
                }

                /* Pitch offset indicator */
                int8_t pitch = track->steps[s].pitch_offset;
                if (pitch != 0 && sw > 14) {
                    if (pitch > 0)
                        cairo_set_source_rgba(cr, 0.4, 0.9, 1.0, 0.7);
                    else
                        cairo_set_source_rgba(cr, 1.0, 0.6, 0.3, 0.7);
                    cairo_set_font_size(cr, 7.0);
                    char pstr[6];
                    snprintf(pstr, sizeof(pstr), "%+d", pitch);
                    cairo_move_to(cr, sx + sw - 16, sy + 9);
                    cairo_show_text(cr, pstr);
                }
            } else {
                /* Inactive step — beat-aligned shading */
                double bg;
                if (s % 8 < 4)
                    bg = (s % 4 == 0) ? 0.10 : 0.07;
                else
                    bg = (s % 4 == 0) ? 0.11 : 0.08;
                cairo_set_source_rgb(cr, bg, bg, bg + 0.015);
                cairo_rectangle(cr, sx, sy, sw, sh);
                cairo_fill(cr);
            }

            /* Cell border */
            cairo_set_source_rgba(cr, 0.18, 0.18, 0.22, 0.4);
            cairo_set_line_width(cr, 0.5);
            cairo_rectangle(cr, sx, sy, sw, sh);
            cairo_stroke(cr);

            /* Hover highlight */
            if ((int)t == s_hover_track && (int)s == s_hover_step) {
                cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.12);
                cairo_rectangle(cr, sx, sy, sw, sh);
                cairo_fill(cr);
            }
        }
    }

    cairo_restore(cr); /* end clip */

    /* Playhead column */
    if (engine->transport.playing) {
        int step = g_gtk.app.visual_step;
        double px = g.grid_x + step * g.cell_w;
        cairo_set_source_rgba(cr, 0.2, 1.0, 0.2, 0.2);
        cairo_rectangle(cr, px, g.grid_y, g.cell_w, g.grid_h);
        cairo_fill(cr);
        /* Playhead line */
        cairo_set_source_rgba(cr, 0.2, 1.0, 0.2, 0.6);
        cairo_set_line_width(cr, 2.0);
        cairo_move_to(cr, px, g.grid_y);
        cairo_line_to(cr, px, g.grid_y + g.grid_h);
        cairo_stroke(cr);
    }

    /* Beat separator lines */
    cairo_set_source_rgba(cr, 0.3, 0.3, 0.35, 0.3);
    cairo_set_line_width(cr, 1.0);
    for (uint32_t s = 4; s < g.max_steps; s += 4) {
        double lx = g.grid_x + s * g.cell_w;
        cairo_move_to(cr, lx, g.grid_y);
        cairo_line_to(cr, lx, g.grid_y + g.grid_h);
        cairo_stroke(cr);
    }

    /* Control column separator */
    cairo_set_source_rgba(cr, 0.25, 0.25, 0.3, 0.5);
    cairo_move_to(cr, CTRL_W - 1, 0);
    cairo_line_to(cr, CTRL_W - 1, height);
    cairo_stroke(cr);
}

/* ─── Click interaction ───────────────────────────────────────────────────── */

static void on_click(GtkGestureClick *gesture, int n_press,
                     double x, double y, gpointer user_data)
{
    (void)gesture; (void)n_press; (void)user_data;
    sq_engine_t *engine = g_gtk.engine;
    if (!engine) return;

    int pi = engine->transport.current_pattern;
    if (pi < 0 || (uint32_t)pi >= engine->num_patterns) return;
    sq_pattern_t *pat = &engine->patterns[pi];
    if (pat->num_tracks == 0) return;

    int w = gtk_widget_get_width(g_gtk.drum_grid_area);
    int h = gtk_widget_get_height(g_gtk.drum_grid_area);
    grid_geom_t g;
    if (!get_geom(w, h, &g)) return;

    /* Track area click — select track or toggle mute/solo or cycle type */
    if (x < CTRL_W && y >= g.grid_y) {
        int track = (int)((y - g.grid_y + s_scroll_offset) / g.cell_h);
        if (track >= 0 && (uint32_t)track < pat->num_tracks) {
            g_gtk.app.selected_track = track;

            /* Check mute/solo button hit */
            double ty = g.grid_y + track * g.cell_h - s_scroll_offset;
            double btn_y = ty + 2;

            /* Mute button: CTRL_W-42 to CTRL_W-24 */
            if (x >= CTRL_W - 42 && x < CTRL_W - 24 &&
                y >= btn_y && y < btn_y + 14) {
                pat->tracks[track].mute = !pat->tracks[track].mute;
            }
            /* Solo button: CTRL_W-22 to CTRL_W-4 */
            if (x >= CTRL_W - 22 && x < CTRL_W - 4 &&
                y >= btn_y && y < btn_y + 14) {
                pat->tracks[track].solo = !pat->tracks[track].solo;
            }

            /* Type badge click: cycle SAMPLER -> SYNTH -> SF2 -> SAMPLER */
            double badge_x = 6;
            double badge_y2 = ty + g.cell_h - 16;
            double badge_w = 28;
            double badge_h = 13;
            if (x >= badge_x && x < badge_x + badge_w &&
                y >= badge_y2 && y < badge_y2 + badge_h) {
                sq_track_t *trk = &pat->tracks[track];
                if (trk->type == TRACK_SAMPLER) {
                    trk->type = TRACK_SYNTH;
                    trk->synth_preset = 0;
                    sq_app_set_status(&g_gtk.app, "Track -> Synth", 60);
                } else if (trk->type == TRACK_SYNTH) {
                    trk->type = TRACK_SF2;
                    if (trk->sf2_preset < 0)
                        trk->sf2_preset = 0;
                    sq_app_set_status(&g_gtk.app, "Track -> SF2", 60);
                } else {
                    trk->type = TRACK_SAMPLER;
                    trk->sample_index = 0;
                    sq_app_set_status(&g_gtk.app, "Track -> Sampler", 60);
                }
            }
        }
        gtk_widget_queue_draw(g_gtk.drum_grid_area);
        return;
    }

    /* Grid area click — toggle step */
    if (x >= g.grid_x && y >= g.grid_y) {
        int step  = (int)((x - g.grid_x) / g.cell_w);
        int track = (int)((y - g.grid_y + s_scroll_offset) / g.cell_h);

        if (step < 0 || (uint32_t)step >= g.max_steps) return;
        if (track < 0 || (uint32_t)track >= pat->num_tracks) return;

        undo_push(engine);
        s_undo_pushed = true;

        uint8_t *vel = &pat->tracks[track].steps[step].velocity;
        s_drag_val = (*vel > 0) ? 0 : 100;
        *vel = (uint8_t)s_drag_val;
        s_drag_track = track;
        s_drag_step = step;

        g_gtk.app.selected_track = track;
        gtk_widget_queue_draw(g_gtk.drum_grid_area);
    }
}

/* ─── Drag interaction (paint multiple steps) ─────────────────────────────── */

static void on_drag_update(GtkGestureDrag *gesture, double dx, double dy,
                           gpointer user_data)
{
    (void)user_data;
    if (s_drag_val < 0) return;

    sq_engine_t *engine = g_gtk.engine;
    if (!engine) return;
    int pi = engine->transport.current_pattern;
    if (pi < 0 || (uint32_t)pi >= engine->num_patterns) return;
    sq_pattern_t *pat = &engine->patterns[pi];

    double start_x, start_y;
    gtk_gesture_drag_get_start_point(gesture, &start_x, &start_y);
    double x = start_x + dx;
    double y = start_y + dy;

    int w = gtk_widget_get_width(g_gtk.drum_grid_area);
    int h = gtk_widget_get_height(g_gtk.drum_grid_area);
    grid_geom_t g;
    if (!get_geom(w, h, &g)) return;

    if (x < g.grid_x) return;

    int step  = (int)((x - g.grid_x) / g.cell_w);
    int track = (int)((y - g.grid_y + s_scroll_offset) / g.cell_h);

    if (step < 0 || (uint32_t)step >= g.max_steps) return;
    if (track < 0 || (uint32_t)track >= pat->num_tracks) return;

    /* Only paint if we moved to a new cell */
    if (step != s_drag_step || track != s_drag_track) {
        if (!s_undo_pushed) {
            undo_push(engine);
            s_undo_pushed = true;
        }
        pat->tracks[track].steps[step].velocity = (uint8_t)s_drag_val;
        s_drag_step = step;
        s_drag_track = track;
        gtk_widget_queue_draw(g_gtk.drum_grid_area);
    }
}

static void on_drag_end(GtkGestureDrag *gesture, double dx, double dy,
                        gpointer user_data)
{
    (void)gesture; (void)dx; (void)dy; (void)user_data;
    s_drag_track = -1;
    s_drag_step = -1;
    s_drag_val = -1;
    s_undo_pushed = false;
}

/* ─── Scroll interaction ───────────────────────────────────────────────────── */

static gboolean on_scroll(GtkEventControllerScroll *ctrl,
                           double dx, double dy, gpointer user_data)
{
    (void)ctrl; (void)dx; (void)user_data;
    s_scroll_offset += dy * 30.0;
    if (s_scroll_offset < 0) s_scroll_offset = 0;
    gtk_widget_queue_draw(g_gtk.drum_grid_area);
    return TRUE;
}

/* ─── Hover interaction ────────────────────────────────────────────────────── */

static void on_motion(GtkEventControllerMotion *ctrl,
                      double x, double y, gpointer user_data)
{
    (void)ctrl; (void)user_data;
    s_hover_x = x;
    s_hover_y = y;

    int old_track = s_hover_track;
    int old_step  = s_hover_step;

    int w = gtk_widget_get_width(g_gtk.drum_grid_area);
    int h = gtk_widget_get_height(g_gtk.drum_grid_area);
    grid_geom_t g;
    if (!get_geom(w, h, &g) || x < g.grid_x || y < g.grid_y) {
        s_hover_track = -1;
        s_hover_step  = -1;
    } else {
        int step  = (int)((x - g.grid_x) / g.cell_w);
        int track = (int)((y - g.grid_y + s_scroll_offset) / g.cell_h);
        if (step >= 0 && (uint32_t)step < g.max_steps &&
            track >= 0 && (uint32_t)track < g.num_tracks) {
            s_hover_track = track;
            s_hover_step  = step;
        } else {
            s_hover_track = -1;
            s_hover_step  = -1;
        }
    }

    if (s_hover_track != old_track || s_hover_step != old_step)
        gtk_widget_queue_draw(g_gtk.drum_grid_area);
}

static void on_motion_leave(GtkEventControllerMotion *ctrl, gpointer user_data)
{
    (void)ctrl; (void)user_data;
    s_hover_track = -1;
    s_hover_step  = -1;
    s_hover_x = -1.0;
    s_hover_y = -1.0;
    gtk_widget_queue_draw(g_gtk.drum_grid_area);
}

/* ─── Right-click context menu callbacks ───────────────────────────────────── */

static void on_vel_changed(GtkSpinButton *spin, gpointer user_data)
{
    (void)user_data;
    sq_engine_t *engine = g_gtk.engine;
    if (!engine || s_popup_track < 0 || s_popup_step < 0) return;

    int pi = engine->transport.current_pattern;
    if (pi < 0 || (uint32_t)pi >= engine->num_patterns) return;
    sq_pattern_t *pat = &engine->patterns[pi];
    if ((uint32_t)s_popup_track >= pat->num_tracks) return;

    int val = gtk_spin_button_get_value_as_int(spin);
    pat->tracks[s_popup_track].steps[s_popup_step].velocity = (uint8_t)val;
    gtk_widget_queue_draw(g_gtk.drum_grid_area);
}

static void on_pitch_changed(GtkSpinButton *spin, gpointer user_data)
{
    (void)user_data;
    sq_engine_t *engine = g_gtk.engine;
    if (!engine || s_popup_track < 0 || s_popup_step < 0) return;

    int pi = engine->transport.current_pattern;
    if (pi < 0 || (uint32_t)pi >= engine->num_patterns) return;
    sq_pattern_t *pat = &engine->patterns[pi];
    if ((uint32_t)s_popup_track >= pat->num_tracks) return;

    int val = gtk_spin_button_get_value_as_int(spin);
    pat->tracks[s_popup_track].steps[s_popup_step].pitch_offset = (int8_t)val;
    gtk_widget_queue_draw(g_gtk.drum_grid_area);
}

static void on_delete_note(GtkWidget *btn, gpointer user_data)
{
    (void)btn; (void)user_data;
    sq_engine_t *engine = g_gtk.engine;
    if (!engine || s_popup_track < 0 || s_popup_step < 0) return;

    int pi = engine->transport.current_pattern;
    if (pi < 0 || (uint32_t)pi >= engine->num_patterns) return;
    sq_pattern_t *pat = &engine->patterns[pi];
    if ((uint32_t)s_popup_track >= pat->num_tracks) return;

    undo_push(engine);
    pat->tracks[s_popup_track].steps[s_popup_step].velocity = 0;
    pat->tracks[s_popup_track].steps[s_popup_step].pitch_offset = 0;
    gtk_widget_queue_draw(g_gtk.drum_grid_area);
    if (s_popover) gtk_popover_popdown(GTK_POPOVER(s_popover));
}

static void on_copy_note(GtkWidget *btn, gpointer user_data)
{
    (void)btn; (void)user_data;
    sq_engine_t *engine = g_gtk.engine;
    if (!engine || s_popup_track < 0 || s_popup_step < 0) return;

    int pi = engine->transport.current_pattern;
    if (pi < 0 || (uint32_t)pi >= engine->num_patterns) return;
    sq_pattern_t *pat = &engine->patterns[pi];
    if ((uint32_t)s_popup_track >= pat->num_tracks) return;

    sq_step_t *step = &pat->tracks[s_popup_track].steps[s_popup_step];
    s_clipboard_vel   = step->velocity;
    s_clipboard_pitch = step->pitch_offset;
    s_clipboard_valid = true;
    sq_app_set_status(&g_gtk.app, "Step copied", 60);
}

static void on_paste_note(GtkWidget *btn, gpointer user_data)
{
    (void)btn; (void)user_data;
    if (!s_clipboard_valid) return;
    sq_engine_t *engine = g_gtk.engine;
    if (!engine || s_popup_track < 0 || s_popup_step < 0) return;

    int pi = engine->transport.current_pattern;
    if (pi < 0 || (uint32_t)pi >= engine->num_patterns) return;
    sq_pattern_t *pat = &engine->patterns[pi];
    if ((uint32_t)s_popup_track >= pat->num_tracks) return;

    undo_push(engine);
    sq_step_t *step = &pat->tracks[s_popup_track].steps[s_popup_step];
    step->velocity     = s_clipboard_vel;
    step->pitch_offset = s_clipboard_pitch;

    /* Update spinners to reflect pasted values */
    if (s_vel_spin)
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(s_vel_spin), s_clipboard_vel);
    if (s_pitch_spin)
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(s_pitch_spin), s_clipboard_pitch);

    gtk_widget_queue_draw(g_gtk.drum_grid_area);
    sq_app_set_status(&g_gtk.app, "Step pasted", 60);
}

static void on_popover_closed(GtkPopover *popover, gpointer user_data)
{
    (void)user_data;
    (void)popover;
    s_popup_track = -1;
    s_popup_step  = -1;
    s_vel_spin    = NULL;
    s_pitch_spin  = NULL;
}

static void show_step_popover(int track, int step, double click_x, double click_y)
{
    sq_engine_t *engine = g_gtk.engine;
    if (!engine) return;

    int pi = engine->transport.current_pattern;
    if (pi < 0 || (uint32_t)pi >= engine->num_patterns) return;
    sq_pattern_t *pat = &engine->patterns[pi];
    if ((uint32_t)track >= pat->num_tracks) return;

    sq_step_t *st = &pat->tracks[track].steps[step];

    /* Push undo before any edits via the popover */
    undo_push(engine);

    s_popup_track = track;
    s_popup_step  = step;

    /* Destroy previous popover if any */
    if (s_popover) {
        gtk_widget_unparent(s_popover);
        s_popover = NULL;
    }

    /* Build popover content */
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_margin_start(box, 10);
    gtk_widget_set_margin_end(box, 10);
    gtk_widget_set_margin_top(box, 8);
    gtk_widget_set_margin_bottom(box, 8);

    /* Header label */
    char header[48];
    snprintf(header, sizeof(header), "Track %d, Step %d", track + 1, step + 1);
    GtkWidget *lbl = gtk_label_new(header);
    PangoAttrList *attrs = pango_attr_list_new();
    pango_attr_list_insert(attrs, pango_attr_weight_new(PANGO_WEIGHT_BOLD));
    gtk_label_set_attributes(GTK_LABEL(lbl), attrs);
    pango_attr_list_unref(attrs);
    gtk_box_append(GTK_BOX(box), lbl);

    /* Velocity row */
    GtkWidget *vel_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_append(GTK_BOX(vel_row), gtk_label_new("Velocity:"));
    GtkAdjustment *vel_adj = gtk_adjustment_new(
        st->velocity, 1, 127, 1, 10, 0);
    s_vel_spin = gtk_spin_button_new(vel_adj, 1, 0);
    gtk_widget_set_hexpand(s_vel_spin, TRUE);
    g_signal_connect(s_vel_spin, "value-changed", G_CALLBACK(on_vel_changed), NULL);
    gtk_box_append(GTK_BOX(vel_row), s_vel_spin);
    gtk_box_append(GTK_BOX(box), vel_row);

    /* Pitch row */
    GtkWidget *pitch_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_append(GTK_BOX(pitch_row), gtk_label_new("Pitch:"));
    GtkAdjustment *pitch_adj = gtk_adjustment_new(
        st->pitch_offset, -12, 12, 1, 4, 0);
    s_pitch_spin = gtk_spin_button_new(pitch_adj, 1, 0);
    gtk_widget_set_hexpand(s_pitch_spin, TRUE);
    g_signal_connect(s_pitch_spin, "value-changed", G_CALLBACK(on_pitch_changed), NULL);
    gtk_box_append(GTK_BOX(pitch_row), s_pitch_spin);
    gtk_box_append(GTK_BOX(box), pitch_row);

    /* Separator */
    gtk_box_append(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

    /* Button row */
    GtkWidget *btn_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_widget_set_halign(btn_row, GTK_ALIGN_CENTER);

    GtkWidget *del_btn = gtk_button_new_with_label("Delete");
    g_signal_connect(del_btn, "clicked", G_CALLBACK(on_delete_note), NULL);
    gtk_box_append(GTK_BOX(btn_row), del_btn);

    GtkWidget *copy_btn = gtk_button_new_with_label("Copy");
    g_signal_connect(copy_btn, "clicked", G_CALLBACK(on_copy_note), NULL);
    gtk_box_append(GTK_BOX(btn_row), copy_btn);

    GtkWidget *paste_btn = gtk_button_new_with_label("Paste");
    gtk_widget_set_sensitive(paste_btn, s_clipboard_valid);
    g_signal_connect(paste_btn, "clicked", G_CALLBACK(on_paste_note), NULL);
    gtk_box_append(GTK_BOX(btn_row), paste_btn);

    gtk_box_append(GTK_BOX(box), btn_row);

    /* Create popover parented on the drum grid drawing area */
    s_popover = gtk_popover_new();
    gtk_popover_set_child(GTK_POPOVER(s_popover), box);
    gtk_widget_set_parent(s_popover, g_gtk.drum_grid_area);
    gtk_popover_set_autohide(GTK_POPOVER(s_popover), TRUE);

    /* Position the popover at the click point */
    GdkRectangle rect = {
        .x = (int)click_x,
        .y = (int)click_y,
        .width = 1,
        .height = 1
    };
    gtk_popover_set_pointing_to(GTK_POPOVER(s_popover), &rect);

    g_signal_connect(s_popover, "closed", G_CALLBACK(on_popover_closed), NULL);

    gtk_popover_popup(GTK_POPOVER(s_popover));
}

/* ─── Right-click handler ─────────────────────────────────────────────────── */

static void on_right_click(GtkGestureClick *gesture, int n_press,
                           double x, double y, gpointer user_data)
{
    (void)gesture; (void)n_press; (void)user_data;
    sq_engine_t *engine = g_gtk.engine;
    if (!engine) return;

    int pi = engine->transport.current_pattern;
    if (pi < 0 || (uint32_t)pi >= engine->num_patterns) return;
    sq_pattern_t *pat = &engine->patterns[pi];
    if (pat->num_tracks == 0) return;

    int w = gtk_widget_get_width(g_gtk.drum_grid_area);
    int h = gtk_widget_get_height(g_gtk.drum_grid_area);
    grid_geom_t g;
    if (!get_geom(w, h, &g)) return;

    /* Only handle clicks in the grid area */
    if (x < g.grid_x || y < g.grid_y) return;

    int step  = (int)((x - g.grid_x) / g.cell_w);
    int track = (int)((y - g.grid_y + s_scroll_offset) / g.cell_h);

    if (step < 0 || (uint32_t)step >= g.max_steps) return;
    if (track < 0 || (uint32_t)track >= pat->num_tracks) return;

    /* If cell is empty, activate it first with default velocity */
    if (pat->tracks[track].steps[step].velocity == 0) {
        undo_push(engine);
        pat->tracks[track].steps[step].velocity = 100;
        gtk_widget_queue_draw(g_gtk.drum_grid_area);
    }

    g_gtk.app.selected_track = track;
    show_step_popover(track, step, x, y);
}

/* ─── Add track buttons ───────────────────────────────────────────────────── */

static void on_add_sampler_track(GtkWidget *btn, gpointer user_data)
{
    (void)btn; (void)user_data;
    sq_engine_t *engine = g_gtk.engine;
    if (!engine) return;
    int pi = engine->transport.current_pattern;
    if (pi < 0 || (uint32_t)pi >= engine->num_patterns) return;
    sq_pattern_t *pat = &engine->patterns[pi];

    if (pat->num_tracks >= SQ_MAX_TRACKS) {
        sq_app_set_status(&g_gtk.app, "Max tracks reached!", 90);
        return;
    }

    sq_track_t *t = &pat->tracks[pat->num_tracks];
    memset(t, 0, sizeof(*t));
    t->type         = TRACK_SAMPLER;
    t->sample_index = 0;
    t->length       = 16;
    t->volume       = 0.8f;
    t->color_index  = (uint8_t)(pat->num_tracks % NUM_TRACK_COLORS);
    pat->num_tracks++;

    g_gtk.app.selected_track = (int)(pat->num_tracks - 1);
    sq_app_set_status(&g_gtk.app, "+ Sampler track", 90);
    gtk_widget_queue_draw(g_gtk.drum_grid_area);
}

static void on_add_synth_track(GtkWidget *btn, gpointer user_data)
{
    (void)btn; (void)user_data;
    sq_engine_t *engine = g_gtk.engine;
    if (!engine) return;
    int pi = engine->transport.current_pattern;
    if (pi < 0 || (uint32_t)pi >= engine->num_patterns) return;
    sq_pattern_t *pat = &engine->patterns[pi];

    if (pat->num_tracks >= SQ_MAX_TRACKS) {
        sq_app_set_status(&g_gtk.app, "Max tracks reached!", 90);
        return;
    }

    sq_track_t *t = &pat->tracks[pat->num_tracks];
    memset(t, 0, sizeof(*t));
    t->type         = TRACK_SYNTH;
    t->synth_preset = 0;
    t->length       = 16;
    t->volume       = 0.6f;
    t->color_index  = (uint8_t)(pat->num_tracks % NUM_TRACK_COLORS);
    pat->num_tracks++;

    g_gtk.app.selected_track = (int)(pat->num_tracks - 1);
    sq_app_set_status(&g_gtk.app, "+ Synth track", 90);
    gtk_widget_queue_draw(g_gtk.drum_grid_area);
}

/* ─── Constructor ─────────────────────────────────────────────────────────── */

GtkWidget *gtk_drum_grid_new(void)
{
    GtkWidget *area = gtk_drawing_area_new();
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(area), on_draw, NULL, NULL);

    /* Left-click for toggling steps + selecting tracks */
    GtkGesture *click = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), 1);
    g_signal_connect(click, "pressed", G_CALLBACK(on_click), NULL);
    gtk_widget_add_controller(area, GTK_EVENT_CONTROLLER(click));

    /* Right-click for context menu */
    GtkGesture *rclick = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(rclick), 3);
    g_signal_connect(rclick, "pressed", G_CALLBACK(on_right_click), NULL);
    gtk_widget_add_controller(area, GTK_EVENT_CONTROLLER(rclick));

    /* Drag for painting multiple steps */
    GtkGesture *drag = gtk_gesture_drag_new();
    g_signal_connect(drag, "drag-update", G_CALLBACK(on_drag_update), NULL);
    g_signal_connect(drag, "drag-end", G_CALLBACK(on_drag_end), NULL);
    gtk_widget_add_controller(area, GTK_EVENT_CONTROLLER(drag));

    /* Motion for hover highlight */
    GtkEventController *motion = gtk_event_controller_motion_new();
    g_signal_connect(motion, "motion", G_CALLBACK(on_motion), NULL);
    g_signal_connect(motion, "leave", G_CALLBACK(on_motion_leave), NULL);
    gtk_widget_add_controller(area, motion);

    /* Scroll for vertical track scrolling */
    GtkEventController *scroll = gtk_event_controller_scroll_new(
        GTK_EVENT_CONTROLLER_SCROLL_VERTICAL);
    g_signal_connect(scroll, "scroll", G_CALLBACK(on_scroll), NULL);
    gtk_widget_add_controller(area, scroll);

    return area;
}

void gtk_drum_grid_queue_redraw(void)
{
    if (g_gtk.drum_grid_area)
        gtk_widget_queue_draw(g_gtk.drum_grid_area);
}

void gtk_drum_grid_add_sampler_track(GtkWidget *btn, gpointer data)
{
    on_add_sampler_track(btn, data);
}

void gtk_drum_grid_add_synth_track(GtkWidget *btn, gpointer data)
{
    on_add_synth_track(btn, data);
}
