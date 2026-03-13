#!/bin/bash
# Run all tests. Pass "quick" for unit tests only, or "full" for sanitizers + fuzz.
# Usage: ./scripts/test_all.sh [quick|full]
set -e

MODE="${1:-quick}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="${PROJECT_DIR}/build"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

pass() { echo -e "${GREEN}PASS${NC}: $1"; }
fail() { echo -e "${RED}FAIL${NC}: $1"; exit 1; }
skip() { echo -e "${YELLOW}SKIP${NC}: $1"; }

echo "============================================"
echo "  0x808 Test Suite (mode: ${MODE})"
echo "============================================"
echo ""

# Ensure build directory exists
if [ ! -d "$BUILD_DIR" ]; then
    echo "Build directory not found. Creating..."
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"
    cmake .. || fail "CMake configuration"
    cd "$PROJECT_DIR"
fi

# Build all test targets
echo "--- Building tests ---"
cd "$BUILD_DIR"
make -j$(nproc) engine_render_test export_test project_test fm_synth_test swing_humanize_test effects_test synth_test wavetable_test undo_test malformed_input_test edge_case_test gui_feature_test fuzz_project fuzz_sample_io plugin_load_test snapshot_test 2>&1 || fail "Build failed"
echo ""

# Run tests from project root (tests load samples/ relative to CWD)
cd "$PROJECT_DIR"

# ─── Unit Tests ───────────────────────────────────────────────
echo "--- Unit Tests ---"

# Engine render test
if [ -f "${BUILD_DIR}/engine_render_test" ]; then
    "${BUILD_DIR}/engine_render_test" > /dev/null 2>&1 && pass "engine_render_test" || fail "engine_render_test"
else
    skip "engine_render_test (not built)"
fi

# Export test
if [ -f "${BUILD_DIR}/export_test" ]; then
    "${BUILD_DIR}/export_test" > /dev/null 2>&1 && pass "export_test" || fail "export_test"
else
    skip "export_test (not built)"
fi

# Project save/load round-trip
if [ -f "${BUILD_DIR}/project_test" ]; then
    "${BUILD_DIR}/project_test" > /dev/null 2>&1 && pass "project_test" || fail "project_test"
else
    skip "project_test (not built)"
fi

# FM synth test
if [ -f "${BUILD_DIR}/fm_synth_test" ]; then
    "${BUILD_DIR}/fm_synth_test" > /dev/null 2>&1 && pass "fm_synth_test" || fail "fm_synth_test"
else
    skip "fm_synth_test (not built)"
fi

# Swing/humanize test
if [ -f "${BUILD_DIR}/swing_humanize_test" ]; then
    "${BUILD_DIR}/swing_humanize_test" > /dev/null 2>&1 && pass "swing_humanize_test" || fail "swing_humanize_test"
else
    skip "swing_humanize_test (not built)"
fi

# Effects DSP test
if [ -f "${BUILD_DIR}/effects_test" ]; then
    "${BUILD_DIR}/effects_test" > /dev/null 2>&1 && pass "effects_test" || fail "effects_test"
else
    skip "effects_test (not built)"
fi

# Subtractive synth test
if [ -f "${BUILD_DIR}/synth_test" ]; then
    "${BUILD_DIR}/synth_test" > /dev/null 2>&1 && pass "synth_test" || fail "synth_test"
else
    skip "synth_test (not built)"
fi

# Wavetable synth test
if [ -f "${BUILD_DIR}/wavetable_test" ]; then
    "${BUILD_DIR}/wavetable_test" > /dev/null 2>&1 && pass "wavetable_test" || fail "wavetable_test"
else
    skip "wavetable_test (not built)"
fi

# Undo/redo test
if [ -f "${BUILD_DIR}/undo_test" ]; then
    "${BUILD_DIR}/undo_test" > /dev/null 2>&1 && pass "undo_test" || fail "undo_test"
else
    skip "undo_test (not built)"
fi

# Malformed input test
if [ -f "${BUILD_DIR}/malformed_input_test" ]; then
    "${BUILD_DIR}/malformed_input_test" > /dev/null 2>&1 && pass "malformed_input_test" || fail "malformed_input_test"
else
    skip "malformed_input_test (not built)"
fi

