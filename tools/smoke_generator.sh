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

# Verify the ConstEmitter produced one .h per `const` declaration.
EXPECTED=(
    "${OUTPUT_DIR}/hello/max_age.h"
    "${OUTPUT_DIR}/hello/max_name_length.h"
    "${OUTPUT_DIR}/hello/enable_bonus.h"
)
for h in "${EXPECTED[@]}"; do
    if [[ ! -f "${h}" ]]; then
        echo "smoke: expected generated header missing: ${h}" >&2
        echo "smoke: contents of ${OUTPUT_DIR}:" >&2
        find "${OUTPUT_DIR}" -type f >&2
        exit 1
    fi
done

# Verify the headers compile cleanly under our top-level warning set.
echo "smoke: compiling generated headers under -std=c99 -Wall -Wextra -Werror..."
SMOKE_C="${REPO_ROOT}/build-generator-smoke/include_test.c"
{
    printf '%s\n' "$(for h in "${EXPECTED[@]}"; do
        printf '#include "%s"\n' "${h#${OUTPUT_DIR}/}"
    done)"
    printf 'int main(void) { return 0; }\n'
} > "${SMOKE_C}"
gcc -std=c99 -Wall -Wextra -Wpedantic -Werror \
    -I"${OUTPUT_DIR}" \
    "${SMOKE_C}" \
    -o "${REPO_ROOT}/build-generator-smoke/include_test"

echo "smoke: zserio-c extension loaded and ran cleanly; generated headers compile."
