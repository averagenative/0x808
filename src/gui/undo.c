/*
 * undo.c — Pattern snapshot undo/redo.
 *
 * Stores full pattern copies in a circular buffer. Each undo_push()
 * saves the current pattern before an edit. Undo restores the saved
 * state, redo re-applies it.
 *
 * We store both the pattern data and which pattern index was active,
 * so undo works correctly even if the user switches patterns.
 */

#include "gui/undo.h"
#include <string.h>

typedef struct {
    sq_pattern_t pattern;   /* snapshot of the pattern data */
    int          pattern_index; /* which pattern this snapshot is for */
    bool         valid;
} undo_entry_t;

/* Circular undo buffer */
static undo_entry_t g_undo_stack[UNDO_MAX_LEVELS];
static int g_undo_top = 0;     /* next write position */
static int g_undo_count = 0;   /* how many entries in undo stack */

/* Redo buffer */
static undo_entry_t g_redo_stack[UNDO_MAX_LEVELS];
static int g_redo_top = 0;
static int g_redo_count = 0;

void undo_push(sq_engine_t *engine)
{
    int pi = engine->transport.current_pattern;
    if (pi < 0 || (uint32_t)pi >= engine->num_patterns) return;

    undo_entry_t *entry = &g_undo_stack[g_undo_top];
    entry->pattern = engine->patterns[pi];
    entry->pattern_index = pi;
    entry->valid = true;

    g_undo_top = (g_undo_top + 1) % UNDO_MAX_LEVELS;
    if (g_undo_count < UNDO_MAX_LEVELS)
        g_undo_count++;

    /* Clear redo stack — new edit branch */
    g_redo_count = 0;
    g_redo_top = 0;
}

bool undo_undo(sq_engine_t *engine)
{
    if (g_undo_count == 0) return false;

    /* Pop from undo stack */
    g_undo_top = (g_undo_top - 1 + UNDO_MAX_LEVELS) % UNDO_MAX_LEVELS;
    g_undo_count--;

    undo_entry_t *entry = &g_undo_stack[g_undo_top];
    if (!entry->valid) return false;

    int pi = entry->pattern_index;
    if (pi < 0 || (uint32_t)pi >= engine->num_patterns) return false;

    /* Save current state to redo stack before restoring */
    undo_entry_t *redo = &g_redo_stack[g_redo_top];
    redo->pattern = engine->patterns[pi];
    redo->pattern_index = pi;
    redo->valid = true;
    g_redo_top = (g_redo_top + 1) % UNDO_MAX_LEVELS;
    if (g_redo_count < UNDO_MAX_LEVELS)
        g_redo_count++;

    /* Restore the pattern */
    engine->patterns[pi] = entry->pattern;
    engine->transport.current_pattern = pi;

    return true;
}

bool undo_redo(sq_engine_t *engine)
{
    if (g_redo_count == 0) return false;

    /* Pop from redo stack */
    g_redo_top = (g_redo_top - 1 + UNDO_MAX_LEVELS) % UNDO_MAX_LEVELS;
    g_redo_count--;

    undo_entry_t *entry = &g_redo_stack[g_redo_top];
    if (!entry->valid) return false;

    int pi = entry->pattern_index;
    if (pi < 0 || (uint32_t)pi >= engine->num_patterns) return false;

    /* Save current state back to undo stack */
    undo_entry_t *undo = &g_undo_stack[g_undo_top];
    undo->pattern = engine->patterns[pi];
    undo->pattern_index = pi;
    undo->valid = true;
    g_undo_top = (g_undo_top + 1) % UNDO_MAX_LEVELS;
    if (g_undo_count < UNDO_MAX_LEVELS)
        g_undo_count++;

    /* Restore the pattern */
    engine->patterns[pi] = entry->pattern;
    engine->transport.current_pattern = pi;

    return true;
}

void undo_clear(void)
{
    g_undo_count = 0;
    g_undo_top = 0;
    g_redo_count = 0;
    g_redo_top = 0;
    memset(g_undo_stack, 0, sizeof(g_undo_stack));
    memset(g_redo_stack, 0, sizeof(g_redo_stack));
}

bool undo_can_undo(void)
{
    return g_undo_count > 0;
}

bool undo_can_redo(void)
{
    return g_redo_count > 0;
}
