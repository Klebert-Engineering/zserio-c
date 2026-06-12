---
id: REQ-FN-019
title: _free returns the struct to post-_init state
type: functional
status: accepted
verification: test
---

## Statement

After a successful invocation of `<t>_free(&self, &alloc)`,
the in-memory contents of `self` SHALL be byte-identical to
the contents of a fresh `<t>_t` variable that has just had
`<t>_init(&new_self)` called on it.

This includes:

1. All scalar fields zero / false / 0.0.
2. All pointer fields NULL.
3. All `<name>_present` sentinel flags `false`.
4. All `<name>_count` length sentinels zero.
5. Any tag / discriminator fields in the type-defined zero
   state (typically zero).

After `_free`, the struct SHALL be safe to:

- Pass to `_read` again (decode a fresh value into the same
  storage).
- Pass to `_write` (encode the zero value, which produces a
  defined byte sequence per the schema's zero semantics).
- Pass to `_free` again (idempotent per
  [REQ-FN-018](REQ-FN-018.md)).

## Rationale

The equivalence of post-`_free` and post-`_init` states is what
makes message-struct reuse work. A typical hot loop reads many
messages into the same struct, calling `_free` between reads:
the contract here guarantees the second `_read` does not see
stale state from the first.

The "byte-identical, not merely semantically equal" wording is
deliberate. A semantic-only guarantee would let `_free` leave
some fields in a different but still-valid zero (e.g. setting a
sentinel to a non-default sentinel value). That would interact
badly with `memcmp` and with caller code that initializes
related state by `memset`.

The "safe to `_read` again" clause is the practical consequence:
no buffer-management dance, no explicit re-init between reads.

## Verification

- `tests/runtime/test_free_returns_to_init.c` exercises one
  representative of each field shape (scalar, optional, array,
  string, choice, union) and `memcmp`s post-`_free` to
  post-`_init`.
- The auto-roundtrip harness implicitly exercises this
  property: round-trip relies on a single struct passed
  through `_read` → `_write` → `_free` → `_read` cleanly.

## Notes

The implementation strategy is uniform: `_free` walks every
field, releases through the allocator if the sentinel is set,
then `memset`s the entire struct to zero at the end. The
`memset` is the explicit "return to init state" step.

`memset` of the whole struct is also what guarantees the
post-`_free` byte-identical-to-post-`_init` property: padding
bytes are zeroed as well, even though their values are
otherwise unspecified.
