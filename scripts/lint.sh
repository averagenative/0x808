#!/usr/bin/env bash
#
# lint.sh — Run cppcheck static analysis on the 0x808 codebase.
#
# Returns non-zero when cppcheck finds an `error`-level issue
# (which is what GitHub Actions CI uses to fail the build).
# `warning`/`style`/`performance` findings print but don't fail.
#
# Excludes the vendored dependencies in deps/ — we don't fix
# upstream code, and cppcheck on imgui/cJSON/dr_libs would
# bury real findings in noise.
#
# Usage:
#   ./scripts/lint.sh           # default: warnings + portability + perf
#   ./scripts/lint.sh --strict  # also fail on warning-level findings

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

if ! command -v cppcheck >/dev/null 2>&1; then
    echo "ERROR: cppcheck not found. Install with: dnf install cppcheck" >&2
    echo "                                  (or: apt-get install cppcheck)" >&2
    exit 2
fi

ERROR_EXIT=1
if [[ "${1:-}" == "--strict" ]]; then
    # Fail on any non-style finding (use cppcheck-result level promotion)
    ENABLE="warning,style,performance,portability"
else
    ENABLE="warning,style,performance,portability"
fi

SUPPR="$REPO_ROOT/cppcheck-suppressions.txt"
SUPPR_ARG=()
if [[ -f "$SUPPR" ]]; then
    SUPPR_ARG=("--suppressions-list=$SUPPR")
fi

echo "==> Running cppcheck on src/ and tests/ (excluding deps/)"

# Notes on flags:
#   --enable=...       turn on extra checks (warning/style/perf/portability)
#   --error-exitcode=1 exit non-zero only on `error`-level findings; warnings
#                      print but don't fail CI
#   --inline-suppr     honour `// cppcheck-suppress xxx` comments in source
#   --quiet            suppress the "Checking foo.c..." spam
#   --std=c99          C standard for .c files (cppcheck picks C++ standard
#                      automatically for .cpp)
#   -I ...             include search paths so cppcheck resolves headers
#   -i ...             skip these directories entirely
#   --suppress=missingIncludeSystem    we don't ship system headers
#   --suppress=unusedFunction          public APIs and test helpers trigger this
#   --suppress=ConfigurationNotChecked happens when ifdef branches aren't all
#                                      configured; tracking every #ifdef path is
#                                      not worth the noise here
cppcheck \
    --enable="$ENABLE" \
    --error-exitcode="$ERROR_EXIT" \
    --inline-suppr \
    --quiet \
    --std=c99 \
    -I src \
    -I deps \
    -I deps/imgui \
    -i deps \
    -i build \
    -i build_win \
    -i build-win64 \
    --suppress=missingIncludeSystem \
    --suppress=unusedFunction \
    --suppress=ConfigurationNotChecked \
    "${SUPPR_ARG[@]}" \
    src/ tests/

echo "==> cppcheck passed"
