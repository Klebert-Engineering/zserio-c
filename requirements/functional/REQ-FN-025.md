---
id: REQ-FN-025
title: extend fields default to zero on short bitstreams
type: functional
status: accepted
phase_target: 1
verification: test
---

## Statement

For an `extend T name;` declaration in a top-level struct, the
generated `_read` SHALL, at the position where `name` would be
decoded:

1. Check whether the bitstream reader has any bytes remaining.
2. If bytes remain, decode `name` according to type `T`'s
   read rules; mark `name`'s presence sentinel (where one
   applies) as set.
3. If no bytes remain, leave `name` in its post-`_init` zero
   state; mark `name`'s presence sentinel (where one applies)
   as unset. Return `ZSERIO_OK`.

The generated `_write` SHALL always encode the `extend` field,
in its declared schema position, at the next byte-aligned
position in the bitstream. The encoder does not have a "skip
this field" path for `extend` — it is the *reader* that
tolerates short bitstreams, not the *writer*.

The "no bytes remain" check SHALL operate on whole bytes, not
sub-byte alignment: an `extend` field is byte-aligned on write,
and the reader checks for at least one byte of payload remaining
(or the field-type-defined minimum, when known).

This requirement applies only to `extend` declared at the
outermost (struct top-level) position. Inner `extend`
declarations are constrained by the schema language reference
([REQ-DM-004](../domain/REQ-DM-004.md)).

## Rationale

`extend` is the schema's mechanism for *backward-compatible
top-level extension*: a v2 encoder produces bitstreams that v1
decoders can still consume, by stopping early at the v1
schema's last field.

The semantics here implement exactly that: a v1 decoder running
against a v2 bitstream stops when its own schema's fields are
exhausted, leaving the v2-only `extend` field unread; a v2
decoder running against a v1 bitstream finds the bitstream
short and defaults the `extend` field to zero. In both
directions, the decode succeeds.

The "writer always emits" asymmetry is what makes the
contract one-sided in the right direction: an old decoder
benefits from the new encoder's strict behaviour without the
new encoder needing to know which decoders exist.

## Verification

- The cross-validation harness includes `extend`-using
  schemas (`extend_*` family). Successful byte-equality
  against the upstream reference verifies write semantics.
- `tests/runtime/test_extend_truncated.c` exercises:
  - Decode a v1 bitstream under a v2 schema (extend field
    short) — verify default-zero behaviour.
  - Decode a v2 bitstream under a v2 schema (extend field
    present) — verify decode succeeds with value present.
- The auto-roundtrip harness verifies the byte-equal property
  on full bitstreams (not the truncation case, which is
  inherently lossy on round-trip).

## Notes

The "short bitstream" detection mechanism is the bitstream
reader's `remaining_bits()` query, called at the field
boundary. The reader does not have to predict the field's
encoded size — only whether *any* bytes remain. If bytes
remain but the field's decode runs short of them, that becomes
a normal `ZSERIO_ERR_END_OF_STREAM`, which is a malformed-input
error, not the `extend`-defaults-to-zero path.

This requirement is the runtime-layer discharge of
[REQ-DM-006](../domain/REQ-DM-006.md) for the `extend`
construct specifically.
