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

# Verify each emitter produced its expected output file. The list
# grows as Phase 2 batches add new emitters.
EXPECTED=(
    "${OUTPUT_DIR}/hello/max_age.h"
    "${OUTPUT_DIR}/hello/max_name_length.h"
    "${OUTPUT_DIR}/hello/enable_bonus.h"
    "${OUTPUT_DIR}/hello/block_index.h"
    "${OUTPUT_DIR}/hello/color.h"
    "${OUTPUT_DIR}/hello/permission.h"
    "${OUTPUT_DIR}/hello/hello.h"
    "${OUTPUT_DIR}/hello/hello.c"
    "${OUTPUT_DIR}/hello/address.h"
    "${OUTPUT_DIR}/hello/address.c"
    "${OUTPUT_DIR}/hello/greeting.h"
    "${OUTPUT_DIR}/hello/greeting.c"
    "${OUTPUT_DIR}/hello/variant.h"
    "${OUTPUT_DIR}/hello/variant.c"
    "${OUTPUT_DIR}/hello/container.h"
    "${OUTPUT_DIR}/hello/container.c"
    "${OUTPUT_DIR}/hello/profile.h"
    "${OUTPUT_DIR}/hello/profile.c"
    "${OUTPUT_DIR}/hello/telemetry.h"
    "${OUTPUT_DIR}/hello/telemetry.c"
    "${OUTPUT_DIR}/hello/catalog.h"
    "${OUTPUT_DIR}/hello/catalog.c"
    "${OUTPUT_DIR}/hello/layer_data.h"
    "${OUTPUT_DIR}/hello/layer_data.c"
    "${OUTPUT_DIR}/hello/power_of_two_block.h"
    "${OUTPUT_DIR}/hello/power_of_two_block.c"
    "${OUTPUT_DIR}/hello/frame.h"
    "${OUTPUT_DIR}/hello/frame.c"
)
for f in "${EXPECTED[@]}"; do
    if [[ ! -f "${f}" ]]; then
        echo "smoke: expected generated file missing: ${f}" >&2
        echo "smoke: contents of ${OUTPUT_DIR}:" >&2
        find "${OUTPUT_DIR}" -type f >&2
        exit 1
    fi
done

# Compile the generated headers (via a stub TU that #includes each)
# under our top-level warning set. The runtime headers are needed
# because Structure.h pulls in <zserio/...>.
echo "smoke: compiling generated headers under -std=c99 -Wall -Wextra -Werror..."
HEADER_SMOKE_C="${REPO_ROOT}/build-generator-smoke/include_test.c"
{
    for h in "${EXPECTED[@]}"; do
        case "${h}" in
            *.h) printf '#include "%s"\n' "${h#${OUTPUT_DIR}/}" ;;
        esac
    done
    printf 'int main(void) { return 0; }\n'
} > "${HEADER_SMOKE_C}"
gcc -std=c99 -Wall -Wextra -Wpedantic -Werror \
    -I"${OUTPUT_DIR}" \
    -I"${REPO_ROOT}/runtime/include" \
    "${HEADER_SMOKE_C}" \
    -o "${REPO_ROOT}/build-generator-smoke/include_test"

# Compile each generated .c to an object file under the same warning
# set. We don't link — the runtime objects aren't on the path here —
# but compile-only catches missing includes, wrong types, unused vars,
# etc. in the bodies the StructureEmitter wrote.
echo "smoke: compiling generated .c files (compile-only, no link)..."
for c in "${EXPECTED[@]}"; do
    case "${c}" in
        *.c) ;;
        *) continue ;;
    esac
    obj="${REPO_ROOT}/build-generator-smoke/$(basename "${c}").o"
    gcc -std=c99 -Wall -Wextra -Wpedantic -Werror \
        -I"${OUTPUT_DIR}" \
        -I"${REPO_ROOT}/runtime/include" \
        -c "${c}" \
        -o "${obj}"
done

echo "smoke: zserio-c extension loaded and ran cleanly; generated headers + sources compile."
