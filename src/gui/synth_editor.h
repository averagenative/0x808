/*
 * synth_editor.h — Synth parameter editor panel.
 */

#ifndef SQ_SYNTH_EDITOR_H
#define SQ_SYNTH_EDITOR_H

#include "engine/engine.h"

struct nk_context;

/* Draw the synth editor panel for the given preset index.
 * preset_index_ptr: pointer to track's synth_preset — allows preset switching.
 * Returns nonzero if the panel was drawn (preset valid). */
int synth_editor_draw(struct nk_context *ctx, sq_engine_t *engine,
                      int *preset_index_ptr,
                      float x, float y, float w, float h);

#endif /* SQ_SYNTH_EDITOR_H */
