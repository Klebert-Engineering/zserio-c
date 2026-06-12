---
work_product: ISO 26262-8 §11 — Tool qualification (lcov / gcov)
status: draft
tool: lcov, gcov
---

# Tool qualification — lcov / gcov

## Identification

| Field          | Value                                                       |
| -------------- | ----------------------------------------------------------- |
| Tool name      | gcov (gcc coverage collector) + lcov (lcov post-processor + summary tool) |
| Role           | Measures line, branch, and function coverage of the runtime test suite. Gates the coverage claim (line ≥ 99%, branch ≥ 85%, function = 100%). |
| Source         | gcov: distributed with gcc. lcov: Ubuntu 24.04 LTS (Noble) apt package `lcov`. |
| Pinned by      | The GitHub Actions runner image `ubuntu-latest`.            |
| Configuration  | `cmake -DZSERIO_C_COVERAGE=ON` triggers `--coverage -O0 -g` on the runtime; `lcov --capture --directory build-cov --rc lcov_branch_coverage=1`; gate enforced by `tools/check_coverage.py`. |
| License        | gcov: GPLv3 (with gcc runtime exception). lcov: GPLv2.      |

## TCL analysis (ISO 26262-8 §11.4.5)

| Axis                       | Classification | Rationale                                                                                                |
| -------------------------- | -------------- | -------------------------------------------------------------------------------------------------------- |
| Tool Impact (TI)           | **TI2**        | The coverage gate is one of the load-bearing safety claims. A false positive (over-reporting coverage) would let undertested code ship under the safety case. |
| Tool error Detection (TD)  | **TD2**        | gcov is deterministic (counters are per-line counts captured at runtime); under-reporting is detectable by spot-checking with `gcov -f` against expected test paths. Over-reporting requires gcov to count an unexecuted line, which has never been reported on the gcc/gcov branch we use. lcov's summary is a derived statistic; the gate uses lcov's raw `.info` data, not the summary, so summary bugs cannot affect the gate. |
| Derived **TCL**            | **TCL2**       | TI2 × TD2 per ISO 26262-8 Table 4.                                                                       |

## Use cases

1. Instrument the runtime build with `--coverage` so gcc emits
   `.gcno` (compile-time) and `.gcda` (runtime) coverage data.
2. Run `lcov --capture` to convert per-translation-unit gcov data
   into a single `.info` file.
3. Filter coverage to `runtime/src/*` only — test code is
   intentionally excluded from the safety-case coverage claim.
4. Run `tools/check_coverage.py` against the filtered `.info` to
   enforce thresholds (line ≥ 99%, branch ≥ 85%, function = 100%).
5. Honor `LCOV_EXCL_LINE` / `LCOV_EXCL_START` / `LCOV_EXCL_STOP`
   markers in source for documented unreachable code paths
   (defensive returns, etc.).

## Qualification approach (TCL2)

Method **(2) — evaluation of the tool development process** is
unavailable (lcov is a community tool without published ISO 26262
process evidence). We instead use methods **(3) — validation of
the software tool** and **(1) — increased confidence from use**:

- **Increased confidence from use.** gcov has been the de-facto
  coverage tool for gcc-compiled code since gcc 3.x (~2002). lcov
  is the standard wrapper used by every gcc-based coverage CI in
  open source. Both are integrated into commercial qualified
  toolchains.
- **Validation of the software tool.** Specific validation per
  property the gate depends on:

  | Property                                            | Validation                                                                                  |
  | --------------------------------------------------- | ------------------------------------------------------------------------------------------- |
  | Line counts equal actual executions                 | Spot-check: insert a known-untested branch, observe lcov correctly reports 0 hits.          |
  | Branch counts equal actual taken/not-taken          | Spot-check: tests for trait-driven dispatch (`test_delta_traits.c`) cover both branches of the traits-null check; lcov correctly attributes both branches. |
  | `LCOV_EXCL_*` markers honored                      | The three `LCOV_EXCL_LINE` lines in `varint.c` (130, 156, 209) are absent from the report despite being source-visible; verified per commit by inspecting the posted coverage report. |
  | Function counts equal actual entries                | Function coverage went from 70% to 100% when the trait wrappers were exercised (`test_delta_traits.c`); the delta exactly matched the number of newly-exercised functions. |

## Specific criteria and evidence

- **Coverage build is hermetic.** `build-cov/` is a separate
  build directory from `build/`. The instrumented runtime is not
  shipped; it is used only to produce coverage data.
- **Gate is deterministic.** `tools/check_coverage.py` returns a
  fixed exit code; the gate fails on any threshold miss.
- **Per-commit evidence.** The `coverage` CI job posts the full
  per-file coverage breakdown as a commit comment, so the
  measurement is preserved against the commit SHA.

## Known limitations

- **Branch coverage with optimization.** Branch coverage is
  taken at `-O0`; at higher optimization levels gcc may merge or
  hoist branches, changing the count. The gate uses `-O0 -g`
  specifically to keep the count comparable across commits.
- **gcov line attribution.** gcov attributes execution to the
  line containing the first instruction of a statement; multi-
  line statements may report a hit at a line that visually looks
  unexecuted. Spot-checking is needed when triaging a coverage
  gap.
- **No data-flow coverage.** lcov measures line + branch +
  function, not data-flow or condition coverage. The safety case
  does not claim those.

## Re-qualification triggers

1. An lcov version bump that changes the `.info` format or the
   threshold computation.
2. A gcc version bump that changes the gcov instrumentation
   (unlikely on the same gcc branch).
3. A new coverage threshold added to `tools/check_coverage.py`
   that uses a metric not validated above.

## Audit re-execution

```
.github/workflows/ci.yml → coverage job
```

The job posts the full per-file coverage report as a commit
comment. An auditor can reproduce the result locally with:

```
cmake -B build-cov -S . -G Ninja -DCMAKE_BUILD_TYPE=Debug \
    -DZSERIO_C_COVERAGE=ON -DZSERIO_C_WERROR=OFF
cmake --build build-cov
ctest --test-dir build-cov --output-on-failure
lcov --capture --directory build-cov --output-file coverage.info \
     --rc lcov_branch_coverage=1 --ignore-errors mismatch,unused,gcov
lcov --extract coverage.info '*/runtime/*' \
     --output-file coverage-runtime.info --rc lcov_branch_coverage=1
python3 tools/check_coverage.py coverage-runtime.info
```