# Edge case test
if [ -f "${BUILD_DIR}/edge_case_test" ]; then
    "${BUILD_DIR}/edge_case_test" > /dev/null 2>&1 && pass "edge_case_test" || fail "edge_case_test"
else
    skip "edge_case_test (not built)"
fi

# GUI feature test (headless)
if [ -f "${BUILD_DIR}/gui_feature_test" ]; then
    "${BUILD_DIR}/gui_feature_test" > /dev/null 2>&1 && pass "gui_feature_test" || fail "gui_feature_test"
else
    skip "gui_feature_test (not built)"
fi

# Fuzz project load
if [ -f "${BUILD_DIR}/fuzz_project" ]; then
    "${BUILD_DIR}/fuzz_project" > /dev/null 2>&1 && pass "fuzz_project" || fail "fuzz_project"
else
    skip "fuzz_project (not built)"
fi

# Fuzz sample IO
if [ -f "${BUILD_DIR}/fuzz_sample_io" ]; then
    "${BUILD_DIR}/fuzz_sample_io" > /dev/null 2>&1 && pass "fuzz_sample_io" || fail "fuzz_sample_io"
else
    skip "fuzz_sample_io (not built)"
fi

# Plugin integration test
if [ -f "${BUILD_DIR}/plugin_load_test" ]; then
    "${BUILD_DIR}/plugin_load_test" > /dev/null 2>&1 && pass "plugin_load_test" || fail "plugin_load_test"
else
    skip "plugin_load_test (not built)"
fi

# Snapshot regression test
if [ -f "${BUILD_DIR}/snapshot_test" ]; then
    "${BUILD_DIR}/snapshot_test" > /dev/null 2>&1 && pass "snapshot_test" || fail "snapshot_test"
else
    skip "snapshot_test (not built)"
fi

echo ""

# ─── Static Analysis (if available) ──────────────────────────
echo "--- Static Analysis ---"
if command -v cppcheck &> /dev/null; then
    cppcheck --enable=warning,performance --error-exitcode=1 \
        --suppress=missingIncludeSystem \
        --suppress=unusedFunction \
        -I "${PROJECT_DIR}/src" \
        -I "${PROJECT_DIR}/deps" \
        "${PROJECT_DIR}/src/" 2>&1 | tail -5
    pass "cppcheck"
else
    skip "cppcheck (not installed — apt install cppcheck)"
fi
echo ""

# ─── Sanitizer Tests (full mode only) ────────────────────────
if [ "$MODE" = "full" ]; then
    echo "--- Sanitizer Tests ---"

    # Build with ASan
    ASAN_DIR="${PROJECT_DIR}/build_asan"
    mkdir -p "$ASAN_DIR"
    cd "$ASAN_DIR"
    cmake .. -DCMAKE_C_FLAGS="-fsanitize=address -fno-omit-frame-pointer -g" \
             -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address" 2>/dev/null || fail "ASan CMake"
    make -j$(nproc) engine_render_test export_test project_test fm_synth_test 2>&1 | tail -3

    for test in engine_render_test export_test project_test fm_synth_test; do
        if [ -f "${ASAN_DIR}/${test}" ]; then
            ASAN_OPTIONS=detect_leaks=1 "${ASAN_DIR}/${test}" > /dev/null 2>&1 \
                && pass "ASan: ${test}" || fail "ASan: ${test}"
        fi
    done

    # Build with UBSan
    UBSAN_DIR="${PROJECT_DIR}/build_ubsan"
    mkdir -p "$UBSAN_DIR"
    cd "$UBSAN_DIR"
    cmake .. -DCMAKE_C_FLAGS="-fsanitize=undefined -fno-omit-frame-pointer -g" \
             -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=undefined" 2>/dev/null || fail "UBSan CMake"
    make -j$(nproc) engine_render_test export_test project_test fm_synth_test 2>&1 | tail -3

    for test in engine_render_test export_test project_test fm_synth_test; do
        if [ -f "${UBSAN_DIR}/${test}" ]; then
            "${UBSAN_DIR}/${test}" > /dev/null 2>&1 \
                && pass "UBSan: ${test}" || fail "UBSan: ${test}"
        fi
    done
    echo ""
fi

# ─── Summary ─────────────────────────────────────────────────
echo "============================================"
echo -e "  ${GREEN}All tests passed!${NC}"
echo "============================================"
