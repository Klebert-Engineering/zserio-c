---
id: REQ-FN-012
title: Cross-package include qualification
type: functional
status: accepted
verification: test
---

## Statement

When a generated file in package `a.b.c` references a type whose
declaration is in package `x.y.z`, the generator SHALL emit an
`#include` directive resolving to the referenced type's header
file under `-c <output-dir>`.

The emitted `#include` path SHALL be the path of the referenced
type's header relative to `-c <output-dir>`, using forward
slashes regardless of host operating system:

```
#include "x/y/z/<referenced_type>.h"
```

The path SHALL NOT be platform-specific (no backslashes), SHALL
NOT be absolute, and SHALL NOT depend on the current working
directory at generation time.

When the referenced type is in the same package as the
referring file, the generator MAY use a same-directory relative
include (`#include "<referenced_type>.h"`) or the full
output-relative path; both forms SHALL be consistent within a
single generator run.

## Rationale

Cross-package references are the common case in any
non-trivial schema set. The emitted `#include` paths determine
whether the generated code compiles when the output directory
is on the build system's include path.

Output-relative paths (rather than package-source-relative or
absolute) are what make the output directory self-contained: a
build can use a single `-I <output-dir>` flag, and every
generated `#include` resolves from there.

Forward-slash normalization is the convention every C
preprocessor accepts on every platform we target. The dual
acceptance on Windows (forward or backward slash) makes this a
free choice; choosing forward-slash makes the output portable
to repositories that share generated code across platforms.

## Verification

- The cross-validation harness compiles generated code with
  `-I <output-dir>` and nothing else; any `#include` resolution
  failure would fail the build.
- `tests/parser/test_cross_package_include.c` exercises:
  - Same-package reference.
  - Adjacent-package reference (same package depth, sibling).
  - Deeper-package reference (referring file is shallower).
  - Shallower-package reference (referring file is deeper).
- The NDS Live conformance corpus exercises 1823 generated
  files with extensive cross-package and cross-template
  references; all compile under `-Werror`.

## Notes

The implementation strategy is to keep package paths as
canonical lists of dotted components in the AST, and to compute
the output-relative path at emission time. This keeps the
generator free of OS-dependent path handling on the hot path.

Same-package case ambiguity (relative vs full path) is
intentional: a future refactor that flattens generated output
or that adds a `pragma once`-style guard may change which form
is preferred. The requirement allows either as long as a single
run is consistent.
