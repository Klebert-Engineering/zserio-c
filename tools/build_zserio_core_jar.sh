#!/usr/bin/env bash
#
# Build the upstream zserio_core.jar via Ant from the third_party/zserio
# submodule. Caches the resulting JAR at a stable path so other build
# steps don't have to know Ant's per-Java-version directory layout.
#
# Output (on stdout): absolute path to the built zserio_core.jar.
#
# Verifies: REQ-FN-001 (the JAR drives our generator's interpretation of
#                       the wire format)

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
UPSTREAM_DIR="${REPO_ROOT}/third_party/zserio"
CACHE_DIR="${REPO_ROOT}/build-jars"
CACHE_JAR="${CACHE_DIR}/zserio_core.jar"
STAMP="${CACHE_DIR}/zserio_core.stamp"

if [[ ! -d "${UPSTREAM_DIR}/.git" ]] && [[ ! -f "${UPSTREAM_DIR}/build.xml" ]]; then
    echo "upstream submodule not initialised at ${UPSTREAM_DIR}" >&2
    echo "run: git submodule update --init third_party/zserio" >&2
    exit 2
fi

mkdir -p "${CACHE_DIR}"

# Skip rebuild if the cached jar is newer than every upstream source.
if [[ -f "${CACHE_JAR}" ]] && [[ -f "${STAMP}" ]]; then
    NEWEST_SRC="$(find "${UPSTREAM_DIR}/compiler/core" \
        -type f \
        \( -name '*.java' -o -name '*.g4' -o -name 'build.xml' \) \
        -newer "${STAMP}" -print -quit 2>/dev/null || true)"
    if [[ -z "${NEWEST_SRC}" ]]; then
        echo "${CACHE_JAR}"
        exit 0
    fi
fi

if ! command -v ant >/dev/null 2>&1; then
    echo "ant not installed (apt-get install -y ant)" >&2
    exit 2
fi

(
    cd "${UPSTREAM_DIR}"
    ant -f compiler/core/build.xml jar 1>&2
)

# Locate the freshly-built jar (path varies by Java version).
BUILT_JAR="$(find "${UPSTREAM_DIR}/build/compiler/core" -name 'zserio_core.jar' -print -quit)"
if [[ -z "${BUILT_JAR}" ]]; then
    echo "build_zserio_core_jar: could not locate zserio_core.jar after ant build" >&2
    exit 1
fi

cp "${BUILT_JAR}" "${CACHE_JAR}"
touch "${STAMP}"
echo "${CACHE_JAR}"
