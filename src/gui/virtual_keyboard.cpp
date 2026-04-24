/*
 * virtual_keyboard.cpp — Clickable piano keyboard for live synth playing,
 * Dear ImGui port.
 *
 * Draws a 3-octave piano keyboard (C3-B5). Left-click a key to trigger
 * a synth voice; release to send note-off. Supports mouse drag across
 * keys for glissando.
 *
 * Layout (per octave):
 *   +--+-++-+--+--+-++-++-+--+
 *   |  |X||X|  |  |X||X||X|  |
 *   |  |X||X|  |  |X||X||X|  |
 *   | C| || | E| F| || || | B|
 *   +--+-++-+--+--+-++-++-+--+
 */

#include "imgui.h"

extern "C" {
#include "engine/engine.h"
#include "engine/synth.h"
#include "gui/virtual_keyboard.h"
#include "gui/gui.h"
}

#include <cstdio>
#include <cstring>
#include <cmath>

#include <SDL2/SDL_keycode.h>

/* --- Constants ----------------------------------------------------------- */

#define KB_NUM_OCTAVES   3
#define KB_WHITE_PER_OCT 7
#define KB_NUM_WHITE    (KB_NUM_OCTAVES * KB_WHITE_PER_OCT + 1) /* +1 for top C */

/* Which notes in an octave are white keys (0=C,2=D,4=E,5=F,7=G,9=A,11=B) */
static const int WHITE_NOTES[] = {0, 2, 4, 5, 7, 9, 11};
/* Black key offsets within an octave (1=C#,3=D#,6=F#,8=G#,10=A#) */
static const int BLACK_NOTES[] = {1, 3, 6, 8, 10};

/* --- State --------------------------------------------------------------- */

static int  s_held_note     = -1;      /* currently held MIDI note (-1 = none) */
static int  s_held_voice    = -1;      /* voice index (for release) */
static bool s_mouse_was_down = false;
static int  s_octave_offset = 0;       /* octave shift: -2..+4 (base C3 = octave 0) */

/* --- Note names ---------------------------------------------------------- */

static const char *NOTE_NAMES[] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};

/* --- Helpers ------------------------------------------------------------- */

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

/* Release a held note by finding its voice and setting envelope to release */
static void release_note(sq_engine_t *engine, int midi_note)
{
    float freq = 440.0f * powf(2.0f, ((float)midi_note - 69.0f) / 12.0f);
    for (int v = 0; v < SQ_MAX_SYNTH_VOICES; v++) {
        if (engine->synth_voices[v].active &&
            fabsf(engine->synth_voices[v].frequency - freq) < 0.1f) {
            engine->synth_voices[v].amp_env.stage = ENV_RELEASE;
        }
    }
}

/* --- Draw ---------------------------------------------------------------- */

