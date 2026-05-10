#!/usr/bin/env bash
#
# Phase 4 conformance driver for the NDS Live SmartLayerTile fixture.
#
# 1. Stage corpus from the asset zip (extract.sh).
# 2. Run our zserio-c generator on the staged schema.
# 3. Compile every generated .c plus the runtime under -std=c99 -Werror.
# 4. Build conformance/ndslive/read_tile.c and run it on tile.bin.
# 5. Diff the decoded scalar fields against tile.yaml's expected values.
#
# Verifies: REQ-FN-001 (byte-exact read against upstream-produced bitstream).

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "${REPO_ROOT}"

# --- 1. stage corpus ---------------------------------------------------------
bash conformance/ndslive/extract.sh

# --- 2. generate -------------------------------------------------------------
CORE_JAR="$(tools/build_zserio_core_jar.sh)"
GEN_JAR="$(tools/build_generator_jar.sh)"
OUT="${REPO_ROOT}/build-conformance/ndslive-out"
rm -rf "${OUT}" && mkdir -p "${OUT}"
java -cp "${CORE_JAR}:${GEN_JAR}" zserio.tools.ZserioTool \
    -src conformance/ndslive/zs all.zs -c "${OUT}" 1>&2

# --- 3. compile every generated .c + runtime --------------------------------
mkdir -p build-conformance/objs build-conformance/runtime-objs
for c in runtime/src/*.c; do
    obj="build-conformance/runtime-objs/$(basename "${c}" .c).o"
    gcc -std=c99 -Wall -Wextra -Wpedantic -Werror \
        -Iruntime/include -c "${c}" -o "${obj}"
done
ERRS=0
while IFS= read -r c; do
    obj="build-conformance/objs/$(echo "${c#${OUT}/}" | tr / _).o"
    if ! gcc -std=c99 -Wall -Wextra -Wpedantic -Werror \
            -I"${OUT}" -Iruntime/include -c "${c}" -o "${obj}"; then
        ERRS=$((ERRS+1))
    fi
done < <(find "${OUT}" -name '*.c')
if (( ERRS > 0 )); then
    echo "ndslive: ${ERRS} generated .c files failed to compile" >&2
    exit 1
fi

# --- 4. build + run reader --------------------------------------------------
gcc -std=c99 -Wall -Wextra -Wpedantic -Werror \
    -I"${OUT}" -Iruntime/include \
    -c conformance/ndslive/read_tile.c \
    -o build-conformance/read_tile.o
gcc -std=c99 -O2 \
    build-conformance/read_tile.o \
    build-conformance/objs/*.o \
    build-conformance/runtime-objs/*.o \
    -o build-conformance/read_tile

OUTPUT="$(build-conformance/read_tile conformance/ndslive/tile.bin)"
echo "${OUTPUT}"

# --- 5. cross-check against tile.yaml ---------------------------------------
# tile.yaml is canonical (zs-yaml dump from the same upstream encoder).
# We compare a fixed set of scalar / string fields; this isn't a full
# round-trip yet — externs are opaque blobs at this layer — but it's
# enough to catch off-by-one wire-format regressions.
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
expect "layer[0] descriptor"  "name=ROAD version=2024.03 target=road.layer.RoadLayer"
expect "layer[1] descriptor"  "name=ROAD version=2024.03 target=road.layer.RoadGeometryLayer"
expect "layer[2] descriptor"  "name=CHARACTERISTICS version=2024.03 target=characteristics.layer.RoadCharacteristicsLayer"
expect "layer[3] descriptor"  "name=RULES version=2024.03 target=rules.layer.RoadRulesLayer"

if (( fail )); then
    echo "ndslive: conformance MISMATCH" >&2
    exit 1
fi
echo "ndslive: conformance OK"
