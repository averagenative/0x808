/*
 * drum_grid.cpp — Visual drum step grid (Dear ImGui port).
 *
 * Layout:
 * +----------------+----+----+----+----+----+----+ ... +----+
 * |  Track Name    | 1  | 2  | 3  | 4  | 5  | 6  |     | 16 |
 * |  [Vol] [Pan]   |    |    | XX |    | XX |    |     |    |
 * |  [M] [S]       |    |    |    |    |    |    |     |    |
 * +----------------+----+----+----+----+----+----+ ... +----+
 * |  Snare         |    |    |    |    | XX |    |     |    |
 * |  ...           |    |    |    |    |    |    |     |    |
 * +----------------+----+----+----+----+----+----+ ... +----+
 *                                ^ playback position highlight
 *
 * Interactions:
 *   Left-click:   toggle step on/off (velocity 0 <-> 100)
 *   Right-click:  open velocity/pitch editor popup at mouse position
 *   Scroll wheel: adjust velocity +/-5 per tick when hovering
 */

#include "imgui.h"

extern "C" {
#include "gui/drum_grid.h"
#include "gui/gui.h"
#include "gui/knobs.h"
#include "gui/undo.h"
#include "gui/theme.h"
#include "gui/mixer_view.h"
#include "engine/sampler.h"
#include "engine/sequencer.h"
#include "engine/kits.h"
}

extern "C" {
#define LOG_TAG "drum_grid"
#include "core/log.h"
}

#include <stdio.h>
#include <string.h>
#include <math.h>

/* --- Colors for the grid ------------------------------------------------- */

/* Track color palette -- 8 user-selectable colors, cycled via right-click on track name */
#define NUM_TRACK_COLORS 8
static const ImU32 track_colors[] = {
    IM_COL32(220, 80,  80,  255),  /* 0: red     */
    IM_COL32(80,  180, 220, 255),  /* 1: blue    */
    IM_COL32(80,  200, 120, 255),  /* 2: green   */
    IM_COL32(220, 180, 60,  255),  /* 3: yellow  */
    IM_COL32(180, 100, 220, 255),  /* 4: purple  */
    IM_COL32(220, 140, 60,  255),  /* 5: orange  */
    IM_COL32(100, 200, 200, 255),  /* 6: cyan    */
    IM_COL32(200, 120, 160, 255),  /* 7: pink    */
};

/* Extract RGBA components from an ImU32 color */
static inline int col_r(ImU32 c) { return (int)(c >>  0) & 0xFF; }
static inline int col_g(ImU32 c) { return (int)(c >>  8) & 0xFF; }
static inline int col_b(ImU32 c) { return (int)(c >> 16) & 0xFF; }

static inline ImU32 make_col(int r, int g, int b, int a = 255) {
    if (r > 255) r = 255; if (r < 0) r = 0;
    if (g > 255) g = 255; if (g < 0) g = 0;
    if (b > 255) b = 255; if (b < 0) b = 0;
    if (a > 255) a = 255; if (a < 0) a = 0;
    return IM_COL32(r, g, b, a);
}

static const ImU32 cell_inactive = IM_COL32(50, 50, 55, 255);

/* --- State for right-click velocity/pitch editor ------------------------- */

static int  popup_track = -1;
static int  popup_step  = -1;
static bool popup_open  = false;
static bool popup_just_opened = false; /* true on the frame we first open */

/* Track right-click state for reliable one-shot detection. */
static bool rclick_was_down = false;

/* --- State for left-click drag across pads ------------------------------- */
static bool  drag_active     = false;  /* currently dragging? */
static bool  drag_set_on     = false;  /* true = turning on, false = turning off */
static int   drag_last_track = -1;     /* last toggled cell (avoid re-toggling same) */
static int   drag_last_step  = -1;

/* --- State for right-click drag (velocity/pitch) ------------------------- */
static int   rclick_drag_track = -1;
static int   rclick_drag_step  = -1;
static ImVec2 rclick_drag_origin;
static bool  rclick_dragging = false;
static int   rclick_drag_base_vel = 100;
static int   rclick_drag_base_pitch = 0;

/* --- Globals ------------------------------------------------------------- */
extern "C" {
    extern int g_win_width;
    extern int g_win_height;
    extern int g_visual_step;
    extern int g_selected_track;
}

/* --- Helper: convert ImU32 to ImVec4 ------------------------------------- */
static ImVec4 col32_to_vec4(ImU32 c) {
    return ImVec4(
        (float)((c >>  0) & 0xFF) / 255.0f,
        (float)((c >>  8) & 0xFF) / 255.0f,
        (float)((c >> 16) & 0xFF) / 255.0f,
        (float)((c >> 24) & 0xFF) / 255.0f
    );
}

/* --- Draw the grid ------------------------------------------------------- */

