/*
 * gui.cpp — SDL2 + OpenGL + Dear ImGui GUI initialization and main frame loop.
 *
 * Immediate-mode GUI: every frame we call ImGui functions that both draw
 * widgets AND return interaction results. No persistent widget state.
 *
 * Frame loop:
 * 1. Poll SDL events and feed to ImGui
 * 2. ImGui::NewFrame()
 * 3. Draw all panels (toolbar, drum grid, synth editor, etc.)
 * 4. ImGui::Render() + OpenGL render
 * 5. Swap buffers
 */

#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_opengl3.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>

extern "C" {
#include "gui/gui.h"
#include "gui/drum_grid.h"
#include "gui/knobs.h"
#include "gui/synth_editor.h"
#include "gui/sample_browser.h"
#include "gui/export_dialog.h"
#include "gui/piano_roll.h"
#include "gui/arrangement.h"
#include "gui/mixer_view.h"
#include "gui/undo.h"
#include "gui/pattern_presets.h"
#include "gui/virtual_keyboard.h"
#include "gui/theme.h"
#include "formats/project.h"
#include "engine/export.h"
#define LOG_TAG "gui"
#include "core/log.h"
}

#include <cstdio>
#include <cstring>
#include <cmath>

#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#endif

#define STEPS_PER_BEAT 4

/* ─── Native file dialogs ─────────────────────────────────────────────────── */

/* Returns true if user picked a file, writes path into buf (bufsize).
   is_save=true for Save dialog, false for Open dialog. */
static bool native_file_dialog(char *buf, size_t bufsize, bool is_save,
                                const char *default_path)
{
#ifdef _WIN32
    OPENFILENAMEA ofn;
    char file[512] = "";
    if (default_path && default_path[0])
        snprintf(file, sizeof(file), "%s", default_path);

    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL;
    ofn.lpstrFilter = "0x808 Projects (*.sqproj)\0*.sqproj\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = sizeof(file);
    ofn.lpstrDefExt = "sqproj";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;

    BOOL ok;
    if (is_save) {
        ofn.lpstrTitle = "Save Project";
        ok = GetSaveFileNameA(&ofn);
    } else {
        ofn.lpstrTitle = "Open Project";
        ofn.Flags |= OFN_FILEMUSTEXIST;
        ok = GetOpenFileNameA(&ofn);
    }
    if (ok) {
        snprintf(buf, bufsize, "%s", file);
        return true;
    }
    return false;
#else
    /* On Linux, just use the default path (no native dialog without GTK) */
    if (default_path && default_path[0])
        snprintf(buf, bufsize, "%s", default_path);
    return true;
#endif
}

/* ─── Module state ────────────────────────────────────────────────────────── */

static SDL_Window    *g_window = NULL;
static SDL_GLContext  g_gl_ctx = NULL;
/* globals defined in gui_globals.cpp (linked via libsq_gui) */
static bool g_show_browser = false;
static bool g_show_mixer = false;
static bool g_show_piano_roll = false;
static bool g_show_keyboard  = false;
static char g_project_path[512] = "";
static bool g_project_path_init = false;
static char g_save_status[128] = "";
static uint32_t g_status_timer = 0;
static sq_pattern_t g_clipboard_pattern;
static bool g_clipboard_valid = false;

static Uint64 g_play_start_ticks = 0;
static bool   g_was_playing      = false;

/* ─── Helper: draw a soft glow behind a rect ─────────────────────────────── */

static void DrawGlow(ImVec2 min, ImVec2 max, ImVec4 color, float expand = 4.0f,
                     int alpha = 50)
{
    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImU32 glow = IM_COL32(
        (int)(color.x * 255), (int)(color.y * 255), (int)(color.z * 255), alpha / 2);
    dl->AddRectFilled(
        ImVec2(min.x - expand * 2, min.y - expand * 2),
        ImVec2(max.x + expand * 2, max.y + expand * 2),
        glow, 8.0f);
    ImU32 inner = IM_COL32(
        (int)(color.x * 255), (int)(color.y * 255), (int)(color.z * 255), alpha);
    dl->AddRectFilled(
        ImVec2(min.x - expand, min.y - expand),
        ImVec2(max.x + expand, max.y + expand),
        inner, 6.0f);
}

/* ─── Helper: colored button with optional glow ──────────────────────────── */

static bool ColoredButton(const char *label, bool active,
                          ImVec4 active_color = ImVec4(0.39f, 0.71f, 1.00f, 1.00f),
                          ImVec2 size = ImVec2(0, 0))
{
    if (active) {
        /* Draw glow behind active buttons */
        ImVec2 pos = ImGui::GetCursorScreenPos();
        ImVec2 sz = size;
        if (sz.x <= 0) sz.x = ImGui::CalcTextSize(label).x + 16;
        if (sz.y <= 0) sz.y = ImGui::GetFrameHeight();
        DrawGlow(pos, ImVec2(pos.x + sz.x, pos.y + sz.y), active_color, 3.0f, 40);

        ImGui::PushStyleColor(ImGuiCol_Button, active_color);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(active_color.x * 1.1f, active_color.y * 1.1f, active_color.z * 1.1f, 1.0f));
    }
    bool clicked = ImGui::Button(label, size);
    if (active)
        ImGui::PopStyleColor(2);
    return clicked;
}

