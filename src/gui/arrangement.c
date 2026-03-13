/*
 * arrangement.c — Song arrangement view.
 *
 * Layout:
 * ┌─────────────────────────────────────────────────────────┐
 * │ Mode: [Pattern] [Song] [Perform]                        │
 * ├─────────────────────────────────────────────────────────┤
 * │ Arrangement:                                             │
 * │ [Intro x2] → [Verse x4] → [Chorus x2] → [Outro x1]    │
 * │ [+Add Section]                                           │
 * ├─────────────────────────────────────────────────────────┤
 * │ Perform: [1] [2] [3] [4] [5]    Playing: 2  Queued: 4  │
 * └─────────────────────────────────────────────────────────┘
 */

#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#include "nuklear.h"

#include "gui/arrangement.h"

#define LOG_TAG "arrange"
#include "core/log.h"

#include <stdio.h>
#include <string.h>

/* Section colors */
static const struct nk_color section_colors[] = {
    {100, 160, 220, 255},  /* blue      */
    {220, 130,  60, 255},  /* orange    */
    { 80, 200, 120, 255},  /* green     */
    {200,  80, 180, 255},  /* pink      */
    {220, 200,  60, 255},  /* yellow    */
    {120,  80, 200, 255},  /* purple    */
    { 60, 200, 200, 255},  /* cyan      */
    {200, 100, 100, 255},  /* salmon    */
};

