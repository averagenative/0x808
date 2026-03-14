/*
 * undo.h — Undo/redo system for pattern edits.
 *
 * Stores snapshots of the current pattern before edits.
 * Ctrl+Z undoes, Ctrl+Shift+Z redoes.
 */

#ifndef SQ_UNDO_H
#define SQ_UNDO_H

#include "engine/engine.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Max undo levels (circular buffer) */
#define UNDO_MAX_LEVELS 32

/*
 * Push the current pattern state onto the undo stack.
 * Call this BEFORE modifying the pattern.
 * Clears the redo stack (any future-branching is lost).
 */
void undo_push(sq_engine_t *engine);

/*
 * Undo: restore the last saved pattern state.
 * Returns true if undo was performed, false if nothing to undo.
 */
bool undo_undo(sq_engine_t *engine);

/*
 * Redo: restore a previously undone pattern state.
 * Returns true if redo was performed, false if nothing to redo.
 */
bool undo_redo(sq_engine_t *engine);

/*
 * Clear all undo/redo history (e.g., when loading a new project).
 */
void undo_clear(void);

#ifdef __cplusplus
}
#endif

#endif /* SQ_UNDO_H */
