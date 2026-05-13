# Safety case

This is the root document for the ISO 26262 safety case for `zserio-c`.
Phase 5 (safety case maturation) is open. The technical work products
live under `docs/safety-case/`; this file is the navigation entry
point and records the high-level target.

## Target

- **Standard**: ISO 26262:2018 (parts 1, 6, 8, 9 most relevant).
- **ASIL target for v1**: ASIL-B, with architectural headroom toward
  ASIL-D (no design choices that would have to be reversed).
- **Coding standard**: MISRA C:2012 (with amendments 1–4), enforced by
  cppcheck `--addon=misra` in CI and reviewed by a qualified tool prior
  to a release claiming compliance.

## Work products

The full work-product matrix with status, owner, and dependencies
lives in `docs/safety-case/safety-plan.md` § Work products. The
short summary, current as of the Phase 5 launch:

Done (CI-gated, evidence reproducible per commit):

- [x] Software architectural design — `docs/architecture.md`.
- [x] Safety requirements REQ-FS-001..008 — `requirements/safety/`.
- [x] Functional requirement REQ-FN-001 (wire compatibility) and
      performance requirement REQ-PF-002 (size gate).
- [x] Requirements traceability — `tools/check_traceability.py`.
- [x] MISRA enforcement — `tools/run_misra.sh --strict`, zero
      findings; deviations under `docs/misra-deviations/`.
- [x] Coding rules — `docs/coding-rules.md`.
- [x] Coverage gate (line ≥ 99%, branch ≥ 85%, function = 100%) —
      `tools/check_coverage.py`.
- [x] Size regression gate — `tools/measure_size.py` +
      `tools/check_size_regression.py`.
- [x] Wire-format conformance harness — `tests/xvalidation/`,
      182 schemas × 5 seeds per CI run.

Draft (delivered in the Phase 5 launch commit):

- [/] Safety plan — `docs/safety-case/safety-plan.md`.
- [/] Item definition — `docs/safety-case/item-definition.md`.
- [/] Hazard analysis (HARA, item-level) —
      `docs/safety-case/hazard-analysis.md`.
- [/] Stack-budget analysis — `docs/safety-case/stack-budget.md` +
      `tools/measure_stack_usage.py` + CI `stack` job. Closes hazard
      H-09; current worst frame 144 B, gate at 256 B per function.
- [/] Failure-mode injection tests — `tests/runtime/test_failure_modes.c`.
      One test per actionable HARA row (H-01, H-02, H-03, H-06);
      remaining rows handled by other gates or out of scope.
- [/] Functional safety concept —
      `docs/safety-case/functional-safety-concept.md`. 9 safety goals
      mapped to existing `requirements/` entries and architecture
      layers.
- [/] Technical safety concept —
      `docs/safety-case/technical-safety-concept.md`. Per-goal
      implementation mechanisms with mechanism-to-gate matrix.

Open (Phase 5 work items, in dependency order):
- [ ] Software unit verification report.
- [ ] Software integration and verification report.
- [ ] Tool qualification records — compiler, cppcheck, lcov/gcov,
      generator.
- [ ] Safety case report (final assembly).

## Decisions already taken

These are recorded in the requirements set under `requirements/safety/`
and are not negotiable without an explicit waiver process:

- Allocation only via the allocator vtable (REQ-FS-001).
- No `setjmp`/`longjmp` (REQ-FS-002).
- No recursion (REQ-FS-003).
- No floating-point in control flow (REQ-FS-004).
- All loops have a statically determinable upper bound (REQ-FS-005).
- Public-API input validation at the boundary (REQ-FS-006).
- Freedom from interference between codec instances (REQ-FS-007).
- No runtime reflection in generated code (REQ-FS-008).
- Byte-equal wire compatibility with upstream zserio v2.18.1
  (REQ-FN-001).

## Open questions

- Tool qualification path for the Java generator in Phase 2 — qualified
  as TCL3 or treated as a development tool with the *generated code*
  being the safety-relevant artifact subject to inspection?
- Bounded-stack analysis approach: static analysis tool, manual budget
  per call site, or both?
- Random-input fuzzing vs. structured property-based testing for the
  decode path's robustness against malformed inputs.
