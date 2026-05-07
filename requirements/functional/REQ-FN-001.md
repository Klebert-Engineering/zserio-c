---
id: REQ-FN-001
title: Wire compatibility with upstream zserio
type: functional
status: accepted
phase_target: 4
verification: test
---

## Statement

For every schema construct that zserio-c claims to support, the bytes
produced by the zserio-c encoder when serializing a given value SHALL
be byte-exact identical to the bytes produced by the upstream
`ndsev/zserio` reference implementation at the pinned version
`v2.18.1`, and the value decoded by the zserio-c decoder from a given
byte stream SHALL be equal (per the schema's value-equality semantics)
to the value decoded by the reference implementation from the same
stream.

## Rationale

Zserio is defined by its wire format more than by any single
implementation. A port that diverges from the reference is by definition
broken. Anchoring the contract to the pinned upstream version makes the
test deterministic and lets us track upstream changes deliberately
through submodule bumps.

## Verification

- Conformance test suite (Phase 4) round-trips a corpus of representative
  schemas and values:
  - Generate `n` random valid values per schema with a seeded PRNG.
  - Encode with zserio-c, decode with upstream Java zserio (or the
    upstream C++ runtime), assert equality.
  - Encode with upstream, decode with zserio-c, assert equality.
  - Encode with both, byte-compare the outputs.
- The conformance suite runs in CI on every change.

## Notes

- "Schema constructs supported by zserio-c" is itself tracked in
  `docs/upstream-analysis/schema-language.md` (in-scope vs. deferred).
  A construct that is deferred is not subject to this requirement until
  it is in scope.
- Endianness, alignment, and varint encoding details are part of the
  wire format and therefore in scope of this requirement.
