---
id: REQ-DM-002
title: Encode and decode are mutual inverses
type: domain
status: accepted
verification: test
---

## Statement

For any value `v` that conforms to a schema `S`, encoding `v`
under `S` to produce a bitstream `b`, and decoding `b` under `S`,
SHALL produce a value semantically equal to `v` (round-trip
property).

Conversely, for any well-formed bitstream `b` decodable under
`S`, decoding `b` to produce a value `v` and re-encoding `v`
under `S` SHALL produce a bitstream byte-identical to `b`
(canonical-encoding round-trip).

Semantic equality of values is defined field-wise on the
schema's type structure, with primitive equality interpreted as
the corresponding C operator equality.

## Rationale

The mutual-inverse property is the central correctness claim of
a serialization system. Without it, the encode and decode
operations describe two different formats that happen to share a
name. With it, the schema is a single contract that both
directions honor.

The two-way phrasing (value→bytes→value AND bytes→value→bytes)
catches asymmetric bugs that a one-way test would miss:
- A decoder that silently discards a field but re-encodes
  zero passes the value-round-trip but fails the byte-round-trip.
- An encoder that pads or normalizes bytes differently from the
  decoder's expectation passes the byte-round-trip in one
  direction but not the other.

This requirement is the basis for canonical encoding
([REQ-DM-003](REQ-DM-003.md)) and for the cross-validation gate
([REQ-FN-022](../functional/REQ-FN-022.md)).

## Verification

The bidirectional cross-validation harness
(`tests/xvalidation/run_xvalidation.sh`) exercises both
directions on 183 schemas with 5 seeded inputs each:

- `cpp_writes → c_reads → byte-equal against cpp_writes`
- `c_writes → cpp_reads → byte-equal against c_writes`
- `c_writes → c_reads → c_writes → byte-equal` (auto-roundtrip)

Current result: 183/183 byte-equal in all three modes
(`docs/safety-case/safety-case-report.md`).

## Notes

"Semantically equal" excludes one source of bit-pattern divergence
that the round-trip property allows: floating-point payload
fields may decode to a different bit pattern only if the schema
declares the field as `float16` and the value was originally a
NaN. The encoder/decoder are bit-pattern preserving for all
non-NaN values (see [`docs/spec/wire-format.md`](../../docs/spec/wire-format.md)
§ Floating-point).
