# `tests/`

| Directory | What it is | Gate |
| --- | --- | --- |
| `runtime/` | Unity-based unit tests for `runtime/src/*.c` — one `test_<module>.c` per module plus `test_failure_modes.c` (HARA-driven fault injection) and `test_decode_hardening.c` (untrusted-input limits). | `ctest`, coverage thresholds |
| `parser/` | Unity-based unit tests for the C-native parser: lexer, parser, sema, emit, session, plus corpus sweeps over the upstream test schemas. | `ctest` |
| `xvalidation/` | The flagship correctness gate — 182 schemas × 5 seeds, cpp ↔ C byte-equal in both directions. See its [README](xvalidation/README.md). | `xvalidation` CI job |
| `benchmarks/` | REQ-PF-001 throughput harness vs upstream cpp; per-host baselines. | `perf` CI job |
| `fuzz/` | libFuzzer decode-path targets (REQ-FS-006), ASan + UBSan. | `fuzz` CI job |
| `conformance/` | `error_mapping.csv` — the upstream C++ exception → `zserio_status_t` mapping table referenced by the user guide. Not a test harness. | — |

Unity is vendored via CMake FetchContent (see `runtime/CMakeLists.txt`).

Test files reference requirements with the marker
`/* Verifies: REQ-... */`; `tools/check_traceability.py` (the
`traceability` CI job) fails the build if any accepted
requirement has no marker.

Run the unit tests:

```sh
cmake --preset gcc-debug
cmake --build --preset gcc-debug
ctest --preset gcc-debug
```
