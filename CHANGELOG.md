# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Phase 6 — measurement framework (2026-05-13)

`.phase` advanced from 4 to 6. Phase 5 is substantively complete
(every work product at `draft`); Phase 6 opens with the benchmark
infrastructure that REQ-PF-001 calls for.

#### Added

- **`tests/benchmarks/`** — REQ-PF-001 throughput harness.
  - `run_benchmark.sh` — generates C and cpp code from each
    workload's `.zs` schema, builds both drivers at `-O2`, runs
    each `RUNS` times (default 5), takes the median per metric,
    and emits aggregated JSON.
  - `workloads/small_flat/` — first workload: 10 fixed-width
    primitives + bool + bit:4, ~31 bytes encoded. Drivers
    measure encode and decode ns/op using `CLOCK_MONOTONIC` on
    the C side and `std::chrono::steady_clock` on the cpp side.
- **`tools/check_perf_regression.py`** — analogue of the size
  regression checker. Compares current measurement against
  `docs/benchmarks/perf.baseline.json` and fails on a zserio-c
  metric regression > 15% (initial loose threshold to absorb
  public CI runner noise; tightening is a follow-up).
- **`docs/benchmarks/perf.baseline.json`** — initial baseline,
  seeded from a local measurement. To be replaced with CI-measured
  numbers once 2–3 green runs characterise the runner noise floor.
- **`docs/benchmarks/README.md`** — methodology + gate-semantics
  + planned workloads.
- **CI: new `perf` job (advisory)** — runs the harness on every
  commit, posts the full output as a commit comment. Does NOT
  fail the build on regression yet (`|| true` on the check step);
  the gate flips to mandatory once we have stable CI baseline
  numbers.

#### Current performance state (local measurement, x86_64)

| Workload   | metric         | zserio_c (ns) | zserio_cpp (ns) | cpp/c |
| ---------- | -------------- | ------------: | --------------: | ----: |
| small_flat | encode_ns/op   |         69.24 |           34.91 | 0.504 |
| small_flat | decode_ns/op   |         68.74 |           19.94 | 0.290 |

The cpp/c ratio < 1.0 confirms the optimisation gap REQ-PF-001
exists to close. The documented next step is the bit-cache +
mask-table read pattern in `runtime/src/bitstream.c:78-80`.

### Phase 5 — substantively complete (2026-05-13)

The ISO 26262 safety case is drafted end to end. Every work product
listed in `docs/safety-case/safety-plan.md` exists at `status: draft`;
the case as a whole moves to `accepted` after independent review and
the closure of the three Open items recorded in
`docs/safety-case/safety-case-report.md`.

#### Added

- **`docs/safety-case/` directory — 14 ISO 26262 work products**:
  - **Foundation**: `item-definition.md` (ISO 26262-3 §5),
    `hazard-analysis.md` (ISO 26262-3 §6, 10 item-level hazards),
    `safety-plan.md` (ISO 26262-2 §6, work-product matrix).
  - **Closure**: `stack-budget.md` (closes hazard H-09 with the
    `stack` CI gate; 256 B per-function ceiling, currently 144 B
    max), failure-mode injection tests at
    `tests/runtime/test_failure_modes.c` (one test per actionable
    HARA row).
  - **Concept**: `functional-safety-concept.md` (ISO 26262-3 §7,
    9 safety goals SG-1..SG-9 mapped to existing
    `requirements/`), `technical-safety-concept.md` (ISO 26262-4 §6,
    mechanism-to-gate matrix anchored to specific source files and
    CI jobs).
  - **Tool qualification (ISO 26262-8 §11)**:
    `tool-qualification-gcc.md` (TCL2),
    `tool-qualification-cppcheck.md` (TCL2),
    `tool-qualification-coverage.md` (TCL2 for lcov / gcov),
    `tool-qualification-generator.md` (TCL1 via xvalidation TD1).
  - **Verification**: `unit-verification.md` (ISO 26262-6 §9,
    per-runtime-module evidence), `integration-verification.md`
    (ISO 26262-6 §10, xvalidation + conformance/ndslive).
  - **Assembly**: `safety-case-report.md` (ISO 26262-2 §6.4, the
    single entry document for a reviewer).
- **`tools/measure_stack_usage.py`** — `-fstack-usage` build + per-
  function frame parser; gates strict mode in CI as the new `stack`
  job (REQ-FS-003 / hazard H-09).
- **`tests/runtime/test_failure_modes.c`** — seven tests covering
  H-01 (buffer over/underrun), H-02 (bounded execution on malformed
  varint), H-03 (failing allocator propagates `ZSERIO_ERR_OUT_OF_MEMORY`),
  and H-06 (freedom from interference between codec instances).
- **MISRA deviation D006** — type-parametric macros in `varint.c`
  (Rules 20.7 and 20.10); covers the `DELTA_TRAITS_{UNSIGNED,SIGNED}`
  family. Token-paste and type-name parameters are categorically
  outside the rules' precedence-bug intent.
- **`docs/safety-case.md`** rewritten from "Phase 0 placeholder" to
  the current-state navigation entry pointing into the new
  directory.

#### Changed

- `.phase` advanced from 3 to 4 (REQ-FN-001 byte-equal wire
  conformance is now CI-gated by the xvalidation suite).
- Coverage gate snapshot: line **99.89%**, branch **88.03%**,
  function **100.00%** on `runtime/src/*`. Function coverage rose
  from 70.37% after `test_delta_traits.c` exercised the trait-
  driven delta path's 27 generated wrapper functions plus the 9
  `zserio_bitsize_var*` helpers.
