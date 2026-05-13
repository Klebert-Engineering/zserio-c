#!/usr/bin/env bash
#
# REQ-PF-001 benchmark harness.
#
# For each workload in tests/benchmarks/workloads/<name>/:
#   1. Generate C + cpp code from <name>.zs.
#   2. Compile a c_driver against our runtime and a cpp_driver against
#      upstream's runtime, both at -O2.
#   3. Run each driver `RUNS` times (default 5). Each run does N inner
#      iterations of encode + decode and prints one line of timing.
#      Take the median of the per-run encode_ns_per_op and
#      decode_ns_per_op.
#   4. Emit the aggregated results as JSON to stdout.
#
# Verifies: REQ-PF-001
#
# The harness intentionally reuses the cpp-runtime build path from
# tests/xvalidation/run_xvalidation.sh; if that script has already
# populated build-xvalidation/, we share the libZserioCppRuntime.a and
# the runtime-objs/ directory rather than rebuilding.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
WORKLOADS_DIR="${REPO_ROOT}/tests/benchmarks/workloads"
WORK_ROOT="${REPO_ROOT}/build-benchmarks"

RUNS="${RUNS:-5}"
INNER_ITERS="${INNER_ITERS:-200000}"

ZSERIO_JAR="${REPO_ROOT}/build-jars/zserio.jar"
ZSERIO_CORE_JAR="${REPO_ROOT}/build-jars/zserio_core.jar"
ZSERIO_C_JAR="${REPO_ROOT}/generator/build/jar/zserio_c.jar"

if [[ ! -f "${ZSERIO_JAR}" ]]; then
    echo "perf: upstream zserio.jar not found at ${ZSERIO_JAR}" >&2
    exit 1
fi
if [[ ! -f "${ZSERIO_C_JAR}" ]]; then
    "${REPO_ROOT}/tools/build_generator_jar.sh" >/dev/null
fi

mkdir -p "${WORK_ROOT}"

# --- Pre-build the C runtime (release-mode, -O2) -------------------------
# Reuses build-benchmarks/runtime-objs/ across runs. We do NOT share with
# the xvalidation pre-built objs because xvalidation builds at default
# (debug-ish) flags; benchmarks must compile -O2 to be representative.

