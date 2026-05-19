---
work_product: ISO 26262-3 §7 — Functional safety concept
status: draft
phase_target: 5
---

# Functional safety concept — `zserio-c`

## Purpose

The functional safety concept (FSC) bridges the hazard analysis
and the technical implementation. It states **what** properties
the item must hold to neutralise each hazard, in terms intelligible
to system integrators, and **where** in the item each property is
allocated. It is independent of how the property is implemented —
that mapping is the job of the technical safety concept.

Scope and boundaries follow `item-definition.md`. The hazard
catalogue this concept addresses is `hazard-analysis.md`.

## Safety goals

From each hazard in the HARA, one safety goal:

| Goal ID  | Statement                                                                                                          | Hazard(s) it neutralises |
| -------- | ------------------------------------------------------------------------------------------------------------------ | ------------------------ |
| **SG-1** | The runtime SHALL NOT access memory outside the caller-supplied bitstream buffer.                                  | H-01                     |
| **SG-2** | The runtime's worst-case execution time on any input SHALL be bounded by a constant times the input buffer length. | H-02                     |
| **SG-3** | The runtime SHALL surface allocator failure to the caller as a defined status code without dereferencing the failed allocation. | H-03         |
| **SG-4** | Wire-format payload integrity is delegated to the caller. The runtime SHALL document this assumption.              | H-04, H-05               |
| **SG-5** | The runtime SHALL provide freedom from interference between independent codec instances.                           | H-06                     |
| **SG-6** | The runtime SHALL provide deterministic, statically analysable control flow (no recursion, no reflection, no dynamic dispatch beyond a fixed traits vtable). | H-07, H-09 |
| **SG-7** | The bytes the runtime produces SHALL be identical to the bytes the reference implementation produces from the same value at the pinned upstream version. | H-08 |
| **SG-8** | The runtime SHALL provide a statically bounded stack-frame size per function.                                      | H-09                     |
| **SG-9** | Tool failure modes that affect generated or verified artifacts SHALL be documented as tool qualification records.  | H-10                     |

## Functional safety requirements (FSR)

Functional safety requirements are derived from safety goals.
Rather than introducing a parallel identifier space, the FSR layer
of this safety case is anchored to the existing `requirements/`
set. Each safety goal lists the existing requirements that
discharge it; new entries are added to `requirements/safety/`
when a goal is not already covered.

| SG    | Discharged by (existing requirements)                                       | Verification mechanism                                                                                     |
| ----- | --------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------- |
| SG-1  | REQ-FS-006 (public API input validation at the boundary), REQ-FN-020 (output validity conditioned on ZSERIO_OK status) | Unit tests in `tests/runtime/test_failure_modes.c` (H-01); per-primitive boundary tests across `tests/runtime/test_*.c` |
| SG-2  | REQ-FS-005 (bounded loops)                                                  | `tests/runtime/test_failure_modes.c::test_h02_varuint16_decoder_is_bounded`; static analysis of all loop bounds |
| SG-3  | REQ-FS-001 (allocation only via allocator vtable), REQ-FN-016 (encoder no-allocation), REQ-FN-017 (decoder vtable-only allocation), REQ-FN-018 (_free safe on partial state) | `tests/runtime/test_failure_modes.c::test_h03_failing_allocator_propagates_out_of_memory`; tracking-allocator tests across `test_bytes.c`, `test_string.c`, `test_extern.c` |
| SG-4  | Documented in `item-definition.md` § "Assumed system context" and `hazard-analysis.md` H-04/H-05; REQ-DM-006 (schema evolution constructs) | Item-definition review                                                                                     |
| SG-5  | REQ-FS-007 (freedom from interference between codec instances)              | `tests/runtime/test_failure_modes.c::test_h06_two_readers_disjoint_state`, `test_h06_two_writers_disjoint_state`; `test_bitstream.c::test_two_readers_independent` |
| SG-6  | REQ-FS-002 (no non-local control flow), REQ-FS-003 (no recursion), REQ-FS-008 (no runtime reflection), REQ-FN-006 (no recursion at import load) | `tools/run_misra.sh` strict-mode static analysis (REQ-FS-001/002/003 enforced via the addon + forbidden-symbol grep) |
| SG-7  | REQ-DM-002 (encode/decode mutual inverses), REQ-DM-003 (canonical encoding), REQ-DM-005 (wire-format spec owned), REQ-FN-001 (interoperability with upstream v2.18.1), REQ-FN-007 (generator determinism), REQ-FN-021 (encoder canonical), REQ-FN-022 (round-trip byte equality), REQ-FN-023 (schema-source-order fields) | `tests/xvalidation/run_xvalidation.sh` (CI), 182 schemas × 5 seeds per run, byte-equal vs cpp encoder + bidirectional auto-roundtrip       |
| SG-8  | REQ-FS-003 (no recursion) **plus** the per-function frame ceiling enforced by the stack CI gate (`tools/measure_stack_usage.py --strict`) | Stack-budget CI gate (`stack` job) per commit                                                              |
| SG-9  | None yet; planned tool-qualification records (Phase 5 open items); REQ-DM-004 (schema-language spec owned), REQ-DM-005 (wire-format spec owned), REQ-DM-007 (versioning policy, proposed) | See `safety-plan.md` § Work products — four open items for compiler / cppcheck / coverage / generator      |

