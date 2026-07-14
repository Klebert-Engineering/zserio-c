# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added — synthetic production-shape conformance coverage

- New xvalidation harness `layered_container` (183rd schema): a
  synthetic multi-package schema mirroring the complexity profile
  of a real layered map-tile payload — indexed offsets into
  variable-length layers, enum-dispatched choice per layer,
  parameterized compounds with dynamic bitfields, packed compound
  arrays, unions, strings, an extern blob, and alignment. Restores
  a public production-shape gate after the NDS.Live harness moved
  to the private companion repository. `schemas.txt` entries now
  also resolve against in-tree `tests/xvalidation/schemas/`.

### Added — packaging, releases, and adoption docs

- CMake install/export: `find_package(zserio_c <version>)` imports
  `zserio_c::runtime`, `zserio_c::parser`, and `zserio_c::gen`
  after `cmake --install`; version compatibility is
  SameMinorVersion (pre-1.0 minors may change APIs). A
  `zserio_c_runtime.pc` pkg-config file covers non-CMake builds.
  Tests and install rules now default off when zserio-c is
  consumed via `add_subdirectory`/FetchContent, so embedding it
  no longer pulls the Unity test dependency. A CI `packaging` job
  installs to a scratch prefix and builds a `find_package`
  consumer end-to-end (generate → compile → byte round-trip) on
  every commit.
- Release workflow: pushing a `v*.*.*` tag verifies the tag
  against the source version, builds and tests the release
  commit, and publishes a GitHub release with notes extracted
  from the matching CHANGELOG section
  (`tools/extract_changelog.py`).
- `docs/feature-matrix.md`: every upstream zserio v2.18.1 feature
  with its support status (full / partial / rejected / out of
  scope) and the harness or test that verifies it.

### Fixed — documentation

- README stated the upstream `ndsev/zserio` license as Apache 2.0;
  it is BSD 3-Clause.

### Added — service and pub/sub interface generation (REQ-FN-027)

- `service` declarations now generate transport-agnostic C
  interfaces: typed per-method client wrappers over a new
  `zserio_service_t` transport vtable, and a server dispatch
  (`<pkg>_<svc>_service_init`) that routes the verbatim short
  method name to application handlers. Wire-interoperable with
  upstream zserio v2.18.1 peers over a common transport.
- `pubsub` declarations generate typed `publish_*` / `subscribe_*`
  wrappers over a new `zserio_pubsub_t` vtable, with caller-owned
  subscription state (no hidden heap).
- New runtime headers `zserio/service.h` and `zserio/pubsub.h`
  (transport seams + the grow-and-retry
  `zserio_service_serialize` helper). Design notes:
  `docs/design/service-pubsub.md`.
- Non-literal `topic(...)` expressions are rejected with a
  source-located error diagnostic (generation fails closed).
- Previously these constructs parsed but emitted placeholder
  stubs only; SQL and JSON extensions remain out of scope.

### Fixed — generator correctness (open-source readiness audit)

- `float`/`string`/`bool` and negative signed constants now emit
  correct C literals; previously every `const` was printed through
  the unsigned integer payload, so `const float32 X = 1.5;`
  produced a garbage `#define` (raw bit-pattern decimal).
- `implicit` arrays are rejected with a diagnostic. They
  previously generated silently as length-prefixed auto arrays —
  a wire-format divergence from upstream (the construct is
  deprecated upstream).
- `sql_table` / `sql_database` are rejected with an informative
  diagnostic instead of silently emitting a partial data-holder
  struct. SQLite-backed generation is planned as an optional
  extension outside the ASIL scope.
- `optional` combined with an `if` condition is rejected,
  matching upstream.
- Functions declared inside `choice` bodies are rejected with a
  diagnostic; they were previously parsed and silently discarded,
  and calls to them emitted `0 /*unsupported-call*/`.
- `tools/check_no_alloc_in_writers.py` now really scans the
  runtime write path (`zserio_write_*` naming was invisible to
  the old `*_write` matcher) and fails closed on an empty scan.

### Changed — CI and repository hardening for open source

- Self-hosted runners are no longer reachable from fork pull
  requests: every job computes its runner dynamically — same-repo
  runs (pushes and maintainer PR branches) use the org runners,
  fork PRs fall back to GitHub-hosted `ubuntu-latest`. The `perf`
  benchmark stays on the dedicated measurement hosts with a
  same-repo guard and no hosted fallback.