- Size baseline updated to reflect the legitimate Phase 4 packed-
  array runtime growth: `.text` 8221 → 9756 B, `.rodata` 432 → 720 B
  (the 9 `zserio_delta_traits_*` structs at 32 B each = 288 B
  bucketed into `.rodata` via `.data.rel.ro.local.*`).
- xvalidation surface expanded from 181 to **182 schemas** by
  closing the `packages/zs/index.zs` deferral; the runner applies
  upstream's `-setTopLevelPackage index_workaround` workaround
  for that single schema.

#### MISRA — 11 violations cleared

- 7 findings in `bitstream.c` debug-trace facility addressed by
  gating the entire facility behind `#ifdef ZSERIO_TRACE_ENABLED`
  and adding `-UZSERIO_TRACE_ENABLED` to the cppcheck invocation.
- 2 findings in `varint.c` addressed by removing redundant `#undef`
  trailers (Rule 20.5).
- 2 findings in `varint.c` (Rule 20.7) covered by deviation D006
  (extended to 20.10 after the first iteration).
- 1 finding in `varint.h:107` addressed by moving
  `#include "zserio/delta.h"` to the top of the file.

### Phase 4 — complete (2026-05-12)

#### Added

- **`tests/xvalidation/`** — cross-validation harness. For each
  schema in `schemas.txt`, generates C + cpp code from the same
  AST, compiles drivers against our runtime and upstream's runtime,
  and asserts byte-equal output across 5 seeds. Wires into CI as
  the `xvalidation` job. Gates **REQ-FN-001**.
- **`conformance/ndslive/`** — production-shape end-to-end fixture
  for a 50,896-byte NDS Live SmartLayerTile encoded by upstream
  zserio v2.18.1; verifies every decoded field against zs-yaml
  ground truth.
- Phase 4 packed-array runtime work:
  - Trait-driven delta context for varint elements
    (`zserio_delta_traits_t` + 9 traits structs in `varint.c`).
  - Nested packing contexts.
  - `max_bit_number == 0` elision (deltas-of-equals takes 0 bits).
  - Lazy `delta_init` for dynamic-bit packable fields and for choice
    arms inside packed arrays.
  - Self-referential optional compounds via heap-indirected pointer
    fields.
  - Cross-compound offset patching through dot-path expressions.

#### Fixed

- `test_delta` SEGFAULT — `zserio_delta_context_t` must be memset
  before `delta_init` (idempotent init short-circuits on garbage
  fields, leaving the context unwritten; subsequent observe
  dereferences a garbage `traits` pointer). Generator-emitted code
  always memsets first; the tests now do too.
- Bitstream test assertions: removed three stale rejection checks
  for non-power-of-two alignments. The runtime was relaxed to
  accept any positive alignment (the zserio spec uses
  `align(3) / align(5) / align(6) / align(7)`), but the tests had
  not been updated.

### Phase 3 — complete

#### Added

- **Full schema-language coverage**: 391 / 391 grammar acceptance on
  the upstream test corpus. The generator emits codecs for every
  zserio construct in scope for v1:
  - All compound kinds (struct, choice, union, bitmask, enum).
  - Parameterised types, including templated array elements.
  - Constraints + if-optionals.
  - Indexed offsets with `@index` semantics.
  - Builtin expression functions (`lengthof`, `valueof`,
    `numbits`, `isset`).
  - User-defined zserio functions.
  - Packed arrays of all element kinds (fixed-width, bit-field,
    varint, bool, dynamic-width, compound, parameterised compound).

### Phase 2 — complete

#### Added

- **`generator/`** — Java generator extension that plugs into
  upstream `ZserioTool` and emits C99 against our runtime. Builds
  `zserio_c.jar` end-to-end and smoke-loads under the
  `phase-2-generator` CI job.
- Hello-world end-to-end demo: a simple `.zs` schema generates C
  sources that compile against the runtime and round-trip byte-
  equal against upstream.
- Field formatter, identifier mapper, native-type mapper,
  expression formatter — the generator's emit-side primitives.
- Per-construct emitters: `ConstEmitter`, `SubtypeEmitter`,
  `EnumerationEmitter`, `BitmaskEmitter`, `StructureEmitter`,
  `UnionEmitter`, `ChoiceEmitter`.

### Phase 1 — complete

#### Added

- **Runtime library** (`runtime/`): allocator, status, bitstream,
  primitives, varint, float, string, extern, bytes, delta, plus
  `zserio.h` umbrella.
- **MISRA C:2012 strict** enforcement: `cppcheck --addon=misra` in
  CI; five initial deviations under `docs/misra-deviations/D001..D005`
  for the categorical acceptances (multiple returns, allocator
  wrapper, IEEE 754 memcpy, void-pointer cast, buffer pointer
  arithmetic).
- **Coverage gate** (line / branch / function thresholds) wired
  into CI.
- **Size gate** (REQ-PF-002) — per-section size measurement,
  baseline at `docs/benchmarks/sizes.baseline.json`, fails CI on
  > 5% growth on any of `.text` / `.rodata` / `.data` / `.bss`.

### Phase 0 — complete

#### Added

- Initial repository scaffold: docs, requirements, CMake, CI
  skeleton.
- Upstream `ndsev/zserio` vendored as submodule at
  `third_party/zserio`, pinned to `v2.18.1`.
- Starter requirement set: REQ-FS-001..008 (safety), REQ-FN-001
  (wire compat), REQ-PF-001..002 (performance).
- Six upstream-analysis deep-dives at `docs/upstream-analysis/00..06`:
  structural survey, schema language scope, wire format, Java
  extension SPI, C++ runtime internals (with `pmr/` deep-read),
  ASIL pain-point enumeration, and test corpus reuse plan.
- `tests/conformance/error_mapping.csv` — maps the 21 v1-relevant
  upstream throw sites to specific `ZSERIO_ERR_*` codes.
