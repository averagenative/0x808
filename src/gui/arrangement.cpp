/*
 * arrangement.cpp — Song arrangement view (Dear ImGui port).
 *
 * Layout:
 * +-----------------------------------------------------------+
 * | Mode: [Pattern] [Song] [Perform]                          |
 * +-----------------------------------------------------------+
 * | Sections (N):                                              |
 * | [Intro x2] -> [Verse x4] -> [Chorus x2] -> [Outro x1] [+]|
 * +-----------------------------------------------------------+
 * | Section editor: Pattern selector, Repeat, Remove          |
 * +-----------------------------------------------------------+
 * | Status: Playing: Section 2 | Queued: Yes                  |
 * +-----------------------------------------------------------+
 */

#include "imgui.h"

extern "C" {
#include "engine/engine.h"
}

extern "C" {
#define LOG_TAG "arrange"
#include "core/log.h"
}

#include <cstdio>
#include <cstring>

/* Section colors (RGBA) */
static const ImVec4 section_colors[] = {
    ImVec4(0.39f, 0.63f, 0.86f, 1.0f),  /* blue   */
    ImVec4(0.86f, 0.51f, 0.24f, 1.0f),  /* orange */
    ImVec4(0.31f, 0.78f, 0.47f, 1.0f),  /* green  */
    ImVec4(0.78f, 0.31f, 0.71f, 1.0f),  /* pink   */
    ImVec4(0.86f, 0.78f, 0.24f, 1.0f),  /* yellow */
    ImVec4(0.47f, 0.31f, 0.78f, 1.0f),  /* purple */
    ImVec4(0.24f, 0.78f, 0.78f, 1.0f),  /* cyan   */
    ImVec4(0.78f, 0.39f, 0.39f, 1.0f),  /* salmon */
};
static const int NUM_SECTION_COLORS = 8;

