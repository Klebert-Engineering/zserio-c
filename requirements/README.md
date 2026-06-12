# Requirements

This directory holds the requirements that drive the design and verification
of `zserio-c`. Requirements are first-class citizens of the repository:
the safety case stands on them, the verification plan refers to them by ID,
and CI fails when one is unreferenced by tests.

## Directory layout

```
requirements/
├── README.md          (this file)
├── domain/            REQ-DM-* — what the system is for, independent of implementation
├── functional/        REQ-FN-* — what the software does (parser, generator, runtime)
├── safety/            REQ-FS-* — properties demanded by ISO 26262 / MISRA
├── performance/       REQ-PF-* — non-functional, measurable performance targets
└── traceability.csv   REQ → test → status, regenerable from the test suite
```

## ID scheme

`REQ-<CATEGORY>-<NNN>` where:

- `<CATEGORY>` is `DM` (domain), `FN` (functional), `FS` (safety),
  or `PF` (performance).
- `<NNN>` is a zero-padded three-digit number, allocated sequentially within
  the category. **IDs are append-only** — once published, an ID is never
  reused or renumbered, even after the requirement is withdrawn.

A withdrawn requirement keeps its file with `status: withdrawn` and a
deprecation note.

## Layered requirement model

`zserio-c` uses three layers of requirements, increasing in
specificity:

| Layer | Category | Question it answers |
|---|---|---|
| Domain | `REQ-DM-*` | What is the system for, abstractly? |
| Functional | `REQ-FN-*` | What does each component (parser, generator, runtime, generated code) do? |
| Safety / Performance | `REQ-FS-*`, `REQ-PF-*` | What constraints / invariants / targets must everything observe? |

A safety reviewer reads domain first, then functional, then
safety/performance. Each layer cites the layer above.

## Index

### Domain (`REQ-DM-*`)

| ID | Title |
|---|---|
| [REQ-DM-001](domain/REQ-DM-001.md) | System purpose — schema-driven binary serialization |
| [REQ-DM-002](domain/REQ-DM-002.md) | Encode and decode are mutual inverses |
| [REQ-DM-003](domain/REQ-DM-003.md) | Canonical encoding |
| [REQ-DM-004](domain/REQ-DM-004.md) | Schema-language reference is owned by zserio-c |
| [REQ-DM-005](domain/REQ-DM-005.md) | Wire-format reference is owned by zserio-c |
| [REQ-DM-006](domain/REQ-DM-006.md) | Schema evolution constructs |
| [REQ-DM-007](domain/REQ-DM-007.md) | Versioning policy for schema language and wire format |

### Functional — parser (`REQ-FN-002..006`)

| ID | Title |
|---|---|
| [REQ-FN-002](functional/REQ-FN-002.md) | Parser accepts the schema language defined in docs/spec/schema-language.md |
| [REQ-FN-003](functional/REQ-FN-003.md) | Diagnostic message format |
| [REQ-FN-004](functional/REQ-FN-004.md) | Diagnostics carry precise source locations |
| [REQ-FN-005](functional/REQ-FN-005.md) | Import resolution rules |
| [REQ-FN-006](functional/REQ-FN-006.md) | Import-graph cycles are detected and rejected |

### Functional — generator (`REQ-FN-007..014`)

| ID | Title |
|---|---|
| [REQ-FN-007](functional/REQ-FN-007.md) | Generator output is deterministic across runs |
| [REQ-FN-008](functional/REQ-FN-008.md) | Output file layout mirrors schema package hierarchy |
| [REQ-FN-009](functional/REQ-FN-009.md) | Schema-to-C identifier mapping is total, deterministic, and documented |
| [REQ-FN-010](functional/REQ-FN-010.md) | Identifier-length cap with deterministic hash truncation |
| [REQ-FN-011](functional/REQ-FN-011.md) | All-or-nothing emission semantics |
| [REQ-FN-012](functional/REQ-FN-012.md) | Cross-package include qualification |
| [REQ-FN-013](functional/REQ-FN-013.md) | Template instantiation |
| [REQ-FN-014](functional/REQ-FN-014.md) | -setTopLevelPackage prefix propagation |

### Functional — runtime + generated code (`REQ-FN-015..026`)

