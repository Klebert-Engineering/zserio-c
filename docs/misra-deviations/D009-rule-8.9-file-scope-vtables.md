---
id: D009
rule: MISRA C:2012 Rule 8.9
classification: advisory
status: accepted
approvers: Fabian Klebert
---

## Statement of the rule (paraphrased)

Rule 8.9 advises that an object should be defined at block scope if
its identifier only appears in a single function.

## Where it is deviated

Two sites, verified against cppcheck 2.21 by removing the suppression
and re-running `tools/run_misra.sh`:

- `runtime/src/allocator.c:54` — `LIBC_VTABLE`, referenced only from
  `zserio_allocator_libc`
- `runtime/src/allocator.c:110` — `ARENA_VTABLE`, referenced only from
  `zserio_allocator_arena`

Each is a `static const zserio_allocator_vtable_t` whose address is
returned to the caller inside a `zserio_allocator_t` value and
dereferenced for the lifetime of every object the allocator produces.

## Justification

Both objects could legally be moved to block scope (a block-scope
`static const` retains static storage duration, so the escaping
address would stay valid), but file scope is the deliberate choice:

- The vtable is the binding between the file-scope callback functions
  defined immediately above it (`libc_alloc` / `libc_realloc` /
  `libc_free`; `arena_alloc` / `arena_free`) and the public
  constructor below it. Defining it at file scope keeps the three
  parts of the allocator backend — callbacks, vtable, constructor —
  adjacent and in dependency order.
- The object's address escapes the function. Hiding an
  address-escaping object inside a function body makes the escape
  easy to miss in review; at file scope, the `static const` storage
  duration and the escape via `&LIBC_VTABLE` are visible at a glance.
- The hazard the rule guards against — a wider-than-necessary scope
  inviting unintended mutation or reuse — does not apply to an
  immutable `static const` object with internal linkage.

The rule is **advisory**. The suppression in
`tools/cppcheck-suppressions.txt` is rule-wide rather than
file-scoped because the suppressions file is shared with the
generated-output gate (`tools/run_misra_generated.sh`), where file
names are schema-dependent and cannot be path-scoped.

## Mitigation

- Both objects are `static const` with internal linkage: no external
  visibility, no runtime rebinding of the function pointers.
- The allocator constructors and both backends are covered by the
  unit suite and by every xvalidation run (every generated `_read`
  allocates through one of these vtables).
- New single-function objects default to block scope; a file-scope
  definition is accepted in review only for this escaping-vtable
  shape.

## Reviewer notes

Re-triaged 2026-07 during the pre-release suppression audit: an
earlier companion-file rationale classified this finding as a tool
false positive ("used cross-function within their TU"). Inspection
showed both vtables are in fact referenced from exactly one function
each, so the finding is correct and is recorded here as an accepted
deviation instead.
