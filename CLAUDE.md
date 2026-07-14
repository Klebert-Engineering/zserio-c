# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this project is

`zserio-c` is a clean-room C99 implementation of the zserio schema-driven binary
serialization framework, designed for ISO 26262 ASIL-B (ASIL-D headroom). It is
wire-format compatible with upstream `ndsev/zserio` (pinned at `v2.18.1`,
`third_party/zserio/`).

Three deliverables live in this tree:

- `parser/` — `zserio_c_gen`, a C-native parser + sema + emitter.
  Replaces the old Java generator on the C target. Layout: `lexer → parser
  (recursive descent) → AST (arena-allocated) → sema → emit`. Public headers in
  `parser/include/zsparser/`.
- `runtime/` — `libzserio_c_runtime`, the C99 library generated code links
  against. Public headers in `runtime/include/zserio/` only — nothing else may
  expose public headers.
- `generator/` — legacy Java generator extension that drives the upstream
  `ZserioTool` to produce a **cpp** reference for the xvalidation oracle. It is
  **not** on the C build path; do not confuse it with `parser/`.

The xvalidation harness in `tests/xvalidation/` is the flagship correctness
gate: 183 schemas × 5 seeds, cpp↔C byte-equal in both directions on every
commit. Treat it as the source of truth for "does this still work."

## Commands

The CMake presets in `CMakePresets.json` are the canonical build configs:

```sh
cmake --preset gcc-debug         # configure (also: gcc-release, clang-debug, coverage, asan)
cmake --build --preset gcc-debug
ctest --preset gcc-debug         # ~30s; gcc + clang both strict-warning-clean
```

Single test (after build):

```sh
ctest --test-dir build/gcc-debug -R test_bitstream --output-on-failure
./build/gcc-debug/tests/runtime/test_bitstream      # direct binary also works
```

Targeted gates (see `tools/` for the full set):

```sh
python3 tools/check_traceability.py             # REQ → test reference graph
python3 tools/check_version_consistency.py      # version.h ↔ CMakeLists ↔ docs
tools/run_misra.sh --strict                     # cppcheck MISRA C:2012 on runtime
tools/run_misra_generated.sh --strict           # MISRA on generated output
tools/run_clang_tidy.sh --strict                # clang-tidy bugprone/cert/clang-analyzer on runtime
python3 tools/measure_stack_usage.py --strict   # ≤256B/frame, ≤1024B chain
python3 tools/check_coverage.py coverage.info   # line ≥99% branch ≥85% fn 100%
./tests/xvalidation/run_xvalidation.sh          # cpp↔C wire-format gate
XVAL_SAN=1 ./tests/xvalidation/run_xvalidation.sh   # same, under ASan + UBSan
SEEDS="1 2 7 42 999" bash tests/xvalidation/run_xvalidation.sh <schema_name>  # single harness
RUNS=5 INNER_ITERS=200000 bash tests/benchmarks/run_benchmark.sh    # perf
FUZZ_SECONDS=60 tools/run_fuzz.sh               # libFuzzer + ASan + UBSan
```

The xvalidation, perf, fuzz, and MISRA-on-generator gates require the upstream
Java jars built first (`tools/build_zserio_core_jar.sh`,
`tools/build_zserio_bundle_jar.sh`, `tools/build_generator_jar.sh`) — they need
`ant` + JDK 17 and the `third_party/zserio` submodule initialized.

## Architecture invariants — load-bearing

These are CI-gated, not aspirational. Do not relax any of them without going
through the deviation process under `docs/misra-deviations/` (currently
D001..D009 — eight runtime + one generator-output-scoped).

1. **No implicit heap.** Neither runtime nor generated code may call `malloc`,
   `calloc`, `realloc`, or `free` directly. All allocation flows through the
   `zserio_allocator_t` vtable (`runtime/include/zserio/allocator.h`). The
   forbidden-symbol grep in `tools/run_misra.sh` fails the build on any direct
   call in `runtime/src/*.c`; generated output is covered by the MISRA gate on
   generated code (Rule 21.3, `tools/run_misra_generated.sh`). The `_write`
   path additionally allocates **nothing** —
   `tools/check_no_alloc_in_writers.py` enforces this for both runtime and
   generated output.
