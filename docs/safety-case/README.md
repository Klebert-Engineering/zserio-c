# Safety case — `zserio-c`

This directory holds the ISO 26262 safety-case work products for
`zserio-c`. The parent `docs/safety-case.md` is the index of what
the safety case is *for*; this directory is what the safety case
*says*.

## Status

Phase 5 (safety-case maturation) is open. The launch commit
delivers the three foundational documents below; the remaining work
products are tracked in `safety-plan.md` § Work products.

| Document | Status | Purpose |
| --- | --- | --- |
| [item-definition.md](item-definition.md) | draft | What `zserio-c` is, what its boundaries are, what its assumed system context is. ISO 26262-3 §5. |
| [hazard-analysis.md](hazard-analysis.md) | draft | Catalogue of failure modes the item exposes, with mitigations and residual integrator responsibilities. ISO 26262-3 §6 (item-level). |
| [safety-plan.md](safety-plan.md) | draft | Meta-document: which work products will be produced, in what order, by whom. ISO 26262-2 §6. |
| [stack-budget.md](stack-budget.md) | draft | Stack-usage analysis closing hazard H-09; methodology, current numbers, and the threshold the CI `stack` job enforces. |
| [functional-safety-concept.md](functional-safety-concept.md) | draft | Safety goals derived from the HARA, mapped to existing `requirements/` entries and architecture layers. ISO 26262-3 §7. |
| [technical-safety-concept.md](technical-safety-concept.md) | draft | Per-goal implementation mechanisms, anchored to specific source files, tests, and CI gates. ISO 26262-4 §6. |

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
