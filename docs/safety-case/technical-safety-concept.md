---
work_product: ISO 26262-4 §6 — Technical safety concept
status: draft
phase_target: 5
---

# Technical safety concept — `zserio-c`

## Purpose

The technical safety concept (TSC) refines the functional safety
concept by specifying **how** each safety goal is implemented in
the code and verified in CI. Every functional safety requirement
from `functional-safety-concept.md` is decomposed into one or more
technical mechanisms here, and each mechanism is anchored to a
specific source file, a specific test (or static analysis run),
and a specific CI gate.

This document is intended to be read alongside the FSC. Tables in
the FSC summarise the safety case; tables here describe the
implementation in enough detail that an auditor can verify each
claim by inspecting the named artifacts.

## Implementation mechanisms by safety goal

### SG-1 — No out-of-bounds memory access

**Mechanism: per-call buffer-bound check.**

Every `zserio_bitstream_read_*` and `zserio_bitstream_write_*`
in `runtime/src/bitstream.c` computes `bit_index + n_bits` and
compares it against `bit_capacity` before dereferencing the buffer
byte. On overflow, the call returns `ZSERIO_ERR_END_OF_STREAM` or
`ZSERIO_ERR_BUFFER_OVERFLOW` and leaves the bitstream state
unchanged. Higher-level codecs propagate that status through
`ZSERIO_TRY` (see `runtime/include/zserio/status.h`) — no codec
ignores a status return.

| Element                              | Verifies                                                                |
| ------------------------------------ | ----------------------------------------------------------------------- |
| `runtime/src/bitstream.c`            | Source of the bound check on every primitive read/write                 |
| `tests/runtime/test_bitstream.c`     | `test_read_past_end`, `test_write_overflow`, `test_writer_seek_past_capacity` |
| `tests/runtime/test_failure_modes.c` | `test_h01_read_past_end_returns_eof`, `test_h01_write_past_end_returns_overflow` |

### SG-2 — Bounded execution time on any input

**Mechanism: structural loop bounds.**

No loop in `runtime/src/*.c` reads its iteration count from
unauthenticated wire bytes. The varint decoders cap iterations at
the type's `max_bytes` (2 for varuint16, 4 for varuint32, …, 9 for
varuint). The packed-delta decoder reads `num_elements` once at
init time and treats it as the loop bound for the remaining
elements. Counter values out of range produce
`ZSERIO_ERR_INVALID_ENCODING` rather than a longer loop.

| Element                              | Verifies                                                                |
| ------------------------------------ | ----------------------------------------------------------------------- |
| `runtime/src/varint.c` `read_varuint_impl` / `read_varint_impl` | Loop runs at most `max_bytes` iterations, parameterised at the call site |
| `tests/runtime/test_failure_modes.c` | `test_h02_varuint16_decoder_is_bounded_by_max_bytes`, `test_h02_truncated_varuint_returns_eof` |
| `tests/runtime/test_varint.c`        | `test_read_eof_propagation` and the round-trip suite                    |

### SG-3 — Allocator failure propagation

**Mechanism: status check after every vtable call.**

Every call to `zserio_allocator_t::vtable->alloc` (or `realloc`) in
the runtime is followed by a `NULL` check on the returned pointer.
On `NULL`, the call returns `ZSERIO_ERR_OUT_OF_MEMORY` and free's
any allocations already made along this code path, so the integrator
never sees a partially-constructed value.

| Element                              | Verifies                                                                |
| ------------------------------------ | ----------------------------------------------------------------------- |
| `runtime/include/zserio/allocator.h` | Vtable contract: alloc may return NULL                                  |
| `runtime/src/bytes.c`, `string.c`, `extern.c` | Status check + cleanup on alloc failure                        |
| `tests/runtime/test_failure_modes.c` | `test_h03_failing_allocator_propagates_out_of_memory`                   |
| `tests/runtime/test_{bytes,string,extern}.c` | Tracking-allocator tests with `fail_alloc = 1`                  |

### SG-4 — Integrity delegation documented

**Mechanism: documentation, not code.**

Hazards H-04 and H-05 are out-of-scope at the item level. The
delegation is recorded in `item-definition.md` § "Assumed system
context" and in `hazard-analysis.md` H-04 / H-05 rows.

### SG-5 — Freedom from interference

**Mechanism: no static mutable state.**

