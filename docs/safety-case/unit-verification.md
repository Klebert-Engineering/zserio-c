---
work_product: ISO 26262-6 §9 — Software unit verification report
status: draft
---

# Software unit verification report — `zserio-c`

## Purpose

Collates the per-unit evidence that the runtime library satisfies
its allocated functional, safety, and performance requirements.
This report is taken as a **snapshot against `main` HEAD**; per-
release reports are derived by running the same gates against the
release tag and replacing the numbers below.

The runtime is the unit boundary the safety case treats as
verifiable: every `runtime/src/*.c` file plus its companion header
in `runtime/include/zserio/`. Generated codec sources are verified
at the integration level — see `integration-verification.md`.

## Verification method per source file

For each runtime translation unit, the table below names the
dedicated unit-test file, the requirements that test file
references via `Verifies:` markers, and the relevant safety goal
from `functional-safety-concept.md`.

| Source file                 | Unit test                                 | Verified requirements                 | Discharges safety goal       |
| --------------------------- | ----------------------------------------- | ------------------------------------- | ---------------------------- |
| `runtime/src/allocator.c`   | `tests/runtime/test_allocator.c`          | REQ-FS-001                            | SG-3                         |
| `runtime/src/bitstream.c`   | `tests/runtime/test_bitstream.c`          | REQ-FS-005, REQ-FS-006                | SG-1, SG-2, SG-5             |
| `runtime/src/bytes.c`       | `tests/runtime/test_bytes.c`              | REQ-FS-001, REQ-FS-006                | SG-1, SG-3                   |
| `runtime/src/delta.c`       | `tests/runtime/test_delta.c`, `test_delta_traits.c` | REQ-FS-006, REQ-FN-001     | SG-1, SG-2, SG-7             |
| `runtime/src/extern.c`      | `tests/runtime/test_extern.c`             | REQ-FS-001, REQ-FS-006                | SG-1, SG-3                   |
| `runtime/src/float.c`       | `tests/runtime/test_float.c`              | REQ-FS-004, REQ-FS-006                | SG-6                         |
| `runtime/src/primitives.c`  | `tests/runtime/test_primitives.c`         | REQ-FS-006                            | SG-1                         |
| `runtime/src/string.c`      | `tests/runtime/test_string.c`             | REQ-FS-001, REQ-FS-006                | SG-1, SG-3                   |
| `runtime/src/varint.c`      | `tests/runtime/test_varint.c`             | REQ-FS-005, REQ-FS-006, REQ-FN-001    | SG-1, SG-2, SG-7             |
| Cross-cutting               | `tests/runtime/test_failure_modes.c`      | REQ-FS-001, REQ-FS-005, REQ-FS-006, REQ-FS-007 | SG-1, SG-2, SG-3, SG-5 (one test per actionable HARA row) |

## Coverage measurement (snapshot against `main`)

| Metric             | Result | Threshold | Margin |
| ------------------ | -----: | --------: | -----: |
| Line coverage      | (see latest `coverage` CI job comment) | ≥ 99.0% | — |
| Branch coverage    | (see latest `coverage` CI job comment) | ≥ 85.0% | — |
| Function coverage  | (see latest `coverage` CI job comment) | = 100%  | — |

The CI `coverage` job posts a per-file breakdown as a commit
comment on every push. The last green run on `main` is the
authoritative coverage record for this report.

## Static analysis (snapshot against `main`)

| Check                              | Result                                       | Threshold |
| ---------------------------------- | -------------------------------------------- | --------- |
| cppcheck MISRA strict              | (see latest `misra` CI job comment)          | 0 findings |
| Forbidden-symbol grep (REQ-FS-001) | (see latest `misra` CI job comment)          | 0 hits    |
| Forbidden-symbol grep (REQ-FS-002) | (see latest `misra` CI job comment)          | 0 hits    |

## Stack budget (snapshot against `main`)

| Metric                          | Result                                       | Threshold |
| ------------------------------- | -------------------------------------------- | --------- |
| Max per-function frame          | (see latest `stack` CI job comment)          | ≤ 256 B   |
| Functions with `static` qualifier | All                                          | All       |
| Functions with `dynamic`/`bounded` | None                                       | 0         |

See `stack-budget.md` for the analysis methodology and the
threshold rationale.

## Size budget (snapshot against `main`)

| Section   | Current (bytes) | Baseline | Δ % | Gated |
| --------- | --------------: | -------: | --: | ----- |
| `.text`   | (see `size` CI comment) | `docs/benchmarks/sizes.baseline.json` | ≤ +5% | yes |
| `.rodata` | (see `size` CI comment) | same | ≤ +5% | yes |
| `.data`   | 0 | 0 | 0 | yes |
| `.bss`    | 0 | 0 | 0 | yes |

## Requirements traceability (snapshot against `main`)

`tools/check_traceability.py` runs as a CI gate. Every
`accepted` requirement must
be referenced by at least one `Verifies:` marker. Output of the
last green run is preserved in the `traceability` job log; key
counts:

- `accepted` requirements at phase ≤ 4: 10 (8 × REQ-FS-, 1 × REQ-FN-, 1 × REQ-PF-).
- Test files referencing requirements: 18 (one per runtime test
  file plus the cross-cutting test_failure_modes.c plus the four
  tools/run_misra.sh/run_xvalidation.sh markers).
- Pending requirements: none — every accepted requirement is gated.

## How to reproduce this report

For any commit SHA `<SHA>`:

```
.github/workflows/ci.yml runs on every push to main
gh run list --branch main --limit 10
gh run view <run-id> --json jobs
gh api "repos/<owner>/<repo>/commits/<SHA>/comments"
```

The CI commit comments preserve the exact numbers for coverage,
size, stack, and MISRA. Combine those with the snapshot tables
above to produce a per-release version of this report.

## Cross-references

- Functional safety concept: `functional-safety-concept.md`
- Technical safety concept: `technical-safety-concept.md`
- Integration verification: `integration-verification.md`
- Stack-budget analysis: `stack-budget.md`
- HARA: `hazard-analysis.md`
- Requirements: `../../requirements/`
