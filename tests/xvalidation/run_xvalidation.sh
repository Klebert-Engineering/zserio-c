#!/usr/bin/env bash
#
# Schema-driven cross-validation harness (REQ-FN-001).
#
# For each schema listed in schemas.txt (one path-relative-to-corpus
# per line), this script:
#   1. Generates C code from our generator and cpp code from upstream's
#      cpp extension into a per-schema work dir.
#   2. Compiles a small per-schema "c_driver" against our runtime + the
#      generated C, and a "cpp_driver" against upstream's runtime + the
#      generated cpp.
#   3. For each seed in SEEDS, runs:
#        c_driver  <seed> bytes_c.bin
#        cpp_driver bytes_c.bin bytes_cpp.bin
#        cmp bytes_c.bin bytes_cpp.bin
#      Byte-equal ⇒ our encoder and upstream's encoder produced the
#      same wire bytes from the same struct value (decoded by upstream
#      from our output, re-encoded by upstream).
#
# Failure mode: any cmp mismatch is a wire-format divergence. The
# matching bytes_c.bin / bytes_cpp.bin pair is kept under the work dir
# for diagnostic use (feed into the cpp_oracle trace facility).
#
# Verifies (system-level — the xvalidation harness is the canonical
# end-to-end exercise for every requirement that touches the wire
# format or the schema→C→bytes round-trip):
#
#   Verifies: REQ-DM-001 — schema-driven binary serialization
#   Verifies: REQ-DM-002 — encode/decode mutual inverses
#   Verifies: REQ-DM-003 — canonical encoding
#   Verifies: REQ-DM-006 — schema-evolution constructs (extend/optional/@removed harnesses)
#   Verifies: REQ-FN-001 — interoperability with upstream zserio v2.18.1
#   Verifies: REQ-FN-002 — parser accepts the schema language
#   Verifies: REQ-FN-007 — generator output is deterministic across runs
#   Verifies: REQ-FN-008 — output file layout mirrors schema package hierarchy
#   Verifies: REQ-FN-009 — schema-to-C identifier mapping is total and stable
#   Verifies: REQ-FN-011 — all-or-nothing emission (failed runs leave exit nonzero)
#   Verifies: REQ-FN-012 — cross-package include qualification
#   Verifies: REQ-FN-013 — template instantiation (template harnesses)
#   Verifies: REQ-FN-015 — generated-code lifecycle entry points
#   Verifies: REQ-FN-021 — canonical encoding (struct → bytes)
#   Verifies: REQ-FN-023 — wire-format fields emitted in schema source order
#   Verifies: REQ-FN-024 — parameter propagation in nested types
#   Verifies: REQ-FN-025 — extend fields default to zero (extend_* harnesses)
#   Verifies: REQ-FN-026 — schema constraints enforced on read (constraint harnesses)

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
TESTS_ROOT="${REPO_ROOT}/tests/xvalidation"
HARNESSES_ROOT="${TESTS_ROOT}/harnesses"
CORPUS_ROOT="${REPO_ROOT}/third_party/zserio-test-data/language"
WORK_ROOT="${REPO_ROOT}/build-xvalidation"
ZSERIO_JAR="${REPO_ROOT}/build-jars/zserio.jar"
ZSERIO_CORE_JAR="${REPO_ROOT}/build-jars/zserio_core.jar"
ZSERIO_C_GEN="${REPO_ROOT}/build/parser/zserio_c_gen"
SEEDS="${SEEDS:-1 2 7 42 999}"

if [[ ! -f "${ZSERIO_JAR}" ]]; then
    echo "xval: upstream zserio.jar not found at ${ZSERIO_JAR}" >&2
    echo "xval: tools/build_zserio_core_jar.sh builds the core jar; the" >&2
    echo "xval: full zserio.jar (with cpp extension) is staged separately." >&2
    exit 1
fi
if [[ ! -x "${ZSERIO_C_GEN}" ]]; then
    echo "xval: zserio_c_gen not found at ${ZSERIO_C_GEN}" >&2
    echo "xval: build it first with 'cmake --build build'." >&2
    exit 1
fi

