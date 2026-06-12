---
id: REQ-FS-006
title: Public-API input validation at the boundary
type: safety
status: accepted
verification: test
---

## Statement

Every public function in the runtime SHALL validate its input arguments
at function entry and SHALL return `ZSERIO_ERR_INVALID_ARGUMENT` (or a
more specific status) on detection of an invalid argument, before any
mutation of caller-visible state. Internal (file-local) functions MAY
rely on contracts established by their public callers and need not
re-validate.

## Rationale

Validation at the boundary is a precondition for any ISO 26262
"freedom from interference" argument: a caller passing nonsense must not
be able to corrupt internal state. Concentrating validation at the
boundary also keeps the hot internal path cheap, which is what makes
the performance requirement (REQ-PF-001) reachable.

## Verification

- Negative-path unit tests for every public entry point: pass `NULL`
  pointers, out-of-range sizes, and malformed bitstreams; assert the
  documented status code is returned and that no observable state has
  changed.

## Notes

- Validation that depends on resources (e.g. checking a length against
  remaining buffer capacity) is part of this requirement. A function
  must not partially write its output and then discover the input was
  invalid.
- The "before any mutation" rule includes calls to the allocator: the
  allocator is not invoked until inputs have been validated.