Every safety goal except SG-9 is currently fully discharged by a
combination of requirement(s) and an in-CI verification mechanism.
SG-9 is the gating Phase-5 work that remains.

## Allocation to architecture elements

The HARA mitigations apply to specific architecture layers from
`docs/architecture.md`. The mapping below shows where in the item
each safety goal is enforced:

| Layer                                       | Goals primarily enforced here                              | Notes                                                                                       |
| ------------------------------------------- | ---------------------------------------------------------- | ------------------------------------------------------------------------------------------- |
| Bitstream layer (`runtime/src/bitstream.c`) | SG-1 (buffer bound), SG-5 (no shared state), SG-8 (frames) | Every read/write is checked against `bit_capacity`; reader/writer state is caller-supplied. |
| Primitives + varints (`runtime/src/{primitives,varint,float,extern,bytes,string}.c`) | SG-2 (bounded loops on hostile encodings), SG-1 (range checks) | Each codec is a bounded loop or a closed-form transform; no inputs drive iteration count beyond `max_bytes`. |
| Delta-packing (`runtime/src/delta.c`)       | SG-1, SG-6 (traits vtable is read-only static const), SG-8 | The traits-driven path uses a fixed-shape vtable in `.rodata`; no dynamic dispatch over schema-level types. |
| Allocator interface (`runtime/include/zserio/allocator.h` + `src/allocator.c`) | SG-3 (NULL propagation), SG-5 (vtable + userdata is the entire shared surface) | Caller supplies the vtable and the userdata pointer.                                        |
| Generated code (per schema)                 | SG-7 (wire format), SG-6 (no reflection), SG-3 (allocator-routed) | Generated by `zserio_c.jar`; verified by `tests/xvalidation/` and `tools/run_misra.sh`'s forbidden-symbol grep over generated output. |

## Boundary with the integrator

The safety case explicitly delegates some responsibilities to the
integrator. These are derived from the HARA rightmost column and
re-stated here so the integration interface is unambiguous:

1. **Buffer ownership.** The integrator supplies the byte buffer
   and its size to `zserio_bitstream_reader_init` /
   `zserio_bitstream_writer_init`. Both functions verify the
   buffer pointer is non-NULL and the size is non-zero. Internal
   reads and writes are bounded by that size.
2. **Allocator policy.** The integrator chooses the allocator
   implementation (reference libc wrapper, fixed pool, or other).
   The runtime never assumes any policy beyond "vtable returns
   non-NULL on success and NULL on failure".
3. **Payload integrity.** Zserio's wire format has no checksum or
   message authentication code. The integrator wraps the byte
   stream in an integrity layer when end-to-end protection is
   required (H-04, SG-4).
4. **Schema versioning.** Schemas are part of the trusted input
   set. The integrator pins the schema version per channel; the
   runtime makes no version-skew claim (H-05, SG-4).
5. **Toolchain qualification.** The integrator selects the production
   toolchain. Our tool-qualification records (planned, SG-9) describe
   the criteria we used in development; the integrator's qualification
   process replays those criteria against their toolchain.
6. **Target-side stack budget.** The runtime guarantees a per-function
   stack ceiling. The integrator allocates target-side thread stacks
   large enough to absorb that ceiling times the deepest expected
   call chain, plus their own caller frames.

## Cross-references

- HARA: `hazard-analysis.md`
- Item definition: `item-definition.md`
- Safety plan: `safety-plan.md`
- Stack-budget analysis: `stack-budget.md`
- Architecture: `../architecture.md`
- Existing requirements: `../../requirements/`
- Failure-mode tests: `../../tests/runtime/test_failure_modes.c`
- Wire conformance: `../../tests/xvalidation/`
- MISRA enforcement: `../../tools/run_misra.sh`, `../misra-deviations/`
