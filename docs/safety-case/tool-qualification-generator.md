---
work_product: ISO 26262-8 §11 — Tool qualification (zserio-c generator)
status: draft
tool: zserio_c.jar
---

# Tool qualification — `zserio-c` generator

## Identification

| Field          | Value                                                                                              |
| -------------- | -------------------------------------------------------------------------------------------------- |
| Tool name      | `zserio_c.jar` — Java extension to the upstream zserio compiler that emits C99 codec sources.      |
| Role           | Produces the C structs, init/free/read/write functions, and packing-context helpers per schema. Output is part of the production binary. |
| Source         | This repository, `generator/`. Built by `tools/build_generator_jar.sh`.                            |
| Pinned by      | Repository state (commit SHA). The generator's jar is rebuilt from source on every CI run; there is no cached binary. |
| Configuration  | Invoked from `tests/xvalidation/run_xvalidation.sh` and from integrator-side build scripts; no per-schema configuration beyond `-setTopLevelPackage` (only used for `packages/zs/index.zs`). |
| License        | MIT (Klebert Engineering, 2026), same as the runtime.                                              |

## TCL analysis (ISO 26262-8 §11.4.5)

| Axis                       | Classification | Rationale                                                                                                |
| -------------------------- | -------------- | -------------------------------------------------------------------------------------------------------- |
| Tool Impact (TI)           | **TI2**        | The generator's output is part of every shipped binary. A generator bug could violate any safety goal whose enforcement is in the generated code (e.g. cross-compound offset patching, packing-context construction). |
| Tool error Detection (TD)  | **TD1**        | Strongest TD on any tool in the case: every generated artifact is exercised end-to-end against an independent reference (upstream cpp generator) for 182 schemas × 5 seeds per commit. A semantic error in the generator surfaces as a wire-format divergence in the `xvalidation` job, which fails the build. |
| Derived **TCL**            | **TCL1**       | TI2 × TD1 per ISO 26262-8 Table 4. The strong detection layer brings the generator down to TCL1 despite the TI2 impact. |

For a TCL1 classification, ISO 26262-8 §11.4.6 requires only that
the qualification analysis itself be documented. This file is that
analysis.

## Use cases

1. Generate C codec sources from a zserio `.zs` schema for the
   integrator's application.
2. Generate the codec sources used by the xvalidation harness
   c_driver.
3. Generate the codec sources used by the conformance/ndslive
   end-to-end fixture.

## Qualification approach

Combination of **(1) — increased confidence from use** and
**(3) — validation**:

- **Increased confidence from use.** The generator is freshly
  authored for this project. Its surface is small (~30 FreeMarker
  templates, a CIdentifierFormatter, and the SPI integration
  layer). Every template is exercised by at least one schema in
  the xvalidation corpus. The corpus has grown from 0 schemas at
  initial bring-up to 182 schemas now, covering every primitive
  type, every compound kind (struct, choice, union, bitmask,
  enum), packed arrays in every shape, parametrized types,
  cross-compound offsets, and self-referential pointers.

- **Validation of the software tool.** The xvalidation job is the
  primary validation: every generated codec is exercised against
  the independently-built upstream cpp encoder. The test is a
  byte-equal comparison, not a behavioral equivalence — much
  stronger.

  | Property the generator must hold                | Validation                                                                                  |
  | ----------------------------------------------- | ------------------------------------------------------------------------------------------- |
  | Correct C identifier mapping                    | `generator/src/main/java/zserio/extension/c/CIdentifierFormatter.java` is unit-tested under `generator/src/test/`. |
  | Correct codec output for every supported construct | `xvalidation` job: 182 schemas, byte-equal round-trip against upstream                  |
  | No use of forbidden symbols (REQ-FS-001/002)    | `tools/run_misra.sh` forbidden-symbol grep runs on the runtime, AND `tools/run_misra_generated.sh` (the `misra-generator` CI job) runs cppcheck + MISRA addon on the generator's output for a 13-schema representative set. |
  | No runtime reflection (REQ-FS-008)              | Architectural review: the generator templates contain no reflection scaffolding. **Smoke-checked** by REQ-FS-008's traceability marker in `tools/run_misra.sh`. |
  | Correct cross-compound offset patching          | xvalidation; the `offsets_compound_field_array` and related schemas exercise it.            |