/* ─── Public API ──────────────────────────────────────────────────────────── */

int gui_init(int width, int height, const char *title)
{
    g_win_width  = width;
    g_win_height = height;

    LOG_INFO("gui_init: starting (w=%d h=%d)", width, height);

    LOG_INFO("gui_init: SDL_Init(VIDEO|AUDIO)...");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
        LOG_ERROR("SDL_Init failed: %s", SDL_GetError());
        return -1;
    }
    LOG_INFO("gui_init: SDL_Init OK");

    /* Request OpenGL 3.3 Core Profile */
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    LOG_INFO("gui_init: SDL_CreateWindow...");
    g_window = SDL_CreateWindow(
        title,
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        width, height,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI
    );
    if (!g_window) {
        LOG_ERROR("SDL_CreateWindow failed: %s", SDL_GetError());
        SDL_Quit();
        return -1;
    }
    SDL_SetWindowMinimumSize(g_window, 800, 500);
    LOG_INFO("gui_init: SDL_CreateWindow OK (min size 800x500)");

    LOG_INFO("gui_init: SDL_GL_CreateContext...");
    g_gl_ctx = SDL_GL_CreateContext(g_window);
    if (!g_gl_ctx) {
        LOG_ERROR("SDL_GL_CreateContext failed: %s", SDL_GetError());
        SDL_DestroyWindow(g_window);
        SDL_Quit();
        return -1;
    }
    LOG_INFO("gui_init: SDL_GL_CreateContext OK");

    SDL_GL_SetSwapInterval(1);

    /* Initialize Dear ImGui */
    LOG_INFO("gui_init: ImGui init...");
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    /* Disable imgui.ini file — we manage layout ourselves */
    io.IniFilename = NULL;

    ImGui_ImplSDL2_InitForOpenGL(g_window, g_gl_ctx);
    ImGui_ImplOpenGL3_Init("#version 330 core");

    /* Load embedded DejaVu Sans Mono Bold — open source console font */
    {
        #include "font_dejavu_mono_bold.h"
        ImFontConfig fc;
        fc.FontDataOwnedByAtlas = false; /* data is static, don't free */
        io.Fonts->AddFontFromMemoryTTF(
            DejaVuSansMono_Bold_ttf, DejaVuSansMono_Bold_ttf_len,
            15.0f, &fc);
        LOG_INFO("Loaded embedded font: DejaVu Sans Mono Bold (15px)");
    }

    LOG_INFO("gui_init: ImGui init OK");

    /* Apply hacker theme (90s green-on-black aesthetic) */
    theme_apply(THEME_HACKER);

    LOG_INFO("GUI initialized: %dx%d, OpenGL 3.3, Dear ImGui", width, height);
    return 0;
}