- GitHub Actions are pinned to commit SHAs; dependabot keeps the
  pins and submodules current; per-job timeouts added.
- NDS.Live conformance and benchmark harnesses relocated to a
  private companion repository
  (`zserio-c-ndslive-conformance`, NDS Association members only);
  public docs and requirements reference it as supplementary
  evidence.
- Added `SECURITY.md`, `CODE_OF_CONDUCT.md`, root
  `CONTRIBUTING.md`, issue/PR templates, `CODEOWNERS`,
  `.editorconfig`.
- MISRA deviation register extended by D008 (Rule 10.8) and D009
  (Rule 8.9) after a re-triage of blanket suppressions; the
  suppression rationale document was rewritten with verified
  code sites.
- Documentation accuracy pass: every quantified claim
  (work-product count, deviation count, gate scopes, doc-comment
  and `compatibility_version` status, import semantics) now
  matches the code.

## [0.1.0] - 2026-06-12

First release. Wire format 1.0 and schema language 1.0 are stable;
release `0.1.0` declares the tool surface (runtime API,
generated-code shape, CLI) as pre-1.0 — still subject to change
between minor releases until `1.0.0`. See
[`docs/spec/compatibility.md`](docs/spec/compatibility.md) for the
full versioning policy.

The C-native parser + generator (`zserio_c_gen`) replaced the Java
front-end on the C target; the legacy Java extension under
`generator/` remains as the cpp-side oracle for the xvalidation
gate only. NDS.Live end-to-end conformance + bidirectional
roundtrip under ASan + UBSan are clean. Safety-case documents (13 work
products under `docs/safety-case/`) are structurally complete at
`status: draft`, awaiting independent review.

### Security hardening — untrusted-decode path (2026-06-10)

Audit of the decode path (generated decoders run on
attacker-controlled bitstreams; ASIL-B / REQ-FS-*). No wire-format
change — xvalidation stays 182/182 byte-equal in both directions.

#### Added — runtime API

- **Decode depth gate.** Generated compound / choice readers
  bracket their body with `zserio_bitstream_reader_enter` /
  `_leave`; the reader tracks nesting depth and fails with the new
  `ZSERIO_ERR_MAX_DEPTH_EXCEEDED` past its ceiling (default 1024,
  tunable per reader via the new
  `zserio_bitstream_reader_set_max_depth`). Recursive schemas
  (lists, trees, mutually-recursive structs) can no longer exhaust
  the C stack on crafted input (REQ-FS-003).
- New status codes: `ZSERIO_ERR_MAX_DEPTH_EXCEEDED` (safety-limit
  band), `ZSERIO_ERR_ARRAY_TOO_LARGE`, `ZSERIO_ERR_INVALID_TAG`.

#### Changed — generated decode path

- Array allocation guards the `size_t` count × element-size
  multiply against overflow and bounds the count by the bits
  remaining in the stream (`ZSERIO_ERR_ARRAY_TOO_LARGE`); packed
  arrays skip the remaining-bits bound (delta encoding packs many
  elements into few bits) but keep the overflow guard.
- Union read rejects an unknown wire tag with
  `ZSERIO_ERR_INVALID_TAG` instead of returning OK with an
  untouched union.

#### Changed — parser robustness (build-time tool)

- Expression / type parsing gains a depth gate
  (`ZSP_MAX_PARSE_DEPTH=256`); template instantiation a depth cap
  (`ZSP_MAX_TEMPLATE_DEPTH=64`); `rule_group` body loop gains the
  stall guard its sibling loops already had; float-exponent lexing
  clamps the exponent (previously signed-overflow UB on inputs
  like `1e9999999999`).

#### Fixed — tooling

- `tools/run_misra.sh` REQ-FS-002 scan: `find` precedence bug
  meant the runtime `.c` files were never scanned; now fixed and
  fails closed if the scan matches zero files.

### Performance optimization arc (2026-05-13 → 2026-05-15)

With the measurement framework live, the optimization work that
REQ-PF-001 calls for landed across both perf hosts (AMD `primary`,
Raspberry-Pi 4 `pi4`). Headline outcome: **zserio-c beats or ties
upstream cpp on 9 of 13 workloads** at the libc-vs-libc default
allocator, and pulls ahead 1.2–2.0× on the arena workload when
both sides use a monotonic allocator. The remaining two losses
(`addressbook_align`, `addressbook_packed`) are documented as a
libc-string-allocation vs `std::string` SSO difference, not an
algorithmic gap.

