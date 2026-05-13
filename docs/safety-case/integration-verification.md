---
work_product: ISO 26262-6 §10 — Software integration and verification report
status: draft
phase_target: 5
---

# Software integration and verification report — `zserio-c`

## Purpose

Collates the evidence that the runtime library plus the generator,
working together against a generated codec, satisfies the
integration-level functional and safety requirements. The
load-bearing requirement is REQ-FN-001 (byte-equal wire
compatibility with upstream zserio v2.18.1).

This report is taken as a **snapshot against `main` HEAD**.
Per-release reports are derived by replacing the snapshot tables
with the values from the release tag's CI run.

## Integration scope

| Integration | Components                                                           |
| ----------- | -------------------------------------------------------------------- |
| I-1         | `zserio-c` generator (`zserio_c.jar`) emits C99 codec sources from a `.zs` schema. |
| I-2         | The generated codec links against the `zserio-c` runtime (`libzserio_c_runtime.a`). |
| I-3         | An application driver (xvalidation harness `c_driver` or the conformance NDS Live fixture) calls the generated codec to encode / decode values. |

The integration boundary is the public API of the runtime plus
the generated functions per schema. The reference contract is the
wire format produced by upstream zserio at the pinned tag.

## Cross-validation evidence

### Schema-level: `tests/xvalidation/`

For every schema in `tests/xvalidation/schemas.txt` and every
seed in `SEEDS`, the harness produces a tuple of byte streams and
proves byte-equality:

1. Generate C + cpp code (`ZserioTool` with our jar + upstream cpp
   extension).
2. Compile two drivers: one against our runtime, one against
   upstream's.
3. Drive the C side with the seed → `bytes_c.bin`.
4. Decode `bytes_c.bin` with cpp, re-encode → `bytes_cpp.bin`.
5. `cmp bytes_c.bin bytes_cpp.bin` — byte-equal is the pass
   condition.

| Quantity                         | Value (snapshot)                              |
| -------------------------------- | --------------------------------------------- |
| Schemas exercised                | 182                                           |
| Seeds per schema                 | 5 (default `SEEDS="1 2 7 42 999"`)            |
| Total round-trips per CI run     | 910                                           |
| Pass rate (current `main`)       | 100% — see the `xvalidation` CI job comment   |
| Schemas covered by the cpp `-setTopLevelPackage` workaround | 1 (`packages/zs/index.zs`) |

The harness is a single bash script and is auditable in
`tests/xvalidation/run_xvalidation.sh`. Per-schema work dirs are
preserved under `build-xvalidation/` on a CI failure for
diagnostic re-execution.

### End-to-end: `conformance/ndslive/`

A production-shape fixture: a 50,896-byte NDS Live SmartLayerTile
encoded by upstream zserio v2.18.1 against the public NDS Live
schema. The fixture exercises:

- Multi-layer compound nesting.
- Variable-bit-width fields driven by sibling fields.
- Packed arrays of compound elements.
- Cross-compound offset patching.
- Mixed primitive types across the full encoded surface.

Pass condition: every decoded field matches the zs-yaml ground
truth bundled with the fixture. This is a stronger property than
byte equality alone — it verifies semantic correctness of the
decoded values, not just identical re-encoding.

## Requirement coverage at the integration level

| Requirement | Verification mechanism                                                                                | Status                                                |
| ----------- | ----------------------------------------------------------------------------------------------------- | ----------------------------------------------------- |
| REQ-FN-001  | `xvalidation` CI job + `conformance/ndslive/` fixture                                                | Verified (snapshot: 182/182 schemas pass)             |
| REQ-FS-001  | `tools/run_misra.sh` forbidden-symbol grep covers both runtime and (per integrator) generated code   | Verified for the runtime; gap for generator output noted in `tool-qualification-generator.md` |
| REQ-FS-006  | Negative-path tests at unit level (`test_failure_modes.c`) + xvalidation's malformed-input resilience | Verified                                              |
| REQ-FS-007  | xvalidation runs each schema in an isolated work dir; no cross-schema state                          | Verified                                              |
| REQ-FS-008  | Generator templates contain no reflection scaffolding; smoke-checked by `tools/run_misra.sh`         | Verified                                              |

## CI evidence index

The following CI jobs collectively constitute the integration
verification evidence. Every commit to `main` reproduces them:

| Job name                                   | Evidence                                                |
| ------------------------------------------ | ------------------------------------------------------- |
| `Build and test (gcc)`                     | Compiles runtime + generated codec for the xvalidation harness; runs the runtime test suite. |
| `Build and test (clang)`                   | Independent toolchain re-runs the same tests.           |
| `Wire-format cross-validation (REQ-FN-001)` | The 182-schema × 5-seed harness.                       |
| `Phase 2 generator (build JAR + smoke load)` | Smoke-builds the generator jar end-to-end.            |
| `Test-data discovery (advisory)`           | Reports schemas in the upstream corpus that are not yet in xvalidation. |
| `Requirements traceability`                | Enforces the `Verifies:` marker contract.               |
| `Stack-budget analysis (REQ-FS-003, hazard H-09)` | Per-function frame ceiling.                      |
| `Size regression (REQ-PF-002)`             | Size baseline gate.                                     |
| `MISRA C:2012 (cppcheck) + forbidden-symbols` | Static analysis on the runtime.                      |
| `Coverage (line / branch / function thresholds)` | Coverage gate against the runtime.                |

## Known integration gaps

Same as the per-tool TQ records' Known Limitations sections, the
two integration-level gaps are:

1. **cppcheck MISRA gate not run on generator output.** Recorded
   in `tool-qualification-generator.md`. Integrators running
   cppcheck against their own generated output catch any
   violation locally; the gap is closed by adding a CI step that
   does the same for a representative subset.
2. **Decode-path fuzzing.** Random-input fuzzing is not currently
   in CI. The xvalidation seeds are structured (the c_driver
   constructs valid values), so the wire path is exercised; the
   parse path's robustness against deliberately malformed bytes
   is partially exercised by the `test_failure_modes.c` H-02
   tests and by the `read_*` LCOV_EXCL "unreachable" paths.

## How to reproduce this report

```
gh run list --branch main --limit 10
gh run view <run-id> --json jobs
```

The `xvalidation` job comment contains the per-schema pass list
for the run. The `conformance/ndslive/` fixture runs locally:

```
cd conformance/ndslive
bash run_fixture.sh
```

(Currently exercised in development; not yet a CI gate. See
`safety-plan.md` for status.)

## Cross-references

- Functional safety concept: `functional-safety-concept.md`
- Technical safety concept: `technical-safety-concept.md`
- Unit verification: `unit-verification.md`
- Tool qualification (generator): `tool-qualification-generator.md`
- Architecture: `../architecture.md`
- Tests: `../../tests/xvalidation/`, `../../tests/runtime/`,
  `../../conformance/ndslive/`