void drum_grid_draw(sq_engine_t *engine,
                    float x, float y, float w, float h)
{
    int pat_idx = engine->transport.current_pattern;
    if (pat_idx < 0 || (uint32_t)pat_idx >= engine->num_patterns) return;
    sq_pattern_t *pattern = &engine->patterns[pat_idx];
    if (pattern->num_tracks == 0) return;

    /* Layout constants */
    float track_panel_w = 200.0f;  /* width for track name + controls */
    float cell_pad = 2.0f;
    /* Use longest track length for playhead and global grid width */
    uint32_t num_steps = 16;
    for (uint32_t t = 0; t < pattern->num_tracks; t++) {
        if (pattern->tracks[t].length > num_steps)
            num_steps = pattern->tracks[t].length;
    }
    float grid_w = w - track_panel_w - 32.0f; /* extra margin for glow bleed */
    float cell_w = grid_w / (float)num_steps;
    float row_h_collapsed = 66.0f;  /* unselected: name + mute/solo + type badge */
    float row_h_expanded  = 130.0f; /* selected: all controls visible */
    float row_h = row_h_collapsed;  /* default, overridden per track below */
    /* Use the GUI's wall-clock-driven visual step */
    int current_step = g_visual_step;

    ImGui::SetNextWindowPos(ImVec2(x, y));
    ImGui::SetNextWindowSize(ImVec2(w, h));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoCollapse |
                             ImGuiWindowFlags_NoTitleBar;

    if (!ImGui::Begin("DrumGrid", nullptr, flags)) {
        ImGui::End();
        return;
    }

    ImDrawList *draw_list = ImGui::GetWindowDrawList();
    ImGuiIO &io = ImGui::GetIO();

    /* --- Kit selector combo ------------------------------------------ */
    {
        ImGui::Text("Kit:");
        ImGui::SameLine();
        ImGui::PushItemWidth(100);
        int sel_kit = sq_current_kit;
        const char *preview = (sel_kit >= 0 && sel_kit < SQ_NUM_KITS)
                              ? sq_kits[sel_kit].name : "(custom)";
        if (ImGui::BeginCombo("##kit_sel", preview, ImGuiComboFlags_HeightRegular)) {
            for (int ki = 0; ki < SQ_NUM_KITS; ki++) {
                bool is_selected = (ki == sel_kit);
                if (ImGui::Selectable(sq_kits[ki].name, is_selected)) {
                    if (ki != sel_kit) {
                        sq_kit_load(engine, ki, engine->base_dir);
                    }
                }
                if (is_selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        /* TASK-213: scroll-wheel on the closed combo cycles kits. Uses
         * manual rect test because ImGui::IsItemHovered() on a combo
         * preview returns false when the dropdown popup isn't open. */
        {
            ImVec2 rmin = ImGui::GetItemRectMin();
            ImVec2 rmax = ImGui::GetItemRectMax();
            ImVec2 mp = ImGui::GetIO().MousePos;
            float wheel = ImGui::GetIO().MouseWheel;
            if (wheel != 0.0f && sel_kit >= 0 &&
                mp.x >= rmin.x && mp.x <= rmax.x &&
                mp.y >= rmin.y && mp.y <= rmax.y) {
                int delta = (wheel > 0) ? -1 : 1;
                int next = sel_kit + delta;
                if (next < 0) next = 0;
                if (next >= SQ_NUM_KITS) next = SQ_NUM_KITS - 1;
                if (next != sel_kit) sq_kit_load(engine, next, engine->base_dir);
            }
        }
        ImGui::PopItemWidth();
        ImGui::SameLine();
        ImGui::TextDisabled("(%u samples loaded)", engine->num_samples);
    }

    /* Get theme-aware pad/glow colors */
    float theme_pad[3], theme_glow[3];
    theme_get_pad_colors(theme_pad, theme_glow);

    /* Draw each track as a row, with a separator between types */
    bool drew_separator = false;
    for (uint32_t t = 0; t < pattern->num_tracks; t++) {
        sq_track_t *track = &pattern->tracks[t];
        ImU32 base_color = track_colors[track->color_index % NUM_TRACK_COLORS];

        /* Dynamic row height: expanded for selected track, collapsed for others */
        row_h = (g_selected_track == (int)t) ? row_h_expanded : row_h_collapsed;

        /* Draw separator line before first synth/SF2 track */
        if ((track->type == TRACK_SYNTH || track->type == TRACK_SF2) && !drew_separator) {
            drew_separator = true;
            ImVec2 sep_min = ImGui::GetCursorScreenPos();
            ImVec2 sep_max = ImVec2(sep_min.x + ImGui::GetContentRegionAvail().x, sep_min.y + 2);
            draw_list->AddRectFilled(sep_min, sep_max, IM_COL32(100, 180, 255, 120));
            ImGui::Dummy(ImVec2(0, 2));

            ImGui::Text("-- SYNTH --");
        }

        /* We draw track controls and step cells on one horizontal line.
         * Use a child window for track controls, then same-line for cells. */

        /* --- Track controls column ----------------------------------- */
        char child_id[64];
        snprintf(child_id, sizeof(child_id), "Track_%u", t);

        ImGui::BeginChild(child_id, ImVec2(track_panel_w - 10, row_h - 4), true,
                          ImGuiWindowFlags_NoScrollbar);
        {
            /* Determine track display name with track number */
            const char *raw_name = "(empty)";
            if (track->type == TRACK_SYNTH &&
                track->synth_preset >= 0 &&
                (uint32_t)track->synth_preset < engine->num_synth_presets) {
                raw_name = engine->synth_presets[track->synth_preset].name;
            } else if (track->type == TRACK_SAMPLER &&
                       track->sample_index >= 0 &&
                       (uint32_t)track->sample_index < engine->num_samples) {
                raw_name = engine->samples[track->sample_index].name;
            } else if (track->type == TRACK_SF2 &&
                       track->sf2_preset >= 0 &&
                       (uint32_t)track->sf2_preset < engine->num_sf2_presets) {
                raw_name = engine->sf2_presets[track->sf2_preset].name;
            }
            char track_name[128];
            snprintf(track_name, sizeof(track_name), "%u. %s", t + 1, raw_name);

            /* Row 1: Track name button + Mute + Solo */
            {
                /* Track name button - tinted with track color */
                ImU32 tint = base_color;
                ImVec4 btn_col;
                if (g_selected_track == (int)t) {
                    btn_col = ImVec4(
                        (col_r(tint) / 3 + 30) / 255.0f,
                        (col_g(tint) / 3 + 30) / 255.0f,
                        (col_b(tint) / 3 + 60) / 255.0f,
                        1.0f);
                } else {
                    btn_col = ImVec4(
                        (col_r(tint) / 5 + 35) / 255.0f,
                        (col_g(tint) / 5 + 35) / 255.0f,
                        (col_b(tint) / 5 + 38) / 255.0f,
                        1.0f);
                }
                ImGui::PushStyleColor(ImGuiCol_Button, btn_col);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                    ImVec4(btn_col.x + 0.04f, btn_col.y + 0.04f, btn_col.z + 0.04f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                    ImVec4(btn_col.x - 0.02f, btn_col.y - 0.02f, btn_col.z - 0.02f, 1.0f));
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 2.0f);

                float avail_w = ImGui::GetContentRegionAvail().x;
                float name_btn_w = avail_w - 56.0f; /* leave room for M and S buttons */
                if (name_btn_w < 50.0f) name_btn_w = 50.0f;

                ImVec2 name_btn_pos = ImGui::GetCursorScreenPos();
                ImVec2 name_btn_size = ImVec2(name_btn_w, 24);

                if (ImGui::Button(track_name, name_btn_size)) {
                    g_selected_track = (g_selected_track == (int)t) ? -1 : (int)t;
                    mixer_view_set_fx_track(g_selected_track);
                    LOG_DEBUG("Selected track %d", g_selected_track);
                }

                /* Right-click on track name: cycle through colors */
                if (ImGui::IsItemHovered() && io.MouseDown[1] && !rclick_was_down) {
                    track->color_index = (track->color_index + 1) % NUM_TRACK_COLORS;
                    LOG_DEBUG("Track %u color -> %d", t, track->color_index);
                }

                ImGui::PopStyleVar();
                ImGui::PopStyleColor(3);

                /* Mute button */
                ImGui::SameLine();
                if (track->mute) {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.71f, 0.24f, 0.24f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.78f, 0.31f, 0.31f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.63f, 0.16f, 0.16f, 1.0f));
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_Button));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
                }
                char mute_id[32];
                snprintf(mute_id, sizeof(mute_id), "M##m%u", t);
                if (ImGui::Button(mute_id, ImVec2(22, 24))) {
                    track->mute = !track->mute;
                }
                ImGui::PopStyleColor(3);

                /* Solo button */
                ImGui::SameLine();
                if (track->solo) {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.24f, 0.63f, 0.24f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.31f, 0.71f, 0.31f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.16f, 0.55f, 0.16f, 1.0f));
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_Button));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
                }
                char solo_id[32];
                snprintf(solo_id, sizeof(solo_id), "S##s%u", t);
                if (ImGui::Button(solo_id, ImVec2(22, 24))) {
                    track->solo = !track->solo;
                }
                ImGui::PopStyleColor(3);
            }

            /* Only show detailed controls for the selected track */
            bool is_expanded = (g_selected_track == (int)t);

            /* Row 2: Volume + Humanize knobs */
            if (is_expanded)
            {
                ImGui::Text("Vol");
                if (ImGui::IsItemHovered()) SQ_TOOLTIP("Track Volume");
                ImGui::SameLine();
                char vol_id[32];
                snprintf(vol_id, sizeof(vol_id), "##vol%u", t);
                ImGui::PushItemWidth(40);
                ImGui::SliderFloat(vol_id, &track->volume, 0.0f, 1.0f, "");
                ImGui::PopItemWidth();
                ImGui::SameLine();
                ImGui::Text("H");
                if (ImGui::IsItemHovered()) SQ_TOOLTIP("Velocity Humanize\nRandomizes hit velocity each loop");
                ImGui::SameLine();
                char hum_id[32];
                snprintf(hum_id, sizeof(hum_id), "##hum%u", t);
                ImGui::PushItemWidth(35);
                ImGui::SliderFloat(hum_id, &track->humanize, 0.0f, 1.0f, "");
                ImGui::PopItemWidth();
                ImGui::SameLine();
                ImGui::Text("T");
                if (ImGui::IsItemHovered()) SQ_TOOLTIP("Timing Humanize\nRandomizes step timing each loop");
                ImGui::SameLine();
                char thum_id[32];
                snprintf(thum_id, sizeof(thum_id), "##thum%u", t);
                ImGui::PushItemWidth(35);
                ImGui::SliderFloat(thum_id, &track->timing_humanize, 0.0f, 1.0f, "");
                ImGui::PopItemWidth();
            }

            /* Choke group selector (compact) */
            if (is_expanded)
            {
                const char *choke_labels[] = { "--", "C1", "C2", "C3", "C4", "C5", "C6", "C7", "C8" };
                int cg = track->choke_group;
                if (cg < 0 || cg > 8) cg = 0;
                ImGui::Text("Chk");
                if (ImGui::IsItemHovered()) SQ_TOOLTIP("Choke Group\nTracks in same group silence each other");
                ImGui::SameLine();
                char choke_id[32];
                snprintf(choke_id, sizeof(choke_id), "##chk%u", t);
                ImGui::PushItemWidth(36);
                if (ImGui::BeginCombo(choke_id, choke_labels[cg], ImGuiComboFlags_NoArrowButton)) {
                    for (int c = 0; c <= 8; c++) {
                        if (ImGui::Selectable(choke_labels[c], cg == c))
                            track->choke_group = (uint8_t)c;
                    }
                    ImGui::EndCombo();
                }
                ImGui::PopItemWidth();

                /* Track length (polymeter) */
                ImGui::SameLine();
                {
                    static const int len_opts[] = {4, 6, 8, 12, 16, 24, 32, 48, 64};
                    static const char *len_labels[] = {"4","6","8","12","16","24","32","48","64"};
                    int cur_sel = 4; /* default to 16 */
                    for (int li = 0; li < 9; li++) {
                        if ((int)track->length == len_opts[li]) { cur_sel = li; break; }
                    }
                    char len_id[32];
                    snprintf(len_id, sizeof(len_id), "##len%u", t);
                    ImGui::PushItemWidth(36);
                    if (ImGui::BeginCombo(len_id, len_labels[cur_sel], ImGuiComboFlags_NoArrowButton)) {
                        for (int li = 0; li < 9; li++) {
                            if (ImGui::Selectable(len_labels[li], cur_sel == li))
                                track->length = (uint32_t)len_opts[li];
                        }
                        ImGui::EndCombo();
                    }
                    ImGui::PopItemWidth();
                }
            }

            /* Randomize + Euclidean buttons */
            if (is_expanded)
            {
                char rnd_id[32], euc_id[32];
                snprintf(rnd_id, sizeof(rnd_id), "Rnd##r%u", t);
                snprintf(euc_id, sizeof(euc_id), "Euc##e%u", t);
                if (ImGui::SmallButton(rnd_id)) {
                    sequencer_randomize_track(engine, (int)t, 0.4f);
                }
                if (ImGui::IsItemHovered()) SQ_TOOLTIP("Randomize pattern (40%% density)");
                ImGui::SameLine();
                if (ImGui::SmallButton(euc_id)) {
                    sequencer_euclidean_fill(engine, (int)t, 4, (int)track->length, 0, 100);
                }
                if (ImGui::IsItemHovered()) SQ_TOOLTIP("Euclidean rhythm (4 pulses)");
                /* Sample reverse toggle for sampler tracks */
                if (track->type == TRACK_SAMPLER) {
                    ImGui::SameLine();
                    char rev_id[32];
                    snprintf(rev_id, sizeof(rev_id), "Rev##rv%u", t);
                    if (ImGui::SmallButton(rev_id))
                        track->sample_reverse = !track->sample_reverse;
                    if (track->sample_reverse) {
                        ImGui::SameLine();
                        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "<");
                    }
                }
            }

            /* Row 3: Track type toggle (Sampler / Synth / SF2) + Sample/Preset selector */
            {
                const char *type_label;
                ImVec4 type_col;
                if (track->type == TRACK_SYNTH) {
                    type_label = "Synth";
                    type_col = ImVec4(0.31f, 0.24f, 0.63f, 1.0f);
                } else if (track->type == TRACK_SF2) {
                    type_label = "SF2";
                    type_col = ImVec4(0.63f, 0.47f, 0.16f, 1.0f);
                } else {
                    type_label = "Sampler";
                    type_col = ImVec4(0.24f, 0.39f, 0.24f, 1.0f);
                }

                ImGui::PushStyleColor(ImGuiCol_Button, type_col);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                    ImVec4(type_col.x + 0.08f, type_col.y + 0.08f, type_col.z + 0.08f, 1.0f));
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 2.0f);

                char type_id[32];
                snprintf(type_id, sizeof(type_id), "%s##type%u", type_label, t);
                if (ImGui::Button(type_id, ImVec2(60, 24))) {
                    if (track->type == TRACK_SAMPLER) {
                        track->type = TRACK_SYNTH;
                        if (track->synth_preset < 0)
                            track->synth_preset = 0;
                        LOG_INFO("Track %u -> SYNTH (preset %d)", t, track->synth_preset);
                    } else if (track->type == TRACK_SYNTH) {
                        track->type = TRACK_SF2;
                        if (track->sf2_preset < 0)
                            track->sf2_preset = 0;
                        LOG_INFO("Track %u -> SF2", t);
                    } else {
                        track->type = TRACK_SAMPLER;
                        LOG_INFO("Track %u -> SAMPLER", t);
                    }
                }
                ImGui::PopStyleVar();
                ImGui::PopStyleColor(2);

                /* Sample/Preset selector combo */
                ImGui::SameLine();
                ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);

                if (track->type == TRACK_SAMPLER && engine->num_samples > 0) {
                    int sel = track->sample_index;
                    if (sel < 0) sel = 0;
                    const char *preview = engine->samples[sel].name;
                    char combo_id[32];
                    snprintf(combo_id, sizeof(combo_id), "##samp%u", t);
                    if (ImGui::BeginCombo(combo_id, preview, ImGuiComboFlags_HeightRegular)) {
                        for (uint32_t si = 0; si < engine->num_samples; si++) {
                            bool is_selected = ((uint32_t)sel == si);
                            if (ImGui::Selectable(engine->samples[si].name, is_selected)) {
                                track->sample_index = (int)si;
                            }
                            if (is_selected)
                                ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                    {
                        ImVec2 rmin = ImGui::GetItemRectMin();
                        ImVec2 rmax = ImGui::GetItemRectMax();
                        ImVec2 mp = ImGui::GetIO().MousePos;
                        float wheel = ImGui::GetIO().MouseWheel;
                        if (wheel != 0.0f &&
                            mp.x >= rmin.x && mp.x <= rmax.x &&
                            mp.y >= rmin.y && mp.y <= rmax.y) {
                            int delta = (wheel > 0) ? -1 : 1;
                            int next = sel + delta;
                            if (next < 0) next = 0;
                            if (next >= (int)engine->num_samples) next = (int)engine->num_samples - 1;
                            track->sample_index = next;
                        }
                    }
                } else if (track->type == TRACK_SYNTH && engine->num_synth_presets > 0) {
                    int sel = track->synth_preset;
                    if (sel < 0) sel = 0;
                    const char *preview = engine->synth_presets[sel].name;
                    char combo_id[32];
                    snprintf(combo_id, sizeof(combo_id), "##synp%u", t);
                    if (ImGui::BeginCombo(combo_id, preview, ImGuiComboFlags_HeightRegular)) {
                        for (uint32_t pi = 0; pi < engine->num_synth_presets; pi++) {
                            bool is_selected = ((uint32_t)sel == pi);
                            if (ImGui::Selectable(engine->synth_presets[pi].name, is_selected)) {
                                track->synth_preset = (int)pi;
                            }
                            if (is_selected)
                                ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                    {
                        ImVec2 rmin = ImGui::GetItemRectMin();
                        ImVec2 rmax = ImGui::GetItemRectMax();
                        ImVec2 mp = ImGui::GetIO().MousePos;
                        float wheel = ImGui::GetIO().MouseWheel;
                        if (wheel != 0.0f &&
                            mp.x >= rmin.x && mp.x <= rmax.x &&
                            mp.y >= rmin.y && mp.y <= rmax.y) {
                            int delta = (wheel > 0) ? -1 : 1;
                            int next = sel + delta;
                            if (next < 0) next = 0;
                            if (next >= (int)engine->num_synth_presets) next = (int)engine->num_synth_presets - 1;
                            track->synth_preset = next;
                        }
                    }
                } else if (track->type == TRACK_SF2 && engine->num_sf2_presets > 0) {
                    int sel = track->sf2_preset;
                    if (sel < 0) sel = 0;
                    const char *preview = engine->sf2_presets[sel].name;
                    char combo_id[32];
                    snprintf(combo_id, sizeof(combo_id), "##sf2p%u", t);
                    if (ImGui::BeginCombo(combo_id, preview, ImGuiComboFlags_HeightRegular)) {
                        for (uint32_t pi = 0; pi < engine->num_sf2_presets; pi++) {
                            bool is_selected = ((uint32_t)sel == pi);
                            if (ImGui::Selectable(engine->sf2_presets[pi].name, is_selected)) {
                                track->sf2_preset = (int)pi;
                            }
                            if (is_selected)
                                ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                }

                ImGui::PopItemWidth();
            }
        }
        ImGui::EndChild();

        /* --- Step cells on the same row ------------------------------ */
        ImGui::SameLine();

        /* Use an invisible child region for the step grid cells so we
         * can draw them with the draw list and handle interactions. */
        char cells_id[64];
        snprintf(cells_id, sizeof(cells_id), "Steps_%u", t);
        ImVec2 cells_origin = ImGui::GetCursorScreenPos();
        uint32_t track_steps = track->length;
        if (track_steps < 1) track_steps = 1;
        if (track_steps > SQ_MAX_STEPS) track_steps = SQ_MAX_STEPS;
        float track_cell_w = grid_w / (float)track_steps;
        float cells_region_w = (float)track_steps * track_cell_w;

        ImGui::BeginChild(cells_id, ImVec2(cells_region_w, row_h - 4), false,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        {
            ImDrawList *cell_dl = ImGui::GetWindowDrawList();
            ImVec2 origin = ImGui::GetCursorScreenPos();

            for (uint32_t s = 0; s < track_steps; s++) {
                sq_step_t *step = &track->steps[s];
                bool is_active = (step->velocity > 0);
                bool is_playhead = (engine->transport.playing &&
                                    (int)s == current_step);

                /* Determine cell color — pure track color, no blending */
                ImU32 cell_color;
                if (is_active) {
                    float bright = 0.35f + 0.65f * ((float)step->velocity / 127.0f);
                    cell_color = make_col(
                        (int)(col_r(base_color) * bright),
                        (int)(col_g(base_color) * bright),
                        (int)(col_b(base_color) * bright));
                } else {
                    cell_color = cell_inactive;
                }

                /* Playhead highlight */
                if (is_playhead) {
                    int cr = (col_r(cell_color) + 40); if (cr > 255) cr = 255;
                    int cg = (col_g(cell_color) + 40); if (cg > 255) cg = 255;
                    int cb = (col_b(cell_color) + 40); if (cb > 255) cb = 255;
                    cell_color = make_col(cr, cg, cb);
                }

                /* Cell rectangle */
                float cx = origin.x + (float)s * track_cell_w;
                float cy = origin.y;
                ImVec2 cell_min = ImVec2(cx + cell_pad * 0.5f, cy);
                ImVec2 cell_max = ImVec2(cx + track_cell_w - cell_pad * 0.5f, cy + row_h - 6);

                /* Hover highlight */
                bool hovered = ImGui::IsWindowHovered() &&
                    io.MousePos.x >= cell_min.x && io.MousePos.x < cell_max.x &&
                    io.MousePos.y >= cell_min.y && io.MousePos.y < cell_max.y;

                ImU32 draw_color = cell_color;
                if (hovered) {
                    draw_color = make_col(
                        col_r(cell_color) + 20,
                        col_g(cell_color) + 20,
                        col_b(cell_color) + 20);
                }

                /* Pad rounding */
                float pad_round = 6.0f;

                /* Glow color from theme (e.g. neon purple on hacker) */
                int glow_r = (int)(theme_glow[0] * 255);
                int glow_g = (int)(theme_glow[1] * 255);
                int glow_b = (int)(theme_glow[2] * 255);

                /* Subtle diffuse glow halo for active pads */
                if (is_active) {
                    float vel_f = (float)step->velocity / 127.0f;
                    float intensity = 0.3f + 0.4f * vel_f;
                    if (is_playhead) intensity = fminf(intensity + 0.15f, 0.85f);

                    /* Concentric outline rings — gentle halo */
                    for (int ring = 6; ring >= 1; ring--) {
                        float expand = (float)ring * 0.8f;
                        float falloff = 1.0f - ((float)ring / 7.0f);
                        int a = (int)(30.0f * intensity * falloff * falloff);
                        if (a < 2) continue;
                        cell_dl->AddRect(
                            ImVec2(cell_min.x - expand, cell_min.y - expand),
                            ImVec2(cell_max.x + expand, cell_max.y + expand),
                            make_col(glow_r, glow_g, glow_b, a),
                            pad_round + expand * 0.3f, 0, 1.0f);
                    }
                }

                /* Draw rounded filled pad */
                cell_dl->AddRectFilled(cell_min, cell_max, draw_color, pad_round);

                /* Subtle inner glow — brighter/whiter toward center */
                if (is_active) {
                    float cx_mid = (cell_min.x + cell_max.x) * 0.5f;
                    float cy_mid = (cell_min.y + cell_max.y) * 0.5f;
                    float pw = cell_max.x - cell_min.x;
                    float ph = cell_max.y - cell_min.y;
                    float vel_f = (float)step->velocity / 127.0f;
                    int boost = is_playhead ? 10 : 0;

                    /* Soft white-tinted overlay (70% of pad) */
                    int a1 = (int)(15 + 20 * vel_f) + boost;
                    cell_dl->AddRectFilled(
                        ImVec2(cx_mid - pw * 0.35f, cy_mid - ph * 0.35f),
                        ImVec2(cx_mid + pw * 0.35f, cy_mid + ph * 0.35f),
                        make_col(
                            col_r(draw_color) / 3 + 170,
                            col_g(draw_color) / 3 + 170,
                            col_b(draw_color) / 3 + 170, a1),
                        pad_round);
                    /* White core (30%) */
                    int a2 = (int)(8 + 15 * vel_f) + boost;
                    cell_dl->AddRectFilled(
                        ImVec2(cx_mid - pw * 0.15f, cy_mid - ph * 0.15f),
                        ImVec2(cx_mid + pw * 0.15f, cy_mid + ph * 0.15f),
                        IM_COL32(255, 255, 255, a2),
                        3.0f);
                }

                /* Thin bright edge on active pads */
                if (is_active) {
                    int edge_a = 40 + (int)(step->velocity * 0.3f);
                    if (is_playhead) edge_a += 15;
                    if (edge_a > 100) edge_a = 100;
                    cell_dl->AddRect(cell_min, cell_max,
                        make_col(
                            glow_r / 3 + 170,
                            glow_g / 3 + 170,
                            glow_b / 3 + 170, edge_a),
                        pad_round, 0, 1.0f);
                } else {
                    cell_dl->AddRect(cell_min, cell_max, IM_COL32(30, 30, 35, 160),
                                     pad_round, 0, 1.0f);
                }

                /* Cell label: show velocity centered, pitch indicator at bottom */
                if (is_active && step->velocity > 0) {
                    float pw = cell_max.x - cell_min.x;
                    float ph = cell_max.y - cell_min.y;

                    /* Velocity number — centered (shift up slightly if pitch shown) */
                    char label[16];
                    snprintf(label, sizeof(label), "%d", step->velocity);
                    ImVec2 text_size = ImGui::CalcTextSize(label);
                    float tx = cell_min.x + (pw - text_size.x) * 0.5f;
                    float ty_offset = (step->pitch_offset != 0) ? -5.0f : 0.0f;
                    float ty = cell_min.y + (ph - text_size.y) * 0.5f + ty_offset;
                    cell_dl->AddText(ImVec2(tx, ty), IM_COL32(255, 255, 255, 220), label);

                    /* Pitch indicator — small arrow + number at bottom of pad */
                    if (step->pitch_offset != 0) {
                        char plbl[16];
                        snprintf(plbl, sizeof(plbl), "%+d", step->pitch_offset);
                        ImVec2 psz = ImGui::CalcTextSize(plbl);
                        float px = cell_min.x + (pw - psz.x) * 0.5f;
                        float py = cell_max.y - psz.y - 2.0f;
                        /* Color: cyan for up, orange for down */
                        ImU32 pcol = (step->pitch_offset > 0)
                            ? IM_COL32(80, 220, 255, 200)
                            : IM_COL32(255, 180, 60, 200);
                        cell_dl->AddText(ImVec2(px, py), pcol, plbl);

                        /* Small triangle arrow above the pitch text */
                        float ax = cell_min.x + pw * 0.5f;
                        float ay = py - 2.0f;
                        float as = 4.0f; /* arrow size */
                        if (step->pitch_offset > 0) {
                            /* Up arrow */
                            cell_dl->AddTriangleFilled(
                                ImVec2(ax, ay - as),
                                ImVec2(ax - as, ay),
                                ImVec2(ax + as, ay), pcol);
                        } else {
                            /* Down arrow */
                            cell_dl->AddTriangleFilled(
                                ImVec2(ax - as, ay - as),
                                ImVec2(ax + as, ay - as),
                                ImVec2(ax, ay), pcol);
                        }
                    }

                    /* Probability indicator: small "P%" at top-right if < 100% */
                    if (step->probability > 0 && step->probability < 100) {
                        char plbl[8];
                        snprintf(plbl, sizeof(plbl), "%d%%", step->probability);
                        ImVec2 psz = ImGui::CalcTextSize(plbl);
                        float px = cell_max.x - psz.x - 2.0f;
                        float py = cell_min.y + 1.0f;
                        cell_dl->AddText(ImVec2(px, py),
                            IM_COL32(255, 200, 50, 180), plbl);
                    }
                }

                /* Left-click drag: toggle pads as mouse drags across them */
                if (hovered) {
                    bool lb_down = io.MouseDown[0];

                    if (lb_down) {
                        if (!drag_active) {
                            /* Start of a new drag */
                            drag_active = true;
                            drag_set_on = !is_active;
                            drag_last_track = -1;
                            drag_last_step = -1;
                        }

                        /* Only toggle if this is a new cell */
                        if ((int)t != drag_last_track || (int)s != drag_last_step) {
                            if (drag_set_on && !is_active) {
                                undo_push(engine);
                                step->velocity = 100;
                            } else if (!drag_set_on && is_active) {
                                undo_push(engine);
                                step->velocity = 0;
                            }
                            drag_last_track = (int)t;
                            drag_last_step = (int)s;
                        }
                    }

                    /* Right click: start drag for velocity/pitch */
                    {
                        bool rb_down = io.MouseDown[1];
                        if (rb_down && !rclick_was_down) {
                            if (!is_active) {
                                step->velocity = 100;
                            }
                            rclick_drag_track = (int)t;
                            rclick_drag_step = (int)s;
                            rclick_drag_origin = io.MousePos;
                            rclick_drag_base_vel = (int)step->velocity;
                            rclick_drag_base_pitch = (int)step->pitch_offset;
                            rclick_dragging = false;
                        }
                    }
                }
            }

            /* Reserve space so the child region has the right size */
            ImGui::Dummy(ImVec2(cells_region_w, row_h - 6));
        }
        ImGui::EndChild();
    }

    /* --- Right-click drag: continuous velocity/pitch adjustment ----------- */
    if (io.MouseDown[1] && rclick_drag_track >= 0 && rclick_drag_step >= 0) {
        float dx = io.MousePos.x - rclick_drag_origin.x;
        float dy = io.MousePos.y - rclick_drag_origin.y;
        if (!rclick_dragging && (fabsf(dx) > 3.0f || fabsf(dy) > 3.0f)) {
            rclick_dragging = true;
        }
        if (rclick_dragging) {
            sq_track_t *dt = &pattern->tracks[rclick_drag_track];
            sq_step_t *ds = &dt->steps[rclick_drag_step];
            /* Horizontal: velocity relative to starting value */
            int new_vel = rclick_drag_base_vel + (int)(dx * 0.5f);
            if (new_vel < 1) new_vel = 1;
            if (new_vel > 127) new_vel = 127;
            ds->velocity = (uint8_t)new_vel;
            /* Vertical: pitch relative to starting value (drag up = pitch up) */
            int new_pitch = rclick_drag_base_pitch - (int)(dy * 0.2f);
            if (new_pitch < -24) new_pitch = -24;
            if (new_pitch > 24) new_pitch = 24;
            ds->pitch_offset = (int8_t)new_pitch;
        }
    }

    /* --- Add track buttons ----------------------------------------------- */
    if (pattern->num_tracks < SQ_MAX_TRACKS) {
        /* Count sampler and synth tracks */
        uint32_t num_samplers = 0, num_synths = 0;
        for (uint32_t t = 0; t < pattern->num_tracks; t++) {
            if (pattern->tracks[t].type == TRACK_SAMPLER || pattern->tracks[t].type == TRACK_SF2)
                num_samplers++;
            else
                num_synths++;
        }

        float add_btn_h = 24.0f;
        ImVec4 sampler_col(0.24f, 0.39f, 0.24f, 1.0f);
        ImVec4 synth_col(0.31f, 0.24f, 0.63f, 1.0f);

        if (ImGui::Button("+ Sampler Track", ImVec2(track_panel_w - 10, add_btn_h))) {
            /* Insert new sampler track before the first synth track */
            uint32_t insert_at = num_samplers;
            if (insert_at < pattern->num_tracks) {
                /* Shift synth tracks down */
                for (uint32_t t = pattern->num_tracks; t > insert_at; t--) {
                    pattern->tracks[t] = pattern->tracks[t - 1];
                }
            }
            sq_track_t *nt = &pattern->tracks[insert_at];
            memset(nt, 0, sizeof(*nt));
            nt->type = TRACK_SAMPLER;
            nt->length = 16;
            nt->volume = 0.8f;
            nt->sample_index = (int)(insert_at < engine->num_samples ? insert_at : 0);
            nt->synth_preset = -1;
            pattern->num_tracks++;
            LOG_INFO("Added sampler track at index %u", insert_at);
        }

        if (pattern->num_tracks < SQ_MAX_TRACKS) {
            if (ImGui::Button("+ Synth Track", ImVec2(track_panel_w - 10, add_btn_h))) {
                uint32_t idx = pattern->num_tracks;
                sq_track_t *nt = &pattern->tracks[idx];
                memset(nt, 0, sizeof(*nt));
                nt->type = TRACK_SYNTH;
                nt->length = 16;
                nt->volume = 0.6f;
                nt->synth_preset = 0;
                nt->sample_index = -1;
                pattern->num_tracks++;
                LOG_INFO("Added synth track at index %u", idx);
            }
        }
    }

    /* --- Smooth playhead line -------------------------------------------- */
    if (engine->transport.playing) {
        ImVec2 win_pos = ImGui::GetWindowPos();
        ImVec2 win_size = ImGui::GetWindowSize();

        /* Compute fractional position within the current step */
        double beats_per_step = 0.25; /* 16th notes */
        double step_frac = fmod(engine->transport.current_beat, beats_per_step) / beats_per_step;
        if (step_frac < 0.0) step_frac = 0.0;
        if (step_frac > 1.0) step_frac = 1.0;
        float playhead_pos = (float)current_step + (float)step_frac;

        /* Grid starts after the track panel */
        float grid_start_x = win_pos.x + track_panel_w;
        float playhead_x = grid_start_x + playhead_pos * cell_w;
        float grid_top = win_pos.y;
        float grid_bot = win_pos.y + win_size.y;

        draw_list->AddLine(
            ImVec2(playhead_x, grid_top),
            ImVec2(playhead_x, grid_bot),
            IM_COL32(255, 255, 255, 180), 2.0f);
    }

    ImGui::End(); /* DrumGrid */

    /* Right-click release: open popup if it was a click (no drag) */
    if (rclick_was_down && !io.MouseDown[1]) {
        if (!rclick_dragging && rclick_drag_track >= 0) {
            popup_track = rclick_drag_track;
            popup_step = rclick_drag_step;
            popup_open = true;
            popup_just_opened = true;
            LOG_DEBUG("RIGHT click: track %d step %d -> popup", popup_track, popup_step);
        }
        rclick_drag_track = -1;
        rclick_drag_step = -1;
        rclick_dragging = false;
    }

    /* Update right-click state for one-shot detection */
    rclick_was_down = io.MouseDown[1];

    /* Reset drag state when left mouse is released */
    if (!io.MouseDown[0]) {
        drag_active = false;
    }

    /* --- Velocity / Pitch editor (separate window) ----------------------- */
    if (popup_open && popup_track >= 0 && popup_step >= 0) {
        sq_track_t *pt = &pattern->tracks[popup_track];
        sq_step_t *ps = &pt->steps[popup_step];
        bool want_close = false;

        /* Only set position on the frame we first open */
        if (popup_just_opened) {
            popup_just_opened = false;
            ImGui::SetNextWindowPos(ImVec2(
                (float)g_win_width / 2 - 130,
                (float)g_win_height / 2 - 90));
            ImGui::SetNextWindowFocus();
        }

        ImGui::SetNextWindowSize(ImVec2(260, 400), ImGuiCond_Appearing);

        bool window_open = true;
        ImGuiWindowFlags edit_flags = ImGuiWindowFlags_NoResize;

        if (ImGui::Begin("StepEdit", &window_open, edit_flags)) {
            /* Header: which step we're editing */
            char header[64];
            snprintf(header, sizeof(header), "Track %d, Step %d",
                     popup_track + 1, popup_step + 1);
            float text_w = ImGui::CalcTextSize(header).x;
            ImGui::SetCursorPosX((ImGui::GetContentRegionAvail().x - text_w) * 0.5f);
            ImGui::Text("%s", header);

            ImGui::Spacing();

            /* Velocity slider */
            int vel = ps->velocity;
            ImGui::Text("Velocity:");
            ImGui::SameLine();
            ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);
            ImGui::SliderInt("##vel_edit", &vel, 1, 127);
            ImGui::PopItemWidth();
            ps->velocity = (uint8_t)vel;

            /* Pitch offset slider */
            int pitch = ps->pitch_offset;
            ImGui::Text("Pitch:");
            ImGui::SameLine();
            ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);
            ImGui::SliderInt("##pitch_edit", &pitch, -24, 24);
            ImGui::PopItemWidth();
            ps->pitch_offset = (int8_t)pitch;

            /* Probability slider */
            int prob = ps->probability;
            if (prob == 0) prob = 100; /* 0 means 100% (always) */
            ImGui::Text("Prob:");
            ImGui::SameLine();
            ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);
            ImGui::SliderInt("##prob_edit", &prob, 1, 100, "%d%%");
            ImGui::PopItemWidth();
            ps->probability = (prob >= 100) ? 0 : (uint8_t)prob;

            /* Retrigger selector */
            const char *retrig_labels[] = { "Off", "2x", "3x", "4x" };
            int retrig = ps->retrigger;
            if (retrig < 2) retrig = 0; else retrig -= 1; /* 0=off, 1=2x, 2=3x, 3=4x */
            ImGui::Text("Retrig:");
            ImGui::SameLine();
            ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);
            if (ImGui::Combo("##retrig_edit", &retrig, retrig_labels, 4))
                ps->retrigger = (retrig == 0) ? 0 : (uint8_t)(retrig + 1);
            ImGui::PopItemWidth();

            /* Micro-timing offset */
            float utime = ps->micro_offset;
            ImGui::Text("Timing:");
            ImGui::SameLine();
            ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);
            ImGui::SliderFloat("##utime_edit", &utime, -0.5f, 0.5f, "%.2f");
            ImGui::PopItemWidth();
            ps->micro_offset = utime;

            /* Parameter locks (synth tracks only) */
            if (pt->type == TRACK_SYNTH) {
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "P-Locks:");
                float pcut = ps->param[0];
                ImGui::Text("Cutoff:");
                ImGui::SameLine();
                ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);
                ImGui::SliderFloat("##plock_cut", &pcut, 0.0f, 20000.0f, "%.0f");
                ImGui::PopItemWidth();
                ps->param[0] = pcut;

                float pres = ps->param[1];
                ImGui::Text("Reso:");
                ImGui::SameLine();
                ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);
                ImGui::SliderFloat("##plock_res", &pres, 0.0f, 20.0f, "%.1f");
                ImGui::PopItemWidth();
                ps->param[1] = pres;
            }

            /* Display values */
            ImGui::Spacing();
            char val_text[64];
            snprintf(val_text, sizeof(val_text), "Vel: %d  |  Pitch: %+d st",
                     ps->velocity, ps->pitch_offset);
            float val_w = ImGui::CalcTextSize(val_text).x;
            ImGui::SetCursorPosX((ImGui::GetContentRegionAvail().x - val_w) * 0.5f);
            ImGui::Text("%s", val_text);

            ImGui::Spacing();

            /* Reset + Done buttons */
            float btn_w = (ImGui::GetContentRegionAvail().x - 8) * 0.5f;
            if (ImGui::Button("Reset", ImVec2(btn_w, 25))) {
                /* Keep velocity (pad stays active), reset everything else */
                ps->pitch_offset = 0;
                ps->probability = 0;
                ps->retrigger = 0;
                ps->micro_offset = 0.0f;
                ps->length = 0.0f;
                ps->note = 0;
                for (int p = 0; p < 4; p++) ps->param[p] = 0.0f;
                want_close = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Done", ImVec2(btn_w, 25))) {
                want_close = true;
            }
        }

        /* Check if X button was clicked (window_open set to false) */
        if (!window_open) {
            want_close = true;
        }

        /* Capture window bounds before End for click-outside detection */
        ImVec2 popup_pos = ImGui::GetWindowPos();
        ImVec2 popup_size = ImGui::GetWindowSize();
        ImGui::End();

        /* Close if user clicked anywhere outside the StepEdit window.
         * Skip on the frame we just opened. */
        if (!want_close && !popup_just_opened) {
            if (ImGui::IsMouseClicked(0)) {
                ImVec2 mp = io.MousePos;
                if (mp.x < popup_pos.x || mp.x > popup_pos.x + popup_size.x ||
                    mp.y < popup_pos.y || mp.y > popup_pos.y + popup_size.y) {
                    want_close = true;
                }
            }
        }

        if (want_close) {
            LOG_DEBUG("popup CLOSING (track=%d step=%d)",
                     popup_track, popup_step);
            popup_open = false;
            popup_track = -1;
            popup_step  = -1;
        }
    }
}
