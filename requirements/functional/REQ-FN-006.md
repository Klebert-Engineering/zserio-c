---
id: REQ-FN-006
title: Import-graph cycles are detected and rejected
type: functional
status: accepted
phase_target: 1
verification: test
---

## Statement

The parser SHALL maintain, while loading imports, a directed
graph of import relationships among schema files. If at any
point the graph would contain a cycle, the parser SHALL:

1. Emit at least one diagnostic at severity `error` naming the
   file that would close the cycle and at least one other file
   on the cycle, per the format and location requirements of
   [REQ-FN-003](REQ-FN-003.md) and [REQ-FN-004](REQ-FN-004.md).
2. Halt further parsing without entering recursion or unbounded
   work.
3. Exit non-zero, emitting no generator output.

A cycle is any sequence `f0 → f1 → … → fn → f0` where `fi → fi+1`
denotes that `fi` contains an `import` resolved (per
[REQ-FN-005](REQ-FN-005.md)) to `fi+1`. Self-imports (`f → f`)
are degenerate cycles and SHALL be rejected under this
requirement.

## Rationale

Without cycle detection a malformed schema set causes the parser
to recurse indefinitely or loop until memory is exhausted; both
are unacceptable outcomes for a tool that may run in CI or in an
integrator's build. Detection at load time is also a
precondition for [REQ-FS-003](../safety/REQ-FS-003.md) (no
recursion): the import loader uses iterative work-queue
processing, and detection is what stops the work queue.

The schema language has no construct whose meaning depends on
mutually recursive imports — every legitimate cross-file
dependency is acyclic when imports are considered as a graph on
*files* (regardless of which symbols within those files are
referenced). The requirement codifies this: cycles are always
schema bugs.

## Verification

- `tests/parser/test_import_cycle.c` exercises:
  - Direct self-import.
  - Two-file cycle.
  - N-file cycle (N = 5).
  - Diamond (non-cycle, accept case) as a negative control.
- The CI gate fails on any of the cycle inputs producing
  generator output.

## Notes

This requirement is the *load-time* counterpart of recursion
forbidden at the *runtime* layer
([REQ-FS-003](../safety/REQ-FS-003.md)). The parser implements
its load loop iteratively; cycle detection is the gate on the
worklist.

Mutually recursive *types* within a single schema or across
imports are a different concept entirely and ARE permitted (a
struct containing a pointer-like field of its own type, etc.).
Type-level recursion is processed by the runtime via iterative
encode/decode with caller-supplied workspace, per
[REQ-FS-003](../safety/REQ-FS-003.md). This requirement is
strictly about file-level imports.
