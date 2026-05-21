---
id: REQ-FN-003
title: Diagnostic message format
type: functional
status: accepted
phase_target: 1
verification: test
---

## Statement

Every diagnostic emitted by the parser (errors, warnings, and
notes) SHALL be formatted to standard error as a single line of
the form:

```
<file>:<line>:<column>: <severity>: <message>
```

Where:

- `<file>` is the path to the source file containing the
  diagnostic's anchor token, as supplied to or derived from the
  parser's input.
- `<line>` is the 1-based line number of the anchor token.
- `<column>` is the 1-based column number of the anchor token,
  measured in bytes from the start of the line.
- `<severity>` is exactly one of `error`, `warning`, or `note`.
- `<message>` is a single line of human-readable text describing
  the diagnostic, without embedded newlines.

Multi-line context (source quotation, secondary anchors,
suggested fixes) MAY follow the primary line, but each such
continuation SHALL itself be a complete formatted line under
this scheme.

The parser SHALL NOT emit diagnostics to standard output. The
parser SHALL NOT emit diagnostics in any other format.

## Rationale

The GCC-compatible diagnostic format is the convention every C
toolchain user already recognises. IDEs, editor plugins, and CI
log parsers consume it out of the box. Inventing a different
format would gain nothing and lose interoperability.

Single-line primary messages make every diagnostic addressable by
grep, by line-based filters, and by simple regex matching in
build systems. Multi-line continuations are explicitly secondary
so that the primary-line invariant holds even for richly
contextualised messages.

`stderr`-only output is the convention for diagnostics across
Unix tooling. Mixing diagnostics with successful output on
stdout would break pipes that consume the generator's progress
report (where one exists).

## Verification

`tests/parser/test_diagnostics_format.c` exercises ten
representative error cases and asserts the format on each
output line via regex. The CI gate fails on any deviation.

The xvalidation harness's CI logs are parsed for diagnostic
lines on failure paths; any deviation would surface as a
harness-parse failure.

## Notes

The byte-based column counting (rather than character or display
width) is the same choice gcc and clang make: it is unambiguous
for any input, including invalid UTF-8 sequences in the schema
file. Editors are responsible for translating to display column
if needed.
