#!/usr/bin/env bash
#
# Wire-format oracle: build upstream's C++ runtime + generated NDS Live
# code with a small trace hook, decode tile.bin, optionally focused on
# a single layer, and write a per-primitive-read trace.
#
# The trace can be diff'd against an equivalent trace from zserio-c's
# own reader to localise wire-format divergence to a specific bit
# position. See conformance/ndslive/cpp_oracle/README.md for the diff
# workflow.
#
# Usage:
#   bash conformance/ndslive/cpp_oracle/run_oracle.sh [--layer N] <out_trace>
#
# Requires that conformance/ndslive/extract.sh has staged the cpp/
# tree (it's extracted from the user-supplied asset zip). The
# generator + main reader builds are NOT touched.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
ND_ROOT="${REPO_ROOT}/conformance/ndslive"
ORACLE_DIR="${ND_ROOT}/cpp_oracle"
CPP_DIR="${ND_ROOT}/cpp"
BUILD_DIR="${REPO_ROOT}/build-oracle/cpp"

LAYER=""
OUT_TRACE=""
while [[ $# -gt 0 ]]; do
    case "$1" in
        --layer) LAYER="$2"; shift 2 ;;
        *) OUT_TRACE="$1"; shift ;;
    esac
done

if [[ ! -d "${CPP_DIR}" ]]; then
    echo "oracle: cpp/ not staged — run conformance/ndslive/extract.sh first" >&2
    exit 1
fi

# Apply the BitStreamReader trace patch idempotently. The patch is
# guarded by a sentinel string we check for before applying so multiple
# invocations don't double-patch.
PATCH="${ORACLE_DIR}/bitstream_reader_trace.patch"
BSR="${CPP_DIR}/zserio_runtime/zserio/BitStreamReader.cpp"
if ! grep -q "zsc_trace_set_enabled" "${BSR}"; then
    (cd "${CPP_DIR}" && patch -p0 < "${PATCH}") || {
        echo "oracle: failed to apply trace patch to ${BSR}" >&2
        exit 1
    }
fi

# Drop in the oracle driver source + extend the cpp CMakeLists to
# build it. Both operations are idempotent.
if [[ ! -f "${CPP_DIR}/read_tile_cpp.cpp" ]]; then
    cp "${ORACLE_DIR}/read_tile_cpp.cpp" "${CPP_DIR}/read_tile_cpp.cpp"
fi
if ! grep -q "read_tile_cpp" "${CPP_DIR}/CMakeLists.txt"; then
    cat >> "${CPP_DIR}/CMakeLists.txt" <<'EOF'

# Oracle driver (added by cpp_oracle/run_oracle.sh): reads tile.bin via
# upstream's C++ runtime, optionally emitting a per-primitive-read trace
# to the path in ZSERIO_TRACE.
add_executable (read_tile_cpp read_tile_cpp.cpp)
target_include_directories (read_tile_cpp PRIVATE nds_live_module)
target_link_libraries (read_tile_cpp nds_live_module ZserioCppRuntime)
EOF
fi

mkdir -p "${BUILD_DIR}"
cmake -S "${CPP_DIR}" -B "${BUILD_DIR}" 1>&2
cmake --build "${BUILD_DIR}" --target read_tile_cpp --parallel 1>&2

ARGS=("${ND_ROOT}/tile.bin")
if [[ -n "${LAYER}" ]]; then ARGS+=(--layer "${LAYER}"); fi
if [[ -n "${OUT_TRACE}" ]]; then
    ZSERIO_TRACE="${OUT_TRACE}" "${BUILD_DIR}/read_tile_cpp" "${ARGS[@]}"
else
    "${BUILD_DIR}/read_tile_cpp" "${ARGS[@]}"
fi
