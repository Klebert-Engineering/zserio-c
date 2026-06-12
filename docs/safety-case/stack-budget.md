---
work_product: Stack-budget analysis (hazard H-09)
status: draft
---

# Stack-budget analysis — `zserio-c`

## Purpose

Close hazard **H-09 — Stack overflow on deeply nested compounds**
from `hazard-analysis.md`. The hazard concerns the possibility that
a maliciously or accidentally deep encoded message could drive the
decode path's stack usage past the integrator's available stack
budget. REQ-FS-003 bounds this at runtime — the reader's depth gate
rejects input past `max_depth` — and this analysis bounds the
per-level cost that multiplies against that ceiling.

Establishing a **statically bounded** worst-case stack usage for
the runtime — and gating it in CI — turns the hazard into a fixed,
documentable budget the integrator can size against.

## Method

The analysis is performed at build time by **two** complementary gcc
options, both enabled in the dedicated build:

- `-fstack-usage` emits one `.su` file per translation unit, with
  the per-function frame size and a qualifier — `static`, `dynamic`,
  or `bounded`:

  | Qualifier | Meaning                                                              | Acceptable? |
  | --------- | -------------------------------------------------------------------- | ----------- |
  | `static`  | Frame size is a compile-time constant.                               | yes         |
  | `dynamic` | Frame size depends on runtime data (alloca, VLA). Unbounded.         | **no**      |
  | `bounded` | Frame size has a runtime upper bound but is not a constant.          | **no**      |

- `-fcallgraph-info=su,da` emits one `.ci` file per translation unit
  (VCG format) with the per-TU call graph: each `node:` carries the
  same frame-size + qualifier the `.su` does, and each `edge:`
  records one caller → callee relationship.

Combined, the two artifacts allow the analysis to enforce three
properties:

1. Every function in the runtime has a `static` qualifier.
2. No function's frame exceeds the per-function ceiling
   `MAX_FRAME_BYTES = 256`.
3. The longest path through the (unioned, cross-TU) call graph,
   weighted by per-function frame, does not exceed the transitive
   ceiling `MAX_PATH_BYTES = 1024`. The traversal is preceded by a
   cycle check — the runtime call graph is recursion-free per
   REQ-FS-003, so the analyzer fails with the cycle path if it
   ever finds one.

Tooling: `tools/measure_stack_usage.py` does the build, parses both
file kinds, runs both gates, and either prints a report or fails
under `--strict`. CI runs the gate as the `stack` job (see
`.github/workflows/ci.yml`).

The transitive bound is exact under the no-recursion guarantee — it
is the deepest call chain reported by the unified call graph for the
build, not an over-approximation. The build does **not** enable LTO,
so static-inline helpers in headers emit out-of-line copies and
contribute their own frame; this gives a deliberately pessimistic
bound that is conservative for any production build (LTO collapses
those frames into the caller's). The integrator allocates target-
side stack accordingly, adding budget for their own caller frames
and for generated codec intermediate frames.

## Current numbers

Measured against `runtime/src/*.c` on the current `main`:

| metric                                    | value                |
| ----------------------------------------- | -------------------- |
| functions analyzed                        | 126                  |
| max per-function frame                    | **160 bytes**        |
| function holding the max                  | `zserio_read_extern` |
| qualifier on every function               | `static`             |
| functions over the 256 B per-function gate | 0                    |
| functions with non-static frames          | 0                    |
| **max call-chain stack (transitive)**     | **512 bytes**        |
| call chains over the 1024 B transitive gate | 0                  |
| cycles in the call graph                  | 0                    |

Top ten per-function consumers (bytes):

| frame | function                              |
| ----: | ------------------------------------- |
|   160 | `zserio_read_extern`                  |
|   160 | `zserio_write_extern`                 |
|   128 | `write_varint_impl`                   |
|   128 | `zserio_read_bytes`                   |
|   128 | `zserio_read_string`                  |
|   112 | `zserio_delta_read`                   |
|   112 | `write_varuint_impl`                  |
|    96 | `zserio_delta_write`                  |
|    96 | `read_varuint_impl`                   |
|    96 | `read_varint_impl`                    |

Worst call chain (512 bytes, 7 frames):

```
zserio_read_extern (160)
  -> zserio_read_varsize (48)
    -> read_varuint_impl (96)
      -> zserio_bitstream_read_bits (64)
        -> zserio_bitstream_read_bits_impl (48)
          -> zserio_bitstream_load_cache_next (80)
            -> zserio_bitstream_parse64_be (16)
```

## Threshold rationale

The per-function ceiling of **256 bytes** is set roughly 1.6× the
current observed max (160). The transitive ceiling of **1024 bytes**
is roughly 2× the current observed max (512). Both ratios are
generous enough to absorb new code without churn while making any
regression loud in CI. If a future change demands a frame over
256 bytes or a chain over 1024, the right path is to either
(a) refactor the offender down (extract a workspace to allocator
storage, or restructure the call chain), or (b) raise the threshold
in `tools/measure_stack_usage.py` with a justification recorded in
the commit message — same pattern as the size-baseline gate.

## Limitations

- **Runtime only.** The analysis covers `runtime/src/*.c`. Generated
  codec code is structurally trivial (lots of small functions, each
  one `_read` / `_write` / `_init` for one struct) and inherits the
  same `-fstack-usage` discipline when integrators build with the
  same flag — but its frames are not in this report. Adding the
  generator's output to the gate is the natural next step once a
  representative schema is chosen.
- **Compiler version specific.** Frame sizes vary slightly across
  gcc versions and optimization levels. The CI configuration pins
  the gcc version through the Ubuntu image; integrators using a
  different toolchain MUST re-run the analysis with theirs.

## Cross-references

- Tool: `tools/measure_stack_usage.py`
- Gate: `.github/workflows/ci.yml` → `stack:` job
- Hazard row: `hazard-analysis.md` → H-09
- Requirement: `requirements/safety/REQ-FS-003.md` (bounded decode depth)
- Safety plan entry: `safety-plan.md` → work-product table → "Stack-budget analysis"
