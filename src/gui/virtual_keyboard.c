/*
 * virtual_keyboard.c — Clickable piano keyboard for live synth playing.
 *
 * Draws a 3-octave piano keyboard (C3–B5). Left-click a key to trigger
 * a synth voice; release to send note-off.  Supports mouse drag across
 * keys for glissando.
 *
 * Layout (per octave):
 *   ┌──┬─┬┬─┬──┬──┬─┬┬─┬┬─┬──┐
 *   │  │█││█│  │  │█││█││█│  │
 *   │  │█││█│  │  │█││█││█│  │
 *   │ C│ ││ │ E│ F│ ││ ││ │ B│
 *   └──┴─┴┴─┴──┴──┴─┴┴─┴┴─┴──┘
 */

#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#include "nuklear.h"

#include "gui/virtual_keyboard.h"
#include "gui/gui.h"
#include "engine/synth.h"

#include <string.h>
#include <math.h>

/* ─── Constants ──────────────────────────────────────────────────────────── */

#define KB_NUM_OCTAVES   3
#define KB_WHITE_PER_OCT 7
#define KB_NUM_WHITE    (KB_NUM_OCTAVES * KB_WHITE_PER_OCT + 1) /* +1 for top C */

/* Which notes in an octave are white keys (0=C,2=D,4=E,5=F,7=G,9=A,11=B) */
static const int WHITE_NOTES[] = {0, 2, 4, 5, 7, 9, 11};
/* Black key offsets within an octave (1=C#,3=D#,6=F#,8=G#,10=A#) */
static const int BLACK_NOTES[] = {1, 3, 6, 8, 10};

/* ─── State ──────────────────────────────────────────────────────────────── */

static int  s_held_note = -1;       /* currently held MIDI note (-1 = none) */
static int  s_held_voice = -1;      /* voice index (for release) */
static bool s_mouse_was_down = false;
static int  s_octave_offset = 0;    /* octave shift: -2..+4 (base C3 = octave 0) */

/* ─── Helpers ────────────────────────────────────────────────────────────── */

static bool is_black(int note_in_octave)
{
    return (note_in_octave == 1 || note_in_octave == 3 ||
            note_in_octave == 6 || note_in_octave == 8 ||
            note_in_octave == 10);
}

/* Compute start note from octave offset (base = C3 = MIDI 48) */
static int kb_start_note(void) {
    int n = 48 + s_octave_offset * 12;
    if (n < 0) n = 0;
    if (n > 96) n = 96;
    return n;
}
static int kb_end_note(void) {
    return kb_start_note() + KB_NUM_OCTAVES * 12;
}

/* Given a mouse position, determine which MIDI note it maps to.
 * Black keys are checked first (they overlap white keys). */
static int hit_test(float mx, float my,
                    float kb_x, float kb_y, float kb_w, float kb_h)
{
    if (mx < kb_x || mx >= kb_x + kb_w || my < kb_y || my >= kb_y + kb_h)
        return -1;

    int start = kb_start_note();
    int end   = kb_end_note();

    float white_w = kb_w / (float)KB_NUM_WHITE;
    float black_w = white_w * 0.6f;
    float black_h = kb_h * 0.6f;

    /* Check black keys first (they're on top) */
    for (int oct = 0; oct < KB_NUM_OCTAVES; oct++) {
        for (int b = 0; b < 5; b++) {
            int midi = start + oct * 12 + BLACK_NOTES[b];
            if (midi >= end) continue;

            int left_white = -1;
            int n = BLACK_NOTES[b];
            for (int i = 0; i < 7; i++) {
                if (WHITE_NOTES[i] < n) left_white = i;
            }
            if (left_white < 0) continue;

            int wki = oct * 7 + left_white;
            float bx = kb_x + ((float)wki + 1.0f) * white_w - black_w * 0.5f;

            if (mx >= bx && mx < bx + black_w && my >= kb_y && my < kb_y + black_h)
                return midi;
        }
    }

    /* Check white keys */
    int wk = (int)((mx - kb_x) / white_w);
    if (wk < 0) wk = 0;
    if (wk >= KB_NUM_WHITE) wk = KB_NUM_WHITE - 1;

    int octave = wk / 7;
    int key_in_oct = wk % 7;
    int midi = start + octave * 12 + WHITE_NOTES[key_in_oct];
    if (midi >= end) midi = end - 1;

    return midi;
}