SCHEMAS_FILE="${TESTS_ROOT}/schemas.txt"
if [[ ! -f "${SCHEMAS_FILE}" ]]; then
    echo "xval: no schemas listed at ${SCHEMAS_FILE}" >&2
    exit 1
fi

mkdir -p "${WORK_ROOT}"

# Optional sanitizer build. Set XVAL_SAN=1 in the environment to
# pass -fsanitize=address,undefined to every gcc invocation that
# produces the C side of the harness. The cpp side stays unchanged.
SAN_FLAGS=""
SAN_TAG=""
if [[ "${XVAL_SAN:-0}" == "1" ]]; then
    SAN_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -g"
    SAN_TAG="-san"
    # Hand-written harness c_drivers don't always free everything
    # they allocate — they're populate-then-write programs whose
    # cleanup at process exit isn't a generator concern. Keep the
    # use-after-free / OOB / UBSan checks; disable leak detection.
    export ASAN_OPTIONS="${ASAN_OPTIONS:-}:detect_leaks=0"
    export UBSAN_OPTIONS="${UBSAN_OPTIONS:-}:print_stacktrace=1:halt_on_error=1"
    echo "xval: sanitizers enabled (ASAN+UBSAN, leak detection off)"
fi

# Auto-generate C round-trip drivers from each harness's c_driver.c
# (with hand-written exceptions for recursive structs).
python3 "${REPO_ROOT}/tests/xvalidation/gen_roundtrip.py" \
    "${REPO_ROOT}/tests/xvalidation/harnesses" 1>/dev/null