Every public API entry point takes a `zserio_bitstream_reader_t *`,
`zserio_bitstream_writer_t *`, `zserio_allocator_t *`, or
`zserio_delta_context_t *` from the caller. The reader/writer/
context structs are caller-allocated; the runtime never touches a
file-scope variable that could carry state across calls. The
allocator's `userdata` pointer is the entire span of mutable state
the runtime can observe outside its own caller-supplied structs.

The optional `zsc_trace_*` developer facility in `bitstream.c` is
the only file-scope state in the runtime; it is gated behind
`-DZSERIO_TRACE_ENABLED` and never compiled into production builds
(see `docs/misra-deviations/` for the MISRA implications).

| Element                              | Verifies                                                                |
| ------------------------------------ | ----------------------------------------------------------------------- |
| `requirements/safety/REQ-FS-007.md`  | Statement of the property                                               |
| `runtime/src/*.c`                    | grep for `^static [^( ]+\s+s_` would surface any file-scope state; only `zsc_trace_*` matches, and only under the ifdef |
| `tests/runtime/test_failure_modes.c` | `test_h06_two_readers_disjoint_state`, `test_h06_two_writers_disjoint_state` |
| `tests/runtime/test_bitstream.c`     | `test_two_readers_independent`, `test_round_trip_random`                |

### SG-6 — Statically analysable control flow

**Mechanism: cppcheck MISRA strict mode + forbidden-symbol grep.**

Three layers stack here:

1. **Rule 17.2 (no recursion).** Enforced by cppcheck's MISRA
   addon. Strict mode (`tools/run_misra.sh --strict`) fails the
   build on any reported violation; no deviation entry exists for
   17.2.
2. **No `setjmp`/`longjmp`.** Enforced by the forbidden-symbol grep
   in `tools/run_misra.sh`. The grep covers `setjmp`, `longjmp`,
   `sigsetjmp`, `siglongjmp`, `_setjmp`, `_longjmp`. Any hit fails
   the CI gate.
3. **No runtime reflection.** REQ-FS-008 documents the property;
   the generator emits no type-info structures. The `tools/run_misra.sh`
   smoke check verifies no reflection symbol is generated.

| Element                              | Verifies                                                                |
| ------------------------------------ | ----------------------------------------------------------------------- |
| `tools/run_misra.sh`                 | Runs cppcheck addon strict + forbidden-symbol greps; CI-gated           |
| `.github/workflows/ci.yml`           | `misra` job runs the above on every commit                              |
| `requirements/safety/REQ-FS-002.md`, `REQ-FS-003.md`, `REQ-FS-008.md` | Properties stated and verification method declared              |

### SG-7 — Wire-format byte-equality with upstream

**Mechanism: round-trip cross-validation against upstream cpp.**

For each schema in `tests/xvalidation/schemas.txt` and each seed in
`SEEDS`, the harness:

1. Generates C code from our generator and cpp code from upstream's
   cpp extension into a per-schema work dir.
2. Compiles a C driver against our runtime and a cpp driver
   against upstream's runtime.
3. Drives the C side with the seed to produce `bytes_c.bin`.
4. Reads `bytes_c.bin` through the cpp driver and re-writes to
   `bytes_cpp.bin`.
5. `cmp bytes_c.bin bytes_cpp.bin` — byte-equal proves the C
   encoder and the cpp encoder produced the same wire bytes from
   the same value.

| Element                              | Verifies                                                                |
| ------------------------------------ | ----------------------------------------------------------------------- |
| `tests/xvalidation/run_xvalidation.sh` | Harness driver                                                        |
| `tests/xvalidation/schemas.txt`      | Schema corpus (currently 182 entries)                                   |
| `.github/workflows/ci.yml`           | `xvalidation` job runs the harness with `SEEDS="1 2 7 42 999"` per commit |
| `third_party/zserio` (submodule at `v2.18.1`) | The reference encoder we compare against                        |

### SG-8 — Bounded per-function stack frame

**Mechanism: gcc `-fstack-usage` + CI gate.**

The `tools/measure_stack_usage.py` script configures a `-fstack-usage`
build, parses the resulting `.su` files, and (in `--strict` mode)
fails on either:

- a function with a frame larger than `MAX_FRAME_BYTES = 256`, or
- a function with a `dynamic` / `bounded` qualifier (i.e. variable-
  size frame: alloca, VLA).