C_RT_OBJS_DIR="${WORK_ROOT}/runtime-objs"
mkdir -p "${C_RT_OBJS_DIR}"
for c in "${REPO_ROOT}/runtime/src"/*.c; do
    obj="${C_RT_OBJS_DIR}/$(basename "${c}" .c).o"
    if [[ ! -f "${obj}" ]] || [[ "${c}" -nt "${obj}" ]]; then
        gcc -std=c99 -O2 -DNDEBUG -Wall -Wextra -Wpedantic \
            -I"${REPO_ROOT}/runtime/include" \
            -c "${c}" -o "${obj}"
    fi
done

# --- Pre-build upstream cpp runtime (release-mode, -O2) -----------------

CPP_RT_SRC="${REPO_ROOT}/third_party/zserio/compiler/extensions/cpp/runtime/src"
CPP_RT_LIB="${WORK_ROOT}/libZserioCppRuntime.a"
if [[ ! -f "${CPP_RT_LIB}" ]]; then
    mkdir -p "${WORK_ROOT}/cpp_rt_objs"
    while IFS= read -r src; do
        obj="${WORK_ROOT}/cpp_rt_objs/$(echo "${src#${CPP_RT_SRC}/}" | tr / _).o"
        g++ -std=c++11 -O2 -DNDEBUG -Wall -Wextra -Wpedantic \
            -I"${CPP_RT_SRC}" \
            -c "${src}" -o "${obj}"
    done < <(find "${CPP_RT_SRC}" -name '*.cpp')
    ar rcs "${CPP_RT_LIB}" "${WORK_ROOT}/cpp_rt_objs"/*.o
fi

# --- Median of N numeric samples on stdin -------------------------------

median_of() {
    # Reads numeric samples (one per line), prints the median to stdout.
    sort -n | awk '{a[NR]=$1} END {
        if (NR == 0) { print "0"; exit }
        if (NR % 2 == 1) { printf "%.2f\n", a[(NR+1)/2] }
        else             { printf "%.2f\n", (a[NR/2] + a[NR/2+1]) / 2 }
    }'
}

# --- Per-workload run ---------------------------------------------------

results_json='{"workloads":{'
first_workload=1

for workload_dir in "${WORKLOADS_DIR}"/*/; do
    name="$(basename "${workload_dir}")"
    schema="${workload_dir}/${name}.zs"
    work="${WORK_ROOT}/${name}"
    rm -rf "${work}" && mkdir -p "${work}/c_out" "${work}/cpp_out" "${work}/staging"

    # The zserio compiler wants the schema laid out per its package
    # declaration. Mirror what run_xvalidation.sh does, simplified for
    # benchmarks (no sibling imports).
    pkg="$(awk '/^[[:space:]]*package[[:space:]]+/ {sub(/^[[:space:]]*package[[:space:]]+/,""); sub(/;.*/,""); print; exit}' "${schema}")"
    pkg_prefix_dots="${pkg%.*}"
    pkg_last="${pkg##*.}"
    if [[ "${pkg_prefix_dots}" == "${pkg}" ]]; then
        pkg_dir=""
    else
        pkg_dir="$(echo "${pkg_prefix_dots}" | tr . /)"
    fi
    mkdir -p "${work}/staging/${pkg_dir}"
    cp "${schema}" "${work}/staging/${pkg_dir}/${pkg_last}.zs"
    rel_schema="${pkg_dir:+${pkg_dir}/}${pkg_last}.zs"

    java -cp "${ZSERIO_CORE_JAR}:${ZSERIO_C_JAR}:${ZSERIO_JAR}" \
        zserio.tools.ZserioTool \
        -src "${work}/staging" "${rel_schema}" \
        -c "${work}/c_out" -cpp "${work}/cpp_out" \
        1>/dev/null 2>"${work}/gen.log" || {
            echo "perf: ${name}: codegen failed (see ${work}/gen.log)" >&2
            exit 1
        }

    # Compile + link c_driver
    c_objs=()
    while IFS= read -r c; do
        obj="${work}/$(echo "${c#${work}/c_out/}" | tr / _).o"
        gcc -std=c99 -O2 -DNDEBUG -Wall -Wextra -Wpedantic \
            -I"${REPO_ROOT}/runtime/include" \
            -I"${work}/c_out" \
            -c "${c}" -o "${obj}"
        c_objs+=("${obj}")
    done < <(find "${work}/c_out" -name '*.c')
    gcc -O2 -DNDEBUG -std=c99 \
        -I"${REPO_ROOT}/runtime/include" -I"${work}/c_out" \
        "${workload_dir}/c_driver.c" "${c_objs[@]}" "${C_RT_OBJS_DIR}"/*.o \
        -o "${work}/c_driver"

    # Compile + link cpp_driver
    cpp_objs=()
    while IFS= read -r cpp; do
        obj="${work}/$(echo "${cpp#${work}/cpp_out/}" | tr / _).o"
        g++ -std=c++11 -O2 -DNDEBUG -Wall -Wextra -Wpedantic \
            -I"${CPP_RT_SRC}" \
            -I"${work}/cpp_out" \
            -c "${cpp}" -o "${obj}"
        cpp_objs+=("${obj}")
    done < <(find "${work}/cpp_out" -name '*.cpp')
    g++ -O2 -DNDEBUG -std=c++11 \
        -I"${CPP_RT_SRC}" -I"${work}/cpp_out" \
        "${workload_dir}/cpp_driver.cpp" "${cpp_objs[@]}" "${CPP_RT_LIB}" \
        -o "${work}/cpp_driver"

    # Run RUNS samples; collect encode + decode ns per op.
    c_encode_samples=""; c_decode_samples=""
    cpp_encode_samples=""; cpp_decode_samples=""
    bytes_observed="0"
    for run in $(seq 1 "${RUNS}"); do
        line="$("${work}/c_driver" "${INNER_ITERS}")"
        c_enc=$(echo "${line}" | awk -F'[= ]' '{for(i=1;i<=NF;i++) if ($i=="encode_ns_per_op") print $(i+1)}')
        c_dec=$(echo "${line}" | awk -F'[= ]' '{for(i=1;i<=NF;i++) if ($i=="decode_ns_per_op") print $(i+1)}')
        b=$(echo "${line}" | awk -F'[= ]' '{for(i=1;i<=NF;i++) if ($i=="bytes") print $(i+1)}')
        c_encode_samples="${c_encode_samples}${c_enc}\n"
        c_decode_samples="${c_decode_samples}${c_dec}\n"
        bytes_observed="${b}"

        line="$("${work}/cpp_driver" "${INNER_ITERS}")"
        cpp_enc=$(echo "${line}" | awk -F'[= ]' '{for(i=1;i<=NF;i++) if ($i=="encode_ns_per_op") print $(i+1)}')
        cpp_dec=$(echo "${line}" | awk -F'[= ]' '{for(i=1;i<=NF;i++) if ($i=="decode_ns_per_op") print $(i+1)}')
        cpp_encode_samples="${cpp_encode_samples}${cpp_enc}\n"
        cpp_decode_samples="${cpp_decode_samples}${cpp_dec}\n"
    done

    c_enc_med="$(printf "${c_encode_samples}" | median_of)"
    c_dec_med="$(printf "${c_decode_samples}" | median_of)"
    cpp_enc_med="$(printf "${cpp_encode_samples}" | median_of)"
    cpp_dec_med="$(printf "${cpp_decode_samples}" | median_of)"

    # ratio = cpp_ns / our_ns. > 1.0 means we are faster.
    enc_ratio="$(awk -v a="${cpp_enc_med}" -v b="${c_enc_med}" 'BEGIN { if (b > 0) printf "%.3f\n", a/b; else print "0" }')"
    dec_ratio="$(awk -v a="${cpp_dec_med}" -v b="${c_dec_med}" 'BEGIN { if (b > 0) printf "%.3f\n", a/b; else print "0" }')"

    if [[ "${first_workload}" -eq 0 ]]; then results_json="${results_json},"; fi
    first_workload=0
    results_json="${results_json}\"${name}\":{"
    results_json="${results_json}\"bytes\":${bytes_observed},"
    results_json="${results_json}\"inner_iters\":${INNER_ITERS},"
    results_json="${results_json}\"runs\":${RUNS},"
    results_json="${results_json}\"zserio_c\":{\"encode_ns_per_op\":${c_enc_med},\"decode_ns_per_op\":${c_dec_med}},"
    results_json="${results_json}\"zserio_cpp\":{\"encode_ns_per_op\":${cpp_enc_med},\"decode_ns_per_op\":${cpp_dec_med}},"
    results_json="${results_json}\"ratio_cpp_to_c\":{\"encode\":${enc_ratio},\"decode\":${dec_ratio}}"
    results_json="${results_json}}"

    echo "perf: ${name}: c_enc=${c_enc_med} c_dec=${c_dec_med} cpp_enc=${cpp_enc_med} cpp_dec=${cpp_dec_med} bytes=${bytes_observed}" >&2
done

results_json="${results_json}}}"
echo "${results_json}"