#### Added — runtime API

- **Reference bump arena allocator** (`88550f7`). New public
  `zserio_arena_t` state + `zserio_allocator_arena(...)` adapter
  in `runtime/include/zserio/allocator.h`. Monotonic upward bump,
  `zserio_arena_reset()` reclaims the whole arena in O(1), free
  is a no-op. The runtime's allocator vtable already supported
  this kind of plug-in; what was missing was a usable reference
  implementation so callers don't roll their own. Unit-tested
  for alignment-up, OOM, integer-wrap defense, reset-preserves-
  high-water, free-is-noop. D004 / D005 MISRA deviations
  extended to cover the new code (allocator `void*` cast +
  buffer-byte pointer arithmetic — same patterns previously
  accepted for the bitstream).
- **`tests/benchmarks/workloads/addressbook_align_arena/`** — a
  showcase workload pairing the bump arena (c_driver) against
  `std::pmr::PropagatingPolymorphicAllocator` backed by a custom
  `BumpMemoryResource` (cpp_driver, `b5ce535`). The cpp_driver
  exercises upstream's `-setCppAllocator polymorphic` codegen
  mode so the comparison is fair (arena-vs-arena, not arena-vs-
  std::allocator).

#### Added — performance infrastructure

- **Multi-host perf gate.** AMD `primary` (per-commit canonical
  x86_64 reference) plus Raspberry-Pi 4 `pi4` (per-commit
  Cortex-A72 reference). Self-hosted runners are arch-aware
  (`decb065`), host-namespaced (`a7c8410`), label-clean
  (`0fa66ab`); the perf job's matrix routes per-host (`783fce7`);
  transition guards (`continue-on-error` + 30 min timeout from
  `7babcf5`) removed once both hosts were stable (`c57bd09`).
- **Per-host baselines** at `docs/benchmarks/perf.baseline.<host>.json`
  with `PERF_HOST` env-var routing through
  `tools/check_perf_regression.py` — gate fails on ≥ 15 %
  zserio-c regression, advises "↓ rebaseline?" on ≥ 30 % win.
- **`docs/benchmarks/methodology.md`** — per-host comparison
  rationale, host-label table, Pi-tuning subsection (governor /
  swap / apt-daily timers), and an allocator-choice section that
  calls out the arena-workload framing explicitly so a row at
  cpp/c > 2 isn't mistaken for a wire-format advantage.

#### Optimizations landed

Listed in order of commit and impact:

1. **Inline `_checked` byte-loop helpers** (`9aac4bd`).
   `zserio_bitstream_read_bits_checked` and a matching signed
   variant — keep the bit-cache state live in registers across
   consecutive byte reads in varint / string slow paths.
   **+13–23 %** on packed-varint workloads.
2. **Packed-delta steady-state inlining** ("trait-devirt",
   `6c7ce68` + dead-code cleanup `8b4b69c`).
   `zserio_delta_read` / `_write` become `static inline`
   wrappers in `delta.h` that open-code the packed steady-state
   branch and defer cold first-element / unpacked-fallback to
   out-of-line `_slow` helpers. The hot loop now drops three
   function-call boundaries per element (state machine +
   `read_signed_checked` + `read_bits_impl`). **−46 % / −47 %**
   on `simpletrace_packed` (primary / pi4). cpp/c ratio: 0.58
   → 1.116 (primary).
3. **Conditional zero-init of owned-element arrays**
   (`a795668` for non-packed, `8326da2` for packed-compound).
   The generator no longer emits `memset(buffer, 0, N*sizeof)`
   before the per-element loop. Count starts at 0, commits to
   the full value only after the loop succeeds; on per-element
   error, local cleanup walks the decoded slots, frees the
   buffer at its captured alloc size, and propagates the status.
   Wire format unchanged; xvalidation 182 / 182 pass at every
   commit. **−7.6 %** on vanilla `addressbook` (now at cpp/c
   1.020 — the algorithmic gap closed), −4.6 % on
   `addressbook_align_arena`, −4.5 % / −1.4 % on
   `addressbook_packed`, −2.7 % / −4.9 % on
   `simpletrace_packed`.

