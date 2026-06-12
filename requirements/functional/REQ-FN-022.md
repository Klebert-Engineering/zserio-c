---
id: REQ-FN-022
title: Round-trip byte equality on valid bitstreams
type: functional
status: accepted
verification: test
---

## Statement

For every schema `S` supported by `zserio-c` and every
bitstream `b` that is a valid encoding under `S`, the operation
sequence:

1. `T_init(&v); T_set_parameters(&v, ...);  /* if applicable */`
2. `st1 = T_read(&v, &reader_over_b, &alloc); assert st1 == ZSERIO_OK;`
3. `st2 = T_write(&v, &writer_into_b'); assert st2 == ZSERIO_OK;`

SHALL produce a bitstream `b'` that is byte-identical to `b`,
where `b` is the original valid encoding and `b'` is the
re-encoding.

This identity SHALL hold for all 182 schemas in the
cross-validation harness, at every seed exercised by the
harness.

The identity SHALL hold over the entire bitstream, not merely
up to the last decoded field: trailing alignment padding and
end-of-stream bytes are part of the encoding and SHALL be
preserved.

## Rationale

Round-trip byte equality is the most demanding correctness
test we can apply to the codec. It catches:

- Asymmetric encoders/decoders (where one side writes a layout
  the other does not produce on re-encode).
- Lossy decode paths (where a field reads then writes back a
  defaulted value).
- Encoder-side normalization that the decoder does not invert
  (e.g. trimming trailing zero bits).
- Padding mishandling at the schema-level alignment boundaries.

Combined with canonical encoding ([REQ-FN-021](REQ-FN-021.md)),
round-trip byte equality is the operational definition of
"`zserio-c` correctly implements the schema".

## Verification

- `tests/xvalidation/c_roundtrip.c` is a per-harness driver
  that does exactly the operation sequence above and `cmp`s
  the result against the input. It runs against all 182
  cross-validation harnesses on every commit.
- The NDS Live conformance corpus (`conformance/ndslive/`)
  exercises the same property on a 50 KiB production NDS Live
  tile: `read_tile` decodes the tile via `_read` and re-encodes
  via `_write`, and verifies byte-equality.

## Notes

The "valid bitstream" qualifier is essential: round-trip
identity is undefined for inputs the decoder rejects (those
produce a non-OK status from step 2, and re-encoding the
partial struct is not meaningful).

Discharges Safety Goal SG-7 jointly with REQ-DM-003 and
REQ-FN-021.

The schema-evolution constructs are partially exempt from this
identity: a buffer encoded under schema version 1 then decoded
under version 2 may re-encode to a *longer* buffer (the
version-2 fields are present on re-encode). This is correct
behavior, not a round-trip violation. The
[REQ-DM-006](../domain/REQ-DM-006.md) evolution semantics
define which schema-version transitions preserve byte identity
and which extend it.