extern "C" void arrangement_draw(sq_engine_t *engine,
                                  float x, float y, float w, float h)
{
    ImGui::SetNextWindowPos(ImVec2(x, y));
    ImGui::SetNextWindowSize(ImVec2(w, h));
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar |
                             ImGuiWindowFlags_NoScrollbar;

    if (!ImGui::Begin("Arrangement", nullptr, flags)) {
        ImGui::End();
        return;
    }

    sq_arrangement_t *arr = &engine->arrangement;

    /* --- Mode selector --- */
    ImGui::Text("Mode:");
    ImGui::SameLine();

    /* Pattern mode button */
    if (engine->transport.mode == MODE_PATTERN) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.24f, 0.51f, 0.24f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.31f, 0.59f, 0.31f, 1.0f));
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_Button));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered));
    }
    if (ImGui::Button("Pattern")) {
        engine->transport.mode = MODE_PATTERN;
        LOG_INFO("Mode -> PATTERN");
    }
    ImGui::PopStyleColor(2);
    ImGui::SameLine();

    /* Song mode button */
    if (engine->transport.mode == MODE_SONG) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.24f, 0.39f, 0.71f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.31f, 0.47f, 0.78f, 1.0f));
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_Button));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered));
    }
    if (ImGui::Button("Song")) {
        engine->transport.mode = MODE_SONG;
        engine->transport.current_section = 0;
        engine->transport.section_repeat = 0;
        if (arr->num_sections > 0) {
            engine->transport.current_pattern = arr->sections[0].pattern_index;
        }
        LOG_INFO("Mode -> SONG");
    }
    ImGui::PopStyleColor(2);
    ImGui::SameLine();

    /* Perform mode button */
    if (engine->transport.mode == MODE_PERFORM) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.71f, 0.24f, 0.39f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.78f, 0.31f, 0.47f, 1.0f));
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_Button));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered));
    }
    if (ImGui::Button("Perform")) {
        engine->transport.mode = MODE_PERFORM;
        LOG_INFO("Mode -> PERFORM");
    }
    ImGui::PopStyleColor(2);

    ImGui::Separator();

    /* --- Section list --- */
    ImGui::Text("Sections (%u):", arr->num_sections);

    if (arr->num_sections > 0) {
        /* Draw sections as colored buttons in a row */
        for (uint32_t s = 0; s < arr->num_sections; s++) {
            sq_section_t *sec = &arr->sections[s];
            ImVec4 col = section_colors[s % NUM_SECTION_COLORS];

            /* Get pattern name */
            const char *pat_name = "???";
            if (sec->pattern_index >= 0 &&
                (uint32_t)sec->pattern_index < engine->num_patterns) {
                pat_name = engine->patterns[sec->pattern_index].name;
            }

            char label[64];
            snprintf(label, sizeof(label), "%s x%d", pat_name, sec->repeat_count);

            bool is_current = ((int)s == engine->transport.current_section &&
                               engine->transport.mode != MODE_PATTERN);
            bool is_queued = ((int)s == engine->transport.queued_section);

            ImVec4 btn_col;
            if (is_current) {
                btn_col = col;
            } else if (is_queued) {
                btn_col = ImVec4(col.x * 0.5f, col.y * 0.5f, col.z * 0.5f, 1.0f);
            } else {
                btn_col = ImVec4(col.x * 0.33f, col.y * 0.33f, col.z * 0.33f, 1.0f);
            }

            ImGui::PushStyleColor(ImGuiCol_Button, btn_col);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                                  ImVec4(btn_col.x + 0.08f, btn_col.y + 0.08f,
                                         btn_col.z + 0.08f, 1.0f));

            /* Queued section gets a highlight border */
            if (is_queued) {
                ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1.0f, 1.0f, 0.4f, 1.0f));
                ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.0f);
            }

            ImGui::PushID((int)s);
            if (ImGui::Button(label, ImVec2(0, 30))) {
                if (engine->transport.mode == MODE_PERFORM) {
                    engine->transport.queued_section = (int)s;
                    LOG_INFO("Queued section %u", s);
                } else {
                    engine->transport.current_pattern = sec->pattern_index;
                    engine->transport.current_section = (int)s;
                    engine->transport.section_repeat = 0;
                    engine->transport.current_beat = 0.0;
                    engine->transport.current_step = 0;
                    LOG_INFO("Jump to section %u (pattern %d)", s, sec->pattern_index);
                }
            }
            ImGui::PopID();

            if (is_queued) {
                ImGui::PopStyleVar();
                ImGui::PopStyleColor();
            }
            ImGui::PopStyleColor(2);

            ImGui::SameLine();
        }

        /* Add section button */
        if (arr->num_sections < SQ_MAX_SECTIONS) {
            if (ImGui::Button("+", ImVec2(30, 30))) {
                int new_idx = (int)arr->num_sections;
                arr->sections[new_idx].pattern_index = 0;
                arr->sections[new_idx].repeat_count = 1;
                arr->num_sections++;
                LOG_INFO("Added section %d", new_idx);
            }
        }
        ImGui::NewLine();
    } else {
        if (ImGui::Button("+ Add First Section")) {
            arr->sections[0].pattern_index = 0;
            arr->sections[0].repeat_count = 1;
            arr->num_sections = 1;
            LOG_INFO("Added first section");
        }
    }

    ImGui::Separator();

    /* --- Section editor (edit current section) --- */
    if (arr->num_sections > 0) {
        int sec_idx = engine->transport.current_section;
        if (sec_idx >= 0 && (uint32_t)sec_idx < arr->num_sections) {
            sq_section_t *sec = &arr->sections[sec_idx];

            ImGui::Text("Section %d:", sec_idx + 1);
            ImGui::SameLine();

            /* Pattern selector */
            ImGui::Text("Pattern:");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(120.0f);

            /* Build pattern names list */
            const char *pat_names[SQ_MAX_PATTERNS];
            for (uint32_t p = 0; p < engine->num_patterns; p++)
                pat_names[p] = engine->patterns[p].name;

            int sel = sec->pattern_index;
            if (sel < 0) sel = 0;
            ImGui::PushID("PatSel");
            if (ImGui::Combo("##Pattern", &sel, pat_names, (int)engine->num_patterns)) {
                sec->pattern_index = sel;
            }
            ImGui::PopID();

            ImGui::SameLine();

            /* Repeat count */
            ImGui::Text("Repeat: %d", sec->repeat_count);
            ImGui::SetNextItemWidth(120.0f);
            ImGui::SliderInt("##Repeat", &sec->repeat_count, 1, 16);

            ImGui::SameLine();

            /* Remove section */
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.2f, 0.2f, 1.0f));
            if (ImGui::Button("Remove")) {
                for (uint32_t i = (uint32_t)sec_idx; i < arr->num_sections - 1; i++) {
                    arr->sections[i] = arr->sections[i + 1];
                }
                arr->num_sections--;
                if (engine->transport.current_section >= (int)arr->num_sections)
                    engine->transport.current_section = (int)arr->num_sections - 1;
                LOG_INFO("Removed section %d", sec_idx);
            }
            ImGui::PopStyleColor();

            ImGui::SameLine();

            /* Add new pattern */
            if (engine->num_patterns < SQ_MAX_PATTERNS) {
                if (ImGui::Button("+ New Pattern")) {
                    int idx = (int)engine->num_patterns;
                    sq_pattern_t *p = &engine->patterns[idx];
                    p->num_tracks = 4;
                    snprintf(p->name, SQ_PATTERN_NAME_LEN, "Pattern %d", idx + 1);
                    for (uint32_t t = 0; t < p->num_tracks; t++) {
                        p->tracks[t].type = TRACK_SAMPLER;
                        p->tracks[t].length = 16;
                        p->tracks[t].volume = 0.8f;
                        p->tracks[t].sample_index = (int)t < (int)engine->num_samples ? (int)t : -1;
                        p->tracks[t].synth_preset = -1;
                    }
                    engine->num_patterns++;
                    LOG_INFO("Created pattern %d", idx);
                }
            }
        }
    }

    ImGui::Separator();

    /* --- Status --- */
    if (engine->transport.mode == MODE_PERFORM) {
        ImGui::Text("Playing: Section %d  |  Queued: %s",
                     engine->transport.current_section + 1,
                     engine->transport.queued_section >= 0 ? "Yes" : "None");
    } else if (engine->transport.mode == MODE_SONG) {
        ImGui::Text("Song: Section %d/%u  |  Repeat %d",
                     engine->transport.current_section + 1,
                     arr->num_sections,
                     engine->transport.section_repeat + 1);
    } else {
        ImGui::Text("Pattern mode: %s",
                     engine->patterns[engine->transport.current_pattern].name);
    }

    ImGui::End();
}