#### Current performance state (per-host CI baselines, decode)

cpp/c ratio per workload — values ≥ 1.0 mean zserio-c wins.

| Workload                  | primary | pi4   | Verdict                          |
| ------------------------- | ------: | ----: | -------------------------------- |
| `large_array`             |  2.295  | 1.592 | clear win (bulk throughput)      |
| `addressbook_align_arena` |  1.215  | 2.039 | arena vs pmr cpp (fair)          |
| `simpletrace_packed`      |  1.116  | 1.336 | trait-devirt + transactional     |
| `carsales_align`          |  1.333  | 1.253 | win                              |
| `carsales`                |  1.158  | 1.191 | win                              |
| `carsales_packed`         |  1.088  | 1.150 | win                              |
| `small_flat`              |  1.193  | 1.008 | parity / small win               |
| `medium_nested`           |  0.994  | 1.023 | parity                           |
| `apollo`                  |  0.980  | 1.012 | parity                           |
| `addressbook`             |  0.981  | 0.990 | parity (libc-vs-libc gap closed) |
| `simpletrace`             |  0.887  | 0.956 | narrow loss                      |
| `addressbook_align`       |  0.747  | 0.776 | libc-string-alloc gap            |
| `addressbook_packed`      |  0.820  | 0.860 | libc-string-alloc gap            |

The two `addressbook_*` losses are dominated by per-string
`malloc` / `free` traffic. cpp's `std::string` SSO avoids the
malloc on short names; ours always allocates. An embedded
caller plugs the bump arena and the gap collapses (see
`addressbook_align_arena`).

### Performance measurement framework (2026-05-13)

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
  numbers once 2–3 green runs characterize the runner noise floor.
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

The cpp/c ratio < 1.0 confirms the optimization gap REQ-PF-001
exists to close. The documented next step is the bit-cache +
mask-table read pattern in `runtime/src/bitstream.c:78-80`.

### ISO 26262 safety case (2026-05-13)

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
    (ISO 26262-6 §10, xvalidation + NDS.Live end-to-end
    conformance).
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
- **`docs/safety-case.md`** rewritten from an early placeholder to
  the current-state navigation entry pointing into the new
  directory.

#### Changed

- REQ-FN-001 byte-equal wire conformance is now CI-gated by the
  xvalidation suite.
- Coverage gate snapshot: line **99.89%**, branch **88.03%**,
  function **100.00%** on `runtime/src/*`. Function coverage rose
  from 70.37% after `test_delta_traits.c` exercised the trait-
  driven delta path's 27 generated wrapper functions plus the 9
  `zserio_bitsize_var*` helpers.
- Size baseline updated to reflect the legitimate packed-array
  runtime growth: `.text` 8221 → 9756 B, `.rodata` 432 → 720 B
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

### Wire-format cross-validation + packed arrays (2026-05-12)

#### Added

- **`tests/xvalidation/`** — cross-validation harness. For each
  schema in `schemas.txt`, generates C + cpp code from the same
  AST, compiles drivers against our runtime and upstream's runtime,
  and asserts byte-equal output across 5 seeds. Wires into CI as
  the `xvalidation` job. Gates **REQ-FN-001**.
- **NDS.Live end-to-end conformance fixture** — production-shape
  fixture for a 50,896-byte payload encoded by upstream zserio
  v2.18.1; verifies every decoded field against zs-yaml ground
  truth. (Since relocated to a private companion repository —
  NDS Association membership required.)
- Packed-array runtime work:
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

### Full schema-language coverage

#### Added

- **Full schema-language coverage**: 391 / 391 grammar acceptance on
  the upstream test corpus. The generator emits codecs for every
  zserio construct in scope for v1:
  - All compound kinds (struct, choice, union, bitmask, enum).
  - Parameterized types, including templated array elements.
  - Constraints + if-optionals.
  - Indexed offsets with `@index` semantics.
  - Builtin expression functions (`lengthof`, `valueof`,
    `numbits`, `isset`).
  - User-defined zserio functions.
  - Packed arrays of all element kinds (fixed-width, bit-field,
    varint, bool, dynamic-width, compound, parameterized compound).

### Java generator extension (now the xvalidation oracle)

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

### Runtime library

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

### Bootstrap and upstream analysis

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