void virtual_keyboard_draw(sq_engine_t *engine,
                           int synth_preset,
                           float x, float y, float w, float h)
{
    int start = kb_start_note();
    int end   = kb_end_note();

    /* Set up ImGui window */
    ImGui::SetNextWindowPos(ImVec2(x, y));
    ImGui::SetNextWindowSize(ImVec2(w, h));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar |
                             ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

    if (!ImGui::Begin("VirtualKeyboard", nullptr, flags)) {
        ImGui::End();
        return;
    }

    /* Header row: octave buttons + preset name */
    {
        if (ImGui::Button("<<", ImVec2(30, 18))) {
            if (s_octave_offset > -2) s_octave_offset--;
        }
        ImGui::SameLine();

        int oct_num = (start / 12) - 1;
        char oct_label[16];
        snprintf(oct_label, sizeof(oct_label), "C%d-C%d", oct_num, oct_num + KB_NUM_OCTAVES);
        ImGui::Text("%s", oct_label);
        ImGui::SameLine();

        if (ImGui::Button(">>", ImVec2(30, 18))) {
            if (s_octave_offset < 4) s_octave_offset++;
        }
        ImGui::SameLine();

        ImGui::Dummy(ImVec2(10, 0));
        ImGui::SameLine();

        const char *preset_name = "Synth";
        if (synth_preset >= 0 &&
            (uint32_t)synth_preset < engine->num_synth_presets)
            preset_name = engine->synth_presets[synth_preset].name;
        ImGui::Text("Keyboard: %s", preset_name);
    }

    /* Keyboard area - custom drawing */
    float header_used = ImGui::GetCursorScreenPos().y - y;
    float kb_h = h - header_used - 4.0f;
    if (kb_h < 40) kb_h = 40;

    ImVec2 kb_screen_pos = ImGui::GetCursorScreenPos();
    float kb_x = kb_screen_pos.x;
    float kb_y = kb_screen_pos.y;
    float kb_w = w - 16.0f; /* subtract window padding */

    float white_w = kb_w / (float)KB_NUM_WHITE;
    float black_w = white_w * 0.6f;
    float black_h = kb_h * 0.6f;

    ImDrawList *draw = ImGui::GetWindowDrawList();

    /* Draw white keys */
    for (int i = 0; i < KB_NUM_WHITE; i++) {
        int octave = i / 7;
        int key_in_oct = i % 7;
        int midi = start + octave * 12 + WHITE_NOTES[key_in_oct];

        ImU32 bg;
        if (midi == s_held_note)
            bg = IM_COL32(100, 180, 255, 255); /* pressed highlight */
        else
            bg = IM_COL32(220, 220, 225, 255);  /* white */

        float kx = kb_x + (float)i * white_w;
        float key_rnd = 3.0f;
        draw->AddRectFilled(
            ImVec2(kx, kb_y),
            ImVec2(kx + white_w - 1, kb_y + kb_h - 1),
            bg, key_rnd);

        /* Key border */
        draw->AddRect(
            ImVec2(kx, kb_y),
            ImVec2(kx + white_w - 1, kb_y + kb_h - 1),
            IM_COL32(80, 80, 85, 255), key_rnd, 0, 1.0f);

        /* Label on C notes */
        if (key_in_oct == 0 && white_w > 12) {
            char label[8];
            int o = (midi / 12) - 1;
            snprintf(label, sizeof(label), "C%d", o);
            draw->AddText(
                ImVec2(kx + 2, kb_y + kb_h - 16),
                IM_COL32(80, 80, 90, 255), label);
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

            ImU32 bg;
            if (midi == s_held_note)
                bg = IM_COL32(60, 140, 220, 255); /* pressed highlight */
            else
                bg = IM_COL32(25, 25, 30, 255);    /* black */

            float bk_rnd = 2.0f;
            draw->AddRectFilled(
                ImVec2(bx, kb_y),
                ImVec2(bx + black_w, kb_y + black_h),
                bg, bk_rnd);
            draw->AddRect(
                ImVec2(bx, kb_y),
                ImVec2(bx + black_w, kb_y + black_h),
                IM_COL32(15, 15, 18, 255), bk_rnd, 0, 1.0f);
        }
    }

    /* --- Mouse interaction ------------------------------------------------ */
    ImGuiIO &io = ImGui::GetIO();
    ImVec2 mouse = io.MousePos;
    bool mouse_down = io.MouseDown[0];
    int hover_note = hit_test(mouse.x, mouse.y, kb_x, kb_y, kb_w, kb_h);

    if (mouse_down && hover_note >= 0 && ImGui::IsWindowHovered()) {
        if (hover_note != s_held_note) {
            /* Release previous note if different */
            if (s_held_note >= 0) {
                release_note(engine, s_held_note);
            }

            /* Trigger new note */
            if (synth_preset >= 0) {
                synth_trigger(engine, synth_preset,
                              0.8f, /* velocity */
                              0,    /* pitch_offset */
                              0.7f, /* volume */
                              0.0f, /* pan */
                              (uint8_t)hover_note,
                              -1);  /* no track binding (UI keyboard) */
            }
            s_held_note = hover_note;
        }
        s_mouse_was_down = true;
    }
    else if (s_mouse_was_down && !mouse_down) {
        /* Mouse released - note off */
        if (s_held_note >= 0) {
            release_note(engine, s_held_note);
            s_held_note = -1;
        }
        s_mouse_was_down = false;
    }

    /* Show currently held note name */
    if (s_held_note >= 0) {
        int n = s_held_note % 12;
        int oct = (s_held_note / 12) - 1;
        /* Draw note name centered below the keyboard */
        char note_label[16];
        snprintf(note_label, sizeof(note_label), "%s%d", NOTE_NAMES[n], oct);
        ImVec2 text_size = ImGui::CalcTextSize(note_label);
        draw->AddText(
            ImVec2(kb_x + kb_w * 0.5f - text_size.x * 0.5f, kb_y + kb_h + 2),
            IM_COL32(200, 200, 200, 255), note_label);
    }

    ImGui::End();
}

/* --- QWERTY keyboard -> MIDI note mapping -------------------------------- */

