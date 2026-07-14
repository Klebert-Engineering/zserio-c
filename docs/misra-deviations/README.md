# MISRA C:2012 deviations

Each deviation from a MISRA C:2012 rule used by the zserio-c runtime
or generated code is documented here, one file per deviation. The
file name is `D<NNN>-<short-slug>.md`, where `<NNN>` is the next
available three-digit number.

## File template

```markdown
---
id: D001
rule: MISRA C:2012 Rule X.Y
classification: required | advisory | mandatory
status: proposed | accepted | superseded
approvers: <names>
---

## Statement of the rule

(One-paragraph paraphrase. Do NOT reproduce the proprietary rule
text from the MISRA standard verbatim — cite by number only.)

## Where it is deviated

- File:line citations for each affected location.

## Justification

Why the rule cannot be followed at this site, with reference to
specific design decisions (e.g. REQ-FS-005 bounded-loops, performance
considerations, language limitations).

## Mitigation

How risk is contained: code review, unit-test coverage, defensive
checks, runtime contracts, etc.

## Reviewer notes

(Optional, populated during review.)
```

## Process

1. A new finding from `tools/run_misra.sh` is reviewed.
2. If the finding is a real bug, it is fixed in the code.
3. If the finding is a known acceptable deviation, a deviation file
   is created here AND a corresponding suppression entry is added to
   `tools/cppcheck-suppressions.txt`. Both entries cite each other.
4. If the finding is a cppcheck false positive, only the suppression
   entry is added (with rationale).

Suppressions added without deviation documentation MUST be temporary
and tracked in an issue.

## Current deviations

| ID | Rule(s) | Scope | Summary |
|---|---|---|---|
| [D001](D001-rule-15.5-multiple-returns.md) | 15.5 | project-wide | Early `return` on error paths is the project's chosen control-flow idiom. |
| [D002](D002-rule-21.3-allocator-libc.md) | 21.3 | `runtime/src/allocator.c` only | `zserio_allocator_libc` wraps `malloc` / `realloc` / `free` so callers can use libc through the same vtable as any other backend. |
| [D003](D003-rule-21.15-memcpy-float-bits.md) | 21.15 | `runtime/src/float.c` | `memcpy` used to extract IEEE-754 bit patterns — the standard idiom for type-punning floats without aliasing UB. |
| [D004](D004-rule-11.5-allocator-void-cast.md) | 11.5 (+ overlapping 11.8) | runtime + generated code | Allocator vtable returns `void *`; callers cast to the destination element type. |
| [D005](D005-rule-18.4-buffer-pointer-arithmetic.md) | 18.4 | `runtime/src/bitstream.c` + sibling buffer-walkers | Byte-offset pointer arithmetic on the bitstream's `uint8_t *` backing buffer. |
| [D006](D006-rule-20.7-type-param-macros.md) | 20.7, 20.10 | `runtime/src/varint.c` | Type-parametric `DELTA_TRAITS_{UNSIGNED,SIGNED}` macros — token-paste / type-name parameters are categorically outside the rules' precedence-bug intent. |
| [D007](D007-rule-19.2-tagged-union.md) | 19.2 | generator output only | Zserio `choice` / `union` types map to a tagged C `union` by design. Generator-output scope only, applied via `tools/run_misra_generated.sh`. |
| [D008](D008-rule-10.8-composite-expression-casts.md) | 10.8 | `runtime/src/bitstream.c`, `runtime/src/float.c` + inlined hot-path headers | Composite-expression casts on the bit-manipulation paths: widening casts that carry shift/mask arithmetic in `uint64_t`, and two branch-guarded signed/unsigned boundary casts. |
| [D009](D009-rule-8.9-file-scope-vtables.md) | 8.9 | `runtime/src/allocator.c` | `LIBC_VTABLE` / `ARENA_VTABLE` defined at file scope although each is referenced from a single constructor function — their addresses escape and the file-scope definition keeps callbacks, vtable, and constructor adjacent. |
