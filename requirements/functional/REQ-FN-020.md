---
id: REQ-FN-020
title: Output validity conditioned on ZSERIO_OK status
type: functional
status: accepted
phase_target: 1
verification: test
---

## Statement

For every runtime and generated entry point that returns
`zserio_status_t`, output values written through pointer
parameters SHALL be valid for the caller's use only when the
returned status is `ZSERIO_OK`.

"Valid" means: the bytes in the output are the result the
function semantically computed. "Not valid" means: the caller
SHALL NOT assume any property of the bytes (some may have been
written, some may not have, partial state may exist), and SHALL
NOT consume those bytes as if the function had succeeded.

Specifically:

1. On `_read` returning non-OK, the populated portion of the
   `<t>_t` struct passed in SHALL NOT be consumed by the
   caller. The struct is still safe to `_free`
   ([REQ-FN-018](REQ-FN-018.md)).
2. On `_write` returning non-OK, the populated portion of the
   bitstream writer's underlying buffer SHALL NOT be consumed
   as a valid encoding.
3. On a bitstream-reader primitive returning non-OK, the value
   written to its output pointer SHALL NOT be consumed.

The runtime SHALL surface non-OK status before allowing
caller-visible state corruption. In particular, the runtime
SHALL NOT have a code path that mutates a struct then returns
non-OK without restoring the struct or arranging for `_free` to
clean up the mutation.

## Rationale

The output-validity rule is the *semantic contract* that the
status code anchors. Without it, a caller observing non-OK has
no defined behaviour to follow: a partial output that looks
plausible may be silently consumed and propagate corruption.

The rule is the positive counterpart of
[REQ-FS-002](../safety/REQ-FS-002.md), which forbids non-local
control flow. Together they form a complete contract: errors
travel by `zserio_status_t`, and outputs are only meaningful on
OK.

The "safe to `_free` on non-OK" clause is what bridges the
contract to the cleanup path: even though the output is invalid,
the cleanup operation is still valid, because `_free` is defined
to handle partial state ([REQ-FN-018](REQ-FN-018.md)).

## Verification

- The runtime unit tests for every fallible entry point cover
  at least one negative case and verify the output is not
  consumed.
- `tests/runtime/test_failure_modes.c` exercises the common
  failure modes (end-of-stream, buffer overflow, OOM) and
  verifies the output-pointer state is not relied on.
- Generated code's `_read` failure path is exercised by the
  cross-validation harness for several schemas under deliberate
  truncation; the auto-roundtrip harness validates the
  `_free`-after-failed-`_read` path.

## Notes

This requirement is what makes the `ZSERIO_TRY` propagation
pattern safe at every call site: the helper checks status and
short-circuits if non-OK without ever consuming an output.

Some upstream zserio bindings allow callers to consume partial
outputs in some modes. We do not — partial output is always
invalid here, by requirement. This is one place where the
safety-driven contract is *stricter* than upstream.