/* Two octaves mapped across QWERTY keyboard:
 *
 * Lower octave (Z row = white keys, A/S row = black keys):
 *   Z  X  C  V  B  N  M  ,  .  /
 *   C  D  E  F  G  A  B  C  D  E
 *     S  D     G  H  J     L  ;
 *     C# D#    F# G# A#    C# D#
 *
 * Upper octave (Q row = white keys, number row = black keys):
 *   Q  W  E  R  T  Y  U  I  O  P
 *   C  D  E  F  G  A  B  C  D  E
 *     2  3     5  6  7     9  0
 *     C# D#    F# G# A#    C# D#
 */

/* Map SDL keycode to semitone offset from base octave.
 * Returns -1 if the key is not mapped.
 * octave_out: 0 = lower octave (Z row), 1 = upper octave (Q row) */
static int keycode_to_semitone(int keycode, int *octave_out)
{
    *octave_out = 0;
    switch (keycode) {
    /* Lower octave - white keys (Z row) */
    case SDLK_z: return 0;   /* C */
    case SDLK_x: return 2;   /* D */
    case SDLK_c: return 4;   /* E */
    case SDLK_v: return 5;   /* F */
    case SDLK_b: return 7;   /* G */
    case SDLK_n: return 9;   /* A */
    case SDLK_m: return 11;  /* B */
    case SDLK_COMMA:  return 12; /* C+1 */
    case SDLK_PERIOD: return 14; /* D+1 */
    case SDLK_SLASH:  return 16; /* E+1 */

    /* Lower octave - black keys (A/S row) */
    case SDLK_s: return 1;   /* C# */
    case SDLK_d: return 3;   /* D# */
    case SDLK_g: return 6;   /* F# */
    case SDLK_h: return 8;   /* G# */
    case SDLK_j: return 10;  /* A# */
    case SDLK_l:         return 13; /* C#+1 */
    case SDLK_SEMICOLON: return 15; /* D#+1 */

    /* Upper octave - white keys (Q row) */
    case SDLK_q: *octave_out = 1; return 0;
    case SDLK_w: *octave_out = 1; return 2;
    case SDLK_e: *octave_out = 1; return 4;
    case SDLK_r: *octave_out = 1; return 5;
    case SDLK_t: *octave_out = 1; return 7;
    case SDLK_y: *octave_out = 1; return 9;
    case SDLK_u: *octave_out = 1; return 11;
    case SDLK_i: *octave_out = 1; return 12;
    case SDLK_o: *octave_out = 1; return 14;
    case SDLK_p: *octave_out = 1; return 16;

    /* Upper octave - black keys (number row) */
    case SDLK_2: *octave_out = 1; return 1;
    case SDLK_3: *octave_out = 1; return 3;
    case SDLK_5: *octave_out = 1; return 6;
    case SDLK_6: *octave_out = 1; return 8;
    case SDLK_7: *octave_out = 1; return 10;
    case SDLK_9: *octave_out = 1; return 13;
    case SDLK_0: *octave_out = 1; return 15;

    default: return -1;
    }
}

/* Track which MIDI notes are currently held by keyboard keys */
#define MAX_KB_HELD 16
static struct { int keycode; int midi_note; } s_kb_held[MAX_KB_HELD];
static int s_kb_held_count = 0;

int virtual_keyboard_key_event(sq_engine_t *engine, int synth_preset,
                               int sdl_keycode, int pressed)
{
    if (synth_preset < 0) return 0;

    int octave;
    int semitone = keycode_to_semitone(sdl_keycode, &octave);
    if (semitone < 0) return 0;

    /* Base MIDI note from current octave offset */
    int base = kb_start_note() + octave * 12;
    int midi_note = base + semitone;
    if (midi_note < 0 || midi_note > 127) return 0;

    if (pressed) {
        /* Don't re-trigger if already held (key repeat) */
        for (int i = 0; i < s_kb_held_count; i++) {
            if (s_kb_held[i].keycode == sdl_keycode) return 1;
        }

        /* Trigger note */
        synth_trigger(engine, synth_preset,
                      0.8f, 0, 0.7f, 0.0f, (uint8_t)midi_note, -1);

        if (s_kb_held_count < MAX_KB_HELD) {
            s_kb_held[s_kb_held_count].keycode = sdl_keycode;
            s_kb_held[s_kb_held_count].midi_note = midi_note;
            s_kb_held_count++;
        }
    } else {
        /* Release: find and remove from held list, send note-off */
        for (int i = 0; i < s_kb_held_count; i++) {
            if (s_kb_held[i].keycode == sdl_keycode) {
                int note = s_kb_held[i].midi_note;
                release_note(engine, note);
                /* Remove from held list */
                s_kb_held[i] = s_kb_held[s_kb_held_count - 1];
                s_kb_held_count--;
                break;
            }
        }
    }

    return 1; /* consumed */
}
