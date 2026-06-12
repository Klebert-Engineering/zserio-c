---
id: REQ-FN-026
title: Schema constraints enforced on the read path
type: functional
status: accepted
verification: test
---

## Statement

A schema field declared with a constraint expression of the
form:

```zserio
T fieldName : <expr>;
```

SHALL have the constraint `<expr>` evaluated after the field's
value has been read but before the next field is decoded. The
evaluation context is `self` at its current state of partial
population (all fields prior to and including `fieldName` are
populated; subsequent fields are in their post-`_init` zero
state).

If `<expr>` evaluates to a false (zero) value, `_read` SHALL:

1. Stop further decoding of the current type.
2. Return `ZSERIO_ERR_CONSTRAINT_VIOLATED`.
3. Leave `self` in a state safe to `_free` against the same
   allocator ([REQ-FN-018](REQ-FN-018.md)).

If `<expr>` evaluates to a true (non-zero) value, decoding
continues normally.

The `_write` path SHALL NOT evaluate constraint expressions.
The integrator is responsible for ensuring values they encode
satisfy the schema constraints; the encoder's job is to
serialize the values they pass in.

## Rationale

Constraint expressions are the schema's mechanism for
*structural* validity assertions that cannot be expressed by
the type system alone (e.g. "this `uint8` must be < 128", "this
array length must equal a previously-encoded field's value").
They are part of the schema's contract with the decoder: a
bitstream that violates a constraint is malformed.

Read-side enforcement protects the runtime against malformed
inputs (deliberately or accidentally crafted) before the
malformed values propagate to caller code. The status code
`ZSERIO_ERR_CONSTRAINT_VIOLATED` lets the caller distinguish
"this is a malformed message" from "the message is truncated"
or "we ran out of memory".

Not evaluating constraints on write is a deliberate
performance trade-off: write-path correctness is the
integrator's responsibility, and adding constraint checks
there would double the work the encoder does. Tests can verify
constraints separately at the application layer (e.g. a
property-based test that constructs invalid struct values and
asserts the encoder produces output the decoder rejects under
constraint).

## Verification

- The cross-validation harness includes constraint-using
  schemas (`array_lengthof_constraint`, `choice_constraints`,
  `constraints_in_templates`, …). The expected positive cases
  (constraint satisfied) verify the harness's value selection,
  not the requirement; the requirement is verified by:
- `tests/runtime/test_constraint_violation.c`, which exercises:
  - Constraint satisfied → `_read` returns OK.
  - Constraint violated → `_read` returns
    `ZSERIO_ERR_CONSTRAINT_VIOLATED`.
  - Post-violation struct safe to `_free`.
- A negative test that hand-crafts a violating bitstream is
  added per harness where the constraint is non-trivial.

## Notes

Constraint expressions are evaluated using the same expression
evaluator as parameter-binding expressions
([REQ-FN-024](REQ-FN-024.md)). The evaluator runs in two's
complement modular integer arithmetic, with side-effect-free
semantics, mirroring the wire-format spec
([REQ-DM-005](../domain/REQ-DM-005.md)).

This requirement intentionally leaves "decoder ordering of
constraint evaluation relative to field decode" specified as
*after decode, before next field*. This matches upstream and
makes constraint expressions over the current field's value
trivially representable.