| ID | Title |
|---|---|
| [REQ-FN-015](functional/REQ-FN-015.md) | Generated-code lifecycle entry points |
| [REQ-FN-016](functional/REQ-FN-016.md) | Encoder performs no heap allocation |
| [REQ-FN-017](functional/REQ-FN-017.md) | Decoder allocates only via the supplied allocator vtable |
| [REQ-FN-018](functional/REQ-FN-018.md) | _free is idempotent and safe on partial state |
| [REQ-FN-019](functional/REQ-FN-019.md) | _free returns the struct to post-_init state |
| [REQ-FN-020](functional/REQ-FN-020.md) | Output validity conditioned on ZSERIO_OK status |
| [REQ-FN-021](functional/REQ-FN-021.md) | Canonical encoding (struct values to bytes) |
| [REQ-FN-022](functional/REQ-FN-022.md) | Round-trip byte equality on valid bitstreams |
| [REQ-FN-023](functional/REQ-FN-023.md) | Wire-format fields are emitted in schema source order |
| [REQ-FN-024](functional/REQ-FN-024.md) | Parameter propagation in nested types |
| [REQ-FN-025](functional/REQ-FN-025.md) | extend fields default to zero on short bitstreams |
| [REQ-FN-026](functional/REQ-FN-026.md) | Schema constraints enforced on the read path |

### Functional — interoperability (`REQ-FN-001`)

| ID | Title |
|---|---|
| [REQ-FN-001](functional/REQ-FN-001.md) | Interoperability with upstream zserio v2.18.1 |

### Safety (`REQ-FS-*`)

| ID | Title |
|---|---|
| [REQ-FS-001](safety/REQ-FS-001.md) | No direct heap allocation |
| [REQ-FS-002](safety/REQ-FS-002.md) | No non-local control flow |
| [REQ-FS-003](safety/REQ-FS-003.md) | Bounded decode depth |
| [REQ-FS-004](safety/REQ-FS-004.md) | No floating-point in control flow |
| [REQ-FS-005](safety/REQ-FS-005.md) | All loops have a statically determinable upper bound |
| [REQ-FS-006](safety/REQ-FS-006.md) | Public-API input validation at the boundary |
| [REQ-FS-007](safety/REQ-FS-007.md) | Freedom from interference between codec instances |
| [REQ-FS-008](safety/REQ-FS-008.md) | No runtime reflection in generated code |

### Performance (`REQ-PF-*`)

| ID | Title |
|---|---|
| [REQ-PF-001](performance/REQ-PF-001.md) | Throughput at least matching upstream C++ runtime |
| [REQ-PF-002](performance/REQ-PF-002.md) | Tracked code size, regression-gated |

## File format

One markdown file per requirement, named exactly `REQ-<CATEGORY>-<NNN>.md`,
with this structure:

```markdown
---
id: REQ-FS-001
title: <one-line title>
type: safety
status: accepted | proposed | withdrawn
phase_target: <phase number this must be enforceable by>
verification: test | review | analysis | demonstration
---

## Statement

The system SHALL …  (RFC 2119 SHALL/SHOULD/MAY language).

## Rationale

Why this requirement exists. Cite the standard, the incident, the design
constraint, or the upstream pain point that motivates it.

## Verification

How we know the requirement holds. Name the test ID, the analysis tool,
or the review checklist item.

## Notes

Anything else useful — known interactions with other requirements,
assumptions, open questions.
```

## Traceability

Every requirement must be referenced by at least one test, by ID, in a
comment of the form:

```c
/* Verifies: REQ-FS-001 */
```

The `traceability` job in `.github/workflows/ci.yml` runs
`tools/check_traceability.py`, which scans the test sources for these
markers and the requirement files for their IDs, and fails CI if any
in-phase requirement has no test referencing it (excluding `proposed`
and `withdrawn` entries).

The `traceability.csv` file is regenerated by
`tools/check_traceability.py --write-csv`; the CI job runs it and
fails if the committed file has drifted. Regenerate and commit it
whenever you add or move a `Verifies:` marker. The `last_verified`
column is intentionally empty — the file's git history carries the
timestamps.

## Process

- New requirements start with `status: proposed`.
- A requirement becomes `accepted` only after review (recorded in the PR
  that introduces it).
- A requirement is `withdrawn` only via a PR that documents what
  superseded it.
- Editing the **statement** of an `accepted` requirement is only allowed
  for clarification (no semantic change). Semantic changes require a new
  requirement that supersedes the old one.
