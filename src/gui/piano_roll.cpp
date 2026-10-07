/*
 * piano_roll.cpp — Melodic note editor (pitch x time grid), Dear ImGui port.
 *
 * Layout:
 * +------+----+----+----+----+----+----+----+----+
 * |  C5  |    |XXXX|    |    |    |    |    |    |
 * |  B4  |    |    |XXXX|XXXX|    |    |    |    |
 * |  A#4 |    |    |    |    |    |    |    |    |
 * |  A4  |    |    |    |    |XXXX|XXXX|XXXX|    |
 * |  G#4 |    |    |    |    |    |    |    |    |
 * |  G4  |XXXX|    |    |    |    |    |    |XXXX|
 * |  ... |    |    |    |    |    |    |    |    |
 * +------+----+----+----+----+----+----+----+----+
 *                    ^ playback position
 *
 * Interactions:
 *   Left-click empty:  place note (default vel=100, length=1 step)
 *   Left-click note:   select it
 *   Right-click note:  delete it
 *   Scroll:            vertical scroll (pitch range)
 */

#include "imgui.h"

extern "C" {
#include "engine/engine.h"
#include "engine/synth.h"
#include "gui/piano_roll.h"
#include "gui/gui.h"

#define LOG_TAG "pianoroll"
#include "core/log.h"
}

#include <cstdio>
#include <cstring>
#include <cmath>

/* --- Constants ----------------------------------------------------------- */

#define KEY_PANEL_W   50.0f    /* width of piano key labels */
#define NOTE_HEIGHT   14.0f    /* height of each pitch row */
#define MIN_NOTE      12       /* C0 — bass presets sit in C0-C1 */
#define MAX_NOTE      84       /* C6 */
#define DEFAULT_VEL   100

/* --- State --------------------------------------------------------------- */

static int  s_scroll_note = 60;  /* center note for vertical scroll */
static int  s_drag_step   = -1;  /* step being dragged for note placement */
static int  s_drag_note   = -1;  /* note being dragged */
static bool s_dragging    = false;
static bool s_right_dragging = false;  /* right-click drag erase mode */
static int  s_last_erase_step = -1;    /* last erased step (avoid re-logging) */
static int  s_last_erase_note = -1;

/* Track + notes as of the last frame. When the notes change from outside the
 * piano roll (track switch, preset, randomize, undo), the view scrolls to show
 * as many of them as it can. */
static const sq_track_t *s_seen_track = nullptr;
static int  s_seen_count = 0;
static int  s_seen_lo = -1;
static int  s_seen_hi = -1;

/* --- Helpers ------------------------------------------------------------- */

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

/* Count active steps with a note in [from, to]; lo/hi get the lowest/highest
 * such note (both -1 if none) */
static int track_notes_in(const sq_track_t *track, int from, int to, int *lo, int *hi)
{
    int count = 0;
    *lo = -1;
    *hi = -1;
    for (uint32_t s = 0; s < track->length; s++) {
        const sq_step_t *st = &track->steps[s];
        if (st->velocity == 0 || st->note < from || st->note > to) continue;
        if (*lo < 0 || st->note < *lo) *lo = st->note;
        if (st->note > *hi) *hi = st->note;
        count++;
    }
    return count;
}

/* Visible pitch window for the current scroll position */
static void view_range(int visible_rows, int *top_note, int *bot_note)
{
    int top = s_scroll_note + visible_rows / 2;
    if (top > MAX_NOTE) top = MAX_NOTE;
    int bot = top - visible_rows + 1;
    if (bot < MIN_NOTE) { bot = MIN_NOTE; top = bot + visible_rows - 1; }
    *top_note = top;
    *bot_note = bot;
}

/* Scroll position whose view shows the most notes, centered on them, or -1 if
 * the current view [bot_note, top_note] already shows as many as any could */
static int fit_scroll_note(const sq_track_t *track, int visible_rows,
                           int bot_note, int top_note)
{
    int lo, hi;
    int best_n = track_notes_in(track, bot_note, top_note, &lo, &hi);
    int best_bot = -1;
    for (int b = MIN_NOTE; b + visible_rows - 1 <= MAX_NOTE; b++) {
        int n = track_notes_in(track, b, b + visible_rows - 1, &lo, &hi);
        if (n > best_n) { best_n = n; best_bot = b; }
    }
    if (best_bot < 0) return -1;
    track_notes_in(track, best_bot, best_bot + visible_rows - 1, &lo, &hi);
    return (lo + hi) / 2;
}

/* --- Draw ---------------------------------------------------------------- */

