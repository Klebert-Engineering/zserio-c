---
id: REQ-FN-006
title: Import-graph cycles cause bounded work
type: functional
status: accepted
phase_target: 1
verification: test
---

## Statement

The parser SHALL process the schema-file import graph using
bounded work regardless of cycles in that graph. Specifically,
each schema file SHALL be parsed at most once per generator
invocation (identity by canonical absolute path), and a second
import that resolves to an already-loaded file SHALL be a no-op
on the load worklist.

The consequence is that import cycles — including self-imports
(`f → f`) and diamond shapes — are handled by deduplication
rather than by emission of an error. A cycle is permitted by
the parser; its only observable effect is that the involved
files are loaded once each, not multiple times.

The parser SHALL NOT recurse indefinitely or allocate unboundedly
on any import-graph input, regardless of cycle topology.

## Rationale

Diamond imports (`A→B`, `A→C`, `B→D`, `C→D`) are legitimate and
common — `D` should be loaded once, not twice. The same
"load-once" behaviour that handles diamonds also tolerates
cycles harmlessly, with no downside: a cycle does not introduce
any wire-format or semantic-analysis ambiguity at the file
level (file-level imports are name-resolution hints, not
dependency assertions).

Forbidding cycles would force the parser to distinguish "cycle"
from "diamond" in the graph, which is more work than the
deduplication strategy and offers no benefit. The bounded-work
property — which is the real safety concern — is satisfied by
the parse-once invariant alone.

This requirement is the *load-time* counterpart of recursion
forbidden at the *runtime* layer
([REQ-FS-003](../safety/REQ-FS-003.md)). The parser implements
its load loop iteratively; the parse-once invariant is the gate
on the worklist.

The schema language has no construct whose meaning depends on
mutually recursive imports — every legitimate cross-file
dependency is acyclic when imports are considered as a graph on
*files* (regardless of which symbols within those files are
referenced). The requirement codifies this: cycles are always
schema bugs.

## Verification

- `tests/parser/test_session.c` exercises a two-file cycle
  `A→B`, `B→A` and asserts:
  - The session loads exactly two files (no infinite
    recursion, no duplicate loads).
  - The load completes with `ZSERIO_OK`.
- Diamond imports are exercised across the
  cross-validation harness corpus (many schemas share an
  imported common-types file).

## Notes

Mutually recursive *types* within a single schema or across
imports are a different concept entirely and ARE permitted (a
struct containing a pointer-like field of its own type, etc.).
Type-level recursion is processed by the runtime via iterative
encode/decode with caller-supplied workspace, per
[REQ-FS-003](../safety/REQ-FS-003.md). This requirement is
strictly about file-level imports.

If a future requirement emerges to *diagnose* cycles (e.g.
to help schema authors find unintentional dependency loops),
a separate requirement should add the diagnostic emission as
an additional behaviour, not a replacement for the
parse-once-by-canonical-path invariant defined here.
