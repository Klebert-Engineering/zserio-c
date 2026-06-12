---
id: REQ-FN-024
title: Parameter propagation in nested types
type: functional
status: accepted
verification: test
---

## Statement

When a parameterized compound type appears as a field of an
outer type, the generator SHALL emit code that propagates
parameter values from the outer instance to the inner instance
automatically.

Specifically, when a struct `Outer` contains a field of
parameterized type `Inner(P p)`, where `p` is sourced from a
schema expression `e` over `Outer`'s fields and parameters,
the generator SHALL emit, at the appropriate point in the
`_read` and `_write` paths of `Outer`:

```c
inner_set_parameters(&self->inner_field, /* eval(e, self) */);
```

The caller of `outer_set_parameters` SHALL set parameters only
on the outer instance. The caller SHALL NOT be required to
call `inner_set_parameters` directly.

This propagation SHALL apply transitively: a parameter that
needs to flow three levels deep through nested parameterized
types is propagated three levels deep by the generator without
caller intervention.

When a parameterized compound is an *array element*, the
generator SHALL emit per-element parameter propagation
inside the array's read/write loop.

## Rationale

Without automatic propagation, the schema's "parameter as a
runtime value passed to a nested type" semantics would force
callers to manually traverse the type tree at every
serialization site, which defeats the schema-driven property
([REQ-DM-001](../domain/REQ-DM-001.md)).

Propagation is a generator-side concern because the schema's
parameter-binding expressions can be arbitrary: they may
reference outer fields, outer parameters, or constants. Only the
generator has the AST to evaluate them in the right order at
the right point in the read/write path.

The transitive case is what makes deeply-nested parameterized
schemas work. NDS Live's attribute hierarchy in particular
relies on parameters that flow through multiple
template-instantiated layers.

The per-element propagation for arrays is the consequence of
the parameter possibly being array-index-dependent: the schema
may bind a parameter to the *array index* via `@index`, in
which case every element gets a different parameter value.

## Verification

- The cross-validation harness includes parameterized harnesses
  (`bitmask_param_choice`, `bool_param_choice`,
  `enum_param_choice`, `compound_indexed_offset_array`, …).
  Successful byte-equality against the upstream reference
  implies correct propagation.
- The bidirectional auto-roundtrip harness includes parameter
  chains and verifies they propagate the same way under read
  and write.
- `tests/parser/test_parameter_propagation.c` exercises:
  - Single-level propagation.
  - Two-level transitive propagation.
  - Array-element propagation with `@index`-bound parameter.

## Notes

The implementation strategy: during semantic analysis, every
parameter binding is annotated with the AST node (the outer
type's field or parameter) it references. At emit time, the
generator walks the binding annotation and emits a
`self->`-prefixed access on the bound source, with array index
adjustments where applicable.

This is one of the schema-language features that has no
simple positional encoding — it requires *evaluating*
expressions over `self`. The generator handles this without
introducing reflection or general-purpose expression
evaluation in the runtime
([REQ-FS-008](../safety/REQ-FS-008.md)).
