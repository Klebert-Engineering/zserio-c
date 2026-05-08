#!/usr/bin/env bash
#
# Compile the zserio-c generator (Java sources under generator/src/) and
# package as a JAR with the META-INF service descriptor included.
#
# Inputs:
#   - The upstream zserio_core.jar (built via tools/build_zserio_core_jar.sh)
#   - commons-cli (vendored under third_party/zserio/3rdparty/java/)
#
# Output (on stdout): absolute path to zserio_c.jar.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
GEN_DIR="${REPO_ROOT}/generator"
SRC_DIR="${GEN_DIR}/src/main/java"
RES_DIR="${GEN_DIR}/src/main/resources"
BUILD_DIR="${GEN_DIR}/build"
CLASSES_DIR="${BUILD_DIR}/classes"
JAR_DIR="${BUILD_DIR}/jar"
JAR_FILE="${JAR_DIR}/zserio_c.jar"

CORE_JAR="${CORE_JAR:-$("${REPO_ROOT}/tools/build_zserio_core_jar.sh")}"
COMMONS_CLI_JAR="${REPO_ROOT}/third_party/zserio/3rdparty/java/commons-cli-1.4.jar"

if [[ ! -f "${CORE_JAR}" ]]; then
    echo "build_generator_jar: missing zserio_core.jar at ${CORE_JAR}" >&2
    exit 1
fi
if [[ ! -f "${COMMONS_CLI_JAR}" ]]; then
    echo "build_generator_jar: missing ${COMMONS_CLI_JAR}" >&2
    echo "(make sure third_party/zserio submodule is initialised)" >&2
    exit 1
fi

rm -rf "${BUILD_DIR}"
mkdir -p "${CLASSES_DIR}" "${JAR_DIR}"

# Compile.
mapfile -t SRCS < <(find "${SRC_DIR}" -name '*.java' -print)
javac \
    -encoding UTF-8 \
    -Xlint:all -Werror \
    --release 11 \
    -classpath "${CORE_JAR}:${COMMONS_CLI_JAR}" \
    -d "${CLASSES_DIR}" \
    "${SRCS[@]}"

# Stage resources (the META-INF service descriptor) alongside classes.
if [[ -d "${RES_DIR}" ]]; then
    (cd "${RES_DIR}" && cp -r . "${CLASSES_DIR}/")
fi

# Pack.
jar --create --file="${JAR_FILE}" -C "${CLASSES_DIR}" .
echo "${JAR_FILE}"
