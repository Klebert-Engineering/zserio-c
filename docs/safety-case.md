# Safety case

This is the root document for the ISO 26262 safety case for `zserio-c`.
At Phase 0 it is a structural placeholder. Each subsection becomes a
real document as the project matures.

## Target

- **Standard**: ISO 26262:2018 (parts 1, 6, 8, 9 most relevant).
- **ASIL target for v1**: ASIL-B, with architectural headroom toward
  ASIL-D (no design choices that would have to be reversed).
- **Coding standard**: MISRA C:2012 (with amendments 1–4), enforced by
  cppcheck `--addon=misra` in CI and reviewed by a qualified tool prior
  to a release claiming compliance.

## Work products to be produced

- [ ] Safety plan (when Phase 1 begins).
- [ ] Item definition (what `zserio-c` is, what its boundaries are,
      what its assumed system context is — likely a serializer/
      deserializer library inside a larger ECU stack).
- [ ] Hazard analysis (HARA / HAZOP). The hazards we expect to enumerate:
      buffer over/underrun, malformed input causing unbounded execution,
      allocator misbehavior under low-memory conditions, undetected
      payload corruption.
- [ ] Functional safety concept.
- [ ] Technical safety concept.
- [ ] Software architectural design (this lives in `docs/architecture.md`
      and is referenced from here when frozen).
- [ ] Software unit verification report (test coverage + MISRA pass).
- [ ] Software integration and verification report.
- [ ] Tool qualification (TQ) records for: compiler, static analyzer,
      coverage tool, generator (the generator itself is on the safety
      path because it produces certified code).
- [ ] Safety case report (the assembly of the above into a single argument).

## Decisions already taken

These are recorded in the requirements set under `requirements/safety/`
and are not negotiable without an explicit waiver process:

- Allocation only via the allocator vtable (REQ-FS-001).
- No `setjmp`/`longjmp` (REQ-FS-002).
- No recursion (REQ-FS-003).
- No floating-point in control flow (REQ-FS-004).
- All loops have a statically determinable upper bound (REQ-FS-005).
- Public-API input validation at the boundary (REQ-FS-006).

## Open questions

- Tool qualification path for the Java generator in Phase 2 — qualified
  as TCL3 or treated as a development tool with the *generated code*
  being the safety-relevant artifact subject to inspection?
- Bounded-stack analysis approach: static analysis tool, manual budget
  per call site, or both?
- Random-input fuzzing vs. structured property-based testing for the
  decode path's robustness against malformed inputs.
