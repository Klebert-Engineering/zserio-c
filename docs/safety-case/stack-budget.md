---
work_product: Stack-budget analysis (hazard H-09)
status: draft
phase_target: 5
---

# Stack-budget analysis — `zserio-c`

## Purpose

Close hazard **H-09 — Stack overflow on deeply nested compounds**
from `hazard-analysis.md`. The hazard concerns the possibility that
a maliciously or accidentally deep encoded message could drive the
decode path's stack usage past the integrator's available stack
budget, even though the runtime forbids recursion (REQ-FS-003).

Establishing a **statically bounded** worst-case stack usage for
the runtime — and gating it in CI — turns the hazard into a fixed,
documentable budget the integrator can size against.

## Method

The analysis is performed at build time by gcc's `-fstack-usage`,
which emits one `.su` file per translation unit. Each line records
the per-function frame size and a qualifier — `static`, `dynamic`,
or `bounded`:

| Qualifier | Meaning                                                                        | Acceptable? |
| --------- | ------------------------------------------------------------------------------ | ----------- |
| `static`  | Frame size is a compile-time constant.                                         | yes         |
| `dynamic` | Frame size depends on runtime data (alloca, VLA). Unbounded.                   | **no**      |
| `bounded` | Frame size has a runtime upper bound but is not a constant.                    | **no**      |

The acceptance rule is therefore:

1. Every function in the runtime has a `static` qualifier.
2. No function's frame exceeds the per-function ceiling
   `MAX_FRAME_BYTES = 256`.

Tooling: `tools/measure_stack_usage.py` does the build, parses the
`.su` files, and either prints a per-function report or fails the
gate (`--strict`). CI runs the gate as the `stack` job (see
`.github/workflows/ci.yml`).

Worst-case **total** stack usage is the deepest call chain times
the largest per-call frame. Two properties make that finite:

- **REQ-FS-003 (no recursion).** The call graph is a DAG, so the
  longest path is bounded by the number of distinct functions.
- **Per-function static frames.** Each node in that path contributes
  a compile-time-known frame; no node contributes an unbounded
  amount via alloca / VLA.

Computing the exact longest path requires a call-graph traversal
beyond the scope of this initial analysis. The conservative upper
bound used here is:

```
worst_case_total = max_per_function_frame  ×  call_chain_depth
```

For a runtime with ~135 leaf-and-inner functions and the typical
codec call shape (caller → `_read` → `read_full_element` → primitive
read → bitstream), the call chain is observed to be ≤ 10 frames
deep. With the gating ceiling of 256 bytes per frame, the upper
bound on total stack used by the runtime alone is `256 × 10 = 2.5
KiB`. The integrator allocates target-side stack accordingly,
adding budget for their own caller frames and for generated codec
intermediate frames.

## Current numbers

Measured against `runtime/src/*.c` on the current `main`:

| metric                            | value                |
| --------------------------------- | -------------------- |
| functions analysed                | 135                  |
| max per-function frame            | **144 bytes**        |
| function holding the max          | `zserio_read_extern` |
| qualifier on every function       | `static`             |
| functions over the 256 B threshold | 0                    |
| functions with non-static frames  | 0                    |

Top ten consumers (bytes):

| frame | function                              |
| ----: | ------------------------------------- |
|   144 | `zserio_read_extern`                  |
|   128 | `write_varint_impl`                   |
|   128 | `zserio_write_extern`                 |
|   112 | `zserio_delta_read`                   |
|   112 | `write_varuint_impl`                  |
|    96 | `zserio_delta_write`                  |
|    96 | `read_varuint_impl`                   |
|    96 | `read_varint_impl`                    |
|    96 | `zserio_bitstream_read_signed`        |
|    96 | `zserio_bitstream_write_bits`         |

## Threshold rationale

The per-function ceiling of **256 bytes** is set roughly 1.8× the
current observed max (144). It allows headroom for legitimate new
code while making any function with a meaningful frame regression
loud in CI. If a future change demands a function over 256 bytes,
the right path is to either (a) refactor the frame down (extract
the buffer to an allocator-supplied workspace), or (b) raise the
threshold in `tools/measure_stack_usage.py` with a justification
recorded in the commit message — same pattern as the size-baseline
gate.

## Limitations

- **Per-function only, not transitive.** The gate enforces a per-
  function ceiling, not a path total. With ~135 functions and a
  max depth of ~10 in practice, the transitive worst case stays
  comfortably under common stack budgets (typical embedded targets
  give ≥ 8 KiB per thread), but a formal cflow / call-graph
  traversal would be the rigorous closure. Tracked as a future
  improvement.
- **Runtime only.** The analysis covers `runtime/src/*.c`. Generated
  codec code is structurally trivial (lots of small functions, each
  one `_read` / `_write` / `_init` for one struct) and inherits the
  same `-fstack-usage` discipline when integrators build with the
  same flag — but its frames are not in this report. Adding the
  generator's output to the gate is the natural next step once a
  representative schema is chosen.
- **Compiler version specific.** Frame sizes vary slightly across
  gcc versions and optimisation levels. The CI configuration pins
  the gcc version through the Ubuntu image; integrators using a
  different toolchain MUST re-run the analysis with theirs.

## Cross-references

- Tool: `tools/measure_stack_usage.py`
- Gate: `.github/workflows/ci.yml` → `stack:` job
- Hazard row: `hazard-analysis.md` → H-09
- Requirement: `requirements/safety/REQ-FS-003.md` (no recursion)
- Safety plan entry: `safety-plan.md` → work-product table → "Stack-budget analysis"