/* ─── Note name label ────────────────────────────────────────────────────── */

static const char *NOTE_NAMES[] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};

/* ─── Draw ───────────────────────────────────────────────────────────────── */

void virtual_keyboard_draw(struct nk_context *ctx, sq_engine_t *engine,
                           int synth_preset,
                           float x, float y, float w, float h)
{
    nk_window_set_bounds(ctx, "VirtualKeyboard", nk_rect(x, y, w, h));
    if (nk_begin(ctx, "VirtualKeyboard",
                 nk_rect(x, y, w, h),
                 NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_BORDER))
    {
        int start = kb_start_note();
        int end   = kb_end_note();

        /* Header row: octave buttons + preset name */
        nk_layout_row_begin(ctx, NK_STATIC, 18, 5);
        {
            nk_layout_row_push(ctx, 30);
            if (nk_button_label(ctx, "<<")) {
                if (s_octave_offset > -2) s_octave_offset--;
            }
            nk_layout_row_push(ctx, 60);
            {
                int oct_num = (start / 12) - 1;
                char oct_label[16];
                snprintf(oct_label, sizeof(oct_label), "C%d-C%d", oct_num, oct_num + KB_NUM_OCTAVES);
                nk_label(ctx, oct_label, NK_TEXT_CENTERED);
            }
            nk_layout_row_push(ctx, 30);
            if (nk_button_label(ctx, ">>")) {
                if (s_octave_offset < 4) s_octave_offset++;
            }
            nk_layout_row_push(ctx, 10);
            nk_spacing(ctx, 1);
            nk_layout_row_push(ctx, 200);
            {
                const char *preset_name = "Synth";
                if (synth_preset >= 0 &&
                    (uint32_t)synth_preset < engine->num_synth_presets)
                    preset_name = engine->synth_presets[synth_preset].name;
                nk_labelf(ctx, NK_TEXT_LEFT, "Keyboard: %s", preset_name);
            }
        }
        nk_layout_row_end(ctx);

        /* Keyboard area — custom drawing */
        float kb_h = h - 44;
        if (kb_h < 40) kb_h = 40;
        nk_layout_row_dynamic(ctx, kb_h, 1);
        struct nk_rect bounds = nk_widget_bounds(ctx);
        nk_spacing(ctx, 1);

        struct nk_command_buffer *canvas = nk_window_get_canvas(ctx);

        float kb_x = bounds.x;
        float kb_y = bounds.y;
        float kb_w = bounds.w;

        float white_w = kb_w / (float)KB_NUM_WHITE;
        float black_w = white_w * 0.6f;
        float black_h = kb_h * 0.6f;

        /* Draw white keys */
        for (int i = 0; i < KB_NUM_WHITE; i++) {
            int octave = i / 7;
            int key_in_oct = i % 7;
            int midi = start + octave * 12 + WHITE_NOTES[key_in_oct];

            struct nk_color bg;
            if (midi == s_held_note)
                bg = nk_rgba(100, 180, 255, 255); /* pressed highlight */
            else
                bg = nk_rgba(220, 220, 225, 255);  /* white */

            float kx = kb_x + (float)i * white_w;
            nk_fill_rect(canvas,
                         nk_rect(kx, kb_y, white_w - 1, kb_h - 1),
                         0, bg);

            /* Key border */
            nk_stroke_rect(canvas,
                           nk_rect(kx, kb_y, white_w - 1, kb_h - 1),
                           0, 1, nk_rgba(80, 80, 85, 255));

            /* Label on C notes */
            if (key_in_oct == 0 && white_w > 12) {
                char label[8];
                int oct_num = (midi / 12) - 1;
                snprintf(label, sizeof(label), "C%d", oct_num);
                struct nk_rect lr = nk_rect(kx + 2, kb_y + kb_h - 16,
                                             white_w - 4, 14);
                nk_draw_text(canvas, lr, label, (int)strlen(label),
                             ctx->style.font,
                             nk_rgba(0, 0, 0, 0),
                             nk_rgba(80, 80, 90, 255));
            }
        }

        /* Draw black keys */
        for (int oct = 0; oct < KB_NUM_OCTAVES; oct++) {
            for (int b = 0; b < 5; b++) {
                int midi = start + oct * 12 + BLACK_NOTES[b];
                if (midi >= end) continue;

                /* Find left white key */
                int left_white = -1;
                int n = BLACK_NOTES[b];
                for (int i = 0; i < 7; i++) {
                    if (WHITE_NOTES[i] < n) left_white = i;
                }
                if (left_white < 0) continue;

                int wki = oct * 7 + left_white;
                float bx = kb_x + ((float)wki + 1.0f) * white_w - black_w * 0.5f;

                struct nk_color bg;
                if (midi == s_held_note)
                    bg = nk_rgba(60, 140, 220, 255); /* pressed highlight */
                else
                    bg = nk_rgba(25, 25, 30, 255);    /* black */

                nk_fill_rect(canvas,
                             nk_rect(bx, kb_y, black_w, black_h),
                             0, bg);
                nk_stroke_rect(canvas,
                               nk_rect(bx, kb_y, black_w, black_h),
                               0, 1, nk_rgba(15, 15, 18, 255));
            }
        }

        /* ── Mouse interaction ──────────────────────────────────────── */
        struct nk_vec2 mouse = ctx->input.mouse.pos;
        bool mouse_down = nk_input_is_mouse_down(&ctx->input, NK_BUTTON_LEFT);
        int hover_note = hit_test(mouse.x, mouse.y, kb_x, kb_y, kb_w, kb_h);

        if (mouse_down && hover_note >= 0) {
            if (hover_note != s_held_note) {
                /* Release previous note if different */
                if (s_held_note >= 0) {
                    float freq = 440.0f * powf(2.0f, ((float)s_held_note - 69.0f) / 12.0f);
                    for (int v = 0; v < SQ_MAX_SYNTH_VOICES; v++) {
                        if (engine->synth_voices[v].active &&
                            fabsf(engine->synth_voices[v].frequency - freq) < 0.1f) {
                            engine->synth_voices[v].amp_env.stage = ENV_RELEASE;
                        }
                    }
                }

                /* Trigger new note */
                if (synth_preset >= 0) {
                    synth_trigger(engine, synth_preset,
                                  0.8f, /* velocity */
                                  0,    /* pitch_offset */
                                  0.7f, /* volume */
                                  0.0f, /* pan */
                                  (uint8_t)hover_note);
                }
                s_held_note = hover_note;
            }
            s_mouse_was_down = true;
        }
        else if (s_mouse_was_down && !mouse_down) {
            /* Mouse released — note off */
            if (s_held_note >= 0) {
                float freq = 440.0f * powf(2.0f, ((float)s_held_note - 69.0f) / 12.0f);
                for (int v = 0; v < SQ_MAX_SYNTH_VOICES; v++) {
                    if (engine->synth_voices[v].active &&
                        fabsf(engine->synth_voices[v].frequency - freq) < 0.1f) {
                        engine->synth_voices[v].amp_env.stage = ENV_RELEASE;
                    }
                }
                s_held_note = -1;
            }
            s_mouse_was_down = false;
        }

        /* Show currently held note name */
        if (s_held_note >= 0) {
            nk_layout_row_dynamic(ctx, 14, 1);
            int n = s_held_note % 12;
            int oct = (s_held_note / 12) - 1;
            nk_labelf(ctx, NK_TEXT_CENTERED, "%s%d", NOTE_NAMES[n], oct);
        }
    }
    nk_end(ctx);
}
