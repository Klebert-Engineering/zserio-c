---
id: REQ-FN-011
title: All-or-nothing emission semantics
type: functional
status: accepted
verification: test
---

## Statement

On a successful generator invocation, the generator SHALL exit
with status zero and SHALL have written, to `-c <output-dir>`,
the complete set of files corresponding to the input schemas.

On a failed generator invocation, the generator SHALL:

1. Emit at least one diagnostic at severity `error` (per
   [REQ-FN-003](REQ-FN-003.md) and [REQ-FN-004](REQ-FN-004.md)
   where the failure has a source location, or a tool-level
   message otherwise).
2. Exit with non-zero status.
3. NOT claim, by its exit status alone, that the partial files
   in `-c <output-dir>` constitute valid generator output.

The generator MAY leave partial output behind on failure. It
SHALL NOT attempt to roll back or clean up partial output (such
cleanup is the build system's responsibility, not the
generator's).

A subsequent successful invocation with the same arguments
against the same output directory SHALL overwrite partial
output cleanly and produce the complete expected file set.

## Rationale

The exit-status / stderr / output-tree-state contract is the
build system's only signal of whether to trust the output. By
binding "trust output" exclusively to "exit zero with no
errors", a Makefile or CMake stanza can decide what to do
without inspecting individual files.

Not promising rollback on failure is a deliberate scope
limit: rollback is hard to do correctly under signal
interruption and across crashes, and it provides a false
guarantee. The build system already knows how to clean
intermediate directories.

The "subsequent successful run overwrites cleanly" clause is
what makes the generator safe to re-run after a fix. Without
it, a stale leftover file from a failed prior run could end up
in the next successful build, with no diagnostic.

## Verification

- `tests/parser/test_all_or_nothing.c` exercises:
  - Successful run, verify exit zero and full output set
    present.
  - Run with a malformed schema, verify exit non-zero and at
    least one error diagnostic.
  - Run against a directory pre-populated with stale files, then
    successful run, verify the new output matches the schema.
- The CI gate runs the generator with `set -e` semantics; any
  failure path that leaves exit zero would manifest as a
  silent test pass against expected-failure inputs and would
  be caught by the suite's negative tests.

## Notes

This requirement is the *generator-level* counterpart of the
runtime's [REQ-FS-006](../safety/REQ-FS-006.md) input
validation: both establish that a defined exit / status code is
the only authority for downstream consumers.

Related: [REQ-FN-007](REQ-FN-007.md) (determinism) implies that
a re-run will produce the same output, but only on inputs the
parser accepts. This requirement covers the failure path that
determinism does not.
