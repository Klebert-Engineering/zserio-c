# `tools/`

Repository tooling: CI gates, analysis scripts, and the upstream
jar builds that feed the xvalidation oracle.

## Gates (fail CI on violation)

| Tool | Gate |
| --- | --- |
| `check_traceability.py` | Every in-phase accepted requirement is referenced by a `/* Verifies: REQ-... */` marker in at least one test. |
| `check_version_consistency.py` | `version.h` ↔ `CMakeLists.txt` ↔ spec docs agree on release / wire-format / schema-language versions (REQ-DM-007). |
| `run_misra.sh` | cppcheck MISRA C:2012 on the runtime + forbidden-symbol scans (REQ-FS-001 no direct heap, REQ-FS-002 no setjmp/longjmp). `--strict` fails on any finding. |
| `run_misra_generated.sh` | Same MISRA gate on generator output over a representative schema set. |
| `check_no_alloc_in_writers.py` | No allocator use anywhere in `_write` paths, runtime and generated (REQ-FN-016). |
| `measure_stack_usage.py` | `--strict`: ≤256 B per-function frame, ≤1024 B worst non-recursive chain (REQ-FS-003, hazard H-09). |
| `check_coverage.py` | Line ≥99%, branch ≥85%, function 100% on `runtime/src/*` (from lcov capture). |
| `measure_size.py` + `check_size_regression.py` | Per-section size tracking vs `docs/benchmarks/sizes.baseline.json`; >5% growth fails (REQ-PF-002). |
| `check_perf_regression.py` | Decode/encode ns/op vs per-host baseline `docs/benchmarks/perf.baseline.<host>.json`; ≥15% regression fails (REQ-PF-001). |
| `run_fuzz.sh` | libFuzzer + ASan + UBSan over the decode-path targets in `tests/fuzz/` (REQ-FS-006). |

## Upstream / oracle plumbing

| Tool | Purpose |
| --- | --- |
| `build_zserio_core_jar.sh` | Builds upstream `zserio_core.jar` from the `third_party/zserio` submodule (ant + JDK 17). |
| `build_zserio_bundle_jar.sh` | Builds the upstream bundle jar incl. the cpp extension (the xvalidation wire-format oracle). |
| `build_generator_jar.sh` | Builds the legacy Java extension `zserio_c.jar` (`generator/`) — used only to drive the upstream toolchain, not on the C build path. |
| `smoke_generator.sh` | Smoke-loads `zserio_c.jar` through `ZserioTool`. |
| `discover_test_data.sh` | Advisory sweep of the upstream test-data corpus; reports which schemas the toolchain handles. |
| `render_perf_results.py` | Renders `docs/benchmarks/RESULTS.md` from perf JSON. |

## Suppressions

`cppcheck-suppressions.txt` is the machine-read suppressions list;
`cppcheck-suppressions.md` is the human-readable rationale. Every
MISRA suppression must have a deviation document under
`docs/misra-deviations/` — see the process in that directory's
README.
