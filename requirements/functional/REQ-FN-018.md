---
id: REQ-FN-018
title: _free is idempotent and safe on partial state
type: functional
status: accepted
phase_target: 1
verification: test
---

## Statement

For every generated type `T`, the `<t>_free` entry point SHALL
have the following behaviour:

1. **Safe on zero state** — `_free` called on a struct that has
   been `_init`-ed but never `_read` or otherwise populated
   SHALL succeed without releasing anything.
2. **Safe on partial state** — `_free` called on a struct that
   was being populated by a `_read` that returned a non-OK
   status SHALL release all storage that was acquired before
   the read failed, and SHALL NOT attempt to release storage
   that was never acquired.
3. **Idempotent** — `_free` called twice in succession on the
   same struct SHALL be equivalent to a single call: the
   second invocation finds the struct in zero state and
   does nothing.
4. **Allocator-consistent** — the same allocator vtable
   passed to `_read` SHALL be passed to `_free`. Releasing
   through a different vtable is undefined.

After a successful `_free`, the struct SHALL be in the same
state as immediately after `_init` (per
[REQ-FN-019](REQ-FN-019.md)).

`_free` SHALL NOT have a fallible return type. Any error
condition that `_free` might encounter (NULL pointer arguments
excepted, see [REQ-FS-006](../safety/REQ-FS-006.md)) is a
programming error in the caller, not a runtime fault.

## Rationale

These four properties together make `_free` callable in the
canonical error-recovery pattern without conditional logic:

```c
zserio_status_t st = T_read(&t, &reader, &alloc);
if (st != ZSERIO_OK) {
    T_free(&t, &alloc);   /* safe on partial state */
    return st;
}
```

Without "safe on partial state", the caller would have to
inspect which fields were populated before calling free, which
is impossible without internal knowledge of the read order.

Without "safe on zero state", the caller would have to track
whether `_read` was ever attempted, which doubles the
state-tracking burden.

Without idempotence, a defensive caller (calling `_free` after
encoding-then-decoding into the same struct) would risk a
double-free.

The "allocator-consistent" clause acknowledges that an arena
allocator's `free` is a no-op while a libc allocator's `free`
must match the exact `malloc` it pairs with. The runtime cannot
detect a mismatch; it can only require it doesn't happen.

## Verification

- `tests/runtime/test_free_partial_state.c` exercises:
  - `_free` after `_init` only (zero state).
  - `_free` after `_read` that failed at field 1.
  - `_free` after `_read` that failed at field N.
  - `_free` called twice in succession.
- The auto-roundtrip harness's per-seed test fails any schema
  whose `_free` leaks (verified by ASan).
- `XVAL_SAN=1 ./tests/xvalidation/run_xvalidation.sh` runs the
  full harness under AddressSanitizer + UndefinedBehaviorSanitizer
  every CI run.

## Notes

The implementation of "safe on partial state" is uniform across
the generator: every owned-storage field has a `_present` or
pointer-non-NULL sentinel that `_free` checks before releasing.
`_init` sets every sentinel to its zero state; `_read` flips
sentinels only after the corresponding allocation succeeds.

This requirement is one of the strongest arguments for the
explicit lifecycle: under exceptions or implicit destructors,
the equivalent guarantee would require RAII discipline at every
allocation site.
