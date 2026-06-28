#!/usr/bin/env bash
#
# Run clang-tidy over the runtime sources using the repo .clang-tidy
# config. Complements the cppcheck/MISRA gate (tools/run_misra.sh) with
# clang-tidy's path-sensitive static analyzer (clang-analyzer-*) plus the
# bugprone-*/cert-* families. The .clang-tidy config gates only those
# bug- and security-relevant families (WarningsAsErrors); readability
# size/complexity checks stay advisory.
#
# Used by .github/workflows/ci.yml and locally for fast feedback.
#
# Usage:
#   tools/run_clang_tidy.sh             # report findings, never fail
#   tools/run_clang_tidy.sh --strict    # fail if any gating finding fires
#
# Needs a compile_commands.json: configures a throwaway build under
# build-clang-tidy/ if one is not already present. Tests are disabled in
# that throwaway build so the configure step does not need network access
# for the Unity dependency — the runtime sources clang-tidy scans do not
# depend on it.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-${REPO_ROOT}/build-clang-tidy}"
STRICT=0

if [[ "${1:-}" == "--strict" ]]; then
    STRICT=1
fi

if ! command -v clang-tidy >/dev/null 2>&1; then
    echo "clang-tidy not installed" >&2
    exit 2
fi

echo "Using $(clang-tidy --version | head -1 | sed 's/^[[:space:]]*//')"

# --- 1. Ensure compile_commands.json exists ------------------------------

if [[ ! -f "${BUILD_DIR}/compile_commands.json" ]]; then
    echo "=== configuring ${BUILD_DIR} for compile_commands.json ==="
    cmake -B "${BUILD_DIR}" -S "${REPO_ROOT}" -G Ninja \
        -DCMAKE_BUILD_TYPE=Debug \
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
        -DZSERIO_C_BUILD_TESTS=OFF \
        -DZSERIO_C_WERROR=OFF
fi

# --- 2. Run clang-tidy over the runtime sources --------------------------
#
# Scope: runtime/src/*.c. The .clang-tidy HeaderFilterRegex pulls in the
# runtime headers (including the inline fast-paths in bitstream.h) as they
# are included by these translation units.

mapfile -t SOURCES < <(find "${REPO_ROOT}/runtime/src" -name '*.c' | sort)
if [[ "${#SOURCES[@]}" -eq 0 ]]; then
    echo "FAIL: no runtime sources found to analyze" >&2
    exit 1
fi

echo "=== clang-tidy (${#SOURCES[@]} runtime sources) ==="
set +e
clang-tidy -p "${BUILD_DIR}" --quiet "${SOURCES[@]}" 2>&1 \
    | tee "${REPO_ROOT}/clang-tidy.log"
TIDY_RC=${PIPESTATUS[0]}
set -e

# --- 3. Exit -------------------------------------------------------------

if [[ "${STRICT}" -eq 1 ]] && [[ "${TIDY_RC}" -ne 0 ]]; then
    echo "Strict mode: failing on gating clang-tidy findings."
    exit 1
fi

echo "OK"