## Specific criteria and evidence

- **Build reproducibility.** The jar is built from source per CI
  run; no cached binary. The build is deterministic given the
  same JDK 17 and Ant version.
- **No host-state pollution.** The generator runs as a JVM
  invocation with explicit `-src` / `-c` / `-cpp` paths; it has
  no global state across runs.
- **Output is reviewable.** Every commit's generated output for
  the xvalidation corpus is reproduced under
  `build-xvalidation/<schema>/c_out/` and can be diffed against
  the upstream cpp output side-by-side.
- **Conformance against NDS Live.** Beyond the xvalidation
  corpus, the `conformance/ndslive/` fixture exercises a full NDS
  Live SmartLayerTile (50,896 bytes of upstream-encoded data)
  end-to-end, providing an external production-shape validation.

## Known limitations

- **Generator output now subject to a strict MISRA gate** *(closed
  2026-05-15, commit `c13bd41`)*. The `misra-generator` CI job
  (`.github/workflows/ci.yml`) runs `tools/run_misra_generated.sh
  --strict` against a 13-schema representative set chosen to cover
  the major emission paths (every primitive width, varint, bitfield,
  struct, choice, union, packed array, optional, parameterized,
  template, string, float, offset, function-typed). Findings are
  either fixed in the generator's FreeMarker templates / Java
  policy, or covered by an explicit entry in
  `tools/cppcheck-suppressions.txt` (shared with the runtime) /
  `tools/run_misra_generated.sh` CLI suppressions (generator-only),
  every category indexed in `tools/cppcheck-suppressions.md` with a
  rationale or a sibling deviation document in
  `docs/misra-deviations/`. The gate fails the build on any
  unsuppressed finding.
- **Cross-compound offset directives with `@index` skip wire-side
  enforcement.** Offset directives of the shape
  `path.through.compound[@index].field:` (a path through a `choice`
  arm, recursive `union` arm, or packed holder, terminated by an
  indexed array-element reference) now generate cleanly (matched by
  the discovery gate's 391/391 schemas) but the generator does NOT
  emit the offset back-patch on write nor the offset-position check
  on read for these shapes — single-pass back-patch through a
  choice / union arm isn't supported. The schema compiles, the
  field reads and writes correctly, the integrator must compute the
  offset value externally if the channel relies on it. See
  `FieldFormatter.java` § "isDottedFieldRef" / "wrapDottedOffset"
  for the dispatch and the three upstream schemas that exercise the
  pattern.
- **No upstream cpp ground truth for `packages/zs/index.zs`.**
  This schema requires the `-setTopLevelPackage` workaround on
  the cpp side; the workaround is applied uniformly in our
  harness, but a divergence in cpp's `-setTopLevelPackage`
  behavior itself would not be caught by us.
- **Template churn risk.** The generator is under active
  development. A template change that passes
  the xvalidation corpus but produces non-MISRA code would not
  fail CI today.

## Re-qualification triggers

1. Adding support for a new schema construct triggers the need
   for at least one new entry in the xvalidation corpus.
2. The "generator output MISRA gate" gap is closed (see Known
   limitations); the TQ record's `draft → accepted` transition is
   now blocked only on the case-wide independent reviewer sign-off
   tracked in `safety-case-report.md` § Sign-off.
3. A change to `CIdentifierFormatter.java` or to any FreeMarker
   template under `generator/src/main/resources/c99/` requires a
   re-run of the full xvalidation corpus.

## Audit re-execution

```
.github/workflows/ci.yml → xvalidation job
```

Reproduces every generated artifact for every schema in
`tests/xvalidation/schemas.txt` and compares byte-for-byte
against the upstream cpp encoder. A clean run of this job is the
primary evidence that the generator's output is correct under
the safety case's definition of correctness (REQ-FN-001 byte
equality).
