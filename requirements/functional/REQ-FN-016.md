---
id: REQ-FN-016
title: Encoder performs no heap allocation
type: functional
status: accepted
phase_target: 1
verification: analysis
---

## Statement

The `<t>_write` entry point of any generated type, and every
runtime function reachable from it, SHALL NOT perform heap
allocation in any form. Specifically:

1. No call to `malloc`, `calloc`, `realloc`, `free`,
   `aligned_alloc`, `posix_memalign`, or any equivalent libc
   memory function (already prohibited globally by
   [REQ-FS-001](../safety/REQ-FS-001.md)).
2. No call to the supplied `zserio_allocator_t` vtable. The
   `_write` entry point SHALL NOT take a
   `zserio_allocator_t` parameter.
3. No use of variable-length-array (VLA) declarations of
   schema-dependent size.

Scratch state required during encoding (e.g. per-field
packed-array delta contexts, alignment temporary state) SHALL be
allocated on the encoder's own stack within the per-function
stack budget
([`docs/safety-case/stack-budget.md`](../../docs/safety-case/stack-budget.md)).

## Rationale

The encoder's no-allocation property is what makes
"serialize on demand" cheap and predictable. In an embedded
hot path the integrator can pre-allocate a scratch encoding
buffer once and re-use it without ever entering an allocator.

It also simplifies the safety argument: the encoder cannot fail
with `ZSERIO_ERR_OUT_OF_MEMORY`. Its failure modes are limited
to argument validation
([REQ-FS-006](../safety/REQ-FS-006.md)) and bitstream-buffer
overflow.

Forbidding the `_write` entry point from taking an allocator
parameter is the API-shape consequence of the no-allocation
rule: an integrator who sees no allocator in the signature
knows to expect no allocator dependency.

## Verification

- A static-analysis CI gate
  (`tools/check_no_alloc_in_writers.py`) walks the call graph
  rooted at every emitted `_write` entry point and verifies the
  reachable function set contains no allocator vtable call and
  no libc-heap call.
- `tests/runtime/test_writer_no_alloc.c` runs a representative
  schema's writer against a deliberately-failing allocator and
  verifies the writer returns successfully (the allocator is
  never invoked).

## Notes

This requirement is the positive counterpart of
[REQ-FS-001](../safety/REQ-FS-001.md) (no direct heap
allocation) on the write path. The combined effect is:
*the encoder allocates nothing*. The decoder allocates via
vtable ([REQ-FN-017](REQ-FN-017.md)) but never via libc
directly.

VLAs of *constant* size are not prohibited; the prohibition is
specifically on VLAs sized by schema-dependent expressions,
which would let an adversarial input induce arbitrary stack
growth.
