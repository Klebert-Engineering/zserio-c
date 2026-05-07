---
id: REQ-FS-004
title: No floating-point in control flow
type: safety
status: accepted
phase_target: 1
verification: analysis
---

## Statement

Floating-point types (`float`, `double`, `long double`) SHALL appear only
in the encode and decode paths of fields whose schema type is
`float16`, `float32`, or `float64`. They SHALL NOT participate in branch
conditions, loop bounds, array indexing, allocation sizing, or status-code
production anywhere in the runtime or generated code.

## Rationale

Floating-point comparison and rounding behavior is target-dependent and
historically the source of safety-critical regressions. Restricting FP
to the bit-exact pack/unpack of payload values keeps control flow on
integer arithmetic only, which is fully deterministic across the
toolchains we expect (GCC, Clang, IAR, Green Hills).

This is also the practical path to running on targets without an FPU:
schemas without float fields produce code that can be compiled with
`-mno-float` or equivalent.

## Verification

- A static analysis check rejects floating-point operands in conditional
  expressions, loop conditions, and switch/case values inside `runtime/`
  and generated code.
- Code review checklist item: any FP variable must be local to a
  `pack_floatN` / `unpack_floatN` helper or to a generated field
  encode/decode block.

## Notes

- IEEE 754 bit-exact pack/unpack functions are written using
  integer-only operations on the bit pattern (memcpy through a
  same-sized integer), avoiding any actual FP arithmetic on the
  control-flow path.