2. **No exceptions, no `setjmp`.** Every fallible function returns
   `zserio_status_t`; output goes through pointer params. Callers may not read
   outputs unless status is `ZSERIO_OK`.
3. **Bounded stack depth.** The runtime contains no recursion (loops statically
   bounded, REQ-FS-005). Generated compound readers descend nested types by
   direct calls under a runtime depth gate: `zserio_bitstream_reader_enter/leave`
   tracks nesting on the reader and fails with `ZSERIO_ERR_MAX_DEPTH_EXCEEDED`
   past the ceiling (default 1024, `zserio_bitstream_reader_set_max_depth`).
   Per-frame stack is gated (`tools/measure_stack_usage.py --strict`): ≤256B per
   frame, ≤1024B worst non-recursive chain.
4. **No floating-point in control flow.** FP appears only on the encode/decode
   path of `float16`/`float32`/`float64` fields — never in branches, loop
   bounds, or sizing.
5. **No thread safety.** Runtime is reentrant but not thread-safe; one
   reader/writer per thread (REQ-FS-007). No globals, no atomics.
6. **C99 strict.** `-std=c99 -pedantic -Werror` plus the full warning set in
   `CMakeLists.txt`. No GNU extensions, no VLAs, no flexible array members in
   public APIs, no `_Generic` (C11), no `//` comments in generated headers used
   by ASIL builds.
7. **One typedef per logical type.** `zserio_<name>_t` for types,
   `zserio_<module>_<verb>` for functions, `ZSERIO_<NAME>` for macros. Public
   headers do **not** include `<stdio.h>` or `<assert.h>` — diagnostics are
   status codes.

## Requirements traceability

Requirements live in `requirements/{domain,functional,safety,performance}/` as
one `.md` per ID (`REQ-DM-*`, `REQ-FN-*`, `REQ-FS-*`, `REQ-PF-*`). Every
accepted requirement must be referenced by at least one test via a comment of
the form:

```c
/* Verifies: REQ-FS-001 */
```

`tools/check_traceability.py` fails CI if any accepted ID has no marker, and
`--write-csv` regenerates `requirements/traceability.csv` (CI fails on drift —
regenerate and commit it whenever markers change). **IDs are append-only** —
never reuse or renumber, even after withdrawal. When adding tests, add the
`Verifies:` marker for whichever requirement the test exercises.

## Versioning — three independent axes

`zserio-c` decouples three version numbers (REQ-DM-007, full policy in
`docs/spec/compatibility.md`):

| Axis | Source of truth | Current |
|---|---|---|
| Release | `CMakeLists.txt` `project(... VERSION ...)` | 0.1.0 (pre-1.0; APIs may shift) |
| Wire format | `ZSERIO_WIRE_FORMAT_VERSION` in `CMakeLists.txt` | 1.0 (stable) |
| Schema language | `ZSERIO_SCHEMA_LANGUAGE_VERSION` in `CMakeLists.txt` | 1.0 (stable) |

`runtime/include/zserio/version.h` mirrors these. `tools/check_version_consistency.py`
gates that `version.h`, `CMakeLists.txt`, and the spec docs stay in sync — touch
one, touch all.

## Adding a feature (the canonical loop)

For schema-language gaps, follow this loop (full version in `docs/contributing.md`):

1. Add the smallest reproducing schema as `tests/xvalidation/harnesses/<name>/`.
2. Drive the cpp side via `cpp_driver.cpp` (reference bytes from upstream).
3. Drive the C side via `c_driver.c` (our output).
4. Run `./tests/xvalidation/run_xvalidation.sh <name>` — it reports the exact
   byte offset where streams diverge.
5. Fix in `parser/src/` — usually `emit.c` (codegen), sometimes `sema.c`
   (typing / identifier mangling).
6. Confirm bidirectionally: `XVAL_SAN=1 ./tests/xvalidation/run_xvalidation.sh`.

The 183 existing harnesses are the canonical reference shapes.

## Style

`clang-format` (`.clang-format`) is the source of truth: 4-space indent,
100-column limit, Linux brace style, right-aligned pointers. Lowercase
file-internal `static` symbols with no prefix. Comments default to none; write
one only when the *why* is non-obvious (a hidden constraint, a workaround for a
tool bug, or a requirement-driven choice — in which case cite the REQ ID).
