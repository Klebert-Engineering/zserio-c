---
work_product: ISO 26262-2 §6.4 — Safety case report
status: draft
phase_target: 5
---

# Safety case report — `zserio-c`

> This is the assembling argument. The supporting work products
> are the other files in this directory and the
> `requirements/` set. A reviewer reads this document first,
> then drills into the cited evidence.

## Argument summary

`zserio-c` is a from-scratch C99 implementation of the zserio
binary serialization format, intended for use in automotive
ECU software stacks up to ASIL-B (with architectural headroom
toward ASIL-D). The safety case asserts that, at every commit
to `main`:

1. **Functional correctness** — Wire bytes produced by the
   `zserio-c` encoder are byte-equal to those produced by the
   reference implementation (upstream `ndsev/zserio v2.18.1`)
   for every schema in the cross-validation corpus.
2. **Bounded execution** — Every loop in the runtime terminates
   in a number of iterations bounded by either a compile-time
   constant or the caller-supplied buffer size; no input
   drives unbounded computation.
3. **Bounded memory** — All allocation flows through a
   caller-supplied vtable; the runtime contains no direct heap
   calls and no static mutable state outside the optional
   developer-time trace facility.
4. **Bounded stack** — Every function in the runtime has a
   static-qualifier compile-time-known stack frame, ceilinged
   at 256 bytes per function. Combined with the no-recursion
   rule, the call-chain total is finite.
5. **Defended boundary** — Every public API entry point
   validates its inputs and returns a defined `zserio_status_t`
   on failure; no internal precondition is implicit.
6. **Coding standard** — Code complies with MISRA C:2012
   amendments 1–4 as enforced by cppcheck strict mode; every
   deviation is documented under `docs/misra-deviations/`.

Each assertion is gated in continuous integration. The case
therefore rests on the soundness of the CI gates, on the
qualification of the tools that drive them, and on the
documented hazard analysis that motivated their thresholds.

## Item summary

See `item-definition.md`. In brief: `zserio-c` consists of a
runtime library (`libzserio_c_runtime.a`) plus a Java generator
extension (`zserio_c.jar`) that emits codec sources from `.zs`
schemas. The integration boundary is the runtime's public API.
The intended deployment is an ECU serialization layer inside a
larger system, with a caller-supplied allocator policy.

## Hazard summary

The hazard analysis (`hazard-analysis.md`) enumerates 10 item-level
hazards. Closure status:

| Hazard | Title                                            | Closure                                                                                      |
| ------ | ------------------------------------------------ | -------------------------------------------------------------------------------------------- |
| H-01   | Buffer over/underrun                             | REQ-FS-006 + `test_failure_modes.c::test_h01_*`                                              |
| H-02   | Unbounded execution on malformed input           | REQ-FS-005 + `test_failure_modes.c::test_h02_*`                                              |
| H-03   | Allocator misbehavior                            | REQ-FS-001 + `test_failure_modes.c::test_h03_failing_allocator_propagates_out_of_memory`     |
| H-04   | Undetected payload corruption                    | Out of scope at item level (see `item-definition.md` § Assumed system context)               |
| H-05   | Schema/decoder version skew                      | Out of scope at item level                                                                   |
| H-06   | Leakage between codec instances                  | REQ-FS-007 + `test_failure_modes.c::test_h06_*`                                              |
| H-07   | Reflection / dynamic dispatch surprises          | REQ-FS-008 + `tools/run_misra.sh` smoke check                                                |
| H-08   | Wire-format divergence                           | REQ-FN-001 + `tests/xvalidation/` (CI-gated)                                                 |
| H-09   | Stack overflow on deeply nested compounds        | REQ-FS-003 + `tools/measure_stack_usage.py` (CI-gated; see `stack-budget.md`)                |
| H-10   | Toolchain miscompilation                         | Out of scope at item level; addressed by the four `tool-qualification-*.md` records          |

## Safety goal coverage

The functional safety concept (`functional-safety-concept.md`)
derives nine safety goals from the hazards above. Discharge
status at this report's snapshot:

| Goal | Discharge                                                                 | Status   |
| ---- | ------------------------------------------------------------------------- | -------- |
| SG-1 | REQ-FS-006 + boundary tests + xvalidation                                 | Verified |
| SG-2 | REQ-FS-005 + bounded-loop tests                                           | Verified |
| SG-3 | REQ-FS-001 + failing-allocator tests + forbidden-symbol grep              | Verified |
| SG-4 | Documented delegation to integrator                                       | Verified (documentation work product) |
| SG-5 | REQ-FS-007 + two-readers / two-writers tests                              | Verified |
| SG-6 | REQ-FS-002 + REQ-FS-003 + REQ-FS-008 + MISRA gate + forbidden-symbol grep | Verified |
| SG-7 | REQ-FN-001 + xvalidation gate                                             | Verified |
| SG-8 | REQ-FS-003 + stack CI gate                                                | Verified |
| SG-9 | Four tool-qualification records (gcc, cppcheck, lcov/gcov, generator)     | Verified in `draft`; moves to `accepted` once Open gaps in TQ records close |

