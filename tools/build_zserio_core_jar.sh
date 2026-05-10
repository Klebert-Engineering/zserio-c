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
    # Fallback path: hosts that lack Ant (e.g. CI runners with locked
    # apt) can drop a pre-built upstream-released zserio.jar at one of
    # the locations below. The released jar is the *fat* compiler-core
    # bundle (zserio.tools.ZserioTool entry point + all 3rdparty deps),
    # built by upstream from the same v2.18.1 source we pin against, so
    # it is link-compatible with our generator.
    FALLBACK_CANDIDATES=(
        "${ZSERIO_CORE_JAR:-}"
        "${REPO_ROOT}/build-jars/zserio.jar"
        "${HOME}/assets/zserio.jar"
    )
    for cand in "${FALLBACK_CANDIDATES[@]}"; do
        if [[ -n "${cand}" && -f "${cand}" ]]; then
            cp "${cand}" "${CACHE_JAR}"
            touch "${STAMP}"
            echo "${CACHE_JAR}"
            exit 0
        fi
    done
    echo "ant not installed and no pre-built fallback jar found." >&2
    echo "Either:" >&2
    echo "  - apt-get install -y ant" >&2
    echo "  - or place a v2.18.1-compatible zserio.jar at one of:" >&2
    for cand in "${FALLBACK_CANDIDATES[@]}"; do
        [[ -n "${cand}" ]] && echo "      ${cand}" >&2
    done
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

# zserio_core.jar's manifest declares Class-Path entries referencing
# its 3rdparty jars (antlr, commons-cli, freemarker) as siblings in the
# same directory. The upstream build copies them next to the jar; we
# mirror that layout in our cache directory so anything that loads
# zserio_core.jar (javac, java -jar) finds them.
BUILT_DIR="$(dirname "${BUILT_JAR}")"
cp "${BUILT_DIR}"/*.jar "${CACHE_DIR}/"
touch "${STAMP}"
echo "${CACHE_JAR}"
