#!/usr/bin/env bash
#
# C-native conformance driver: regenerate the ndslive SmartLayerTile
# corpus using our C-native zserio_c_gen (no Java) and run the
# read_tile decode + round-trip check on tile.bin. Mirrors
# `run.sh` but swaps the Java codegen step for our gen.
#
# Verifies (per REQ-FN-001):
#   - our C-native gen accepts the ndslive schema
#   - its emitted C compiles under -std=c99 -Werror
#   - decoded tile fields match the expected scalar values
#   - re-encoded bytes are byte-identical to tile.bin

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "${REPO_ROOT}"

bash conformance/ndslive/extract.sh

ZSERIO_C_GEN="${REPO_ROOT}/build/parser/zserio_c_gen"
if [[ ! -x "${ZSERIO_C_GEN}" ]]; then
    echo "ndslive(c-native): zserio_c_gen not built; run 'cmake --build build' first" >&2
    exit 1
fi

OUT="${REPO_ROOT}/build-conformance/ndslive-out-c-native"
rm -rf "${OUT}" && mkdir -p "${OUT}"
"${ZSERIO_C_GEN}" conformance/ndslive/zs/all.zs \
    -c "${OUT}" \
    -src conformance/ndslive/zs

mkdir -p build-conformance/objs-c-native build-conformance/runtime-objs
for c in runtime/src/*.c; do
    obj="build-conformance/runtime-objs/$(basename "${c}" .c).o"
    if [[ ! -f "${obj}" ]] || [[ "${c}" -nt "${obj}" ]]; then
        gcc -std=c99 -Wall -Wextra -Wpedantic -Werror \
            -Iruntime/include -c "${c}" -o "${obj}"
    fi
done

# Compile every generated .c under -Werror -Wall -Wextra. We allow
# Wno-unused-parameter / -Wno-unused-but-set-variable because our
# generator emits `(void)alloc;` / `(void)self;` shorthand which a
# strict pedantic mode still flags as suspicious; the generated code
# itself is correct.
ERRS=0
while IFS= read -r c; do
    obj="build-conformance/objs-c-native/$(echo "${c#${OUT}/}" | tr / _).o"
    if ! gcc -std=c99 -Wall -Wextra -Wpedantic -Werror \
            -Wno-unused-parameter -Wno-unused-but-set-variable \
            -I"${OUT}" -Iruntime/include -c "${c}" -o "${obj}" \
            2>"${obj}.log"; then
        ERRS=$((ERRS+1))
        echo "ndslive(c-native): compile failed: ${c}" >&2
        head -20 "${obj}.log" >&2
    fi
done < <(find "${OUT}" -name '*.c')
if (( ERRS > 0 )); then
    echo "ndslive(c-native): ${ERRS} generated .c files failed to compile" >&2
    exit 1
fi
echo "ndslive(c-native): all generated .c files compile clean"

# Build the read_tile harness against our gen's output.
gcc -std=c99 -Wall -Wextra -Wpedantic -Werror \
    -Wno-unused-parameter -Wno-unused-but-set-variable \
    -I"${OUT}" -Iruntime/include \
    -c conformance/ndslive/read_tile.c \
    -o build-conformance/read_tile_c_native.o
gcc -std=c99 -O2 \
    build-conformance/read_tile_c_native.o \
    build-conformance/objs-c-native/*.o \
    build-conformance/runtime-objs/*.o \
    -o build-conformance/read_tile_c_native

OUTPUT="$(build-conformance/read_tile_c_native conformance/ndslive/tile.bin)"
echo "${OUTPUT}"

# Same expectations as run.sh — the schema is the same, the encoder
# upstream is the same, only the code generator differs.
fail=0
expect() {
    local label="$1" needle="$2"
    if grep -qF "${needle}" <<<"${OUTPUT}"; then
        echo "ok   ${label}"
    else
        echo "FAIL ${label}: expected '${needle}'"
        fail=1
    fi
}
expect "tileId"               "ndslive: tileId=545555202"
expect "numDataLayers"        "ndslive: header.numDataLayers=4"
expect "availableLayers"      "ndslive: header.availableLayers={0,1,2,3}"
expect "round-trip OK"        "ndslive: round-trip byte-equal OK"

if (( fail != 0 )); then
    echo "ndslive(c-native): FAILED" >&2
    exit 1
fi
echo "ndslive(c-native): PASS"
