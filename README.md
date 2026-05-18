# zserio-c

C99 port of the [zserio](https://github.com/ndsev/zserio) schema IDL, designed
from day one for use in automotive functional-safety systems (ISO 26262,
target ASIL-B, architectural headroom toward ASIL-D).

## Status

**Phase 5 substantively complete · Phase 6 optimisation arc landed.** Every
CI gate green on `main`. Wire compatibility with upstream
`ndsev/zserio v2.18.1` is gated at 182 schemas × 5 seeds per commit. The
ISO 26262 safety case is drafted end to end under
[`docs/safety-case/`](docs/safety-case/), pending independent review for
`draft → accepted`. The Phase 6 perf gate runs per-commit on two self-
hosted hosts (AMD `primary`, Raspberry-Pi 4 `pi4`); zserio-c currently
beats or ties upstream cpp on 9 of 13 workloads at the libc-vs-libc
default, and on the arena workload runs **1.2–2.0×** ahead of upstream
cpp's `std::pmr` equivalent. See
[`CHANGELOG.md`](CHANGELOG.md#phase-6--optimisation-arc-2026-05-13--2026-05-15)
for the per-workload table and the optimisations that produced it.

| Phase | Status | Marker |
| ----- | ------ | ------ |
| 0 — Bootstrap, upstream analysis | ✅ done | repo + submodule + six upstream-analysis deep-dives |
| 1 — Runtime MVP | ✅ done | 11 source files, 9 unit-test files |
| 2 — Java generator extension | ✅ done | `generator/zserio_c.jar` builds end to end |
| 3 — Full schema-language coverage | ✅ done | 391/391 grammar acceptance on upstream test corpus |
| 4 — Conformance against upstream test corpus | ✅ done | `xvalidation` CI gate: 182 schemas × 5 seeds byte-equal vs cpp |
| 5 — Safety case maturation | 🟢 substantively complete | 14 work products in `docs/safety-case/`, all `draft`; **all internal Open items closed**, gated only on case-wide independent reviewer sign-off. G3 — generator-output MISRA gate — closed `c13bd41`; G4 — transitive stack budget — closed via gcc `-fcallgraph-info` longest-path traversal (current max 512 B / 1024 B ceiling); G5 — decode-path fuzzing — closed via libFuzzer + ASan + UBSan, 60 s per target per commit, persistent corpus |
| 6 — Performance benchmarks vs C++ runtime | 🟢 optimisation arc landed | `perf` CI job: strict gate on AMD `primary` + Raspberry-Pi 4 `pi4`. Inline `_checked` byte-loop helpers + packed-delta steady-state inlining ("trait-devirt") + conditional zero-init of owned-element arrays. **9 / 13 workloads beat or tie cpp** at the libc default; arena workload runs 1.2–2.0× ahead of cpp pmr. Reference bump arena (`zserio_arena_t`) ships in `allocator.h` for embedded callers. |
| 7 — C-native parser replaces Java front-end | ⬜ open | |

See [`docs/architecture.md`](docs/architecture.md) for the design and
[`docs/safety-case/safety-case-report.md`](docs/safety-case/safety-case-report.md)
for the assembled safety argument.

## What's in CI

Every commit to `main` runs the following gates. All currently pass:

| Gate | What it proves | Tooling |
| ---- | -------------- | ------- |
| Build and test (gcc) | Runtime compiles and 11 Unity test suites pass | gcc + ctest |
| Build and test (clang) | Independent toolchain catches gcc-specific miscompilation | clang + ctest |
| Wire-format cross-validation (REQ-FN-001) | 182 schemas × 5 seeds × byte-equal vs upstream cpp | `tests/xvalidation/run_xvalidation.sh` |
| MISRA C:2012 (strict) + forbidden-symbol grep | Zero MISRA findings on the runtime; no banned-libc calls | `tools/run_misra.sh --strict` |
| MISRA C:2012 (generator output) | Zero cppcheck/MISRA findings on the C the generator emits for a 13-schema representative set | `tools/run_misra_generated.sh --strict` |
| Coverage (line ≥ 99% / branch ≥ 85% / function = 100%) | Snapshot: 99.89% / 88.03% / 100% | gcov + lcov + `tools/check_coverage.py` |
| Size regression (REQ-PF-002) | ≤ 5% growth on any of `.text` / `.rodata` / `.data` / `.bss` | `tools/measure_size.py` + `tools/check_size_regression.py` |
| Stack-budget analysis (REQ-FS-003, hazard H-09) | Every function has a static frame ≤ 256 B AND every call chain ≤ 1024 B (longest-weighted-path traversal of gcc's `-fcallgraph-info` graph) | `tools/measure_stack_usage.py --strict` |
| Decode-path fuzzing (REQ-FS-006) | libFuzzer + ASan + UBSan over the runtime decode path for two representative schemas (primitives/varints/bitfields; strings + nested compound), 60 s each per commit, persistent corpus | `tools/run_fuzz.sh` |
| Requirements traceability | Every accepted in-phase requirement has a `Verifies:` marker | `tools/check_traceability.py` |
| Phase 2 generator | The generator jar builds and loads end to end | ant |
| Performance benchmark (REQ-PF-001, advisory) | Median-of-5 encode/decode ns/op for each benchmark workload, side-by-side vs upstream cpp | `tests/benchmarks/run_benchmark.sh` |
| Test-data discovery (advisory) | Reports schemas in the upstream corpus not yet in xvalidation | bash |

## Quick start

```bash
# Configure + build runtime, parser/generator (zserio_c_gen), and tests
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure

# Generate C from a schema (no JVM, no upstream jar — just our gen)
build/parser/zserio_c_gen path/to/your/schemas/your_schema.zs \
    -c path/to/output \
    -src path/to/your/schemas

# Cross-validate against upstream cpp (requires both submodules)
git submodule update --init --recursive
bash tests/xvalidation/run_xvalidation.sh        # 182 schemas × 5 seeds

# Same but under ASan + UBSan
XVAL_SAN=1 bash tests/xvalidation/run_xvalidation.sh

# Run the local stack-budget gate
python3 tools/measure_stack_usage.py --strict

# Run the local size gate
python3 tools/measure_size.py > /tmp/sizes.json
python3 tools/check_size_regression.py /tmp/sizes.json
```

For a 10-minute hands-on walkthrough — write a `.zs` schema,
generate, populate a struct, encode, decode — see
[`docs/using-zserio-c.md`](docs/using-zserio-c.md).

## Project goals

The four pillars are CI-gated, not aspirational:

- **Pure C99 generated code** — `-std=c99 -pedantic -Werror` plus the strict
  warning set in `CMakeLists.txt`. No compiler extensions, no GCC-isms.
- **No implicit heap** — `runtime/src/*.c` and generated code never call
  `malloc`/`calloc`/`realloc`/`free` directly. All allocation flows through
  the caller-supplied `zserio_allocator_t` vtable. Enforced by the
  forbidden-symbol grep in `tools/run_misra.sh`. The default backend wraps
  libc malloc; safety builds plug a fixed pool.
- **MISRA C:2012 strict** with documented, justified deviations (six entries
  under [`docs/misra-deviations/`](docs/misra-deviations/)). No suppression
  without a sibling deviation file.
- **ISO 26262 alignment from commit one** — the safety case is grown
  alongside the code, not retrofitted. See
  [`docs/safety-case/`](docs/safety-case/).
- **Coverage gate** — line ≥ 99%, branch ≥ 85%, function = 100% on
  `runtime/src/*`. Currently 99.89% / 88.03% / 100%.
- **Wire-compatible** with upstream `ndsev/zserio v2.18.1` — gated for 182
  schemas × 5 seeds per commit, byte-equal in both directions.
- **Bounded stack** — every function has a static, ≤ 256 B frame (current
  observed max 160 B); the worst transitive call-chain is gated at ≤ 1024 B
  (current observed max 512 B), enforced via a longest-weighted-path
  traversal of gcc's `-fcallgraph-info` graph plus no-recursion (REQ-FS-003).

## Why a port

The upstream C++ generator and runtime are not viable for automotive
functional-safety production:

- STL containers grow under the hood through their allocator template
  parameter, but in practice an allocator override is impractical across an
  entire codec — so the default heap is used.
- Errors propagate via exceptions; ASIL builds typically forbid them.
- RTTI is present in several places. The schema parser pulls in
  `<typeinfo>`.

`zserio-c` removes these by construction: status codes instead of
exceptions, an explicit allocator vtable instead of templates, no
runtime reflection in generated code. See
[`docs/upstream-analysis/05-asil-pain-points.md`](docs/upstream-analysis/05-asil-pain-points.md)
for the detailed enumeration that motivated each design choice.

## Repository layout

```
runtime/             libzserio_c_runtime — bitstream, varint, primitives,
                     delta, allocator, status
generator/           Java generator extension that plugs into upstream's
                     ZserioTool and emits C against the runtime
third_party/zserio/  upstream submodule, pinned at v2.18.1
third_party/zserio-test-data/  upstream test-corpus submodule

docs/architecture.md           — design intent (load-bearing)
docs/coding-rules.md           — code style + MISRA framing
docs/integration-guide.md      — integrator-facing checklist (H-04/H-05, allocator, stack sizing, toolchain qualification)
docs/safety-case.md            — entry point for the safety case
docs/safety-case/              — 14 ISO 26262 work products
docs/misra-deviations/         — D001..D006 with cppcheck-suppressions cross-refs
docs/upstream-analysis/        — six deep-reads of upstream zserio
docs/benchmarks/               — per-host perf baselines + RESULTS.md
                                 (auto-rendered ratio table) + size
                                 baseline + methodology + tuning notes

requirements/                  — REQ-FN-* (functional), REQ-FS-* (safety),
                                 REQ-PF-* (performance), one .md per requirement

tests/runtime/                 — Unity unit tests, 11 suites
tests/xvalidation/             — 182-schema byte-equal cross-validation harness
tests/benchmarks/              — REQ-PF-001 throughput harness (median-of-5
                                 encode/decode vs upstream cpp)
conformance/ndslive/           — NDS Live SmartLayerTile end-to-end fixture

examples/embedded_decode/      — recommended embedded deployment pattern:
                                 static arena pool + decode + error
                                 handling + watermark sizing. Builds
                                 standalone via CMake.

tools/                         — measure_size, check_size_regression,
                                 measure_stack_usage, check_coverage,
                                 check_perf_regression, check_traceability,
                                 render_perf_results, run_misra,
                                 build_zserio_core_jar, build_generator_jar,
                                 build_zserio_bundle_jar
.github/workflows/ci.yml       — the 11 gates listed above
```

## Documentation entry points

For day-one usage — schema → generated C → working executable, plus
the generated-code naming and lifecycle reference:
[`docs/using-zserio-c.md`](docs/using-zserio-c.md).

For the design: [`docs/architecture.md`](docs/architecture.md).

For the safety story:
[`docs/safety-case/safety-case-report.md`](docs/safety-case/safety-case-report.md) — single-document overview of the assembled argument, links into every supporting work product.

For coding rules and MISRA framing:
[`docs/coding-rules.md`](docs/coding-rules.md).

For the upstream-zserio analysis that motivated v1 decisions:
[`docs/upstream-analysis/`](docs/upstream-analysis/).

## License

MIT — see [`LICENSE`](LICENSE). Upstream zserio under `third_party/zserio/`
remains under its own BSD-3-Clause license; the upstream test data under
`third_party/zserio-test-data/` retains its own terms. Neither submodule's
content is redistributed in the shipping binary.