Combined with REQ-FS-003 (no recursion), this bounds the worst-case
total stack. The same gate also runs an exact longest-weighted-path
traversal over the unioned per-TU call graph (gcc
`-fcallgraph-info=su,da`) and rejects any chain whose summed frames
exceed `MAX_PATH_BYTES = 1024`. Current observed max per-function
frame is 160 B (`zserio_read_extern`); current observed max
transitive chain is 512 B over 7 frames.

| Element                              | Verifies                                                                |
| ------------------------------------ | ----------------------------------------------------------------------- |
| `tools/measure_stack_usage.py`       | Measurement + gate                                                      |
| `.github/workflows/ci.yml`           | `stack` job runs the gate per commit                                    |
| `docs/safety-case/stack-budget.md`   | Methodology, threshold rationale, limitations                           |

### SG-9 — Tool qualification (open)

**Mechanism: planned.**

Four tool-qualification records are open work items in
`safety-plan.md` § Work products: compiler (gcc), static analyzer
(cppcheck), coverage tool (lcov / gcov), and the generator itself.
Until those records exist, SG-9 is the only safety goal not fully
discharged by the CI gates. The integrator's qualification process
will need to replay the criteria we establish.

## Mechanism-to-gate matrix

The same content as the per-goal sections above, flattened into a
single audit-friendly table:

| Mechanism                                | Source file                              | Verifying test / tool                          | CI job                                          |
| ---------------------------------------- | ---------------------------------------- | ---------------------------------------------- | ----------------------------------------------- |
| Buffer-bound check                       | `runtime/src/bitstream.c`                | `test_bitstream.c`, `test_failure_modes.c` H-01 | `build-and-test (gcc/clang)`                    |
| Bounded loops                            | `runtime/src/varint.c`, `delta.c`        | `test_varint.c`, `test_failure_modes.c` H-02    | `build-and-test (gcc/clang)`                    |
| Allocator NULL propagation               | `runtime/src/{bytes,string,extern}.c`    | `test_{bytes,string,extern}.c`, `test_failure_modes.c` H-03 | `build-and-test (gcc/clang)`                    |
| No static mutable state                  | `runtime/src/*.c`                        | `test_failure_modes.c` H-06, `test_bitstream.c::test_two_readers_independent` | `build-and-test (gcc/clang)`                    |
| No recursion                             | All runtime + generated code             | cppcheck MISRA Rule 17.2 strict                | `misra`                                         |
| No setjmp/longjmp                        | All runtime + generated code             | `tools/run_misra.sh` forbidden-symbol grep     | `misra`                                         |
| No runtime reflection                    | Generator templates                      | `tools/run_misra.sh` REQ-FS-008 smoke check    | `misra`                                         |
| Wire-format byte-equality                | Whole runtime + generated code           | `tests/xvalidation/`                           | `xvalidation`                                   |
| Per-function frame ≤ 256 B               | All runtime functions                    | `tools/measure_stack_usage.py --strict`        | `stack`                                         |
| Coverage thresholds                      | `runtime/src/*.c`                        | `tools/check_coverage.py`                      | `coverage`                                      |
| Size regression                          | `runtime/libzserio_c_runtime.a`          | `tools/check_size_regression.py`               | `size`                                          |
| Requirements traceability                | `requirements/`, `tests/`, `tools/`      | `tools/check_traceability.py`                  | `traceability`                                  |
| Tool qualification (open)                | n/a                                      | Planned                                        | n/a (Phase 5 open)                              |

## Acceptance criterion for the TSC

This concept is "draft" until every row in the
mechanism-to-gate matrix points to:

1. an artifact that exists in the repo, and
2. a CI job that runs that artifact on every commit, and
3. an evidence trail (test name, gate name, or both) that an
   auditor can re-execute in a single CI run.

Every row except the open "tool qualification" row currently
satisfies all three. The TSC moves from `draft` to `accepted` when
the four open tool-qualification records land and the work-product
table in `safety-plan.md` is complete.

## Cross-references

- Functional safety concept: `functional-safety-concept.md`
- HARA: `hazard-analysis.md`
- Item definition: `item-definition.md`
- Safety plan: `safety-plan.md`
- Stack-budget analysis: `stack-budget.md`
- Architecture: `../architecture.md`
- Coding rules: `../coding-rules.md`
- MISRA deviations: `../misra-deviations/`
