# Coding rules

## Language

- **C99 strict.** `-std=c99 -pedantic`. No GNU extensions.
- No `//` comments inside generated code header files used by ASIL builds —
  treat `/* … */` as the portable form. (Project source can use either.)
- No variable-length arrays.
- No flexible array members in public APIs.
- No `_Generic` (it is C11).

## MISRA C:2012

The project targets MISRA C:2012 with amendments 1–4 (so the active rule
set is the 2023 consolidated edition's content, applied to a C99 baseline).

- `cppcheck --addon=misra` runs in CI and must pass on the runtime and
  generated code with zero unjustified violations.
- Any rule deviation is recorded in a per-deviation document under
  `docs/misra-deviations/` (created when the first deviation is needed).
  A deviation states: rule, scope, justification, mitigation, approver,
  reviewer.
- Deviations may **not** be added retroactively to suppress findings on
  existing code without going through the deviation process.

## Naming

- Types: `zserio_<name>_t`.
- Functions: `zserio_<module>_<verb>` (e.g. `zserio_bitstream_read_uint32`).
- Macros: `ZSERIO_<NAME>`.
- File-internal symbols: `static`, lowercase, no prefix.
- Public headers live in `runtime/include/zserio/`. Nothing else.

## Headers

- Every public header has a `#ifndef ZSERIO_<PATH>_H_` include guard.
- No `extern "C"` (we are C, not C++; if a C++ caller needs us, that's
  their problem to wrap).
- Public headers do not include `<stdio.h>` or `<assert.h>` — diagnostics
  are returned via status codes.

## Style

- Indent: 4 spaces, no tabs.
- Column limit: 100.
- Brace style: Linux (opening brace on same line for control flow, on
  next line for function definitions).
- Pointer alignment: right (`int *p`, not `int* p`).
- `clang-format` is the source of truth — see `.clang-format`.

## Comments

- Default to writing none. The code should explain itself.
- Write a comment when there is a non-obvious *why*: a hidden constraint,
  a workaround for a tool bug, a choice driven by a specific requirement
  ID.
- Reference requirements by ID when a piece of code exists *because of* a
  requirement, e.g. `/* See REQ-FS-005: bounded loop. */`.
- Do not narrate what the code does. Do not reference the current task
  or PR.

## Tests

- Every public function in the runtime has at least one positive and one
  negative test.
- Every requirement under `requirements/` is referenced by at least one
  test (the traceability check in CI enforces this).
- 100% line and branch coverage for runtime and generated code, gated in
  CI. Code that genuinely cannot be covered (e.g. defensive `default:`
  on an exhaustive `switch`) is annotated with `/* GCOV_EXCL_LINE */`
  and justified in review.
