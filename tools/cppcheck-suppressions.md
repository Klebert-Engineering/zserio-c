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
and the suppression entry MUST cite each other. The only exception
is a demonstrated cppcheck false positive, which is recorded below
(with the code site and why the tool is wrong) instead of in the
deviation register.

## Current entries

### Library-mode false positives

| Suppression                | Why                                                                 |
|----------------------------|---------------------------------------------------------------------|
| `unusedFunction`           | The runtime is a library; its public symbols are called from outside our tree. cppcheck cannot see those callers. |
| `missingIncludeSystem`     | cppcheck warns when `<stddef.h>`, `<stdint.h>` etc. cannot be resolved on its include path. They are always present on supported toolchains. |
| `misra-c2012-8.7`          | "Functions and objects should not be defined with external linkage if they are referenced in only one translation unit." cppcheck library-mode is blind to external callers; every public `zserio_*` function definition in `runtime/src/*.c` is flagged (44 findings across all runtime TUs on cppcheck 2.21, e.g. `bitstream.c:184`, `string.c:42`, `primitives.c:26`). All of them are declared in the public headers under `runtime/include/zserio/` and called by generated code and by the test suites, outside the scan. |
| `misra-c2012-2.3` / `misra-c2012-2.4` (`*service.h` only) | Rules 2.3/2.4 flag "unused" type declarations and tags. The `zserio_service` struct tag in `runtime/include/zserio/service.h:58` exists so the public API can forward-declare the type; consumers are generated code and integrator sources outside the scan. Flagged by cppcheck ≥ 2.21 in library mode; scoped to the one header so the rules stay armed everywhere else. |

### Rule-check false positives

| Suppression          | Code site | Why the tool is wrong                                                |
|----------------------|-----------|----------------------------------------------------------------------|
| `misra-c2012-7.2`    | `runtime/src/bitstream.c:111,112,127` — three entries of the `BIT_MASK` lookup table written as `UINT64_C(0x…)` | Rule 7.2 wants a `u`/`U` suffix on constants of unsigned type. `UINT64_C()` appends exactly that suffix (`ULL`) at expansion, but cppcheck's misra addon analyzes the unexpanded macro argument — a bare hex literal — and flags the three table entries whose values do not fit a signed type. Verified on cppcheck 2.21: the identical constant spelled `0x…ULL` directly is not flagged. The compiled constant always carries the suffix. |
| `misra-c2012-10.4`   | `runtime/src/delta.c:57` — `is_signed ? ((int64_t)a > (int64_t)b) : (a > b)` in `abs_delta` | Rule 10.4 requires the second and third operands of `?:` to share an essential type category. Both operands here are relational expressions, which are essentially Boolean under the MISRA essential type model, so the code complies. cppcheck instead derives the operands' essential types from the cast operands *inside* the comparisons (signed on one arm, unsigned on the other) and reports a mismatch. Verified on cppcheck 2.21: a variant with the same `(int64_t)` casts on both arms is not flagged, confirming the addon keys off the inner operands rather than the comparison results. |

### Documented deviations (each has a deviation doc)

| Suppression                          | Scope                          | Deviation doc |
|--------------------------------------|--------------------------------|----------------|
| `misra-c2012-15.5`                   | project-wide (early returns)   | [D001](../docs/misra-deviations/D001-rule-15.5-multiple-returns.md) |
| `misra-c2012-21.3:runtime/src/allocator.c`  | libc-backed allocator wrapper  | [D002](../docs/misra-deviations/D002-rule-21.3-allocator-libc.md) |
| `misra-c2012-21.15:runtime/src/float.c` (+ `runtime/include/zserio/float.h`) | IEEE 754 bit-pattern memcpy | [D003](../docs/misra-deviations/D003-rule-21.15-memcpy-float-bits.md) |
| `misra-c2012-11.5:runtime/src/string.c` (+bytes.c, extern.c, allocator.c, service.c) and the companion `misra-c2012-11.8` entries (string.c, bytes.c, extern.c, service.c) | allocator `void *` cast (11.8 is the overlapping secondary finding) | [D004](../docs/misra-deviations/D004-rule-11.5-allocator-void-cast.md) |
| `misra-c2012-18.4:runtime/src/string.c` (+bytes.c, extern.c, allocator.c) | byte-offset pointer arithmetic on buffer walkers | [D005](../docs/misra-deviations/D005-rule-18.4-buffer-pointer-arithmetic.md) |
| `misra-c2012-20.7:runtime/src/varint.c` (+ `misra-c2012-20.10`) | type-parametric delta-traits macros | [D006](../docs/misra-deviations/D006-rule-20.7-type-param-macros.md) |
| `misra-c2012-19.2` (generator output only, via `run_misra_generated.sh`) | tagged C union for zserio `choice` / `union` types | [D007](../docs/misra-deviations/D007-rule-19.2-tagged-union.md) |
| `misra-c2012-10.8`                   | composite-expression casts on the bitstream/float bit-manipulation paths (`bitstream.c`, `float.c`, inlined hot-path headers). Rule-wide entry because this file is shared with the generated-output gate, where file names are schema-dependent. | [D008](../docs/misra-deviations/D008-rule-10.8-composite-expression-casts.md) |
| `misra-c2012-8.9`                    | `LIBC_VTABLE` / `ARENA_VTABLE` in `runtime/src/allocator.c` (file-scope const vtables referenced from a single constructor each). Rule-wide entry for the same shared-file reason. | [D009](../docs/misra-deviations/D009-rule-8.9-file-scope-vtables.md) |

### Generator-output-only suppressions (in `tools/run_misra_generated.sh` CLI)

These are passed as `--suppress=...` flags by the misra-generator
CI job and are deliberately kept out of the shared
`cppcheck-suppressions.txt` so the runtime gate stays strict on them.

| Suppression                 | Why                                                                                            |
|-----------------------------|------------------------------------------------------------------------------------------------|
| `unmatchedSuppression`      | The shared suppressions file scopes entries to runtime paths (`*float.c`, `*string.c`, …) that don't exist in generated code, producing ~120 informational "Unmatched suppression" notices per generator-output run. |
| `constParameterPointer`     | cppcheck style, not MISRA. Generated `_free` may or may not write to `self` depending on whether the type has nested allocations; the template emits the same signature either way. Marking `_write` / `_observe` as const is an API-quality follow-up. |
| `constParameterCallback`    | cppcheck style, not MISRA. The generated service dispatch must match the `zserio_service_t.call_method` signature (a `void *impl` first parameter), so that parameter cannot be made const even in dispatches that only read through it. |
| `misra-c2012-2.5`           | Unused macro. Triggered by the public `*_TAG_CASE*_FIELD` discriminator constants the generator emits per union arm — consumed by user code that cppcheck can't see. Same justification as `unusedFunction`. |
| `misra-c2012-11.5` / `-11.8` | Same accepted pattern as D004 (allocator `void *` cast). D004 explicitly covers the overlapping 11.8 finding. Applied via CLI rather than a file-scoped entry because generated file names are schema-dependent. |
| `misra-c2012-19.2`          | Same deviation as the [D007](../docs/misra-deviations/D007-rule-19.2-tagged-union.md) row in "Documented deviations" above — repeated here only because it is applied as a CLI flag, not via the shared suppressions file. |
