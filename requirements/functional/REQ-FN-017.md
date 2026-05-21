---
id: REQ-FN-017
title: Decoder allocates only via the supplied allocator vtable
type: functional
status: accepted
phase_target: 1
verification: analysis
---

## Statement

The `<t>_read` entry point of any generated type, and every
runtime function reachable from it that needs to materialise
variable-length data, SHALL acquire that memory exclusively
through the `zserio_allocator_t` vtable supplied as the
`alloc` parameter.

Specifically:

1. No call to `malloc`, `calloc`, `realloc`, `free`,
   `aligned_alloc`, `posix_memalign`, or any equivalent libc
   memory function (already prohibited globally by
   [REQ-FS-001](../safety/REQ-FS-001.md)).
2. No use of VLA declarations of schema-dependent size.
3. No use of `static` or `thread_local` buffers for
   variable-length payload storage.
4. Every variable-length allocation SHALL pass through the
   `alloc->vtable->alloc(alloc->userdata, size, align)` hook.

If the allocator's `alloc` hook returns NULL, the decoder SHALL:

1. Stop further decoding.
2. Return `ZSERIO_ERR_OUT_OF_MEMORY`.
3. Leave `self` in a state safe to `_free` against the same
   allocator ([REQ-FN-018](REQ-FN-018.md)).

The `_read` entry point SHALL NOT silently substitute an
alternative allocation strategy if the supplied allocator
fails.

## Rationale

The single point of allocation (the supplied vtable) is what
gives the integrator a deterministic memory bound. With every
allocation flowing through one hook:

- A bounded arena exhausts at a defined byte count and surfaces
  the exhaustion to the caller (rather than aborting).
- A tracking allocator catches leaks at the source.
- A safety-qualified pool replaces the allocator at integration
  time without touching the runtime source.

Variable-length data — strings, dynamic arrays, owned
`bytes`/`extern` blobs — are the only allocations the decoder
makes. Fixed-size fields live in caller-provided struct
storage; no decoder-internal scratch buffer is needed
([REQ-FS-003](../safety/REQ-FS-003.md): no recursion, all
context is on the caller's stack).

Returning NULL on `alloc` failure (rather than aborting) is what
makes the allocator failure path safe: every code path in the
runtime that calls `alloc->vtable->alloc` checks the return and
short-circuits via `ZSERIO_TRY` on NULL.

## Verification

- A static-analysis CI gate walks the call graph rooted at
  every emitted `_read` entry point and verifies the only
  allocation site is the `alloc->vtable->alloc` call.
- `tests/runtime/test_failure_modes.c::test_h03_failing_allocator_propagates_out_of_memory`
  verifies the OOM propagation path.
- A tracking-allocator test (`tests/runtime/test_bytes.c`,
  `test_string.c`, `test_extern.c`) verifies every allocation
  observed by the tracker is matched by a corresponding free
  on the `_free` path.

## Notes

Discharges Safety Goal SG-3
([`docs/safety-case/functional-safety-concept.md`](../../docs/safety-case/functional-safety-concept.md)).

The contract is symmetric on `_free`: every allocation made on
the `_read` path is released on the `_free` path through the
same vtable's `free` hook
([REQ-FN-018](REQ-FN-018.md)).

The vtable indirection has a measurable performance cost (one
indirect call per allocation), which is acceptable given the
allocation rate (one per variable-length field, not per byte)
and the alternative of a non-replaceable allocator.
