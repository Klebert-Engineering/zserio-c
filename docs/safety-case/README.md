# Safety case — `zserio-c`

This directory holds the ISO 26262 safety-case work products for
`zserio-c`. The parent `docs/safety-case.md` is the index of what
the safety case is *for*; this directory is what the safety case
*says*.

## Status

The safety case is structurally complete: every
work product listed in `safety-plan.md` § Work products is at
`draft` status, and every internal Open item recorded in
`safety-case-report.md` § Open items is now closed. The case as a
whole moves to `accepted` when the integration verification is
replayed against the next release tag and the case-wide independent
reviewer sign-off recorded in `safety-case-report.md` § Sign-off
is in.

| Document | Status | Purpose |
| --- | --- | --- |
| [item-definition.md](item-definition.md) | draft | What `zserio-c` is, what its boundaries are, what its assumed system context is. ISO 26262-3 §5. |
| [hazard-analysis.md](hazard-analysis.md) | draft | Catalog of failure modes the item exposes, with mitigations and residual integrator responsibilities. ISO 26262-3 §6 (item-level). |
| [safety-plan.md](safety-plan.md) | draft | Meta-document: which work products will be produced, in what order, by whom. ISO 26262-2 §6. |
| [stack-budget.md](stack-budget.md) | draft | Stack-usage analysis closing hazard H-09; methodology, current numbers, and the threshold the CI `stack` job enforces. |
| [functional-safety-concept.md](functional-safety-concept.md) | draft | Safety goals derived from the HARA, mapped to existing `requirements/` entries and architecture layers. ISO 26262-3 §7. |
| [technical-safety-concept.md](technical-safety-concept.md) | draft | Per-goal implementation mechanisms, anchored to specific source files, tests, and CI gates. ISO 26262-4 §6. |
| [tool-qualification-gcc.md](tool-qualification-gcc.md) | draft | TCL2 qualification analysis for gcc (production compiler). ISO 26262-8 §11. |
| [tool-qualification-cppcheck.md](tool-qualification-cppcheck.md) | draft | TCL2 qualification analysis for cppcheck (MISRA enforcement). ISO 26262-8 §11. |
| [tool-qualification-coverage.md](tool-qualification-coverage.md) | draft | TCL2 qualification analysis for lcov / gcov (coverage gate). ISO 26262-8 §11. |
| [tool-qualification-generator.md](tool-qualification-generator.md) | draft | TCL1 qualification analysis for the zserio-c generator. ISO 26262-8 §11. |
| [unit-verification.md](unit-verification.md) | draft | Per-runtime-module evidence: coverage, MISRA, stack, size, traceability. ISO 26262-6 §9. |
| [integration-verification.md](integration-verification.md) | draft | Wire-format evidence: xvalidation (183 schemas × 5 seeds) + supplementary end-to-end evidence from the private NDS.Live companion conformance harness. ISO 26262-6 §10. |
| [safety-case-report.md](safety-case-report.md) | draft | Final assembly. The single entry document for a reviewer. ISO 26262-2 §6.4. |

Every document has a YAML frontmatter `status` field. `draft` means
the document is structurally complete and reviewable but has not
yet been signed off; `accepted` means it is the authoritative version.

## How to consume this directory

- **Integrators**: start with `item-definition.md` for the boundary
  of what `zserio-c` is and is not, then `hazard-analysis.md` for
  the failure modes you must allocate to other layers.
- **Reviewers**: `safety-plan.md` is the navigation guide; every
  open work product is listed there with its location and
  dependencies.
- **Developers**: see `requirements/safety/` for the load-bearing
  safety requirements (REQ-FS-001 through REQ-FS-008) that
  every code change must continue to satisfy.
