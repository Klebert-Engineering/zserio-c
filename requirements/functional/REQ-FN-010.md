---
id: REQ-FN-010
title: Identifier-length cap with deterministic hash truncation
type: functional
status: accepted
phase_target: 1
verification: test
---

## Statement

When the identifier produced by the mapping defined in
[REQ-FN-009](REQ-FN-009.md) would, as a filesystem path
component, exceed an internal threshold (currently 160 bytes),
the generator SHALL truncate it to a deterministic form
consisting of:

1. A prefix of the original identifier, of length at most
   `(threshold − 17)` bytes, preserving the leading characters.
2. A 17-byte deterministic suffix of the form
   `_HHHHHHHHHHHHHHHH` where the 16 hex digits are the FNV-1a
   hash (64-bit) of the full pre-truncation identifier.

Truncation SHALL be reproducible: the same pre-truncation
identifier produces the same truncated identifier across runs.

After truncation, every emitted file name and every emitted
identifier SHALL fit within the lesser of (a) the platform's
NAME_MAX (255 bytes on the supported POSIX targets) and (b) the
threshold above.

The threshold value SHALL be a single constant defined in the
generator source, not duplicated across files.

## Rationale

Deep template instantiation chains (e.g. NDS Live's
`AttributeMap<AttributeMap<...>>` nested across five levels of
map-of-set-of-map) produce mangled identifiers that exceed
POSIX NAME_MAX. Without truncation, the generator fails at file
creation time with an opaque OS error.

The chosen scheme (prefix + hash suffix) preserves *prefix
readability* (so a developer can still recognise the identifier
in error messages) while guaranteeing global uniqueness via the
hash (FNV-1a has acceptable collision properties for this size
of input). 64 bits of hash makes the collision probability
negligibly small for any plausible code base.

The "single constant" clause is a maintenance rule: a future
change to the threshold (e.g. for shorter NAME_MAX on a Windows
target) must be a one-line edit.

## Verification

- `tests/parser/test_identifier_truncation.c` exercises
  identifiers of length just under, at, and just over the
  threshold, and verifies the truncated form's prefix and the
  hash suffix.
- The NDS Live conformance corpus (`conformance/ndslive/`)
  contains real-world inputs that exceed the threshold; the
  corpus runs cleanly only when truncation is applied.
- A CI gate verifies that no emitted file name exceeds
  NAME_MAX after truncation.

## Notes

The threshold of 160 (rather than 255) leaves headroom for the
build system's own concatenations of file paths (e.g. the
xvalidation driver concatenates package path components with
`/` separators into per-driver build artefact names).

FNV-1a is chosen over more secure hashes (SHA-256, BLAKE2)
because the collision tolerance here is engineering-grade not
adversarial: an attacker who can choose schema identifiers can
already break the build in dozens of more direct ways. FNV-1a's
compactness (no allocation, table-free) keeps the hash inside
the generator's stack budget without complications.
