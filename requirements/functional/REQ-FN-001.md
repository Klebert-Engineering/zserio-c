---
id: REQ-FN-001
title: Interoperability with upstream zserio v2.18.1
type: functional
status: accepted
verification: test
---

## Statement

`zserio-c` SHALL be byte-interoperable with the upstream
`ndsev/zserio` reference implementation at the pinned version
`v2.18.1`. Specifically:

1. For every schema construct that `zserio-c` claims to support,
   the bytes produced by the `zserio-c` encoder when serializing
   a given value SHALL be byte-exact identical to the bytes
   produced by the upstream reference implementation when
   serializing the same value under the same schema.
2. The value decoded by the `zserio-c` decoder from a given byte
   stream SHALL be equal (per the schema's value-equality
   semantics) to the value decoded by the upstream reference
   implementation from the same byte stream.

## Rationale

This requirement is an **interoperability** guarantee, not a
specification. The binding contract for `zserio-c`'s wire format
is [REQ-DM-005](../domain/REQ-DM-005.md) (`docs/spec/wire-format.md`);
this requirement guarantees that our wire format is, at the
pinned upstream version, identical to the format produced by the
existing reference implementation.

Phrased this way:

- The wire format is *ours* — `zserio-c` is not a wrapper around
  upstream and is not bound to upstream's future evolution.
- Interoperability with the deployed upstream installed base is
  nonetheless a hard requirement for any practical adoption.
- The byte-equality test is the most stringent interoperability
  check possible: if our bytes match, every existing upstream
  decoder accepts our output, and we accept every existing
  upstream encoder's input.

Anchoring to a pinned upstream version (`v2.18.1`) makes the
test deterministic and lets us track upstream changes
deliberately. A future upstream release that changes the wire
format would not automatically rebind us — it would prompt a
review at our [REQ-DM-007](../domain/REQ-DM-007.md) wire-format
versioning gate.

## Verification

- Cross-validation harness `tests/xvalidation/run_xvalidation.sh`
  exercises both directions on 182 schemas with 5 seeded
  inputs each:
  - `cpp_writes → c_reads → byte-equal`
  - `c_writes → cpp_reads → byte-equal`
- Current result: 182/182 byte-equal in both directions, on
  every commit.
- Bidirectional auto-roundtrip
  (`tests/xvalidation/c_roundtrip.c`) provides a third
  redundant gate: `c_writes → c_reads → c_writes → byte-equal`.

## Notes

- "Schema constructs supported by `zserio-c`" is the in-scope
  set of the schema language as defined by
  [REQ-DM-004](../domain/REQ-DM-004.md)
  (`docs/spec/schema-language.md`). Service / RPC and SQL
  extensions are out of scope.
- Endianness, alignment, varint encoding, packed-array delta
  contexts, and offset encoding are part of the wire format
  defined by [REQ-DM-005](../domain/REQ-DM-005.md), and
  interoperability is therefore in scope for those areas as well.
- If a future upstream release introduces a wire-format change,
  this requirement does NOT automatically retarget — it remains
  pinned at v2.18.1 until a deliberate review (per
  [REQ-DM-007](../domain/REQ-DM-007.md)) reissues it for a new
  pinned version.
