---
id: REQ-FN-008
title: Output file layout mirrors schema package hierarchy
type: functional
status: accepted
phase_target: 1
verification: test
---

## Statement

For a schema declaring `package a.b.c;` and containing a
top-level type `T`, the generator SHALL emit, under the
`-c <output-dir>` argument:

- A header file at relative path `a/b/c/<t>.h`.
- A source file at relative path `a/b/c/<t>.c`.

Where `<t>` is the snake_case form of the type name `T` per the
naming rules of [REQ-FN-009](REQ-FN-009.md), and `<output-dir>`
is taken verbatim from the CLI argument.

The generator SHALL NOT emit files outside `-c <output-dir>`. It
SHALL NOT emit files at depths shallower than the schema's
package depth (e.g. it SHALL NOT promote a deeply packaged type
to a flat output).

The generator SHALL create intermediate directories as needed.
It SHALL NOT delete or modify pre-existing files in
`-c <output-dir>` that do not collide with its own output.

## Rationale

Mirroring the package hierarchy as a directory hierarchy is what
makes `#include "a/b/c/t.h"` resolve consistently from inside
the generated tree, what lets multiple packages with overlapping
type names coexist, and what makes the output structure
predictable for build systems that need to enumerate files
without parsing the schema.

The "no files outside output-dir" clause is a containment
guarantee: a generator run cannot have hidden side effects
elsewhere in the filesystem.

The "no deletion or modification of existing files" clause makes
the generator safe to run against a populated output directory.
A stale file from a previous run will linger and be visible to
the build system, which is preferable to silent deletion.

## Verification

- The cross-validation harness compiles 182 schemas with
  distinct package paths; the build system enumerates files at
  expected paths via globbing and fails if any are missing.
- `tests/parser/test_output_layout.c` exercises:
  - Flat package (single name).
  - Three-level package.
  - Multiple types in the same package.
  - Multiple packages in the same generator run.
- A CI gate
  (`tools/check_no_writes_outside_output_dir.py`) runs the
  generator under `strace` and verifies no `open(O_WRONLY|…)`
  occurs outside the declared `-c` directory.

## Notes

The output layout requirement is the dual of the import
resolution requirement ([REQ-FN-005](REQ-FN-005.md)): one is the
path under which an external schema is found, the other is the
path under which the generated code lands. They share the same
package-as-directory mapping.

Cross-package `#include` qualification (per
[REQ-FN-012](REQ-FN-012.md)) depends on this layout requirement:
the relative `#include` paths the generator emits are computed
against the output-dir-relative form defined here.
