---
work_product: ISO 26262-2 §6 — Safety plan
status: draft
phase_target: 5
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
| done   | Architecture description                      | `docs/architecture.md`                              | Fritz       | Frozen for v1; phase-target 1 |
| done   | Safety requirements (REQ-FS-*)                | `requirements/safety/`                              | Fritz       | 8 items, all `accepted` |
| done   | Functional requirements (REQ-FN-*)            | `requirements/functional/`                          | Fritz       | REQ-FN-001 = wire compatibility |
| done   | Performance requirements (REQ-PF-*)           | `requirements/performance/`                         | Fritz       | REQ-PF-001 (phase 6), REQ-PF-002 (size gate, accepted) |
| done   | Requirements traceability (`Verifies:` markers) | tests/* + tools/check_traceability.py             | Fritz       | CI-gated |
| done   | MISRA enforcement                             | tools/run_misra.sh + cppcheck-suppressions.txt      | Fritz       | CI-gated, strict mode, zero findings |
| done   | Coding rules                                  | `docs/coding-rules.md`                              | Fritz       | Frozen for v1 |
| done   | Coverage gate (line ≥ 99%, branch ≥ 85%, function = 100%) | `tools/check_coverage.py` + CI            | Fritz       | CI-gated; baseline currently 99.89% / 88.03% / 100% |
| done   | Size regression gate (REQ-PF-002)             | `tools/measure_size.py` + `tools/check_size_regression.py` | Fritz | CI-gated; 5% per-section threshold |
| done   | Wire-format conformance (REQ-FN-001)          | `tests/xvalidation/run_xvalidation.sh` + CI         | Fritz       | 182 schemas × 5 seeds per CI run; byte-equal vs upstream cpp |
| draft  | Item definition                               | `docs/safety-case/item-definition.md`               | Fritz       | This phase |
| draft  | Hazard analysis (HARA)                        | `docs/safety-case/hazard-analysis.md`               | Fritz       | This phase |
| draft  | Safety plan                                   | `docs/safety-case/safety-plan.md` (this document)   | Fritz       | This phase |
| open   | Functional safety concept                     | `docs/safety-case/functional-safety-concept.md`     | TBD         | Depends on item definition + HARA |
| open   | Technical safety concept                      | `docs/safety-case/technical-safety-concept.md`      | TBD         | Maps HARA mitigations to runtime/generator architecture |
| open   | Software unit verification report             | `docs/safety-case/unit-verification.md`             | TBD         | Assembles coverage + MISRA + traceability evidence per release |
| open   | Software integration and verification report  | `docs/safety-case/integration-verification.md`      | TBD         | Records xvalidation + NDS Live SmartLayerTile end-to-end |
| open   | Tool qualification — compiler                 | `docs/safety-case/tool-qualification-gcc.md`        | TBD         | TCL classification + criteria evidence |
| open   | Tool qualification — cppcheck                 | `docs/safety-case/tool-qualification-cppcheck.md`   | TBD         | Static analyzer for MISRA enforcement |
| open   | Tool qualification — lcov / gcov              | `docs/safety-case/tool-qualification-coverage.md`   | TBD         | Coverage measurement tool |
| open   | Tool qualification — zserio-c generator       | `docs/safety-case/tool-qualification-generator.md`  | TBD         | The generator is on the safety path because it produces certified code |
| open   | Stack-budget analysis                         | `tools/measure_stack_usage.py` + `docs/safety-case/stack-budget.md` | TBD | Hazard H-09; needs `-Wstack-usage` build + per-schema worst-case calculation |
| open   | Failure-mode injection tests                  | `tests/runtime/test_negative_*.c`                   | TBD         | One test per HARA row that asserts the documented mitigation fires |
| open   | Safety case report                            | `docs/safety-case/safety-case-report.md`            | TBD         | Final assembly of the above into one argument |

## Sequencing

Phase 5 deliverables, in dependency order:

1. **Done in the Phase 5 launch commit:** item definition, HARA,
   this plan.
2. **Next:** functional + technical safety concepts (one document each,
   building on the HARA's mitigation column).
3. **Then:** stack-budget analysis (closes hazard H-09, needed before
   the technical safety concept can be marked complete).
4. **Then:** failure-mode injection tests (per HARA row).
5. **Then:** the four tool-qualification records, in parallel.
6. **Finally:** the two verification reports + the safety case
   report.

The Phase 5 → Phase 6 transition gate is: every row in the work
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
