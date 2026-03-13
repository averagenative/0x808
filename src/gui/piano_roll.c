/*
 * piano_roll.c — Melodic note editor (pitch × time grid).
 *
 * Layout:
 * ┌──────┬────┬────┬────┬────┬────┬────┬────┬────┐
 * │  C5  │    │████│    │    │    │    │    │    │
 * │  B4  │    │    │████│████│    │    │    │    │
 * │  A#4 │    │    │    │    │    │    │    │    │
 * │  A4  │    │    │    │    │████│████│████│    │
 * │  G#4 │    │    │    │    │    │    │    │    │
 * │  G4  │████│    │    │    │    │    │    │████│
 * │  ... │    │    │    │    │    │    │    │    │
 * └──────┴────┴────┴────┴────┴────┴────┴────┴────┘
 *                    ▲ playback position
 *
 * Interactions:
 *   Left-click empty:  place note (default vel=100, length=1 step)
 *   Left-click note:   select it
 *   Right-click note:  delete it
 *   Scroll:            vertical scroll (pitch range)
 *   Ctrl+Scroll:       horizontal zoom
 */

#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#include "nuklear.h"

#include "gui/piano_roll.h"
#include "gui/gui.h"

#define LOG_TAG "pianoroll"
#include "core/log.h"

#include <stdio.h>
#include <string.h>
#include <math.h>

/* ─── Constants ──────────────────────────────────────────────────────────── */

#define KEY_PANEL_W   50.0f    /* width of piano key labels */
#define NOTE_HEIGHT   14.0f    /* height of each pitch row */
#define MIN_NOTE      36       /* C2 */
#define MAX_NOTE      84       /* C6 */
#define DEFAULT_VEL   100

/* ─── State ──────────────────────────────────────────────────────────────── */

static int s_scroll_note = 60;  /* center note for vertical scroll */
static int s_drag_step = -1;    /* step being dragged for note placement */
static int s_drag_note = -1;    /* note being dragged */
static bool s_dragging = false;

/* ─── Helpers ────────────────────────────────────────────────────────────── */

static const char *note_names[] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};

static bool is_black_key(int midi_note)
{
    int n = midi_note % 12;
    return (n == 1 || n == 3 || n == 6 || n == 8 || n == 10);
}

static void note_name_str(int midi_note, char *buf, int buflen)
{
    int octave = (midi_note / 12) - 1;
    int n = midi_note % 12;
    snprintf(buf, buflen, "%s%d", note_names[n], octave);
}

/* Find a step at the given note and step position */
static int find_note_at(sq_track_t *track, int step, int note)
{
    /* Check if this step has a note event at this pitch */
    if (step >= 0 && (uint32_t)step < track->length) {
        sq_step_t *s = &track->steps[step];
        if (s->velocity > 0 && s->note == note)
            return step;
    }
    /* Also check if a note from an earlier step extends into this position */
    for (int s = step - 1; s >= 0 && s >= step - 16; s--) {
        sq_step_t *st = &track->steps[s];
        if (st->velocity > 0 && st->note == note) {
            float end = (float)s + st->length;
            if ((float)step < end)
                return s; /* found: note starts at step s */
        }
    }
    return -1;
}

/* ─── Draw ───────────────────────────────────────────────────────────────── */

