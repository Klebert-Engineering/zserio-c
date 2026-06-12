---
id: REQ-FN-004
title: Diagnostics carry precise source locations
type: functional
status: accepted
verification: test
---

## Statement

Every parser diagnostic SHALL carry a non-empty source location
identifying the most specific token to which the diagnostic
applies. The location SHALL consist of:

1. A file path resolvable to a real file on disk at the time the
   parser ran.
2. A 1-based line number within that file.
3. A 1-based byte column within that line.

The reported location SHALL correspond to a token actually
present in the source text. The parser SHALL NOT report
synthesized locations (line `0`, column `0`, file `<unknown>`,
or similar).

Where a diagnostic relates to two or more source spans (e.g. a
redefinition with a prior definition), the primary location
SHALL identify the most recent / most actionable span; secondary
locations MAY be emitted as `note:` continuation lines per
[REQ-FN-003](REQ-FN-003.md).

## Rationale

A diagnostic without a precise location is a guessing game for
the user. The parser already tracks source locations through
lexing; the requirement forces every diagnostic emission path to
preserve them rather than collapsing to "somewhere in the file".

The requirement specifically forbids synthesized locations
because they are easy to introduce as a debugging shortcut and
hard to detect in review — a `note: at line 0` slips through if
no one is looking. A CI grep gate makes the prohibition
enforceable.

The "two or more spans" clause matters for redefinitions,
import-cycle reports, and type-mismatch diagnostics, where
secondary anchors are pedagogically valuable. The primary anchor
remains the one location the IDE will jump to.

## Verification

- `tests/parser/test_diagnostic_locations.c` asserts the
  presence of all three location components on each diagnostic
  in twenty representative error cases.
- A CI grep gate (`tools/check_no_synthetic_locations.py`)
  scans the parser source for `line=0`, `col=0`, `"<unknown>"`,
  and similar markers and fails the build on a match.

## Notes

This requirement is the precision-side counterpart of
[REQ-FN-003](REQ-FN-003.md), which is the format-side
counterpart. They are split because a diagnostic can satisfy
one without the other (correct format with synthesized location;
correct location embedded in a malformed line).