void piano_roll_draw(sq_engine_t *engine,
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

    /* How many pitch rows fit in the grid area (header + padding excluded) */
    float header_h = 30.0f;
    int visible_rows = (int)((h - header_h - 8.0f) / NOTE_HEIGHT);
    if (visible_rows < 4) visible_rows = 4;

    int top_note, bot_note;
    view_range(visible_rows, &top_note, &bot_note);

    /* Notes changed outside the piano roll: scroll to show them */
    {
        int lo, hi;
        int count = track_notes_in(track, 0, 127, &lo, &hi);
        if (track != s_seen_track || count != s_seen_count ||
            lo != s_seen_lo || hi != s_seen_hi) {
            int scroll = fit_scroll_note(track, visible_rows, bot_note, top_note);
            if (scroll >= 0) {
                s_scroll_note = scroll;
                view_range(visible_rows, &top_note, &bot_note);
            }
        }
    }

    /* Set up ImGui window */
    ImGui::SetNextWindowPos(ImVec2(x, y));
    ImGui::SetNextWindowSize(ImVec2(w, h));

    char title[64];
    snprintf(title, sizeof(title), "PianoRoll_%d", track_index);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar |
                             ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

    if (!ImGui::Begin(title, nullptr, flags)) {
        ImGui::End();
        return;
    }

    /* Header */
    {
        const char *preset_name = "(none)";
        if (track->synth_preset >= 0 &&
            (uint32_t)track->synth_preset < engine->num_synth_presets)
            preset_name = engine->synth_presets[track->synth_preset].name;
        ImGui::Text("Piano Roll: Track %d - %s", track_index + 1, preset_name);
    }

    /* Grid drawing area */
    ImVec2 grid_origin = ImGui::GetCursorScreenPos();
    float grid_area_h = h - header_h - 8.0f; /* subtract header + padding */
    float grid_x = grid_origin.x + KEY_PANEL_W;
    float grid_y_start = grid_origin.y;
    float grid_w = w - KEY_PANEL_W - 16.0f; /* subtract padding */
    float step_w = grid_w / (float)num_steps;

    ImDrawList *draw = ImGui::GetWindowDrawList();

    /* Draw piano keys and pitch rows */
    for (int note = top_note; note >= bot_note; note--) {
        int row = top_note - note;
        float row_y = grid_y_start + (float)row * NOTE_HEIGHT;

        if (row_y + NOTE_HEIGHT > grid_y_start + grid_area_h)
            break;

        /* Piano key label background */
        bool black = is_black_key(note);
        ImU32 key_bg;
        if (note % 12 == 0)
            key_bg = IM_COL32(40, 50, 60, 255);  /* C notes: octave marker */
        else if (black)
            key_bg = IM_COL32(30, 30, 35, 255);
        else
            key_bg = IM_COL32(50, 50, 55, 255);

        draw->AddRectFilled(
            ImVec2(grid_origin.x, row_y),
            ImVec2(grid_origin.x + KEY_PANEL_W, row_y + NOTE_HEIGHT - 1),
            key_bg);

        /* Label text */
        char name[8];
        note_name_str(note, name, sizeof(name));
        draw->AddText(ImVec2(grid_origin.x + 2, row_y), IM_COL32(180, 180, 180, 255), name);

        /* Draw grid cells for each step */
        for (uint32_t s = 0; s < num_steps; s++) {
            float cell_x = grid_x + (float)s * step_w;

            /* Background - alternate every 4 steps (beat boundaries) */
            ImU32 bg;
            if ((int)s == current_step && engine->transport.playing) {
                bg = IM_COL32(60, 60, 70, 255); /* playhead column */
            } else if (s % 8 < 4) {
                bg = black ? IM_COL32(25, 25, 28, 255)
                           : IM_COL32(35, 35, 38, 255);
            } else {
                bg = black ? IM_COL32(28, 28, 32, 255)
                           : IM_COL32(38, 38, 42, 255);
            }
            draw->AddRectFilled(
                ImVec2(cell_x, row_y),
                ImVec2(cell_x + step_w - 1, row_y + NOTE_HEIGHT - 1),
                bg);
        }

        /* Draw notes as velocity-colored bars */
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

                /* Color: brighter and more opaque with higher velocity */
                ImU32 note_color = IM_COL32(
                    (int)(60 + 160 * vel_norm),
                    (int)(100 + 120 * vel_norm),
                    (int)(180 + 60 * vel_norm),
                    (int)(100 + 155 * vel_norm));

                draw->AddRectFilled(
                    ImVec2(cell_x + 1, row_y + 1 + y_offset),
                    ImVec2(cell_x + 1 + note_w, row_y + 1 + y_offset + note_h),
                    note_color, 2.0f);

                /* Velocity text on wider notes */
                if (note_w > 20) {
                    char vel_str[8];
                    snprintf(vel_str, sizeof(vel_str), "%d", step->velocity);
                    ImU32 text_col = IM_COL32(255, 255, 255,
                                              (int)(120 + 135 * vel_norm));
                    draw->AddText(ImVec2(cell_x + 3, row_y + 1 + y_offset),
                                  text_col, vel_str);
                }
            }
        }
    }

    /* Draw vertical step lines */
    float grid_bottom = grid_y_start + (float)(top_note - bot_note + 1) * NOTE_HEIGHT;
    for (uint32_t s = 0; s <= num_steps; s++) {
        float lx = grid_x + (float)s * step_w;
        ImU32 line_color = (s % 4 == 0)
            ? IM_COL32(80, 80, 85, 200)
            : IM_COL32(50, 50, 55, 100);
        draw->AddLine(ImVec2(lx, grid_y_start), ImVec2(lx, grid_bottom), line_color, 1.0f);
    }

    /* Playhead line */
    if (engine->transport.playing) {
        float px = grid_x + ((float)current_step + 0.5f) * step_w;
        draw->AddLine(ImVec2(px, grid_y_start), ImVec2(px, grid_bottom),
                      IM_COL32(255, 100, 100, 200), 2.0f);
    }

    /* --- Mouse interaction ------------------------------------------------ */
    ImGuiIO &io = ImGui::GetIO();
    ImVec2 mouse = io.MousePos;
    bool in_grid = (mouse.x >= grid_x && mouse.x < grid_x + grid_w &&
                    mouse.y >= grid_y_start && mouse.y < grid_bottom);

    if (in_grid && ImGui::IsWindowHovered()) {
        /* Determine which step and note the mouse is over */
        int hover_step = (int)((mouse.x - grid_x) / step_w);
        int hover_note = top_note - (int)((mouse.y - grid_y_start) / NOTE_HEIGHT);

        if (hover_step >= 0 && (uint32_t)hover_step < num_steps &&
            hover_note >= MIN_NOTE && hover_note <= MAX_NOTE)
        {
            /* Left click: place note */
            if (io.MouseClicked[0]) {
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
            if (s_dragging && io.MouseDown[0]) {
                if (s_drag_step >= 0 && (uint32_t)s_drag_step < num_steps &&
                    hover_note == s_drag_note) {
                    float new_len = (float)(hover_step - s_drag_step + 1);
                    if (new_len >= 1.0f) {
                        track->steps[s_drag_step].length = new_len;
                    }
                }
            }

            /* Right click/drag: delete notes (hold right button to erase) */
            if (io.MouseDown[1]) {
                /* Only erase if we moved to a new cell (or just clicked) */
                if (hover_step != s_last_erase_step || hover_note != s_last_erase_note ||
                    io.MouseClicked[1]) {
                    int existing = find_note_at(track, hover_step, hover_note);
                    if (existing >= 0) {
                        track->steps[existing].velocity = 0;
                        track->steps[existing].note = 0;
                        track->steps[existing].length = 0;
                        LOG_DEBUG("Delete note: step=%d note=%d", existing, hover_note);
                    }
                    s_last_erase_step = hover_step;
                    s_last_erase_note = hover_note;
                    s_right_dragging = true;
                }
            }
        }

        /* Scroll: vertical pitch navigation */
        float scroll = io.MouseWheel;
        if (scroll != 0.0f) {
            s_scroll_note += (int)(scroll * 2);
            if (s_scroll_note > MAX_NOTE - visible_rows / 2)
                s_scroll_note = MAX_NOTE - visible_rows / 2;
            if (s_scroll_note < MIN_NOTE + visible_rows / 2)
                s_scroll_note = MIN_NOTE + visible_rows / 2;
        }
    }

    /* Release drag */
    if (!io.MouseDown[0]) {
        if (s_dragging) {
            s_dragging = false;
            s_drag_step = -1;
            s_drag_note = -1;
        }
    }
    if (!io.MouseDown[1]) {
        s_right_dragging = false;
        s_last_erase_step = -1;
        s_last_erase_note = -1;
    }

    /* Record after this frame's edits so our own clicks never recenter */
    s_seen_track = track;
    s_seen_count = track_notes_in(track, 0, 127, &s_seen_lo, &s_seen_hi);

    ImGui::End();
}
