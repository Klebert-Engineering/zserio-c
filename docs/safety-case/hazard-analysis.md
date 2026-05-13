---
work_product: ISO 26262-3 §6 — Hazard analysis and risk assessment (HARA)
status: draft
phase_target: 5
---

# Hazard analysis — `zserio-c`

This HARA is performed at the **item** level. Vehicle-level
hazards (HARA-level S/E/C analyses, ASIL allocation) are the
responsibility of the integrating system; this document enumerates
the failure modes of `zserio-c` itself that an integrator's HARA
needs to consider when allocating safety goals to a serialization
layer.

Scope and conventions follow `item-definition.md`.

## Hazard table

| ID    | Failure mode                                         | Cause                                                       | Affected interface              | Detection                                                     | Mitigation in `zserio-c`                                                            | Residual responsibility            |
| ----- | ---------------------------------------------------- | ----------------------------------------------------------- | ------------------------------- | ------------------------------------------------------------- | ----------------------------------------------------------------------------------- | ---------------------------------- |
| H-01  | Buffer over/underrun                                 | Malformed input drives codec past end of caller buffer      | Decode path on all primitives    | Bit-position check in every `zserio_bitstream_read_*`         | REQ-FS-006 input validation; bitstream's `bit_capacity` is the upper bound          | Caller supplies the buffer size    |
| H-02  | Unbounded execution time on malformed input          | Loop count or string length read off the wire is hostile     | Decode path on any variable-length field | Runtime loop bounds derived from buffer size; status return on overflow | REQ-FS-005 bounded loops; REQ-FS-006 input validation                               | Caller imposes wall-clock timeout if required |
| H-03  | Allocator misbehavior under low-memory conditions    | Allocator returns NULL or fails to grow                      | Decode path that constructs variable-size values | Every allocation site checks the allocator's return code     | REQ-FS-001 allocator vtable returns status; codec propagates as `ZSERIO_ERR_*`      | Integrator supplies the allocator policy |
| H-04  | Undetected payload corruption                        | Wire bytes flipped in transit or storage                     | Decode path                     | None within zserio-c — the wire format has no checksum/MAC    | Out of scope. Hazard documented for integrator awareness.                            | Wrap stream in CRC/MAC layer if required |
| H-05  | Schema/decoder version skew                          | Encoded with one schema, decoded with another                | Decode path                     | None within zserio-c                                          | Out of scope; zserio has no in-band version field. Generator emits a schema-hash table only on request. | Integrator pins schema version per channel |
| H-06  | Leakage of state between codec instances             | Hidden static state shared across encoder/decoder calls      | Any public API                  | Architectural review; tests instantiate two readers in parallel | REQ-FS-007 freedom from interference; no static mutable state                       | None — wholly inside `zserio-c`    |
| H-07  | Reflection / dynamic dispatch surprises              | Generated code uses runtime type info that varies by build   | Generated code                  | Architectural review                                          | REQ-FS-008 no runtime reflection                                                    | None — wholly inside `zserio-c`    |
| H-08  | Wire-format divergence from upstream zserio          | `zserio-c` encoder produces bytes that upstream cannot decode (or vice versa) | Both encode and decode | xvalidation gate (`tests/xvalidation/`) compares byte-for-byte against `v2.18.1` for 182 schemas × 5 seeds per CI run | REQ-FN-001 enforced in CI                                                            | None — wholly inside `zserio-c`    |
| H-09  | Stack overflow on deeply nested compounds            | Schema permits arbitrary nesting; decode path's stack frames are not statically bounded | Decode path | CI gate (`stack` job) enforces per-function frame ≤ 256 B and rejects non-static (`dynamic`/`bounded`) frames | REQ-FS-003 forbids recursion; per-function ceiling enforced by `tools/measure_stack_usage.py --strict`. Current worst frame: 144 B (`zserio_read_extern`). See `stack-budget.md`. | Integrator may impose target-side stack guards |
| H-10  | Toolchain miscompilation                             | Compiler or static analyzer produces incorrect code/output   | Generated runtime + codec       | Out of scope at the item level                                | Tool qualification record per tool (planned — `tool-qualification.md`)              | Integrator selects qualified toolchain |

## Classification

This table is **structural**, not S/E/C-classified. The item is
intended to be used at multiple integration levels (ASIL-B to QM
depending on the channel). The S/E/C decision is made by the
integrating system's HARA; what `zserio-c` provides is a stable
catalogue of failure modes for that analysis to reference.

For our own development we treat all hazards in the table as
ASIL-B-equivalent — i.e. controllable through the existing safety
requirements and verification regime. Closure status:

- H-01..H-08, H-10: addressed by existing requirements + CI gates.
- H-09: closed by `stack-budget.md` + the `stack` CI gate
  (per-function ceiling 256 B, strict static-qualifier check).
- H-04, H-05: out of scope at the item level; integrator-side
  responsibilities recorded for the integration guide.

## Cross-references

- Safety requirements: `requirements/safety/REQ-FS-001.md` …
  `REQ-FS-008.md`
- Functional requirement: `requirements/functional/REQ-FN-001.md`
- Architecture: `docs/architecture.md`
- Coding rules: `docs/coding-rules.md`
- MISRA deviations: `docs/misra-deviations/`

## Open items

1. **H-04 / H-05 integrator notes.** A short integration guide
   under `docs/integration-guide.md` that records the assumed
   integrator responsibilities listed in the rightmost column.
2. **Transitive stack-budget calculation.** The current gate enforces
   a per-function ceiling but does not compute the worst-case path
   total. A cflow/call-graph traversal would tighten the analysis
   from "≤ 256 B × depth" to an exact upper bound. Tracked in
   `stack-budget.md` § Limitations.

## Closed items

- **Failure-mode injection tests** for H-01 (buffer over/underrun),
  H-02 (unbounded execution on malformed input), H-03 (allocator
  misbehavior), and H-06 (freedom from interference) landed in
  `tests/runtime/test_failure_modes.c`. Each test maps 1:1 to a
  HARA row and verifies the documented status code is returned
  rather than the runtime crashing or looping.