int arrangement_draw(struct nk_context *ctx, sq_engine_t *engine,
                     float x, float y, float w, float h)
{
    int pattern_changed = 0;

    nk_window_set_bounds(ctx, "Arrangement", nk_rect(x, y, w, h));
    if (nk_begin(ctx, "Arrangement",
                 nk_rect(x, y, w, h),
                 NK_WINDOW_BORDER | NK_WINDOW_TITLE | NK_WINDOW_NO_SCROLLBAR))
    {
        sq_arrangement_t *arr = &engine->arrangement;

        /* ── Mode selector ─────────────────────────────────── */
        nk_layout_row_dynamic(ctx, 25, 4);
        nk_label(ctx, "Mode:", NK_TEXT_LEFT);

        {
            struct nk_style_button mode_style = ctx->style.button;
            /* Pattern mode */
            if (engine->transport.mode == MODE_PATTERN) {
                mode_style.normal = nk_style_item_color(nk_rgba(60, 130, 60, 255));
                mode_style.hover  = nk_style_item_color(nk_rgba(80, 150, 80, 255));
            } else {
                mode_style = ctx->style.button;
            }
            if (nk_button_label_styled(ctx, &mode_style, "Pattern")) {
                engine->transport.mode = MODE_PATTERN;
                LOG_INFO("Mode -> PATTERN");
            }

            /* Song mode */
            mode_style = ctx->style.button;
            if (engine->transport.mode == MODE_SONG) {
                mode_style.normal = nk_style_item_color(nk_rgba(60, 100, 180, 255));
                mode_style.hover  = nk_style_item_color(nk_rgba(80, 120, 200, 255));
            }
            if (nk_button_label_styled(ctx, &mode_style, "Song")) {
                engine->transport.mode = MODE_SONG;
                engine->transport.current_section = 0;
                engine->transport.section_repeat = 0;
                if (arr->num_sections > 0) {
                    engine->transport.current_pattern = arr->sections[0].pattern_index;
                }
                LOG_INFO("Mode -> SONG");
            }

            /* Perform mode */
            mode_style = ctx->style.button;
            if (engine->transport.mode == MODE_PERFORM) {
                mode_style.normal = nk_style_item_color(nk_rgba(180, 60, 100, 255));
                mode_style.hover  = nk_style_item_color(nk_rgba(200, 80, 120, 255));
            }
            if (nk_button_label_styled(ctx, &mode_style, "Perform")) {
                engine->transport.mode = MODE_PERFORM;
                LOG_INFO("Mode -> PERFORM");
            }
        }

        /* ── Section list ──────────────────────────────────── */
        nk_layout_row_dynamic(ctx, 14, 1);
        nk_labelf(ctx, NK_TEXT_LEFT, "Sections (%u):", arr->num_sections);

        /* Draw sections as buttons in a row */
        if (arr->num_sections > 0) {
            nk_layout_row_dynamic(ctx, 30, (int)arr->num_sections + 1);
            for (uint32_t s = 0; s < arr->num_sections; s++) {
                sq_section_t *sec = &arr->sections[s];
                struct nk_color col = section_colors[s % 8];

                /* Get pattern name */
                const char *pat_name = "???";
                if (sec->pattern_index >= 0 &&
                    (uint32_t)sec->pattern_index < engine->num_patterns) {
                    pat_name = engine->patterns[sec->pattern_index].name;
                }

                char label[64];
                snprintf(label, sizeof(label), "%s x%d", pat_name, sec->repeat_count);

                struct nk_style_button sec_style = ctx->style.button;

                /* Highlight current section */
                bool is_current = ((int)s == engine->transport.current_section &&
                                   engine->transport.mode != MODE_PATTERN);
                bool is_queued = ((int)s == engine->transport.queued_section);

                if (is_current) {
                    sec_style.normal = nk_style_item_color(col);
                    sec_style.hover  = nk_style_item_color(
                        nk_rgba(col.r + 20, col.g + 20, col.b + 20, 255));
                } else if (is_queued) {
                    sec_style.normal = nk_style_item_color(
                        nk_rgba(col.r / 2, col.g / 2, col.b / 2, 255));
                    sec_style.border_color = nk_rgba(255, 255, 100, 255);
                    sec_style.border = 2.0f;
                } else {
                    sec_style.normal = nk_style_item_color(
                        nk_rgba(col.r / 3, col.g / 3, col.b / 3, 255));
                }

                if (nk_button_label_styled(ctx, &sec_style, label)) {
                    if (engine->transport.mode == MODE_PERFORM) {
                        /* Queue this section for next bar boundary */
                        engine->transport.queued_section = (int)s;
                        LOG_INFO("Queued section %u", s);
                    } else {
                        /* In pattern/song mode, jump to this section's pattern */
                        engine->transport.current_pattern = sec->pattern_index;
                        engine->transport.current_section = (int)s;
                        engine->transport.section_repeat = 0;
                        engine->transport.current_beat = 0.0;
                        engine->transport.current_step = 0;
                        pattern_changed = 1;
                        LOG_INFO("Jump to section %u (pattern %d)",
                                 s, sec->pattern_index);
                    }
                }
            }

            /* Add section button */
            if (arr->num_sections < SQ_MAX_SECTIONS) {
                if (nk_button_label(ctx, "+")) {
                    int new_idx = (int)arr->num_sections;
                    arr->sections[new_idx].pattern_index = 0;
                    arr->sections[new_idx].repeat_count = 1;
                    arr->num_sections++;
                    LOG_INFO("Added section %d", new_idx);
                }
            }
        } else {
            nk_layout_row_dynamic(ctx, 25, 1);
            if (nk_button_label(ctx, "+ Add First Section")) {
                arr->sections[0].pattern_index = 0;
                arr->sections[0].repeat_count = 1;
                arr->num_sections = 1;
                LOG_INFO("Added first section");
            }
        }

        /* ── Section editor (edit selected section) ────────── */
        if (arr->num_sections > 0) {
            int sec_idx = engine->transport.current_section;
            if (sec_idx >= 0 && (uint32_t)sec_idx < arr->num_sections) {
                sq_section_t *sec = &arr->sections[sec_idx];

                nk_layout_row_dynamic(ctx, 20, 4);
                nk_labelf(ctx, NK_TEXT_LEFT, "Section %d:", sec_idx + 1);

                /* Pattern selector */
                nk_label(ctx, "Pattern:", NK_TEXT_RIGHT);
                {
                    static const char *pat_names[SQ_MAX_PATTERNS];
                    for (uint32_t p = 0; p < engine->num_patterns; p++)
                        pat_names[p] = engine->patterns[p].name;

                    int sel = sec->pattern_index;
                    if (sel < 0) sel = 0;
                    nk_combobox(ctx, pat_names, (int)engine->num_patterns,
                                &sel, 18, nk_vec2(120, 100));
                    sec->pattern_index = sel;
                }

                /* Repeat count */
                nk_labelf(ctx, NK_TEXT_LEFT, "Repeat: %d", sec->repeat_count);

                nk_layout_row_dynamic(ctx, 20, 4);
                nk_slider_int(ctx, 1, &sec->repeat_count, 16, 1);

                /* Remove section */
                if (nk_button_label(ctx, "Remove")) {
                    /* Shift sections down */
                    for (uint32_t i = (uint32_t)sec_idx; i < arr->num_sections - 1; i++) {
                        arr->sections[i] = arr->sections[i + 1];
                    }
                    arr->num_sections--;
                    if (engine->transport.current_section >= (int)arr->num_sections)
                        engine->transport.current_section = (int)arr->num_sections - 1;
                    LOG_INFO("Removed section %d", sec_idx);
                }

                /* Add pattern */
                if (engine->num_patterns < SQ_MAX_PATTERNS) {
                    if (nk_button_label(ctx, "+ New Pattern")) {
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

                nk_spacing(ctx, 1);
            }
        }

        /* ── Status ────────────────────────────────────────── */
        nk_layout_row_dynamic(ctx, 14, 1);
        if (engine->transport.mode == MODE_PERFORM) {
            nk_labelf(ctx, NK_TEXT_LEFT,
                      "Playing: Section %d  |  Queued: %s",
                      engine->transport.current_section + 1,
                      engine->transport.queued_section >= 0
                          ? "Yes" : "None");
        } else if (engine->transport.mode == MODE_SONG) {
            nk_labelf(ctx, NK_TEXT_LEFT,
                      "Song: Section %d/%u  |  Repeat %d",
                      engine->transport.current_section + 1,
                      arr->num_sections,
                      engine->transport.section_repeat + 1);
        } else {
            nk_labelf(ctx, NK_TEXT_LEFT, "Pattern mode: %s",
                      engine->patterns[engine->transport.current_pattern].name);
        }
    }
    nk_end(ctx);

    return pattern_changed;
}