int gui_frame(sq_engine_t *engine)
{
    int quit = 0;

    /* Set default project path from engine base_dir on first frame */
    if (!g_project_path_init) {
        g_project_path_init = true;
        if (engine->base_dir[0])
            snprintf(g_project_path, sizeof(g_project_path), "%sproject.sqproj", engine->base_dir);
        else
            snprintf(g_project_path, sizeof(g_project_path), "project.sqproj");
    }

    /* 1. Poll SDL events and feed to ImGui */
    SDL_Event evt;
    while (SDL_PollEvent(&evt)) {
        ImGui_ImplSDL2_ProcessEvent(&evt);
        if (evt.type == SDL_QUIT)
            quit = 1;

        /* Keyboard shortcuts — transport keys (space, esc) and Ctrl combos
           always processed. QWERTY piano only when ImGui doesn't want keyboard. */
        bool is_ctrl = (evt.key.keysym.mod & KMOD_CTRL) != 0;
        bool imgui_wants_kb = ImGui::GetIO().WantCaptureKeyboard;

        /* Transport keys always work, even when ImGui has focus */
        if (evt.type == SDL_KEYDOWN && !imgui_wants_kb) {
            /* nothing — fall through to full handler below */
        }
        if (evt.type == SDL_KEYDOWN) {
            if (evt.key.keysym.sym == SDLK_SPACE) {
                engine->transport.playing = !engine->transport.playing;
                engine->transport.current_beat = 0.0;
                engine->transport.sample_position = 0;
                engine->transport.current_step = 0;
                g_visual_step = 0;
                if (engine->transport.playing)
                    g_play_start_ticks = SDL_GetPerformanceCounter();
                continue;
            }
            if (evt.key.keysym.sym == SDLK_ESCAPE) {
                quit = 1;
                continue;
            }
        }

        /* QWERTY piano — always active when keyboard panel is shown */
        if (!is_ctrl && g_show_keyboard &&
            !(evt.key.keysym.mod & KMOD_ALT) &&
            (evt.type == SDL_KEYDOWN || evt.type == SDL_KEYUP))
        {
            int kb_preset_key = -1;
            if (g_selected_track >= 0) {
                int kpi = engine->transport.current_pattern;
                if (kpi >= 0 && (uint32_t)kpi < engine->num_patterns) {
                    sq_pattern_t *kpat = &engine->patterns[kpi];
                    if ((uint32_t)g_selected_track < kpat->num_tracks &&
                        kpat->tracks[g_selected_track].type == TRACK_SYNTH)
                        kb_preset_key = kpat->tracks[g_selected_track].synth_preset;
                }
            }
            if (kb_preset_key < 0) {
                int kpi = engine->transport.current_pattern;
                if (kpi >= 0 && (uint32_t)kpi < engine->num_patterns) {
                    for (uint32_t tt = 0; tt < engine->patterns[kpi].num_tracks; tt++) {
                        if (engine->patterns[kpi].tracks[tt].type == TRACK_SYNTH) {
                            kb_preset_key = engine->patterns[kpi].tracks[tt].synth_preset;
                            break;
                        }
                    }
                }
            }
            if (kb_preset_key < 0) kb_preset_key = 0;
            if (virtual_keyboard_key_event(engine, kb_preset_key,
                                           evt.key.keysym.sym,
                                           evt.type == SDL_KEYDOWN))
                continue;
        }

        /* Pattern select 1-9 — always active */
        if (evt.type == SDL_KEYDOWN && !is_ctrl) {
            if (evt.key.keysym.sym >= SDLK_1 && evt.key.keysym.sym <= SDLK_9) {
                int pat = evt.key.keysym.sym - SDLK_1;
                if ((uint32_t)pat < engine->num_patterns) {
                    engine->transport.current_pattern = pat;
                    snprintf(g_save_status, sizeof(g_save_status), "Pattern %d", pat + 1);
                    g_status_timer = 90;
                }
                continue;
            }
        }

        if (is_ctrl || !imgui_wants_kb) {
            if (evt.type == SDL_KEYDOWN) {
                switch (evt.key.keysym.sym) {
                case SDLK_s:
                    if (evt.key.keysym.mod & KMOD_CTRL) {
                        char save_path[512];
                        if (native_file_dialog(save_path, sizeof(save_path), true, g_project_path)) {
                            if (project_save(engine, save_path) == 0) {
                                snprintf(g_project_path, sizeof(g_project_path), "%s", save_path);
                                snprintf(g_save_status, sizeof(g_save_status), "Saved: %.100s", save_path);
                            } else {
                                snprintf(g_save_status, sizeof(g_save_status), "Save FAILED!");
                            }
                        }
                        g_status_timer = 180;
                    }
                    break;
                case SDLK_o:
                    if (evt.key.keysym.mod & KMOD_CTRL) {
                        char load_path[512];
                        if (native_file_dialog(load_path, sizeof(load_path), false, g_project_path)) {
                            if (project_load(engine, load_path) == 0) {
                                snprintf(g_project_path, sizeof(g_project_path), "%s", load_path);
                                undo_clear();
                                theme_apply(theme_current());
                                snprintf(g_save_status, sizeof(g_save_status), "Loaded: %.100s", load_path);
                            } else {
                                snprintf(g_save_status, sizeof(g_save_status), "Load FAILED!");
                            }
                        }
                        g_status_timer = 180;
                    }
                    break;
                case SDLK_t:
                    if (evt.key.keysym.mod & KMOD_CTRL)
                        theme_toggle();
                    break;
                /* 1-9 pattern select handled above (always active) */
                case SDLK_c:
                    if (evt.key.keysym.mod & KMOD_CTRL) {
                        int pi = engine->transport.current_pattern;
                        if (pi >= 0 && (uint32_t)pi < engine->num_patterns) {
                            g_clipboard_pattern = engine->patterns[pi];
                            g_clipboard_valid = true;
                            snprintf(g_save_status, sizeof(g_save_status), "Copied pattern %d", pi + 1);
                            g_status_timer = 90;
                        }
                    }
                    break;
                case SDLK_v:
                    if ((evt.key.keysym.mod & KMOD_CTRL) && g_clipboard_valid) {
                        int pi = engine->transport.current_pattern;
                        if (pi >= 0 && (uint32_t)pi < engine->num_patterns) {
                            engine->patterns[pi] = g_clipboard_pattern;
                            snprintf(g_save_status, sizeof(g_save_status), "Pasted to pattern %d", pi + 1);
                            g_status_timer = 90;
                        }
                    }
                    break;
                case SDLK_z:
                    if (evt.key.keysym.mod & KMOD_CTRL) {
                        if (evt.key.keysym.mod & KMOD_SHIFT) {
                            if (undo_redo(engine)) {
                                snprintf(g_save_status, sizeof(g_save_status), "Redo");
                                g_status_timer = 90;
                            }
                        } else {
                            if (undo_undo(engine)) {
                                snprintf(g_save_status, sizeof(g_save_status), "Undo");
                                g_status_timer = 90;
                            }
                        }
                    }
                    break;
                case SDLK_EQUALS:
                    if (engine->num_patterns < SQ_MAX_PATTERNS) {
                        int ni = (int)engine->num_patterns;
                        engine->num_patterns++;
                        sq_pattern_t *np = &engine->patterns[ni];
                        memset(np, 0, sizeof(*np));
                        snprintf(np->name, SQ_PATTERN_NAME_LEN, "Pattern %d", ni + 1);
                        np->num_tracks = 4;
                        for (uint32_t t = 0; t < np->num_tracks; t++) {
                            np->tracks[t].length = 16;
                            np->tracks[t].volume = 0.8f;
                            np->tracks[t].sample_index = -1;
                            np->tracks[t].synth_preset = -1;
                        }
                        engine->transport.current_pattern = ni;
                        snprintf(g_save_status, sizeof(g_save_status), "New pattern %d", ni + 1);
                        g_status_timer = 90;
                    }
                    break;
                default:
                    break;
                }
            }
        }
    }

    /* Get current window size */
    SDL_GetWindowSize(g_window, &g_win_width, &g_win_height);

    /* Start ImGui frame */
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    /* ── Compute visual playhead ──────────────────────────────────────────── */
    if (engine->transport.playing) {
        Uint64 now = SDL_GetPerformanceCounter();
        double elapsed = (double)(now - g_play_start_ticks)
                       / (double)SDL_GetPerformanceFrequency();
        double beats = elapsed * (engine->transport.bpm / 60.0);
        int pat_len = 16;
        int pi = engine->transport.current_pattern;
        if (pi >= 0 && (uint32_t)pi < engine->num_patterns &&
            engine->patterns[pi].num_tracks > 0)
            pat_len = (int)engine->patterns[pi].tracks[0].length;
        g_visual_step = ((int)floor(beats * STEPS_PER_BEAT)) % pat_len;
    } else if (g_was_playing && !engine->transport.playing) {
        g_visual_step = 0;
    }
    g_was_playing = engine->transport.playing;

    /* ── Layout constants ─────────────────────────────────────────────────── */
    float toolbar_h = 60.0f;

    /* ── Toolbar (fixed at top) ───────────────────────────────────────────── */
    {
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2((float)g_win_width, toolbar_h));
        ImGui::Begin("Toolbar", NULL,
                     ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar |
                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBringToFrontOnFocus);

        /* ── Glowing 0x808 logo with box and pulse ────────────────────── */
        {
            const char *logo = "0x808";
            float scale = 2.0f;
            float font_sz = ImGui::GetFontSize() * scale;
            ImFont *font = ImGui::GetFont();

            ImVec2 text_sz = font->CalcTextSizeA(font_sz, FLT_MAX, 0.0f, logo);
            ImVec2 wpos = ImGui::GetWindowPos();
            float pad_x = 10.0f, pad_y = 5.0f;
            float box_w = text_sz.x + pad_x * 2;
            float box_h = text_sz.y + pad_y * 2;
            float bx = wpos.x + 10;
            float by = wpos.y + (toolbar_h - box_h) * 0.5f;
            float lx = bx + pad_x;
            float ly = by + pad_y;

            /* Theme accent color */
            ImVec4 gc = ImGui::GetStyleColorVec4(ImGuiCol_SliderGrab);
            int cr = (int)(gc.x * 255), cg = (int)(gc.y * 255), cb = (int)(gc.z * 255);

            /* Pulse: 0.4 .. 1.0 over ~3 seconds */
            float t = (float)SDL_GetTicks() / 1000.0f;
            float pulse = 0.4f + 0.6f * sinf(t * 2.094f);

            ImDrawList *dl = ImGui::GetForegroundDrawList();
            float rnd = 4.0f;

            /* Single soft glow outline on box — pulses */
            int outer_a = (int)(11 + 11 * pulse);
            dl->AddRect(ImVec2(bx - 2, by - 2), ImVec2(bx + box_w + 2, by + box_h + 2),
                        IM_COL32(cr, cg, cb, outer_a), rnd + 1, 0, 1.0f);
            /* Main box border */
            int border_a = (int)(53 + 33 * pulse);
            dl->AddRect(ImVec2(bx, by), ImVec2(bx + box_w, by + box_h),
                        IM_COL32(cr, cg, cb, border_a), rnd, 0, 1.5f);

            /* Text — single draw, subtle pulse between 93..147 alpha */
            int text_a = 93 + (int)(54 * pulse);
            dl->AddText(font, font_sz, ImVec2(lx, ly),
                        IM_COL32(cr, cg, cb, text_a), logo);

            ImGui::Dummy(ImVec2(box_w + 14, 35));
            ImGui::SameLine();
        }

        /* Transport buttons with glow — capture state before click can change it */
        bool was_playing = engine->transport.playing;
        {
            ImVec2 play_pos = ImGui::GetCursorScreenPos();
            if (was_playing) {
                DrawGlow(play_pos, ImVec2(play_pos.x + 60, play_pos.y + 35),
                         ImVec4(0.2f, 0.8f, 0.3f, 1.0f), 4.0f, 50);
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.55f, 0.20f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.65f, 0.25f, 1.0f));
            }
        }
        if (ImGui::Button(was_playing ? "STOP" : "PLAY", ImVec2(60, 35))) {
            engine->transport.playing = !engine->transport.playing;
            engine->transport.current_beat = 0.0;
            engine->transport.sample_position = 0;
            engine->transport.current_step = 0;
            g_visual_step = 0;
            if (engine->transport.playing)
                g_play_start_ticks = SDL_GetPerformanceCounter();
        }
        if (was_playing)
            ImGui::PopStyleColor(2);
        ImGui::SameLine();

        /* REC button — capture state before button click can change it */
        bool was_recording = engine->recording;
        if (was_recording) {
            ImVec2 rec_pos = ImGui::GetCursorScreenPos();
            DrawGlow(rec_pos, ImVec2(rec_pos.x + 55, rec_pos.y + 35),
                     ImVec4(0.9f, 0.15f, 0.15f, 1.0f), 4.0f, 55);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.78f, 0.12f, 0.12f, 1.0f));
        }
        if (ImGui::Button(was_recording ? "REC *" : "REC", ImVec2(55, 35))) {
            if (engine->recording) {
                engine->recording = false;
                if (engine->rec_frames > 0) {
                    /* Build full output path next to exe */
                    char rec_path[600];
                    snprintf(rec_path, sizeof(rec_path), "%srecording.wav",
                             engine->base_dir[0] ? engine->base_dir : "");
                    sq_export_result_t rec_result = {};
                    rec_result.data = engine->rec_buffer;
                    rec_result.num_frames = engine->rec_frames;
                    rec_result.sample_rate = engine->sample_rate;
                    sq_export_write_wav(rec_path, &rec_result, 16);
                    LOG_INFO("Saved recording: %u frames -> %s", engine->rec_frames, rec_path);
                    snprintf(g_save_status, sizeof(g_save_status), "Saved: %.120s", rec_path);
                    g_status_timer = 300;
                }
                engine->rec_frames = 0;
            } else {
                engine->rec_frames = 0;
                engine->recording = true;
                char rec_path[600];
                snprintf(rec_path, sizeof(rec_path), "%srecording.wav",
                         engine->base_dir[0] ? engine->base_dir : "");
                snprintf(g_save_status, sizeof(g_save_status), "REC -> %.120s", rec_path);
                g_status_timer = 0; /* stays visible while recording */
            }
        }
        if (was_recording)
            ImGui::PopStyleColor();

        /* Separator after transport */
        ImGui::SameLine(0, 4);
        { float sy = ImGui::GetCursorScreenPos().y; float sx = ImGui::GetCursorScreenPos().x;
          ImGui::GetWindowDrawList()->AddLine(ImVec2(sx, sy), ImVec2(sx, sy + 35), IM_COL32(80, 80, 85, 200), 1.0f);
          ImGui::Dummy(ImVec2(1, 35)); }
        ImGui::SameLine(0, 6);

        /* BPM knob */
        {
            ImGui::Text("BPM"); ImGui::SameLine();
            float bpm_f = (float)engine->transport.bpm;
            ImGui::PushID("bpm_knob");
            ImVec2 kpos = ImGui::GetCursorScreenPos();
            if (knob_core_ext("##bpm", &bpm_f, 40.0f, 300.0f, 120.0f, 1.0f, kpos.x, kpos.y, 35, 35))
                engine->transport.bpm = (double)bpm_f;
            ImGui::SameLine();
            char bpm_txt[16];
            snprintf(bpm_txt, sizeof(bpm_txt), "%.0f", bpm_f);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10);
            ImGui::Text("%s", bpm_txt);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 10);
            ImGui::PopID();
        }
        ImGui::SameLine(0, 4);
        { float sy = ImGui::GetCursorScreenPos().y; float sx = ImGui::GetCursorScreenPos().x;
          ImGui::GetWindowDrawList()->AddLine(ImVec2(sx, sy), ImVec2(sx, sy + 35), IM_COL32(80, 80, 85, 200), 1.0f);
          ImGui::Dummy(ImVec2(1, 35)); }
        ImGui::SameLine(0, 6);

        /* Swing knob */
        {
            ImGui::Text("Sw"); ImGui::SameLine();
            float swing_pct = engine->transport.swing * 100.0f;
            ImGui::PushID("sw_knob");
            ImVec2 kpos = ImGui::GetCursorScreenPos();
            if (knob_core_ext("##sw", &swing_pct, 0.0f, 100.0f, 0.0f, 0.5f, kpos.x, kpos.y, 35, 35))
                engine->transport.swing = swing_pct / 100.0f;
            ImGui::SameLine();
            char sw_txt[16];
            snprintf(sw_txt, sizeof(sw_txt), "%.0f%%", swing_pct);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10);
            ImGui::Text("%s", sw_txt);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 10);
            ImGui::PopID();
        }
        ImGui::SameLine(0, 4);
        { float sy = ImGui::GetCursorScreenPos().y; float sx = ImGui::GetCursorScreenPos().x;
          ImGui::GetWindowDrawList()->AddLine(ImVec2(sx, sy), ImVec2(sx, sy + 35), IM_COL32(80, 80, 85, 200), 1.0f);
          ImGui::Dummy(ImVec2(1, 35)); }
        ImGui::SameLine(0, 6);

        /* Volume knob */
        {
            ImGui::Text("Vol"); ImGui::SameLine();
            float vol_pct = engine->master_volume * 100.0f;
            ImGui::PushID("vol_knob");
            ImVec2 kpos = ImGui::GetCursorScreenPos();
            if (knob_core_ext("##vol", &vol_pct, 0.0f, 100.0f, 80.0f, 0.5f, kpos.x, kpos.y, 35, 35))
                engine->master_volume = vol_pct / 100.0f;
            ImGui::SameLine();
            char vol_txt[16];
            snprintf(vol_txt, sizeof(vol_txt), "%.0f%%", vol_pct);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10);
            ImGui::Text("%s", vol_txt);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 10);
            ImGui::PopID();
        }
        ImGui::SameLine(0, 4);
        { float sy = ImGui::GetCursorScreenPos().y; float sx = ImGui::GetCursorScreenPos().x;
          ImGui::GetWindowDrawList()->AddLine(ImVec2(sx, sy), ImVec2(sx, sy + 35), IM_COL32(80, 80, 85, 200), 1.0f);
          ImGui::Dummy(ImVec2(1, 35)); }
        ImGui::SameLine(0, 6);

        /* Panel toggle buttons — consistent sizing */
        const float btn_w = 70.0f;
        const float btn_h = 35.0f;
        const ImVec2 btn_sz(btn_w, btn_h);
        const ImVec2 btn_sm(50.0f, btn_h);  /* narrow buttons (PAT/FX) */

        if (ImGui::Button("EXPORT", btn_sz))
            export_dialog_show();
        ImGui::SameLine();

        {
            int *vis = pattern_presets_visible_ptr();
            if (ColoredButton(*vis ? "PRESETS*" : "PRESETS", *vis != 0, ImVec4(0.71f, 0.55f, 0.24f, 1.0f), btn_sz))
                *vis = !*vis;
        }
        ImGui::SameLine();

        if (ColoredButton(g_show_keyboard ? "PIANO*" : "PIANO", g_show_keyboard, ImVec4(0.24f, 0.47f, 0.71f, 1.0f), btn_sz))
            g_show_keyboard = !g_show_keyboard;
        ImGui::SameLine();

        {
            const char *mode_labels[] = {"PAT", "SONG", "PERF"};
            ImVec4 mode_colors[] = {
                ImVec4(0.22f, 0.22f, 0.24f, 1.0f),
                ImVec4(0.24f, 0.39f, 0.71f, 1.0f),
                ImVec4(0.71f, 0.24f, 0.39f, 1.0f),
            };
            if (ColoredButton(mode_labels[engine->transport.mode],
                              engine->transport.mode != MODE_PATTERN,
                              mode_colors[engine->transport.mode], btn_sm))
                engine->transport.mode = (sq_play_mode_t)((engine->transport.mode + 1) % 3);
        }
        ImGui::SameLine();

        /* Pattern selector: clickable numbered buttons, active one highlighted */
        {
            ImVec4 active_col(0.24f, 0.63f, 0.39f, 1.0f);
            ImVec4 inactive_col(0.18f, 0.18f, 0.20f, 1.0f);
            float pat_btn_w = 28.0f;
            for (uint32_t p = 0; p < engine->num_patterns && p < 9; p++) {
                char plbl[4];
                snprintf(plbl, sizeof(plbl), "%u", p + 1);
                bool is_active = ((int)p == engine->transport.current_pattern);
                if (ColoredButton(plbl, is_active, is_active ? active_col : inactive_col,
                                  ImVec2(pat_btn_w, btn_h)))
                    engine->transport.current_pattern = (int)p;
                ImGui::SameLine(0, 2);
            }
            /* "+" button to add pattern */
            if (engine->num_patterns < SQ_MAX_PATTERNS) {
                if (ImGui::Button("+##addpat", ImVec2(pat_btn_w, btn_h))) {
                    int ni = (int)engine->num_patterns;
                    engine->num_patterns++;
                    sq_pattern_t *np = &engine->patterns[ni];
                    memset(np, 0, sizeof(*np));
                    snprintf(np->name, SQ_PATTERN_NAME_LEN, "Pattern %d", ni + 1);
                    np->num_tracks = 4;
                    for (uint32_t t = 0; t < np->num_tracks; t++) {
                        np->tracks[t].volume = 1.0f;
                        np->tracks[t].sample_index = (int)(t < engine->num_samples ? t : 0);
                        np->tracks[t].synth_preset = -1;
                    }
                    engine->transport.current_pattern = ni;
                }
                ImGui::SameLine(0, 2);
            }
        }
        ImGui::SameLine();

        if (ColoredButton(g_show_mixer ? "FX*" : "FX", g_show_mixer, ImVec4(0.51f, 0.31f, 0.63f, 1.0f), btn_sm))
            g_show_mixer = !g_show_mixer;
        ImGui::SameLine();

        if (ColoredButton(g_show_browser ? "BROWSE*" : "BROWSE", g_show_browser, ImVec4(0.24f, 0.51f, 0.24f, 1.0f), btn_sz))
            g_show_browser = !g_show_browser;
        ImGui::SameLine();

        /* THEME button with popup selector */
        {
            if (ImGui::Button("THEME", btn_sm)) {
                ImGui::OpenPopup("ThemePopup");
            }
            if (ImGui::BeginPopup("ThemePopup")) {
                ImGui::Text("Select Theme:");
                ImGui::Separator();
                for (int t = 0; t < THEME_COUNT; t++) {
                    bool selected = (theme_current() == (sq_theme_t)t);
                    if (ImGui::Selectable(theme_name((sq_theme_t)t), selected)) {
                        theme_apply((sq_theme_t)t);
                    }
                }
                if (theme_num_user_themes() > 0) {
                    ImGui::Separator();
                    ImGui::TextDisabled("User Themes:");
                    for (int u = 0; u < theme_num_user_themes(); u++) {
                        if (ImGui::Selectable(theme_user_name(u), false)) {
                            theme_apply_user(u);
                        }
                    }
                }
                ImGui::EndPopup();
            }
        }
        ImGui::SameLine();

        /* EXIT button — vibrant red with forced white text */
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.80f, 0.10f, 0.10f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.95f, 0.20f, 0.20f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.00f, 0.30f, 0.30f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
        if (ImGui::Button("EXIT", btn_sz)) {
            LOG_INFO("EXIT button pressed");
            quit = 1;
        }
        ImGui::PopStyleColor(4);

        /* Status message — stays visible while recording */
        if (engine->recording) {
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%s", g_save_status);
        } else if (g_status_timer > 0) {
            ImGui::TextColored(ImVec4(0.39f, 1.0f, 0.39f, 1.0f), "%s", g_save_status);
            g_status_timer--;
        }

        ImGui::End();
    }

    /* ── Determine if synth editor should be shown ────────────────────────── */
    bool show_synth_editor = false;
    int synth_preset_idx = -1;
    int *synth_preset_ptr = NULL;
    if (g_selected_track >= 0) {
        int pi = engine->transport.current_pattern;
        if (pi >= 0 && (uint32_t)pi < engine->num_patterns) {
            sq_pattern_t *pat = &engine->patterns[pi];
            if ((uint32_t)g_selected_track < pat->num_tracks &&
                pat->tracks[g_selected_track].type == TRACK_SYNTH) {
                show_synth_editor = true;
                synth_preset_ptr = &pat->tracks[g_selected_track].synth_preset;
                synth_preset_idx = *synth_preset_ptr;
            }
        }
    }

    /* ── Arrangement panel ────────────────────────────────────────────────── */
    float arrange_h = 0.0f;
    if (engine->transport.mode != MODE_PATTERN) {
        arrange_h = 160.0f;
        arrangement_draw(engine, 0.0f, toolbar_h, (float)g_win_width, arrange_h);
    }

    /* ── Main layout ──────────────────────────────────────────────────────── */
    float grid_y = toolbar_h + arrange_h;
    float kb_reserve = g_show_keyboard ? 120.0f : 0.0f;
    float total_h = (float)g_win_height - grid_y - kb_reserve;
    float total_w = (float)g_win_width;

    float browser_w = 0.0f;
    float main_w = total_w;
    if (g_show_browser) {
        browser_w = 350.0f;
        if (browser_w > total_w * 0.4f)
            browser_w = total_w * 0.4f;
        main_w = total_w - browser_w;
    }

    /* ── Full-height piano roll mode ──────────────────────────────────────── */
    if (g_show_piano_roll && show_synth_editor && synth_preset_idx >= 0) {
        if (g_show_mixer) {
            float pr_w = main_w * 0.60f;
            float se_w = main_w * 0.20f;
            float mx_w = main_w - pr_w - se_w;
            piano_roll_draw(engine, g_selected_track, 0.0f, grid_y, pr_w, total_h);
            synth_editor_draw(engine, synth_preset_ptr, pr_w, grid_y, se_w, total_h);
            mixer_view_draw(engine, pr_w + se_w, grid_y, mx_w, total_h);
        } else {
            float pr_w = main_w * 0.70f;
            float se_w = main_w - pr_w;
            piano_roll_draw(engine, g_selected_track, 0.0f, grid_y, pr_w, total_h);
            synth_editor_draw(engine, synth_preset_ptr, pr_w, grid_y, se_w, total_h);
        }
    } else {
        /* Normal layout */
        float grid_h, bottom_h;
        bool has_bottom = show_synth_editor || g_show_mixer;
        if (has_bottom) {
            bottom_h = 360.0f;
            if (g_show_keyboard && bottom_h > total_h - 150.0f)
                bottom_h = total_h - 150.0f;
            if (bottom_h < 120.0f) bottom_h = 120.0f;
            grid_h = total_h - bottom_h;
            if (grid_h < 150.0f) grid_h = 150.0f;
            bottom_h = total_h - grid_h;
        } else {
            grid_h = total_h;
            bottom_h = 0.0f;
        }

        drum_grid_draw(engine, 0.0f, grid_y, main_w, grid_h);

        if (has_bottom) {
            float bottom_y = grid_y + grid_h;
            if (show_synth_editor && synth_preset_idx >= 0 && g_show_mixer) {
                float pr_w = main_w * 0.40f;
                float se_w = main_w * 0.30f;
                float mx_w = main_w - pr_w - se_w;
                piano_roll_draw(engine, g_selected_track, 0.0f, bottom_y, pr_w, bottom_h);
                synth_editor_draw(engine, synth_preset_ptr, pr_w, bottom_y, se_w, bottom_h);
                mixer_view_draw(engine, pr_w + se_w, bottom_y, mx_w, bottom_h);
            } else if (show_synth_editor && synth_preset_idx >= 0) {
                float pr_w = main_w * 0.55f;
                float se_w = main_w - pr_w;
                piano_roll_draw(engine, g_selected_track, 0.0f, bottom_y, pr_w, bottom_h);
                synth_editor_draw(engine, synth_preset_ptr, pr_w, bottom_y, se_w, bottom_h);
            } else if (g_show_mixer) {
                mixer_view_draw(engine, 0.0f, bottom_y, main_w, bottom_h);
            }
        }
    }

    if (g_show_browser) {
        sample_browser_draw(engine, main_w, grid_y, browser_w, total_h);
        if (sample_browser_close_requested())
            g_show_browser = false;
    }

    /* Virtual keyboard */
    if (g_show_keyboard) {
        float kb_h = 120.0f;
        int kb_preset = synth_preset_idx;
        if (kb_preset < 0) {
            int pi2 = engine->transport.current_pattern;
            if (pi2 >= 0 && (uint32_t)pi2 < engine->num_patterns) {
                for (uint32_t tt = 0; tt < engine->patterns[pi2].num_tracks; tt++) {
                    if (engine->patterns[pi2].tracks[tt].type == TRACK_SYNTH) {
                        kb_preset = engine->patterns[pi2].tracks[tt].synth_preset;
                        break;
                    }
                }
            }
        }
        if (kb_preset < 0) kb_preset = 0;
        virtual_keyboard_draw(engine, kb_preset,
                              0.0f, (float)g_win_height - kb_h, main_w, kb_h);
    }

    /* Floating dialogs */
    if (export_dialog_visible())
        export_dialog_draw(engine);
    pattern_presets_draw(engine);

    /* ── Render ───────────────────────────────────────────────────────────── */
    ImGui::Render();
    float bg[4];
    theme_get_clear_color(bg);
    glViewport(0, 0, g_win_width, g_win_height);
    glClearColor(bg[0], bg[1], bg[2], bg[3]);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    SDL_GL_SwapWindow(g_window);

    return quit;
}

void gui_shutdown(void)
{
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    if (g_gl_ctx) SDL_GL_DeleteContext(g_gl_ctx);
    if (g_window) SDL_DestroyWindow(g_window);
    SDL_Quit();
    LOG_INFO("GUI shut down");
}
