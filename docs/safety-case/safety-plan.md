---
work_product: ISO 26262-2 §6 — Safety plan
status: draft
---

# Safety plan — `zserio-c`

## Purpose

Define how the `zserio-c` project produces and maintains the safety
case for ISO 26262 ASIL-B compliance, with architectural headroom
toward ASIL-D where the cost is symmetric. This plan is the meta-
document; the technical work products it commissions live in
sibling files in this directory and in `requirements/`.

## Target

- **Standard**: ISO 26262:2018 (parts 1, 2, 3, 6, 8, 9 are the
  primary references).
- **ASIL allocation**: ASIL-B at the item level. The item is intended
  to be used in channels up to ASIL-D, with the integrating system's
  HARA making the final allocation.
- **Coding standard**: MISRA C:2012 with amendments 1–4. Enforcement
  in CI: cppcheck `--addon=misra` invoked from `tools/run_misra.sh`,
  strict mode (`--strict`) so any new finding fails the build.
  Deviations live under `docs/misra-deviations/` and are tied 1:1 to
  entries in `tools/cppcheck-suppressions.txt`.

## Work products

| Status | Work product                                  | Location                                            | Responsible | Notes |
| ------ | --------------------------------------------- | --------------------------------------------------- | ----------- | ----- |
| done   | Architecture description                      | `docs/architecture.md`                              | Fabian Klebert       | Frozen for v1; phase-target 1 |
| done   | Safety requirements (REQ-FS-*)                | `requirements/safety/`                              | Fabian Klebert       | 8 items, all `accepted` |
| done   | Functional requirements (REQ-FN-*)            | `requirements/functional/`                          | Fabian Klebert       | REQ-FN-001 = wire compatibility |
| done   | Performance requirements (REQ-PF-*)           | `requirements/performance/`                         | Fabian Klebert       | REQ-PF-001 (perf gate), REQ-PF-002 (size gate) — both accepted |
| done   | Requirements traceability (`Verifies:` markers) | tests/* + tools/check_traceability.py             | Fabian Klebert       | CI-gated |
| done   | MISRA enforcement                             | tools/run_misra.sh + cppcheck-suppressions.txt      | Fabian Klebert       | CI-gated, strict mode, zero findings |
| done   | Coding rules                                  | `docs/coding-rules.md`                              | Fabian Klebert       | Frozen for v1 |
| done   | Coverage gate (line ≥ 99%, branch ≥ 85%, function = 100%) | `tools/check_coverage.py` + CI            | Fabian Klebert       | CI-gated; baseline currently 99.89% / 88.03% / 100% |
| done   | Size regression gate (REQ-PF-002)             | `tools/measure_size.py` + `tools/check_size_regression.py` | Fabian Klebert | CI-gated; 5% per-section threshold |
| done   | Wire-format conformance (REQ-FN-001)          | `tests/xvalidation/run_xvalidation.sh` + CI         | Fabian Klebert       | 182 schemas × 5 seeds per CI run; byte-equal vs upstream cpp |
| draft  | Item definition                               | `docs/safety-case/item-definition.md`               | Fabian Klebert       | This phase |
| draft  | Hazard analysis (HARA)                        | `docs/safety-case/hazard-analysis.md`               | Fabian Klebert       | This phase |
| draft  | Safety plan                                   | `docs/safety-case/safety-plan.md` (this document)   | Fabian Klebert       | This phase |
| done   | Functional safety concept                     | `docs/safety-case/functional-safety-concept.md`     | Fabian Klebert       | 9 safety goals, mapped to existing `requirements/` set and architecture layers; SG-9 (tool qualification) is the only goal not yet fully discharged |
| done   | Technical safety concept                      | `docs/safety-case/technical-safety-concept.md`      | Fabian Klebert       | Per-goal implementation mechanisms with mechanism-to-gate matrix; moves to `accepted` when tool-qualification records land |
| done   | Software unit verification report             | `docs/safety-case/unit-verification.md`             | Fabian Klebert       | Per-runtime-module evidence: coverage, MISRA, stack, size, traceability. Snapshot pattern; per-release versions replace snapshot tables. |
| done   | Software integration and verification report  | `docs/safety-case/integration-verification.md`      | Fabian Klebert       | xvalidation (182 schemas × 5 seeds) + private NDS.Live companion conformance harness end-to-end. |
| done   | Tool qualification — compiler                 | `docs/safety-case/tool-qualification-gcc.md`        | Fabian Klebert       | TCL2; method (1) confidence-from-use + (3) validation; validated by `build-and-test`, `xvalidation`, `stack`, `coverage` gates |
| done   | Tool qualification — cppcheck                 | `docs/safety-case/tool-qualification-cppcheck.md`   | Fabian Klebert       | TCL2; method (3) validation per rule category; one off-CI sanity check (Rule 17.2 recursive program) noted for follow-up |
| done   | Tool qualification — lcov / gcov              | `docs/safety-case/tool-qualification-coverage.md`   | Fabian Klebert       | TCL2; method (1) + (3); gate uses raw .info so summary bugs cannot affect outcome |
| done   | Tool qualification — zserio-c generator       | `docs/safety-case/tool-qualification-generator.md`  | Fabian Klebert       | TCL1 (TI2 × TD1 due to xvalidation's strong detection); MISRA gate on generator output landed in the `misra-generator` CI job (commit `c13bd41`) |
| done   | Stack-budget analysis                         | `tools/measure_stack_usage.py` + `docs/safety-case/stack-budget.md` + `stack` CI job | Fabian Klebert | Closes hazard H-09; per-function ≤ 256 B AND transitive call-chain ≤ 1024 B, plus the static-qualifier strict check |
| done   | Failure-mode injection tests                  | `tests/runtime/test_failure_modes.c`                | Fabian Klebert       | One test per actionable HARA row (H-01, H-02, H-03, H-06); others handled by other gates or out of scope |
| done   | Decode-path fuzzing (REQ-FS-006)              | `tools/run_fuzz.sh` + `tests/fuzz/` + `fuzz` CI job | Fabian Klebert       | libFuzzer + ASan + UBSan, two representative targets, 60 s each per commit, persistent corpus via actions/cache |
| done   | Safety case report                            | `docs/safety-case/safety-case-report.md`            | Fabian Klebert       | Single entry document for a reviewer. Assembles the argument from item definition through HARA → SG/FSC/TSC → verification → TQ. |

## Sequencing

Deliverables, in dependency order:

1. **Done in the safety-case launch commit:** item definition, HARA,
   this plan.
2. **Done after that:** stack-budget analysis —
   `tools/measure_stack_usage.py`, the `stack` CI gate, and
   `docs/safety-case/stack-budget.md`. Closes hazard H-09.
3. **Done after that:** failure-mode injection tests —
   `tests/runtime/test_failure_modes.c`. One test per actionable
   HARA row (H-01, H-02, H-03, H-06); others are handled by
   existing gates (H-07: MISRA, H-08: xvalidation, H-09: stack)
   or are explicitly out of scope at the item level (H-04, H-05,
   H-10).
4. **Done after that:** functional + technical safety concepts
   — `functional-safety-concept.md` (9 safety goals SG-1..SG-9
   mapped to existing requirements) and `technical-safety-concept.md`
   (per-goal mechanisms with a single mechanism-to-gate matrix
   covering every claim).
5. **Done after that:** the four tool-qualification records —
   gcc (TCL2), cppcheck (TCL2), lcov/gcov (TCL2), and the
   `zserio-c` generator (TCL1). Each names its TI/TD/TCL
   classification and the validation evidence; open gaps within
   each record are recorded inline (e.g. generator-output MISRA
   gate). SG-9 from `functional-safety-concept.md` is now
   discharged in `draft` status.
6. **Done after that:** the two verification reports —
   `unit-verification.md` (per-runtime-module evidence) and
   `integration-verification.md` (xvalidation + the private
   NDS.Live companion conformance harness). Each is taken as a
   snapshot against `main` HEAD;
   per-release versions replace the snapshot tables.
7. **Done after that:** the safety case report — `safety-case-report.md`.
   The safety case is structurally complete; every work product is at
   `draft`. With every internal Open item in
   `safety-case-report.md` § Open items now closed, acceptance is
   gated only on the case-wide independent reviewer sign-off.

The completion gate is: every row in the work
product table above is at status `done` or has an explicit
"deferred to v2" disposition recorded in the safety case report.

## Process commitments

- Every commit that touches `runtime/`, `generator/`, or
  `requirements/` must keep all CI gates green (build, MISRA,
  coverage, size, xvalidation, traceability). The MISRA and
  xvalidation gates are the load-bearing safety claims.
- Every new MISRA deviation gets a `D<NNN>-*.md` file under
  `docs/misra-deviations/` and a corresponding entry in
  `tools/cppcheck-suppressions.txt`. Suppressions without a
  deviation document are forbidden by review.
- Every new safety requirement begins at status `proposed`. It moves
  to `accepted` only after the verification mechanism is identified
  and a `Verifies:` marker exists in the test / tool that performs
  the check.
- Schedule for re-running the full safety case: every release tag
  (planned: at the close of each phase boundary).

## Out of scope for this plan

- Vehicle-level HARA (system integrator's responsibility).
- Production-toolchain selection. The CI configuration documents
  the development toolchain; the integrator's qualified toolchain
  is referenced from the tool-qualification records, not pinned by
  this plan.
- Field operation and decommissioning (item is a library, not a
  service).
