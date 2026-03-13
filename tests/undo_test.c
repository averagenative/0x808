/*
 * undo_test.c — Test undo/redo snapshot system.
 *
 * Verifies: push, undo, redo, circular buffer wrap, no-op on empty history.
 * The undo system uses static globals, so we link undo.c directly.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "engine/engine.h"
#include "gui/undo.h"

static sq_engine_t engine;

static void init_engine(void) {
    sq_engine_init(&engine, 44100);
    undo_clear();
}

/* ── Test 1: Undo with no history is a no-op ──────────────────────────────── */
static int test_undo_no_history(void) {
    init_engine();

    bool result = undo_undo(&engine);
    assert(!result && "undo on empty history should return false");

    result = undo_redo(&engine);
    assert(!result && "redo on empty history should return false");

    sq_engine_shutdown(&engine);
    printf("PASS: Undo/redo with no history is a no-op\n");
    return 0;
}

/* ── Test 2: Snapshot capture and undo restores previous state ────────────── */
static int test_push_and_undo(void) {
    init_engine();

    /* Set up initial pattern state */
    engine.patterns[0].tracks[0].steps[0].velocity = 100;
    engine.patterns[0].tracks[0].steps[1].velocity = 0;

    /* Push snapshot before edit */
    undo_push(&engine);

    /* Make an edit */
    engine.patterns[0].tracks[0].steps[0].velocity = 0;
    engine.patterns[0].tracks[0].steps[1].velocity = 127;

    /* Verify edit took effect */
    assert(engine.patterns[0].tracks[0].steps[0].velocity == 0);
    assert(engine.patterns[0].tracks[0].steps[1].velocity == 127);

    /* Undo should restore the previous state */
    bool result = undo_undo(&engine);
    assert(result && "undo should succeed");
    assert(engine.patterns[0].tracks[0].steps[0].velocity == 100);
    assert(engine.patterns[0].tracks[0].steps[1].velocity == 0);

    sq_engine_shutdown(&engine);
    printf("PASS: Push and undo restores previous state\n");
    return 0;
}

/* ── Test 3: Redo re-applies the undone change ────────────────────────────── */
static int test_redo(void) {
    init_engine();

    engine.patterns[0].tracks[0].steps[0].velocity = 100;

    /* Push, then edit */
    undo_push(&engine);
    engine.patterns[0].tracks[0].steps[0].velocity = 50;

    /* Undo */
    undo_undo(&engine);
    assert(engine.patterns[0].tracks[0].steps[0].velocity == 100);

    /* Redo should bring back the edit */
    bool result = undo_redo(&engine);
    assert(result && "redo should succeed");
    assert(engine.patterns[0].tracks[0].steps[0].velocity == 50);

    sq_engine_shutdown(&engine);
    printf("PASS: Redo re-applies the undone change\n");
    return 0;
}

/* ── Test 4: Multiple push/undo/redo ──────────────────────────────────────── */
static int test_multiple_undo_redo(void) {
    init_engine();

    /* State A: velocity = 10 */
    engine.patterns[0].tracks[0].steps[0].velocity = 10;
    undo_push(&engine);

    /* State B: velocity = 20 */
    engine.patterns[0].tracks[0].steps[0].velocity = 20;
    undo_push(&engine);

    /* State C: velocity = 30 */
    engine.patterns[0].tracks[0].steps[0].velocity = 30;

    /* Undo to B */
    undo_undo(&engine);
    assert(engine.patterns[0].tracks[0].steps[0].velocity == 20);

    /* Undo to A */
    undo_undo(&engine);
    assert(engine.patterns[0].tracks[0].steps[0].velocity == 10);

    /* No more undo */
    bool result = undo_undo(&engine);
    assert(!result && "no more undo levels");

    /* Redo back to B */
    result = undo_redo(&engine);
    assert(result);
    assert(engine.patterns[0].tracks[0].steps[0].velocity == 20);

    /* Redo back to C */
    result = undo_redo(&engine);
    assert(result);
    assert(engine.patterns[0].tracks[0].steps[0].velocity == 30);

    /* No more redo */
    result = undo_redo(&engine);
    assert(!result && "no more redo levels");

    sq_engine_shutdown(&engine);
    printf("PASS: Multiple undo/redo\n");
    return 0;
}

/* ── Test 5: New edit after undo clears redo stack ────────────────────────── */
static int test_new_edit_clears_redo(void) {
    init_engine();

    engine.patterns[0].tracks[0].steps[0].velocity = 10;
    undo_push(&engine);
    engine.patterns[0].tracks[0].steps[0].velocity = 20;

    /* Undo to 10 */
    undo_undo(&engine);
    assert(engine.patterns[0].tracks[0].steps[0].velocity == 10);

    /* New edit (should clear redo) */
    undo_push(&engine);
    engine.patterns[0].tracks[0].steps[0].velocity = 99;

    /* Redo should fail — redo stack was cleared */
    bool result = undo_redo(&engine);
    assert(!result && "redo should be cleared after new push");

    /* But undo should work to get back to 10 */
    result = undo_undo(&engine);
    assert(result);
    assert(engine.patterns[0].tracks[0].steps[0].velocity == 10);

    sq_engine_shutdown(&engine);
    printf("PASS: New edit after undo clears redo stack\n");
    return 0;
}

/* ── Test 6: Circular buffer wraps correctly after many pushes ────────────── */
static int test_circular_buffer_wrap(void) {
    init_engine();

    /* Push more than UNDO_MAX_LEVELS times */
    for (int i = 0; i < UNDO_MAX_LEVELS + 10; i++) {
        engine.patterns[0].tracks[0].steps[0].velocity = (uint8_t)(i % 128);
        undo_push(&engine);
    }

    /* Final state */
    engine.patterns[0].tracks[0].steps[0].velocity = 42;

    /* We should be able to undo UNDO_MAX_LEVELS times without crash */
    int undo_count = 0;
    while (undo_undo(&engine)) {
        undo_count++;
    }
    assert(undo_count == UNDO_MAX_LEVELS);

    /* No more undos should be possible */
    assert(!undo_undo(&engine));

    sq_engine_shutdown(&engine);
    printf("PASS: Circular buffer wraps correctly (%d undos after %d pushes)\n",
           undo_count, UNDO_MAX_LEVELS + 10);
    return 0;
}

/* ── Test 7: undo_clear resets everything ─────────────────────────────────── */
static int test_clear(void) {
    init_engine();

    engine.patterns[0].tracks[0].steps[0].velocity = 50;
    undo_push(&engine);
    engine.patterns[0].tracks[0].steps[0].velocity = 60;

    undo_clear();

    /* After clear, undo and redo should both fail */
    assert(!undo_undo(&engine));
    assert(!undo_redo(&engine));

    sq_engine_shutdown(&engine);
    printf("PASS: undo_clear resets all history\n");
    return 0;
}

int main(void) {
    printf("=== Undo/Redo Tests ===\n\n");

    test_undo_no_history();
    test_push_and_undo();
    test_redo();
    test_multiple_undo_redo();
    test_new_edit_clears_redo();
    test_circular_buffer_wrap();
    test_clear();

    printf("\n=== ALL UNDO/REDO TESTS PASSED ===\n");
    return 0;
}