void piano_roll_draw(struct nk_context *ctx, sq_engine_t *engine,
                     int track_index,
                     float x, float y, float w, float h)
{
    int pat_idx = engine->transport.current_pattern;
    if (pat_idx < 0 || (uint32_t)pat_idx >= engine->num_patterns) return;
    sq_pattern_t *pattern = &engine->patterns[pat_idx];
    if ((uint32_t)track_index >= pattern->num_tracks) return;
    sq_track_t *track = &pattern->tracks[track_index];

    uint32_t num_steps = track->length;
    int current_step = g_visual_step;

    /* How many pitch rows fit in the panel */
    int visible_rows = (int)((h - 30) / NOTE_HEIGHT);
    if (visible_rows < 4) visible_rows = 4;

    int top_note = s_scroll_note + visible_rows / 2;
    int bot_note = s_scroll_note - visible_rows / 2;
    if (top_note > MAX_NOTE) { top_note = MAX_NOTE; bot_note = top_note - visible_rows; }
    if (bot_note < MIN_NOTE) { bot_note = MIN_NOTE; top_note = bot_note + visible_rows; }

    float step_w = (w - KEY_PANEL_W - 20) / (float)num_steps;

    char title[64];
    snprintf(title, sizeof(title), "PianoRoll_%d", track_index);

    if (nk_begin(ctx, title,
                 nk_rect(x, y, w, h),
                 NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_BORDER))
    {
        /* Header */
        nk_layout_row_dynamic(ctx, 16, 1);
        {
            const char *preset_name = "(none)";
            if (track->synth_preset >= 0 &&
                (uint32_t)track->synth_preset < engine->num_synth_presets)
                preset_name = engine->synth_presets[track->synth_preset].name;
            nk_labelf(ctx, NK_TEXT_LEFT, "Piano Roll: Track %d — %s",
                      track_index + 1, preset_name);
        }

        /* Grid area — use custom drawing */
        nk_layout_row_dynamic(ctx, h - 50, 1);
        struct nk_rect grid_bounds = nk_widget_bounds(ctx);
        nk_spacing(ctx, 1); /* consume the widget slot */

        struct nk_command_buffer *canvas = nk_window_get_canvas(ctx);

        float grid_x = grid_bounds.x + KEY_PANEL_W;
        float grid_y_start = grid_bounds.y;
        float grid_w = grid_bounds.w - KEY_PANEL_W;

        step_w = grid_w / (float)num_steps;

        /* Draw piano keys and pitch rows */
        for (int note = top_note; note >= bot_note; note--) {
            int row = top_note - note;
            float row_y = grid_y_start + (float)row * NOTE_HEIGHT;

            if (row_y + NOTE_HEIGHT > grid_bounds.y + grid_bounds.h)
                break;

            /* Piano key label */
            char name[8];
            note_name_str(note, name, sizeof(name));

            struct nk_color key_bg = is_black_key(note)
                ? nk_rgba(30, 30, 35, 255)
                : nk_rgba(50, 50, 55, 255);

            /* C notes get a brighter background as octave markers */
            if (note % 12 == 0)
                key_bg = nk_rgba(40, 50, 60, 255);

            nk_fill_rect(canvas,
                         nk_rect(grid_bounds.x, row_y, KEY_PANEL_W, NOTE_HEIGHT - 1),
                         0, key_bg);

            /* Label text */
            struct nk_rect label_rect = nk_rect(grid_bounds.x + 2, row_y,
                                                 KEY_PANEL_W - 4, NOTE_HEIGHT);
            nk_draw_text(canvas, label_rect,
                         name, (int)strlen(name),
                         ctx->style.font,
                         nk_rgba(0, 0, 0, 0),
                         nk_rgba(180, 180, 180, 255));

            /* Draw grid cells for each step */
            for (uint32_t s = 0; s < num_steps; s++) {
                float cell_x = grid_x + (float)s * step_w;
                struct nk_rect cell = nk_rect(cell_x, row_y,
                                               step_w - 1, NOTE_HEIGHT - 1);

                /* Background — alternate every 4 steps (beat boundaries) */
                struct nk_color bg;
                if ((int)s == current_step && engine->transport.playing) {
                    bg = nk_rgba(60, 60, 70, 255); /* playhead column */
                } else if (s % 8 < 4) {
                    bg = is_black_key(note)
                        ? nk_rgba(25, 25, 28, 255)
                        : nk_rgba(35, 35, 38, 255);
                } else {
                    bg = is_black_key(note)
                        ? nk_rgba(28, 28, 32, 255)
                        : nk_rgba(38, 38, 42, 255);
                }
                nk_fill_rect(canvas, cell, 0, bg);
            }

            /* Draw notes as velocity-colored bars (TASK-207) */
            for (uint32_t s = 0; s < num_steps; s++) {
                sq_step_t *step = &track->steps[s];
                if (step->velocity > 0 && step->note == note) {
                    float note_len = step->length > 0.0f ? step->length : 1.0f;
                    float cell_x = grid_x + (float)s * step_w;
                    float note_w = note_len * step_w - 2;
                    if (note_w < 4) note_w = 4;

                    /* Velocity-dependent sizing and color */
                    float vel_norm = (float)step->velocity / 127.0f;
                    float base_h = NOTE_HEIGHT - 3;
                    float note_h = base_h * (0.7f + 0.3f * vel_norm);
                    float y_offset = (base_h - note_h) * 0.5f; /* center vertically */

                    struct nk_rect note_rect = nk_rect(cell_x + 1,
                                                        row_y + 1 + y_offset,
                                                        note_w, note_h);

                    /* Color: brighter and more opaque with higher velocity */
                    struct nk_color note_color = nk_rgba(
                        (nk_byte)(60 + (int)(160 * vel_norm)),
                        (nk_byte)(100 + (int)(120 * vel_norm)),
                        (nk_byte)(180 + (int)(60 * vel_norm)),
                        (nk_byte)(100 + (int)(155 * vel_norm)));
                    nk_fill_rect(canvas, note_rect, 2, note_color);

                    /* Velocity text on wider notes */
                    if (note_w > 20) {
                        char vel_str[8];
                        snprintf(vel_str, sizeof(vel_str), "%d", step->velocity);
                        nk_draw_text(canvas, note_rect,
                                     vel_str, (int)strlen(vel_str),
                                     ctx->style.font,
                                     nk_rgba(0, 0, 0, 0),
                                     nk_rgba(255, 255, 255,
                                             (nk_byte)(120 + (int)(135 * vel_norm))));
                    }
                }
            }
        }

        /* Draw vertical step lines */
        for (uint32_t s = 0; s <= num_steps; s++) {
            float lx = grid_x + (float)s * step_w;
            struct nk_color line_color = (s % 4 == 0)
                ? nk_rgba(80, 80, 85, 200)
                : nk_rgba(50, 50, 55, 100);
            nk_stroke_line(canvas, lx, grid_y_start,
                           lx, grid_y_start + (float)(top_note - bot_note + 1) * NOTE_HEIGHT,
                           1, line_color);
        }

        /* Playhead line */
        if (engine->transport.playing) {
            float px = grid_x + ((float)current_step + 0.5f) * step_w;
            nk_stroke_line(canvas, px, grid_y_start,
                           px, grid_y_start + (float)(top_note - bot_note + 1) * NOTE_HEIGHT,
                           2, nk_rgba(255, 100, 100, 200));
        }

        /* ── Mouse interaction ──────────────────────────────────────── */
        struct nk_vec2 mouse = ctx->input.mouse.pos;
        bool in_grid = (mouse.x >= grid_x && mouse.x < grid_x + grid_w &&
                        mouse.y >= grid_y_start &&
                        mouse.y < grid_y_start + (float)(top_note - bot_note + 1) * NOTE_HEIGHT);

        if (in_grid) {
            /* Determine which step and note the mouse is over */
            int hover_step = (int)((mouse.x - grid_x) / step_w);
            int hover_note = top_note - (int)((mouse.y - grid_y_start) / NOTE_HEIGHT);

            if (hover_step >= 0 && (uint32_t)hover_step < num_steps &&
                hover_note >= MIN_NOTE && hover_note <= MAX_NOTE)
            {
                /* Left click: place or select note */
                if (nk_input_is_mouse_pressed(&ctx->input, NK_BUTTON_LEFT)) {
                    int existing = find_note_at(track, hover_step, hover_note);
                    if (existing < 0) {
                        /* Place new note */
                        sq_step_t *s = &track->steps[hover_step];
                        s->velocity = DEFAULT_VEL;
                        s->note = (uint8_t)hover_note;
                        s->length = 1.0f;
                        s->pitch_offset = 0;
                        LOG_DEBUG("Place note: step=%d note=%d vel=%d",
                                  hover_step, hover_note, DEFAULT_VEL);

                        s_drag_step = hover_step;
                        s_drag_note = hover_note;
                        s_dragging = true;
                    }
                }

                /* Drag to extend note length */
                if (s_dragging && nk_input_is_mouse_down(&ctx->input, NK_BUTTON_LEFT)) {
                    if (s_drag_step >= 0 && (uint32_t)s_drag_step < num_steps &&
                        hover_note == s_drag_note) {
                        float new_len = (float)(hover_step - s_drag_step + 1);
                        if (new_len >= 1.0f) {
                            track->steps[s_drag_step].length = new_len;
                        }
                    }
                }

                /* Right click: delete note */
                if (nk_input_is_mouse_pressed(&ctx->input, NK_BUTTON_RIGHT)) {
                    int existing = find_note_at(track, hover_step, hover_note);
                    if (existing >= 0) {
                        track->steps[existing].velocity = 0;
                        track->steps[existing].note = 0;
                        track->steps[existing].length = 0;
                        LOG_DEBUG("Delete note: step=%d note=%d", existing, hover_note);
                    }
                }
            }

            /* Scroll: vertical pitch navigation */
            float scroll = ctx->input.mouse.scroll_delta.y;
            if (scroll != 0.0f) {
                s_scroll_note += (int)(scroll * 2);
                if (s_scroll_note > MAX_NOTE - visible_rows / 2)
                    s_scroll_note = MAX_NOTE - visible_rows / 2;
                if (s_scroll_note < MIN_NOTE + visible_rows / 2)
                    s_scroll_note = MIN_NOTE + visible_rows / 2;
            }
        }

        /* Release drag */
        if (!nk_input_is_mouse_down(&ctx->input, NK_BUTTON_LEFT)) {
            if (s_dragging) {
                s_dragging = false;
                s_drag_step = -1;
                s_drag_note = -1;
            }
        }
    }
    nk_end(ctx);
}
