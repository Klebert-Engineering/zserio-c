#!/usr/bin/env bash
#
# Build the upstream zserio.jar (full bundle: zserio_core + cpp/java/
# python extensions) via Ant from the third_party/zserio submodule.
# Caches the resulting JAR at a stable path so other build steps don't
# have to know Ant's per-Java-version directory layout.
#
# The cross-validation harness (tests/xvalidation/run_xvalidation.sh)
# loads this JAR to drive both the C99 generator (our zserio_c.jar
# adds its own classpath entry on top) and upstream's cpp generator
# (used as a wire-format oracle).
#
# Output (on stdout): absolute path to the built zserio.jar.
#
# Verifies: REQ-FN-001 (the bundle JAR exposes upstream's cpp extension
#                       so the harness has an independent wire-format
#                       reference to round-trip against).

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
UPSTREAM_DIR="${REPO_ROOT}/third_party/zserio"
CACHE_DIR="${REPO_ROOT}/build-jars"
CACHE_JAR="${CACHE_DIR}/zserio.jar"
STAMP="${CACHE_DIR}/zserio_bundle.stamp"

if [[ ! -d "${UPSTREAM_DIR}/.git" ]] && [[ ! -f "${UPSTREAM_DIR}/build.xml" ]]; then
    echo "upstream submodule not initialized at ${UPSTREAM_DIR}" >&2
    echo "run: git submodule update --init third_party/zserio" >&2
    exit 2
fi

mkdir -p "${CACHE_DIR}"

# Skip rebuild if the cached jar is newer than every upstream source
# under compiler/ (core + every extension).
if [[ -f "${CACHE_JAR}" ]] && [[ -f "${STAMP}" ]]; then
    NEWEST_SRC="$(find "${UPSTREAM_DIR}/compiler" \
        -type f \
        \( -name '*.java' -o -name '*.g4' -o -name '*.xml' \
           -o -name '*.ftl' \) \
        -newer "${STAMP}" -print -quit 2>/dev/null || true)"
    if [[ -z "${NEWEST_SRC}" ]]; then
        echo "${CACHE_JAR}"
        exit 0
    fi
fi

if ! command -v ant >/dev/null 2>&1; then
    # Same fallback pattern as build_zserio_core_jar.sh — accept a
    # pre-built upstream-released zserio.jar at known locations when
    # Ant isn't available. The released bundle is built from the
    # v2.18.1 source we pin against, so it is wire-compatible.
    FALLBACK_CANDIDATES=(
        "${ZSERIO_BUNDLE_JAR:-}"
        "${REPO_ROOT}/build-jars/zserio.jar"
        "${HOME}/assets/zserio.jar"
    )
    for cand in "${FALLBACK_CANDIDATES[@]}"; do
        if [[ -n "${cand}" && -f "${cand}" && "${cand}" != "${CACHE_JAR}" ]]; then
            cp "${cand}" "${CACHE_JAR}"
            touch "${STAMP}"
            echo "${CACHE_JAR}"
            exit 0
        fi
    done
    # CACHE_JAR is already in place (e.g. pre-staged by a sibling
    # build step or a developer's local checkout). Treat that as the
    # answer and refresh the stamp so subsequent invocations skip the
    # rebuild loop above.
    if [[ -f "${CACHE_JAR}" ]]; then
        touch "${STAMP}"
        echo "${CACHE_JAR}"
        exit 0
    fi
    echo "ant not installed and no pre-built fallback bundle jar found." >&2
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
    # `zserio_bundle.jar` in the root build.xml ONLY depends on
    # `install` (= zserio_core.install) — it does NOT build the
    # extensions. The bundle target then sweeps every `*.jar` it
    # finds under `${zserio.jars_dir}` (= distr/zserio_libs/) into
    # the fat jar. So we must build & install the cpp extension
    # FIRST, otherwise the bundle silently lacks the cpp generator
    # and `java -jar zserio.jar ... -cpp out_dir ...` parses but
    # produces no cpp output (codegen returns non-zero, gen.log is
    # empty because the failure is "no extension registered for
    # -cpp" which prints nothing on the error path we capture).
    #
    # Build cpp extension first, then the bundle. The cpp extension's
    # `install` target compiles + jars + copies to zserio_libs/.
    ant -f compiler/extensions/cpp/build.xml install 1>&2
    ant -f build.xml zserio_bundle.jar 1>&2
)

# Locate the freshly-built bundle jar. Upstream lands it at
# build/zserio/<java-version>/zserio.jar; search the whole build/
# subtree so we don't have to encode the Java-version directory.
BUILT_JAR="$(find "${UPSTREAM_DIR}/build" -name 'zserio.jar' -print -quit 2>/dev/null)"
if [[ -z "${BUILT_JAR}" ]]; then
    echo "build_zserio_bundle_jar: could not locate zserio.jar after ant build" >&2
    exit 1
fi

cp "${BUILT_JAR}" "${CACHE_JAR}"
touch "${STAMP}"
echo "${CACHE_JAR}"