## Implementation coverage

The technical safety concept (`technical-safety-concept.md`)
maps each safety goal to a specific source file, a verifying
test or tool, and a CI gate. The mechanism-to-gate matrix in
that document is the single audit-facing artefact; an auditor
can re-execute every claim by re-running the named CI jobs.

## Verification evidence

| Layer       | Report                                | Primary evidence                                  |
| ----------- | ------------------------------------- | ------------------------------------------------- |
| Unit        | `unit-verification.md`                | Coverage gate, MISRA gate, traceability, stack, size |
| Integration | `integration-verification.md`         | xvalidation gate, conformance/ndslive fixture     |

Both reports are snapshots against `main` HEAD. Per-release
versions are produced by replacing the snapshot tables with the
values from the release tag's CI run.

## Tool qualification

Four tools sit on the safety path. Each has a TQ record under
`docs/safety-case/`:

| Tool         | TCL  | Status                                                                                          |
| ------------ | ---- | ----------------------------------------------------------------------------------------------- |
| gcc          | TCL2 | Validated via build-and-test (gcc + clang independent), xvalidation, stack, coverage gates      |
| cppcheck     | TCL2 | Validated per rule category; one off-CI sanity exercise (Rule 17.2 recursion check) noted       |
| lcov / gcov  | TCL2 | Validated via spot-check of measured properties; gate uses raw `.info` data                     |
| Generator    | TCL1 | Strong detection (TD1) via xvalidation; MISRA gate on generator output landed in the `misra-generator` CI job (commit `c13bd41`) |

## Open items and residual risks

The safety case has three categories of open items:

1. **Open items already inside this case** (tracked in the
   relevant work product):
   - Random-input fuzzing of the decode path: structured
     property-style testing currently substitutes (see
     `integration-verification.md` § Known integration gaps).
   - *Closed:* cppcheck MISRA gate now runs strictly on
     generator output via the `misra-generator` CI job over a
     representative 13-schema set (see
     `tool-qualification-generator.md`).
   - *Closed:* Transitive worst-case stack total — the `stack`
     job now runs a longest-weighted-path traversal over the
     unioned per-TU call graph (gcc `-fcallgraph-info=su,da`)
     with a cycle check and a 1024 B ceiling (current observed
     max 512 B). See `stack-budget.md` § Method.

2. **Documents in `draft` status awaiting acceptance.** Every
   document in `docs/safety-case/` is currently at `draft`. The
   acceptance criterion for each is recorded in the document's
   own front matter. The case moves to `accepted` as a whole
   when the one remaining open item above is closed and the
   integration verification is replayed against the next release
   tag.

3. **Residual risks delegated to the integrator** (see
   `item-definition.md` § Assumed system context and
   `docs/integration-guide.md` for the integrator-facing
   checklist):
   - Payload integrity (CRC / MAC layer wraps the byte stream
     when end-to-end protection is required).
   - Schema version pinning per channel.
   - Production toolchain qualification (our TQ records describe
     the validation criteria; the integrator replays them
     against their toolchain).
   - Target-side stack budget sized for the documented per-
     function ceiling.

## Sign-off

| Role                  | Name             | Date              | Decision |
| --------------------- | ---------------- | ----------------- | -------- |
| Author                | Fritz Herrmann   | 2026-05-13        | Draft submitted |
| Independent reviewer  | (TBD)            | (TBD)             | (Pending) |
| Safety manager        | (TBD)            | (TBD)             | (Pending) |

Independent review is the gate that moves this report from
`draft` to `accepted`. The review pack consists of this file,
the entire `docs/safety-case/` directory, the `requirements/`
set, and the named CI job comments on the commit being
reviewed.

## Cross-references

- `safety-case.md` — navigation entry point for the case
- `README.md` — directory index
- `safety-plan.md` — work product matrix + sequencing
- `item-definition.md` — what `zserio-c` is
- `hazard-analysis.md` — failure-mode catalogue
- `functional-safety-concept.md` — goals and goal-to-requirement mapping
- `technical-safety-concept.md` — implementation mechanisms
- `unit-verification.md` — per-runtime-module evidence
- `integration-verification.md` — wire-format evidence
- `stack-budget.md` — H-09 closure
- `tool-qualification-{gcc,cppcheck,coverage,generator}.md` —
  per-tool qualification analyses
- `../misra-deviations/` — deviation register
- `../../requirements/` — requirements set
