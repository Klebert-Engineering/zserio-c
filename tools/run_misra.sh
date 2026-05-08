#!/usr/bin/env bash
#
# Run cppcheck with the MISRA C:2012 addon over the runtime sources.
# Used by .github/workflows/ci.yml and locally during MISRA-deviation triage.
#
# Usage:
#   tools/run_misra.sh                    # report findings
#   tools/run_misra.sh --strict           # fail on any error severity
#
# Verifies: REQ-FS-001
# Verifies: REQ-FS-002
# Verifies: REQ-FS-003
#
# REQ-FS-003 (no recursion) is enforced by cppcheck's misra Rule 17.2,
# which fires under --strict mode now that all categorical findings
# have been triaged into deviation docs under docs/misra-deviations/.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SUPPRESSIONS="${REPO_ROOT}/tools/cppcheck-suppressions.txt"
STRICT=0

if [[ "${1:-}" == "--strict" ]]; then
    STRICT=1
fi

if ! command -v cppcheck >/dev/null 2>&1; then
    echo "cppcheck not installed" >&2
    exit 2
fi

CPPCHECK_VERSION="$(cppcheck --version | head -1)"
echo "Using ${CPPCHECK_VERSION}"

# --- 1. cppcheck with MISRA addon ----------------------------------------

CPPCHECK_ARGS=(
    --enable=warning,style,performance,portability,information
    --inline-suppr
    --std=c99
    --language=c
    --error-exitcode=1
    --suppressions-list="${SUPPRESSIONS}"
    --addon=misra
    -I"${REPO_ROOT}/runtime/include"
    "${REPO_ROOT}/runtime/src"
    "${REPO_ROOT}/runtime/include"
)

echo "=== cppcheck ==="
set +e
cppcheck "${CPPCHECK_ARGS[@]}" 2>&1 | tee "${REPO_ROOT}/cppcheck.log"
CPPCHECK_RC=$?
set -e

# --- 2. Forbidden-symbol grep for REQ-FS-001 (no direct heap) ------------
#
# `runtime/src/allocator.c` is the explicit, sanctioned wrapper around
# malloc/realloc/free; every other runtime source must route through
# the zserio_allocator_t vtable.

# Use Perl regex with negative lookbehind so we don't flag member-call
# expressions like `vtable->free(...)`. The lookbehind rejects `->` and
# `.` immediately before the symbol name.
FORBIDDEN='(?<![>.])\b(malloc|calloc|realloc|free|aligned_alloc|posix_memalign|valloc|pvalloc|memalign|sbrk|mmap)\b\s*\('
FAILED=0
echo "=== forbidden-call check (REQ-FS-001) ==="
while IFS= read -r -d '' src; do
    base="$(basename "${src}")"
    if [[ "${base}" == "allocator.c" ]]; then continue; fi
    if grep -nP "${FORBIDDEN}" "${src}"; then
        echo "FAIL: forbidden call in ${src}"
        FAILED=1
    fi
done < <(find "${REPO_ROOT}/runtime/src" -name '*.c' -print0)

# --- 3. Forbidden-symbol grep for REQ-FS-002 (no setjmp/longjmp) ---------

FORBIDDEN_JMP='(?<![>.])\b(setjmp|longjmp|sigsetjmp|siglongjmp|_setjmp|_longjmp)\b\s*\('
echo "=== forbidden-call check (REQ-FS-002) ==="
while IFS= read -r -d '' src; do
    if grep -nP "${FORBIDDEN_JMP}" "${src}"; then
        echo "FAIL: non-local control flow in ${src}"
        FAILED=1
    fi
done < <(find "${REPO_ROOT}/runtime" -name '*.c' -o -name '*.h' -print0)

# --- 4. Combined exit ----------------------------------------------------

if [[ "${STRICT}" -eq 1 ]]; then
    if [[ "${CPPCHECK_RC}" -ne 0 ]] || [[ "${FAILED}" -ne 0 ]]; then
        echo "Strict mode: failing on findings."
        exit 1
    fi
else
    # Non-strict: report cppcheck findings but only fail on the
    # forbidden-symbol grep checks, which are clear-cut.
    if [[ "${FAILED}" -ne 0 ]]; then
        exit 1
    fi
fi

echo "OK"
