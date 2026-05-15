#!/usr/bin/env bash
#
# Run cppcheck with the MISRA C:2012 addon over the generator's
# output for a small, representative schema set.
#
# Closes the Phase 5 Open item recorded in
# docs/safety-case/tool-qualification-generator.md § "Known limitations":
#
#     Gap to close: add a CI step that runs cppcheck against the
#     xvalidation work-dir c_out for a small representative schema set.
#
# Verifies: REQ-FS-001
# Verifies: REQ-FS-002
#
# Usage:
#   tools/run_misra_generated.sh             # report findings
#   tools/run_misra_generated.sh --strict    # fail on any cppcheck error
#
# Assumes the generator + upstream zserio jars are already built
# (build-jars/zserio.jar, build-jars/zserio_core.jar,
#  generator/build/jar/zserio_c.jar). The companion CI job builds
# them in earlier steps and caches them across runs.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WORK="${REPO_ROOT}/build-misra-generated"
SUPPRESSIONS="${REPO_ROOT}/tools/cppcheck-suppressions.txt"
ZSERIO_JAR="${REPO_ROOT}/build-jars/zserio.jar"
ZSERIO_CORE_JAR="${REPO_ROOT}/build-jars/zserio_core.jar"
ZSERIO_C_JAR="${REPO_ROOT}/generator/build/jar/zserio_c.jar"
CORPUS_BASE="${REPO_ROOT}/third_party/zserio-test-data/language"

STRICT=0
if [[ "${1:-}" == "--strict" ]]; then
    STRICT=1
fi

if ! command -v cppcheck >/dev/null 2>&1; then
    echo "cppcheck not installed" >&2
    exit 2
fi
if [[ ! -f "${ZSERIO_C_JAR}" ]] || [[ ! -f "${ZSERIO_JAR}" ]] || [[ ! -f "${ZSERIO_CORE_JAR}" ]]; then
    echo "zserio jars not built — run tools/build_generator_jar.sh and tools/build_zserio_bundle_jar.sh first" >&2
    exit 2
fi

CPPCHECK_VERSION="$(cppcheck --version | head -1)"
echo "Using ${CPPCHECK_VERSION}"

# --- Representative schema set ------------------------------------------
# Chosen to cover the main emission paths of the generator:
#   - every primitive width + varint family + bitfields
#   - struct, choice, union, enum, bitmask
#   - auto / packed arrays (+ packed array of struct)
#   - optional, parameterised, templates
#   - strings, floats, offsets, function-typed
# Each entry is the schema's path relative to its category's `zs/` root.
# The harness derives the source root by splitting at the first `/zs/`.

SCHEMAS=(
    builtin_types/zs/builtin_types/all_builtin_types.zs
    structure_types/zs/structure_types/simple_structure.zs
    choice_types/zs/choice_types/bool_param_choice.zs
    union_types/zs/union_types/simple_union.zs
    array_types/zs/array_types/auto_array_uint8.zs
    array_types/zs/array_types/packed_auto_array_uint8.zs
    array_types/zs/array_types/packed_auto_array_struct_with_unpacked_field.zs
    optional_members/zs/optional_members/auto_optional.zs
    expressions/zs/expressions/float_type.zs
    functions/zs/functions/structure_string.zs
    parameterized_types/zs/parameterized_types/simple_param.zs
    offsets/zs/offsets/bit_offset.zs
    templates/zs/templates/instantiate_simple.zs
)

rm -rf "${WORK}" && mkdir -p "${WORK}"

# --- 1. Generate C from each schema -------------------------------------

for schema_rel in "${SCHEMAS[@]}"; do
    # Split at the first "/zs/" to get the source root and the rel path
    # inside it. e.g. "array_types/zs/array_types/foo.zs" splits to
    # src="<corpus>/array_types/zs" and rel="array_types/foo.zs".
    src_root="${CORPUS_BASE}/${schema_rel%%/zs/*}/zs"
    rel="${schema_rel#*/zs/}"
    name="${rel%.zs}"
    name="${name//\//_}"
    out="${WORK}/${name}/c_out"
    mkdir -p "${out}"

    java -cp "${ZSERIO_CORE_JAR}:${ZSERIO_C_JAR}:${ZSERIO_JAR}" \
        zserio.tools.ZserioTool \
        -src "${src_root}" "${rel}" \
        -c "${out}" \
        1>/dev/null 2>"${WORK}/${name}/gen.log" || {
            echo "codegen failed: ${schema_rel}" >&2
            cat "${WORK}/${name}/gen.log" >&2
            exit 1
        }
done
echo "codegen: ${#SCHEMAS[@]} schemas OK"

# --- 2. cppcheck each generated tree ------------------------------------

CPPCHECK_ARGS_BASE=(
    --enable=warning,style,performance,portability,information
    --inline-suppr
    --std=c99
    --language=c
    --error-exitcode=1
    --suppressions-list="${SUPPRESSIONS}"
    --addon=misra
    # Match the runtime gate: trace facility off, builtin_expect transparent.
    -UZSERIO_TRACE_ENABLED
    '-D__builtin_expect(x,y)=(x)'
    -I"${REPO_ROOT}/runtime/include"
)

OVERALL_RC=0
N_FAIL=0
for schema_rel in "${SCHEMAS[@]}"; do
    rel="${schema_rel#*/zs/}"
    name="${rel%.zs}"; name="${name//\//_}"
    out="${WORK}/${name}/c_out"

    echo "=== ${name} ==="
    set +e
    cppcheck "${CPPCHECK_ARGS_BASE[@]}" -I"${out}" "${out}" 2>&1 | tee "${WORK}/${name}/cppcheck.log"
    rc=$?
    set -e
    if [[ ${rc} -ne 0 ]]; then
        OVERALL_RC=${rc}
        N_FAIL=$((N_FAIL + 1))
    fi
done

echo
echo "=== summary: ${N_FAIL}/${#SCHEMAS[@]} schemas with cppcheck findings ==="

if [[ "${STRICT}" -eq 1 ]] && [[ "${OVERALL_RC}" -ne 0 ]]; then
    echo "Strict mode: failing on findings."
    exit 1
fi
echo "OK"
