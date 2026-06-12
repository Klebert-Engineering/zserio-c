---
id: REQ-FN-021
title: Canonical encoding (struct values to bytes)
type: functional
status: accepted
verification: test
---

## Statement

For a fixed schema `S` and a fixed in-memory `<t>_t` struct
value `v`, repeated `<t>_write(&v, &writer)` calls SHALL produce
byte-identical output bitstreams, regardless of:

1. The amount of memory the writer's underlying buffer has
   beyond what the encoding requires.
2. The alignment of the buffer.
3. Previous bitstream operations on the same writer (provided
   the writer is in a defined "start of message" state via
   `zserio_bitstream_writer_init`).
4. The current process's address-space layout, scheduling
   state, or environment.

Two struct values `v1` and `v2` that are semantically equal (per
the schema's value-equality semantics) SHALL produce
byte-identical encoded bitstreams. In particular:

1. Different choice arms in a `choice` carry their selector;
   the encoded bytes are determined by the selector and the
   selected arm's value, not by any field outside that union.
2. Optional fields whose `_present` is false SHALL produce
   identical bytes regardless of the (uninitialized) value of
   the optional's storage slot.
3. The byte ordering of the encoding is determined by the
   schema and the wire format spec
   ([REQ-DM-005](../domain/REQ-DM-005.md)), not by the host's
   byte ordering.

## Rationale

Canonical encoding at the encoder level is the implementation-side
discharge of [REQ-DM-003](../domain/REQ-DM-003.md). It is what
makes byte-comparison a sound equality check on encoded
messages, what enables content-addressed message handling, and
what makes the cross-validation byte-equality test meaningful.

The "uninitialized storage" clause is what makes the encoder
*safe* on struct values where some fields legitimately hold
unspecified bytes (e.g. the unselected arm of a `choice`'s
union storage). Without this clause an integrator would have to
zero-fill every struct before encoding to get reproducible
output.

The "host byte ordering" clause is the cross-platform
reproducibility property: the same struct on x86_64-linux and
aarch64-linux encode to the same bytes.

## Verification

- The cross-validation harness compares byte sequences across
  encoders on the same struct values; canonical encoding is the
  prerequisite that makes the comparison defined.
- The bidirectional auto-roundtrip harness verifies
  `_read → _write → _read → _write` produces identical
  bitstreams across iterations.
- A CI matrix runs the harness on x86_64-linux and
  aarch64-linux and verifies byte-identical outputs.

## Notes

The implementation strategy: every primitive encoder writes
exactly the bits the wire format spec requires, with no
"helpful" zero-padding, no implicit alignment beyond the
schema-declared alignment, and no host-byte-order shortcuts.
The bitstream primitives are byte-order-explicit in their
implementation.

The encoder is the canonicalizing side of canonical encoding;
the decoder is the *interpreting* side ([REQ-FN-022](REQ-FN-022.md)
ensures the round-trip).