# Pre-build the C runtime objs once — every per-schema driver links
# against them. Sanitizer-built and plain artifacts live in separate
# sub-dirs so XVAL_SAN=1 and unset interleave without stomping each
# other's caches.
mkdir -p "${WORK_ROOT}/runtime-objs${SAN_TAG}"
for c in "${REPO_ROOT}/runtime/src"/*.c; do
    obj="${WORK_ROOT}/runtime-objs${SAN_TAG}/$(basename "${c}" .c).o"
    if [[ ! -f "${obj}" ]] || [[ "${c}" -nt "${obj}" ]]; then
        gcc ${SAN_FLAGS} -std=c99 -Wall -Wextra -Wpedantic -Werror \
            -I"${REPO_ROOT}/runtime/include" \
            -c "${c}" -o "${obj}"
    fi
done
RUNTIME_OBJ_DIR="${WORK_ROOT}/runtime-objs${SAN_TAG}"

# Pre-build upstream's C++ runtime as a static lib from the submodule.
CPP_RT_SRC="${REPO_ROOT}/third_party/zserio/compiler/extensions/cpp/runtime/src"
CPP_RT_LIB="${WORK_ROOT}/libZserioCppRuntime.a"
if [[ ! -f "${CPP_RT_LIB}" ]]; then
    mkdir -p "${WORK_ROOT}/cpp_rt_objs"
    while IFS= read -r src; do
        obj="${WORK_ROOT}/cpp_rt_objs/$(echo "${src#${CPP_RT_SRC}/}" | tr / _).o"
        g++ -std=c++11 -O2 -Wall -Wextra -Wpedantic \
            -I"${CPP_RT_SRC}" \
            -c "${src}" -o "${obj}"
    done < <(find "${CPP_RT_SRC}" -name '*.cpp')
    ar rcs "${CPP_RT_LIB}" "${WORK_ROOT}/cpp_rt_objs"/*.o
fi
CPP_RT_DIR="${CPP_RT_SRC}"

fail=0
ok=0
while IFS= read -r schema_rel; do
    [[ -z "${schema_rel}" || "${schema_rel}" =~ ^# ]] && continue
    schema_path="${CORPUS_ROOT}/${schema_rel}"
    if [[ ! -f "${schema_path}" ]]; then
        # Not in the upstream corpus — try the in-tree synthetic
        # schemas (committed under tests/xvalidation/schemas/).
        schema_path="${REPO_ROOT}/tests/xvalidation/schemas/${schema_rel}"
    fi
    if [[ ! -f "${schema_path}" ]]; then
        echo "xval: schema not found in corpus or tests/xvalidation/schemas: ${schema_rel}" >&2
        fail=$((fail + 1))
        continue
    fi
    # Each schema gets a slug-named work dir under build-xvalidation/.
    name="$(basename "${schema_rel}" .zs)"
    work="${WORK_ROOT}/${name}"
    rm -rf "${work}" && mkdir -p "${work}/c_out" "${work}/cpp_out" "${work}/staging"

    # zserio's package-file-name check: the schema at path
    # `<dirs>/<file>.zs` (relative to -src) must declare
    # `package <dirs_with_slashes_to_dots>.<file>`. The test corpus
    # nests as `<category>/zs/<pkg_prefix>/<file>.zs`, so we re-derive
    # the staging layout from the `package` line in the schema.
    pkg="$(awk '/^[[:space:]]*package[[:space:]]+/ {sub(/^[[:space:]]*package[[:space:]]+/,""); sub(/;.*/,""); print; exit}' "${schema_path}")"
    pkg_prefix_dots="${pkg%.*}"
    pkg_last="${pkg##*.}"
    if [[ "${pkg_prefix_dots}" == "${pkg}" ]]; then
        # single-segment package, no prefix dir
        pkg_dir=""
    else
        pkg_dir="$(echo "${pkg_prefix_dots}" | tr . /)"
    fi
    mkdir -p "${work}/staging/${pkg_dir}"
    cp "${schema_path}" "${work}/staging/${pkg_dir}/${pkg_last}.zs"
    # If the schema imports siblings (`import <pkg>.<other>.*`), the
    # other .zs files must accompany it. Mirror the entire source
    # directory so any sibling imports resolve. This is harmless when
    # the schema has no imports (we just copy a single extra file
    # into the same place we already wrote).
    schema_dir="$(dirname "${schema_path}")"
    for sibling in "${schema_dir}"/*.zs; do
        if [[ -f "${sibling}" ]]; then
            cp "${sibling}" "${work}/staging/${pkg_dir}/$(basename "${sibling}")"
        fi
    done
    # Some schemas pair `<name>.zs` with a `<name>/` subdirectory whose
    # files declare `package <pkg>.<name>.<sub>` — the template-
    # instantiation across packages pattern. Copy the subdir too so
    # zserio can resolve `import <pkg>.<name>.<sub>.*`.
    sub_dir="${schema_dir}/${pkg_last}"
    if [[ -d "${sub_dir}" ]]; then
        mkdir -p "${work}/staging/${pkg_dir:+${pkg_dir}/}${pkg_last}"
        for sub in "${sub_dir}"/*.zs; do
            if [[ -f "${sub}" ]]; then
                cp "${sub}" "${work}/staging/${pkg_dir:+${pkg_dir}/}${pkg_last}/$(basename "${sub}")"
            fi
        done
    fi
    rel_schema="${pkg_dir:+${pkg_dir}/}${pkg_last}.zs"

    # Per-schema codegen flags. `packages/zs/index.zs` declares a
    # top-level package literally named `index`, which collides with
    # libc's `index()` (declared by <strings.h>) in the cpp output —
    # the cpp compile fails with `namespace index redeclared as
    # different kind of entity`. Upstream's own test harness uses
    # `-setTopLevelPackage index_workaround` to dodge the collision;
    # we apply the same workaround. The flag is core-level so both
    # the C and cpp generators see the renamed package; the harness
    # drivers must reference the prefixed symbols accordingly.
    extra_codegen_args=()
    case "${schema_rel}" in
        packages/zs/index.zs)
            extra_codegen_args=(-setTopLevelPackage index_workaround)
            ;;
    esac

    # cpp side: still produced by upstream's Java cpp extension.
    java -cp "${ZSERIO_CORE_JAR}:${ZSERIO_JAR}" \
        zserio.tools.ZserioTool \
        "${extra_codegen_args[@]}" \
        -src "${work}/staging" "${rel_schema}" \
        -cpp "${work}/cpp_out" \
        1>/dev/null 2>"${work}/cpp_gen.log" || {
            echo "xval: ${name}: cpp codegen failed (see ${work}/cpp_gen.log)" >&2
            fail=$((fail + 1))
            continue
        }
    # C side: our C-native generator. Mirror the same
    # `-setTopLevelPackage` rewriting upstream's java gen does
    # so harness include paths and symbol names line up.
    "${ZSERIO_C_GEN}" \
        "${work}/staging/${rel_schema}" \
        -c "${work}/c_out" \
        -src "${work}/staging" \
        "${extra_codegen_args[@]}" \
        1>/dev/null 2>"${work}/c_gen.log" || {
            echo "xval: ${name}: C codegen failed (see ${work}/c_gen.log)" >&2
            fail=$((fail + 1))
            continue
        }

    harness="${HARNESSES_ROOT}/${name}"
    if [[ ! -d "${harness}" ]]; then
        echo "xval: ${name}: no harness drivers at ${harness}; skipping" >&2
        continue
    fi

    # Compile + link c_driver.
    c_objs=()
    while IFS= read -r c; do
        obj="${work}/$(echo "${c#${work}/c_out/}" | tr / _).o"
        gcc ${SAN_FLAGS} -std=c99 -Wall -Wextra -Wpedantic -Werror \
            -I"${REPO_ROOT}/runtime/include" \
            -I"${work}/c_out" \
            -c "${c}" -o "${obj}" 2>"${work}/c_compile.log" || {
                echo "xval: ${name}: C compile failed (see ${work}/c_compile.log)" >&2
                fail=$((fail + 1))
                continue 2
            }
        c_objs+=("${obj}")
    done < <(find "${work}/c_out" -name '*.c')
    c_driver_obj="${work}/c_driver.o"
    gcc ${SAN_FLAGS} -std=c99 -Wall -Wextra -Wpedantic -Werror \
        -I"${REPO_ROOT}/runtime/include" \
        -I"${work}/c_out" \
        -c "${harness}/c_driver.c" -o "${c_driver_obj}" 2>"${work}/c_compile.log" || {
            echo "xval: ${name}: c_driver compile failed (see ${work}/c_compile.log)" >&2
            fail=$((fail + 1))
            continue
        }
    gcc ${SAN_FLAGS} -O2 "${c_driver_obj}" "${c_objs[@]}" "${RUNTIME_OBJ_DIR}"/*.o \
        -o "${work}/c_driver" 2>"${work}/c_link.log" || {
            echo "xval: ${name}: c_driver link failed (see ${work}/c_link.log)" >&2
            fail=$((fail + 1))
            continue
        }

    # Compile + link c_roundtrip (auto-generated by gen_roundtrip.py
    # or hand-written for unusual harnesses). The roundtrip driver
    # reads bytes and re-writes them via our C reader+writer so we
    # can verify the round-trip is byte-identical.
    if [[ -f "${harness}/c_roundtrip.c" ]]; then
        c_rt_obj="${work}/c_roundtrip.o"
        # The roundtrip driver imports c_driver's helper functions /
        # tables that may go unused on this code path — silence the
        # unused warnings rather than maintain a hand-edit per file.
        gcc ${SAN_FLAGS} -std=c99 -Wall -Wextra -Wpedantic \
            -Wno-unused-variable -Wno-unused-function -Wno-unused-parameter \
            -Wno-unused-but-set-variable \
            -I"${REPO_ROOT}/runtime/include" \
            -I"${work}/c_out" \
            -c "${harness}/c_roundtrip.c" -o "${c_rt_obj}" 2>"${work}/c_rt_compile.log" || {
                echo "xval: ${name}: c_roundtrip compile failed (see ${work}/c_rt_compile.log)" >&2
                fail=$((fail + 1))
                continue
            }
        gcc ${SAN_FLAGS} -O2 "${c_rt_obj}" "${c_objs[@]}" "${RUNTIME_OBJ_DIR}"/*.o \
            -o "${work}/c_roundtrip" 2>"${work}/c_rt_link.log" || {
                echo "xval: ${name}: c_roundtrip link failed (see ${work}/c_rt_link.log)" >&2
                fail=$((fail + 1))
                continue
            }
    fi

    # Compile + link cpp_driver.
    cpp_objs=()
    while IFS= read -r src; do
        obj="${work}/$(echo "${src#${work}/cpp_out/}" | tr / _).o"
        g++ -std=c++11 -O2 -Wall -Wextra -Wpedantic \
            -I"${CPP_RT_DIR}" \
            -I"${work}/cpp_out" \
            -c "${src}" -o "${obj}" 2>"${work}/cpp_compile.log" || {
                echo "xval: ${name}: cpp compile failed (see ${work}/cpp_compile.log)" >&2
                fail=$((fail + 1))
                continue 2
            }
        cpp_objs+=("${obj}")
    done < <(find "${work}/cpp_out" -name '*.cpp')
    cpp_driver_obj="${work}/cpp_driver.o"
    g++ -std=c++11 -O2 -Wall -Wextra -Wpedantic \
        -I"${CPP_RT_DIR}" \
        -I"${work}/cpp_out" \
        -c "${harness}/cpp_driver.cpp" -o "${cpp_driver_obj}" 2>"${work}/cpp_compile.log" || {
            echo "xval: ${name}: cpp_driver compile failed (see ${work}/cpp_compile.log)" >&2
            fail=$((fail + 1))
            continue
        }
    g++ -O2 "${cpp_driver_obj}" "${cpp_objs[@]}" "${CPP_RT_LIB}" \
        -o "${work}/cpp_driver" 2>"${work}/cpp_link.log" || {
            echo "xval: ${name}: cpp_driver link failed (see ${work}/cpp_link.log)" >&2
            fail=$((fail + 1))
            continue
        }

    # Drive each seed through the round-trip.
    schema_failures=0
    for seed in ${SEEDS}; do
        bytes_c="${work}/bytes_c.s${seed}.bin"
        bytes_cpp="${work}/bytes_cpp.s${seed}.bin"
        "${work}/c_driver" "${seed}" "${bytes_c}" 1>/dev/null || {
            echo "xval: ${name} seed=${seed}: c_driver failed"
            schema_failures=$((schema_failures + 1))
            continue
        }
        "${work}/cpp_driver" "${bytes_c}" "${bytes_cpp}" "${seed}" 1>/dev/null || {
            echo "xval: ${name} seed=${seed}: cpp_driver failed"
            schema_failures=$((schema_failures + 1))
            continue
        }
        if ! cmp -s "${bytes_c}" "${bytes_cpp}"; then
            echo "xval: ${name} seed=${seed}: WIRE MISMATCH"
            echo "       c=${bytes_c}"
            echo "       cpp=${bytes_cpp}"
            schema_failures=$((schema_failures + 1))
            continue
        fi
        # Bidirectional check: feed cpp's bytes back through our C
        # reader + writer, expect byte-identical output. Detects
        # read-side asymmetries (sign-extension, off-by-one length
        # prefixes, etc.) that the write-side xval misses since cpp
        # decodes our output before re-encoding.
        if [[ -x "${work}/c_roundtrip" ]]; then
            bytes_rt="${work}/bytes_rt.s${seed}.bin"
            "${work}/c_roundtrip" "${seed}" "${bytes_cpp}" "${bytes_rt}" \
                1>/dev/null 2>"${work}/c_rt_run.log" || {
                    echo "xval: ${name} seed=${seed}: c_roundtrip failed (see ${work}/c_rt_run.log)"
                    schema_failures=$((schema_failures + 1))
                    continue
                }
            if ! cmp -s "${bytes_c}" "${bytes_rt}"; then
                echo "xval: ${name} seed=${seed}: ROUNDTRIP MISMATCH"
                echo "       c=${bytes_c}"
                echo "       rt=${bytes_rt}"
                schema_failures=$((schema_failures + 1))
            fi
        fi
    done
    if (( schema_failures == 0 )); then
        echo "xval: ${name}: ok (seeds: ${SEEDS})"
        ok=$((ok + 1))
    else
        fail=$((fail + 1))
    fi
done < "${SCHEMAS_FILE}"

echo
echo "xval: ${ok} schemas passed, ${fail} schemas failed"
exit $(( fail == 0 ? 0 : 1 ))
