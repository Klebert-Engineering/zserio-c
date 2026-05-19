---
id: REQ-FN-014
title: -setTopLevelPackage prefix propagation
type: functional
status: accepted
phase_target: 1
verification: test
---

## Statement

When the generator is invoked with the `-setTopLevelPackage <pkg>`
flag, where `<pkg>` is a non-empty dotted name of valid schema
identifiers, every loaded unit's `package` declaration SHALL be
treated, for the duration of that invocation, as though it
were prefixed with `<pkg>.`.

The prefix application SHALL be uniform: every unit, including
imported units, SHALL receive the same prefix. The prefix SHALL
NOT affect the import resolution algorithm
([REQ-FN-005](REQ-FN-005.md)) — search paths under `-src` roots
are computed against the unmodified import declarations.

The prefix SHALL flow through to:

1. The output file paths under `-c <output-dir>`
   ([REQ-FN-008](REQ-FN-008.md)).
2. The identifier prefixes for emitted symbols
   ([REQ-FN-009](REQ-FN-009.md)).
3. The cross-package `#include` paths
   ([REQ-FN-012](REQ-FN-012.md)).

When `<pkg>` is omitted or empty, the generator SHALL behave as
though no prefix were applied.

The flag SHALL be specified at most once per invocation; a
second occurrence SHALL be rejected with a diagnostic.

## Rationale

The use case is identifier collision: a schema declares
`package index;` and produces C code whose
`<index>/<symbol>` namespace collides with libc's `index()`
function (pulled in by `<strings.h>` on some platforms). Without
a remapping, the schema author has only the option of editing
the schema's `package` declaration directly, which is not always
possible (vendored schemas, schemas under different ownership).

The flag exists in upstream zserio with the same name and
semantics; matching it is the simplest path to compatibility
with existing build pipelines.

The "import resolution unaffected" clause is critical: prefixing
imports would break vendored schemas, because the on-disk paths
of imported files do not contain the prefix. The flag is purely
a *post-load rewrite*; the file system view of the schema set is
unchanged.

## Verification

- `tests/parser/test_top_level_package_prefix.c` exercises:
  - Single-file schema with a single-component prefix.
  - Multi-package schema set with a multi-component prefix.
  - Schema importing other schemas — verify imports resolve
    against original (unprefixed) paths, but emitted symbols
    carry the prefix.
  - Empty prefix — verify no-op behaviour.
  - Double specification — verify diagnostic.
- A cross-validation harness uses `-setTopLevelPackage` to
  verify the byte-equality property holds under the prefix.

## Notes

The prefix is applied at the semantic-analysis stage, on a
copy of the package name list, before symbol resolution
begins. Token streams and ASTs preserve the original
declarations for diagnostic emission
([REQ-FN-004](REQ-FN-004.md) — locations point to source
exactly as written).

This flag is one of the few generator-CLI features that does
not have a static-analysable alternative (you cannot achieve
it by editing schemas, in the vendored-schema case). It is
load-bearing for one production integrator's workflow.
