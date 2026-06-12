# cppcheck suppressions — rationale

`tools/cppcheck-suppressions.txt` is consumed by `cppcheck
--suppressions-list`. The format the tool accepts is **one ID per
line, no comments, no blank lines**, optionally followed by
`:file[:line]`. This document is the human-readable companion
recording why each entry exists.

## Format

```
<id>[:<file>[:<line>]]
```

For MISRA-rule-specific suppressions, ALSO add a corresponding
deviation document under `docs/misra-deviations/` recording rule,
scope, justification, mitigation, and approver. The deviation doc
and the suppression entry MUST cite each other.

## Current entries

### Library-mode false positives

| Suppression                | Why                                                                 |
|----------------------------|---------------------------------------------------------------------|
| `unusedFunction`           | The runtime is a library; its public symbols are called from outside our tree. cppcheck cannot see those callers. |
| `missingIncludeSystem`     | cppcheck warns when `<stddef.h>`, `<stdint.h>` etc. cannot be resolved on its include path. They are always present on supported toolchains. |
| `misra-c2012-8.7`          | "Functions and objects should not be defined with external linkage if they are referenced in only one translation unit." cppcheck library-mode is blind to external callers; every public runtime function gets flagged. |
| `misra-c2012-8.9`          | "An object should be defined at block scope if its identifier only appears in a single function." Triggered by file-scope `static const` lookup tables (varint max-value tables, allocator vtable) used cross-function within their TU; cppcheck doesn't follow the use chain. |

### Documented deviations (each has a deviation doc)

| Suppression                          | Scope                          | Deviation doc |
|--------------------------------------|--------------------------------|----------------|
| `misra-c2012-15.5`                   | project-wide (early returns)   | [D001](../docs/misra-deviations/D001-rule-15.5-multiple-returns.md) |
| `misra-c2012-21.3:runtime/src/allocator.c`  | libc-backed allocator wrapper  | [D002](../docs/misra-deviations/D002-rule-21.3-allocator-libc.md) |
| `misra-c2012-21.15:runtime/src/float.c` (+ `runtime/include/zserio/float.h`) | IEEE 754 bit-pattern memcpy | [D003](../docs/misra-deviations/D003-rule-21.15-memcpy-float-bits.md) |
| `misra-c2012-11.5:runtime/src/string.c` (+bytes.c, extern.c, allocator.c) | allocator `void *` cast  | [D004](../docs/misra-deviations/D004-rule-11.5-allocator-void-cast.md) |
| `misra-c2012-18.4:runtime/src/bitstream.c` (+ string.c, bytes.c, extern.c, allocator.c) | byte-offset pointer arithmetic on the bitstream buffer | [D005](../docs/misra-deviations/D005-rule-18.4-buffer-pointer-arithmetic.md) |
| `misra-c2012-19.2` (generator output only, via `run_misra_generated.sh`) | tagged C union for zserio `choice` / `union` types | [D007](../docs/misra-deviations/D007-rule-19.2-tagged-union.md) |

### Generator-output-only suppressions (in `tools/run_misra_generated.sh` CLI)

These are passed as `--suppress=...` flags by the misra-generator
CI job and are deliberately kept out of the shared
`cppcheck-suppressions.txt` so the runtime gate stays strict on them.

| Suppression                 | Why                                                                                            |
|-----------------------------|------------------------------------------------------------------------------------------------|
| `unmatchedSuppression`      | The shared suppressions file scopes entries to runtime paths (`*float.c`, `*string.c`, …) that don't exist in generated code, producing ~120 informational "Unmatched suppression" notices per generator-output run. |
| `constParameterPointer`     | cppcheck style, not MISRA. Generated `_free` may or may not write to `self` depending on whether the type has nested allocations; the template emits the same signature either way. Marking `_write` / `_observe` as const is an API-quality follow-up. |
| `misra-c2012-2.5`           | Unused macro. Triggered by the public `*_TAG_CASE*_FIELD` discriminator constants the generator emits per union arm — consumed by user code that cppcheck can't see. Same justification as `unusedFunction`. |
| `misra-c2012-11.5` / `-11.8` | Same accepted pattern as D004 (allocator `void *` cast). D004 explicitly covers the overlapping 11.8 finding. Applied via CLI rather than a file-scoped entry because generated file names are schema-dependent. |
| `misra-c2012-19.2`          | Same deviation as the [D007](../docs/misra-deviations/D007-rule-19.2-tagged-union.md) row in "Documented deviations" above — repeated here only because it is applied as a CLI flag, not via the shared suppressions file. |

### Style / arithmetic categorical accepts

| Suppression          | Why                                                                  |
|----------------------|----------------------------------------------------------------------|
| `misra-c2012-7.2`    | Triggered by `INT64_MIN`, which is a signed integer constant by definition; a `U` suffix would be wrong. False positive on cppcheck's default rule check. |
| `misra-c2012-10.4`   | The signed-aware `abs_delta` comparison in `delta.c` is intentionally polymorphic between signed and unsigned interpretation; the rule's "same essential type category" demand cannot be honored here without duplicating the function. |
| `misra-c2012-10.7`   | Routine bit-shift arithmetic where the shift amount is necessarily narrower than the value being shifted. Each affected site has explicit casts that document intent. Code-review checked. |
| `misra-c2012-10.8`   | One site in `float.c` casting `((int32_t)1 - signed_exponent16)` to `uint32_t`. Value is non-negative by branch analysis; cast is well-defined for the input domain. Code-review checked. |
