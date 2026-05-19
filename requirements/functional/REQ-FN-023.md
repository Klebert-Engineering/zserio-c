---
id: REQ-FN-023
title: Wire-format fields are emitted in schema source order
type: functional
status: accepted
phase_target: 1
verification: test
---

## Statement

When encoding a compound type, the generator-emitted `_write`
SHALL emit fields in the textual order they appear in the
compound's declaration in the schema source file. The
corresponding `_read` SHALL consume bytes in the same order.

There SHALL be no per-field tag prefix, length-prefix, or other
self-description on the wire. The mapping from byte position to
field is determined entirely by the schema's declared order and
each field's encoded width.

Order-preserving applies recursively: a compound containing
another compound encodes the inner compound at the position
where the inner type's field is declared, and inside that inner
compound, fields are emitted in their declaration order.

Fields under `if` clauses, `optional` fields with `_present`
false, and `extend` fields decoded short-of-buffer behave as
defined by their schema-language semantics
([REQ-FN-025](REQ-FN-025.md) for `extend`,
[REQ-DM-006](../domain/REQ-DM-006.md) for evolution
constructs); their *order in the wire format when present* still
follows the schema declaration order.

## Rationale

The "schema declares the layout" property is what makes zserio's
encoding compact: with no tags and no length-prefixes, encoded
data is roughly half the size of equivalent protobuf or
FlatBuffers output. The trade-off is that schema and bitstream
are coupled — the same byte position in two slightly different
schemas decodes to two different things.

The order-of-declaration rule is the lowest-friction way to give
schema authors control. They write fields in the order they
want them on the wire; no separate "wire order" annotation is
needed.

The "no tag, no length prefix" prohibition is *normative*: it
forbids the implementation from sneaking a tag in for
"helpfulness". This matters because such a tag would silently
inflate every message and break interoperability with the
upstream reference.

## Verification

- The cross-validation harness verifies byte-equality against
  the upstream reference for 182 schemas: any field-order
  divergence would manifest as a byte-position mismatch.
- The auto-roundtrip harness verifies `_read` consumes bytes in
  the same order `_write` produces them, by definition of the
  byte-equal property.

## Notes

Discharges Safety Goal SG-7 jointly with REQ-FN-021 and
REQ-FN-022.

The implementation strategy: the generator walks each compound
type's field list in declaration order and emits sequential
read/write calls. There is no "sort by some criterion" step in
the emitter. This matches the schema's mental model.

The field-order rule is what makes zserio bitstreams
*non-self-describing*: a decoder must know the schema to
interpret bytes. This is a feature for our use case (tight
encoding, low bandwidth) and a deliberate non-feature for some
others (no schema-less debugging tooling).
