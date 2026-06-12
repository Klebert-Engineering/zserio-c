#!/usr/bin/env bash
#
# Decode-path fuzzer (libFuzzer + ASan + UBSan). For each target
# in tests/fuzz/targets.txt:
#
#   1. Code-generate the target schema into build-fuzz/<name>/c_out.
#   2. Compile runtime + generated code + the driver with clang
#      `-fsanitize=fuzzer,address,undefined -O1 -g`.
#   3. Run libFuzzer for FUZZ_SECONDS (default 60) against the
#      persistent corpus at tests/fuzz/corpus/<name>/. New inputs
#      libFuzzer discovers extend the corpus across runs.
#   4. Fail (exit 1) if libFuzzer reports a crash / leak / UBSan trip.
#
# Closes the safety-plan open item "Random-input fuzzing of the decode
# path" recorded in safety-case-report.md and
# integration-verification.md.
#
# Verifies: REQ-FS-006
#
# Usage:
#   tools/run_fuzz.sh                       # 60 s per target, default
#   FUZZ_SECONDS=300 tools/run_fuzz.sh      # 5 min per target (local)
#   FUZZ_RUNS=10000 tools/run_fuzz.sh       # cap iterations instead of time
#
# Prerequisites: clang with libFuzzer, the upstream zserio jars
# (build-jars/{zserio,zserio_core}.jar) and zserio_c.jar
# (generator/build/jar/zserio_c.jar). The CI job builds the jars
# in earlier steps; locally, run tools/build_zserio_bundle_jar.sh
# + tools/build_generator_jar.sh first.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WORK="${REPO_ROOT}/build-fuzz"
TARGETS_FILE="${REPO_ROOT}/tests/fuzz/targets.txt"
CORPUS_BASE="${REPO_ROOT}/tests/fuzz/corpus"
FUZZ_SECONDS="${FUZZ_SECONDS:-60}"
FUZZ_RUNS="${FUZZ_RUNS:-}"
ZSERIO_JAR="${REPO_ROOT}/build-jars/zserio.jar"
ZSERIO_CORE_JAR="${REPO_ROOT}/build-jars/zserio_core.jar"
ZSERIO_C_JAR="${REPO_ROOT}/generator/build/jar/zserio_c.jar"

if ! command -v clang >/dev/null 2>&1; then
    echo "clang not installed" >&2
    exit 2
fi
for j in "${ZSERIO_JAR}" "${ZSERIO_CORE_JAR}" "${ZSERIO_C_JAR}"; do
    if [[ ! -f "${j}" ]]; then
        echo "missing jar: ${j}" >&2
        echo "build the jars first (tools/build_zserio_bundle_jar.sh + " >&2
        echo "tools/build_generator_jar.sh)" >&2
        exit 2
    fi
done

mkdir -p "${WORK}" "${CORPUS_BASE}"

CFLAGS=(-std=c99 -O1 -g
        -fsanitize=fuzzer-no-link,address,undefined
        -fno-omit-frame-pointer
        -DNDEBUG -Wall -Wextra -Wpedantic
        -I"${REPO_ROOT}/runtime/include")
LFLAGS=(-fsanitize=fuzzer,address,undefined)

OVERALL_RC=0

# Read each non-empty / non-comment line from targets.txt
while IFS=$' \t' read -r name src_root rel_schema driver _rest; do
    case "${name}" in '#'*|'') continue;; esac

    work="${WORK}/${name}"
    out="${work}/c_out"
    rm -rf "${work}" && mkdir -p "${out}"

    # ----- codegen -----
    if ! java -cp "${ZSERIO_CORE_JAR}:${ZSERIO_C_JAR}:${ZSERIO_JAR}" \
            zserio.tools.ZserioTool \
            -src "${REPO_ROOT}/${src_root}" "${rel_schema}" \
            -c "${out}" > "${work}/gen.log" 2>&1
    then
        echo "codegen failed: ${name}" >&2
        cat "${work}/gen.log" >&2
        exit 1
    fi

    # ----- compile runtime + generated + driver -----
    objs=()
    for c in "${REPO_ROOT}"/runtime/src/*.c; do
        obj="${work}/rt_$(basename "${c}" .c).o"
        clang "${CFLAGS[@]}" -c "${c}" -o "${obj}"
        objs+=("${obj}")
    done
    i=0
    while IFS= read -r c; do
        obj="${work}/g_${i}.o"
        clang "${CFLAGS[@]}" -I"${out}" -c "${c}" -o "${obj}"
        objs+=("${obj}")
        i=$((i+1))
    done < <(find "${out}" -name '*.c')

    drv_obj="${work}/driver.o"
    clang "${CFLAGS[@]}" -I"${out}" -c "${REPO_ROOT}/${driver}" -o "${drv_obj}"
    objs+=("${drv_obj}")

    clang "${LFLAGS[@]}" "${objs[@]}" -o "${work}/fuzzer"

    # ----- run libFuzzer -----
    corpus="${CORPUS_BASE}/${name}"
    mkdir -p "${corpus}"

    run_args=(
        -print_final_stats=1
        # Cap per-input runtime at 10 s — long enough to absorb
        # legitimate deep nesting; short enough that an infinite-
        # loop bug surfaces as a timeout finding.
        -timeout=10
        "${corpus}"
    )
    if [[ -n "${FUZZ_RUNS}" ]]; then
        run_args=(-runs="${FUZZ_RUNS}" "${run_args[@]}")
    else
        run_args=(-max_total_time="${FUZZ_SECONDS}" "${run_args[@]}")
    fi

    echo "=== fuzzing ${name} ==="
    set +e
    "${work}/fuzzer" "${run_args[@]}" 2>&1 | tee "${work}/fuzz.log"
    rc=${PIPESTATUS[0]}
    set -e
    if [[ ${rc} -ne 0 ]]; then
        echo "FAIL: fuzz target '${name}' reported a finding (exit=${rc})"
        OVERALL_RC=1
    fi
done < "${TARGETS_FILE}"

if [[ ${OVERALL_RC} -ne 0 ]]; then
    exit 1
fi
echo "OK"
