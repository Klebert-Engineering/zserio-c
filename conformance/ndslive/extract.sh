#!/usr/bin/env bash
#
# Stage the NDS Live conformance corpus from the user-provided asset
# zip (kept outside the repo; the zip is multi-megabyte and contains
# the C++ generated reference implementation alongside the schemas).
#
# After this runs:
#   conformance/ndslive/zs/       — schema tree, root is zs/all.zs
#   conformance/ndslive/cpp/      — official zserio 2.18.1 C++ output
#                                   (kept for byte-level comparison
#                                    against generated zserio-c output)
#   conformance/ndslive/tile.bin  — encoded SmartLayerTile fixture
#   conformance/ndslive/tile.yaml — zs-yaml dump of the same fixture
#
# Idempotent. Safe to re-run.

set -euo pipefail

ASSETS_DIR="${ASSETS_DIR:-${HOME}/assets}"
ZIP="$(ls "${ASSETS_DIR}"/ndslive-*-zs-cpp.zip 2>/dev/null | head -n1 || true)"
if [[ -z "${ZIP}" || ! -f "${ZIP}" ]]; then
    echo "ndslive: no NDS Live zip found in ${ASSETS_DIR}" >&2
    echo "ndslive: expected ndslive-*-zs-cpp.zip (containing zs/, cpp/)" >&2
    exit 1
fi

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
DEST="${REPO_ROOT}/conformance/ndslive"

# zs/ + cpp/ stay under conformance/ndslive/<dir>; the zip's bin/
# directory ships an embedded zserio toolchain that we don't need.
rm -rf "${DEST}/zs" "${DEST}/cpp"
unzip -q -o "${ZIP}" "zs/*" "cpp/*" -d "${DEST}"

cp "${ASSETS_DIR}/tile.bin"  "${DEST}/tile.bin"
cp "${ASSETS_DIR}/tile.yaml" "${DEST}/tile.yaml"

echo "ndslive: staged $(find "${DEST}/zs" -name '*.zs' | wc -l) .zs files,"
echo "         tile.bin ($(stat -c %s "${DEST}/tile.bin") bytes), tile.yaml,"
echo "         and the upstream C++ reference under ${DEST}/cpp."
