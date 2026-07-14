# Safety case

This is the root document for the ISO 26262 safety case for `zserio-c`.
The technical work products live under `docs/safety-case/`; this file
is the navigation entry point and records the high-level target.

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
short summary:

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
      183 schemas × 5 seeds per CI run.

Draft (delivered in the safety-case launch commit):

- [/] Safety plan — `docs/safety-case/safety-plan.md`.
- [/] Item definition — `docs/safety-case/item-definition.md`.
- [/] Hazard analysis (HARA, item-level) —
      `docs/safety-case/hazard-analysis.md`.
- [/] Stack-budget analysis — `docs/safety-case/stack-budget.md` +
      `tools/measure_stack_usage.py` + CI `stack` job. Closes hazard
      H-09; current worst per-function frame 160 B (gate 256 B) AND
      current worst transitive call-chain 512 B (gate 1024 B).
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
- [/] Tool qualification records — `docs/safety-case/tool-qualification-{gcc,cppcheck,coverage,generator}.md`.
      gcc/cppcheck/lcov-gcov at TCL2 with method (1)+(3) evidence;
      generator at TCL1 thanks to the xvalidation TD1 detection.

Draft (delivered subsequently):

- [/] Software unit verification report —
      `docs/safety-case/unit-verification.md`. Snapshot of per-
      runtime-module evidence against `main` HEAD.
- [/] Software integration and verification report —
      `docs/safety-case/integration-verification.md`. xvalidation
      (183 × 5) + private NDS.Live companion conformance harness
      end-to-end.
- [/] Safety case report —
      `docs/safety-case/safety-case-report.md`. Single entry
      document for a reviewer.

The safety case is structurally complete; every work product is at
`draft`. The case as a whole moves to `accepted` when the three
Open items in `safety-case-report.md` § Open items close.

## Decisions already taken

These are recorded in the requirements set under `requirements/safety/`
and are not negotiable without an explicit waiver process:

- Allocation only via the allocator vtable (REQ-FS-001).
- No `setjmp`/`longjmp` (REQ-FS-002).
- Bounded decode depth — recursion-free runtime, depth-gated
  generated readers (REQ-FS-003).
- No floating-point in control flow (REQ-FS-004).
- All loops have a statically determinable upper bound (REQ-FS-005).
- Public-API input validation at the boundary (REQ-FS-006).
- Freedom from interference between codec instances (REQ-FS-007).
- No runtime reflection in generated code (REQ-FS-008).
- Byte-equal wire compatibility with upstream zserio v2.18.1
  (REQ-FN-001).

## Open questions

(none — all previously-listed open questions closed; see below.)

Previously listed questions, now answered:

- Tool qualification path for the Java generator → TCL1 with the
  xvalidation gate as the detection mechanism. See
  `safety-case/tool-qualification-generator.md`.
- Bounded-stack analysis approach → per-function `-fstack-usage`
  gate with a 256 B ceiling, plus a transitive longest-weighted-
  path gate over the unioned `-fcallgraph-info=su,da` call graph
  with a 1024 B ceiling. See `safety-case/stack-budget.md`.
- Integrator responsibilities checklist → `docs/integration-guide.md`
  (consolidates H-04/H-05 integrator notes from the HARA's
  rightmost column).
- Random-input fuzzing of the decode path → libFuzzer + ASan +
  UBSan, two representative schemas, 60 s per target per commit,
  persistent corpus. See `safety-case/integration-verification.md`
  § How decode-path fuzzing runs in CI.
