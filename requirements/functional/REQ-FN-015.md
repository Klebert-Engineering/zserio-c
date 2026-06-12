---
id: REQ-FN-015
title: Generated-code lifecycle entry points
type: functional
status: accepted
verification: test
---

## Statement

For every top-level schema type `T` (struct, choice, union,
enum, bitmask, subtype, named instantiation), the generator
SHALL emit the following C entry points in the appropriate
header/source pair:

1. `void <t>_init(<t>_t *self);` — zero-initialize `self`.
2. `void <t>_set_parameters(<t>_t *self, /* params */);` — set
   schema-declared parameters; emitted only when `T` declares at
   least one parameter.
3. `zserio_status_t <t>_read(<t>_t *self,
   zserio_bitstream_reader_t *reader,
   const zserio_allocator_t *alloc);` — decode `self` from
   `reader`, allocating variable-length data through `alloc`.
4. `zserio_status_t <t>_write(const <t>_t *self,
   zserio_bitstream_writer_t *writer);` — encode `self` to
   `writer`.
5. `void <t>_free(<t>_t *self, const zserio_allocator_t *alloc);`
   — release decoder-allocated storage and reset `self` to
   post-`_init` state.

Where `<t>` is the snake_case identifier for `T` per
[REQ-FN-009](REQ-FN-009.md), and `<t>_t` is the corresponding
struct typedef.

For types that own no variable-length storage,
`<t>_free` SHALL still be emitted and SHALL be valid to call.
For types that have no parameters, `<t>_set_parameters` SHALL
NOT be emitted (its absence is the signal that no parameters
are needed).

## Rationale

A uniform lifecycle API across every schema type is what makes
the generated code composable: a containing type's `_read` can
mechanically call its fields' `_read`s, a containing `_free`
can mechanically call its fields' `_free`s, etc. Without
uniformity the generator would need to special-case each call
site by type shape.

The "_free always emitted" rule is for caller convenience:
the user writes `T_free(&x, &alloc)` after every `_read` without
needing to look up whether `T` has anything to release. The
compiler optimizes away the trivial `_free` if it has no work
to do.

The "_set_parameters only when needed" rule is the opposite
trade-off: emitting an empty `_set_parameters` for every type
would be wasted source bytes and confuse the user about which
types actually depend on runtime parameters. Its presence in
the header is the signal.

## Verification

- The cross-validation harness drivers call exactly this
  contract on 182 schemas. A missing entry point would fail at
  compile time.
- The bidirectional auto-roundtrip harness
  (`tests/xvalidation/c_roundtrip.c`) runs `_init` →
  `_read` → `_write` → `_free` on every schema and verifies the
  round-trip byte property; a malformed entry point would fail
  the round-trip.
- `tests/parser/test_lifecycle_emission.c` exercises one
  representative of each top-level type kind and asserts the
  emitted prototypes against the documented signatures.

## Notes

This requirement defines the lifecycle *interface*. The
*semantics* of each entry point are constrained by individual
requirements:

- `_init`: postcondition is the zero state
  ([REQ-FN-019](REQ-FN-019.md) — `_free` returns to the same
  state).
- `_set_parameters`: propagates to nested types
  ([REQ-FN-024](REQ-FN-024.md)).
- `_read`: allocates through `alloc` only
  ([REQ-FN-017](REQ-FN-017.md)).
- `_write`: never allocates ([REQ-FN-016](REQ-FN-016.md)).
- `_free`: idempotent and safe on partial state
  ([REQ-FN-018](REQ-FN-018.md)).

Packed-array element types may additionally emit
`_packing_context_t`, `_init_packing_context`,
`_observe_packing`, `_read_packed`, `_write_packed`. These are
secondary entry points for the packed-array implementation, not
part of the lifecycle contract.
