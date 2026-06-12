---
id: REQ-FN-005
title: Import resolution rules
type: functional
status: accepted
verification: test
---

## Statement

The parser SHALL resolve `import` declarations against the
ordered list of source roots supplied to the CLI via `-src`
flags. The resolution algorithm SHALL be:

1. Given an import `import pkg.sub.name;` or
   `import pkg.sub.*;`, the parser SHALL search each `-src` root
   in the order they were provided on the command line.
2. For each root, the parser SHALL look for a file at the path
   `<root>/<pkg>/<sub>/<name>.zs` (single-symbol form) or
   `<root>/<pkg>/<sub>.zs` (wildcard form).
3. The first matching file SHALL be loaded; subsequent roots
   SHALL NOT be consulted for that import.
4. If no root contains the file, the parser SHALL emit a
   diagnostic and exit non-zero.
5. If exactly one `-src` root is supplied or none are, the
   single resolution path is the directory containing the
   `<input.zs>` argument (default), or the single root, in that
   order of precedence.

Loaded files SHALL be parsed exactly once, even if multiple
imports name them. Identity is by canonical absolute path.

## Rationale

A defined search order is what makes multi-root schema layouts
predictable. Without it, an integrator vendoring two schema
trees with overlapping packages cannot tell which version will
be loaded.

"First match wins" matches the convention of every compile-time
include-path system the C ecosystem uses (cpp's `-I` flags,
similarly Java's classpath, similarly Python's `sys.path`). It
is what an experienced toolchain user expects.

Parse-once-by-canonical-path is what prevents pathological
imports from blowing up parse time and what allows the parser
to construct a single AST per source file, which is what the
generator depends on for canonical naming
([REQ-FN-009](REQ-FN-009.md)) and identifier stability
([REQ-FN-010](REQ-FN-010.md)).

## Verification

- `tests/parser/test_import_resolution.c` exercises:
  - Single-root resolution (file in root).
  - Multi-root precedence (file in first root, file in second
    root, file in both).
  - Wildcard resolution (`pkg.*`).
  - Default (no `-src`) resolution against the input file's
    directory.
- `tests/xvalidation/harnesses/cross_package_*/` cover the
  multi-package import scenarios under the cross-validation
  gate.

## Notes

This requirement is the boundary between the parser and the
file system. It is one of the small number of places where the
parser interacts with anything outside its argv inputs.

Cycle detection in the import graph is a separate requirement
([REQ-FN-006](REQ-FN-006.md)). The resolution algorithm here is
about *finding* files; cycle detection is about *processing*
them.

The `-setTopLevelPackage` flag (per [REQ-FN-014](REQ-FN-014.md))
modifies the *interpretation* of `package` declarations but
SHALL NOT alter the search algorithm: imports continue to be
resolved against `-src` roots regardless of any prefix applied.
