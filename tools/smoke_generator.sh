#!/usr/bin/env bash
#
# End-to-end smoke test: invoke the upstream ZserioTool with our generator
# JAR on the classpath, against the hello-world schema. The Phase 2
# skeleton's process() prints a banner and creates the output directory;
# success is "exit 0 and the banner appeared".
#
# When the generator grows real emission, this script becomes the
# place where we cross-check the .c/.h output against expectations.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"

CORE_JAR="$("${REPO_ROOT}/tools/build_zserio_core_jar.sh")"
GENERATOR_JAR="$("${REPO_ROOT}/tools/build_generator_jar.sh")"

# Other 3rdparty deps that ZserioTool needs at runtime.
TP="${REPO_ROOT}/third_party/zserio/3rdparty/java"
RUNTIME_CP="${CORE_JAR}:${GENERATOR_JAR}"
RUNTIME_CP+=":${TP}/commons-cli-1.4.jar"
RUNTIME_CP+=":${TP}/antlr-4.7.2-complete.jar"
RUNTIME_CP+=":${TP}/freemarker-2.3.28.jar"
# Doc-comment parsing (zserio core uses commonmark in some paths even when
# only the parser is exercised).
RUNTIME_CP+=":${TP}/commonmark-0.15.2.jar"
RUNTIME_CP+=":${TP}/autolink-0.10.0.jar"
RUNTIME_CP+=":${TP}/commonmark-ext-autolink-0.15.2.jar"
RUNTIME_CP+=":${TP}/commonmark-ext-gfm-tables-0.15.2.jar"
RUNTIME_CP+=":${TP}/commonmark-ext-heading-anchor-0.15.2.jar"

SCHEMA_DIR="${REPO_ROOT}/examples/hello_world"
OUTPUT_DIR="${REPO_ROOT}/build-generator-smoke/out"

rm -rf "${OUTPUT_DIR}"
mkdir -p "${OUTPUT_DIR}"

echo "running ZserioTool with the C99 extension on the hello_world schema..."
java -cp "${RUNTIME_CP}" zserio.tools.ZserioTool \
    -src "${SCHEMA_DIR}" hello.zs \
    -c "${OUTPUT_DIR}"

if [[ ! -d "${OUTPUT_DIR}" ]]; then
    echo "smoke: extension did not create output directory" >&2
    exit 1
fi

echo "smoke: zserio-c extension loaded and ran cleanly."
