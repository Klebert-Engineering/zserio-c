---
work_product: ISO 26262-8 §11 — Tool qualification (cppcheck)
status: draft
phase_target: 5
tool: cppcheck
---

# Tool qualification — cppcheck

## Identification

| Field          | Value                                                       |
| -------------- | ----------------------------------------------------------- |
| Tool name      | cppcheck — static analyzer for C/C++                        |
| Role           | Enforces MISRA C:2012 compliance (Amendments 1–4) on the runtime sources. Gates one of the load-bearing safety claims. |
| Source         | Distributed with Ubuntu 24.04 LTS (Noble); apt package `cppcheck` (currently 2.13.x). |
| Pinned by      | The GitHub Actions runner image `ubuntu-latest`.            |
| Configuration  | `tools/run_misra.sh` — strict mode (`--strict`) with `--enable=warning,style,performance,portability,information --inline-suppr --std=c99 --language=c --error-exitcode=1 --suppressions-list=tools/cppcheck-suppressions.txt --addon=misra -UZSERIO_TRACE_ENABLED`. |
| Add-on         | Built-in `misra` add-on, configured via the suppressions file. |
| License        | GPLv3.                                                      |

## TCL analysis (ISO 26262-8 §11.4.5)

| Axis                       | Classification | Rationale                                                                                                |
| -------------------------- | -------------- | -------------------------------------------------------------------------------------------------------- |
| Tool Impact (TI)           | **TI2**        | cppcheck gates the MISRA claim. A false negative — missed violation — would silently weaken the safety case. |
| Tool error Detection (TD)  | **TD2**        | False positives are caught the same day a build fails; false negatives are harder to spot. Detection layers: (a) MISRA rule numbers fired in CI are compared against the canonical rule list during deviation review; (b) periodic full-corpus re-runs across the upstream `ndsev/zserio` test data set widen the rule exercise surface; (c) any new rule violation requires a deviation file under `docs/misra-deviations/`, so undocumented passes are flagged at code review. |
| Derived **TCL**            | **TCL2**       | TI2 × TD2 per ISO 26262-8 Table 4.                                                                       |

## Use cases

1. Enforce MISRA C:2012 compliance on `runtime/src/*.c` and
   `runtime/include/zserio/*.h` per commit.
2. Detect categorical issues (unused functions, missing prototypes,
   suspicious casts) that the MISRA addon may miss.
3. Verify that every existing deviation in
   `tools/cppcheck-suppressions.txt` still fires (a deviation that
   no longer triggers is a sign the underlying code changed and
   the deviation should be reviewed).

## Qualification approach (TCL2)

Method **(3) — validation of the software tool** per ISO 26262-8
§11.4.7. Validation evidence per rule category:

| Rule category in use            | Validation approach                                                                                   |
| ------------------------------- | ----------------------------------------------------------------------------------------------------- |
| 5.x (identifiers)               | Manual review of CI output during the original triage commit (`6e61ed6`); spot-checked at every commit by reviewing the cppcheck.log artifact. |
| 11.x (pointer/integer casts)    | Same as above; the four file-scoped deviations (`*allocator.c`, `*float.c`, `*string.c`, `*bytes.c`, `*extern.c`) are independently justified in `docs/misra-deviations/D004` and `D005`. |
| 15.x (control flow)             | Rule 15.5 (multiple-returns) deviation is global and documented (`D001`). Rule 15.6 had specific findings (now fixed) — confirmed by re-running the gate. |
| 17.2 (no recursion)             | The runtime's safety case rests on this; the rule has never triggered, but its operation is verified by inspection of a known-recursive test program (described under "Specific criteria"). |
| 20.x (preprocessor)             | All four deviations (`D006` covers 20.7 + 20.10 for type-parametric macros; the inline 21.6 suppression on the debug-only `<stdio.h>` include is documented inline). |
| 21.x (banned library functions) | `D002` (Rule 21.3 in `allocator.c` only), `D003` (Rule 21.15 in `float.c` only). Both file-scoped. |

## Specific criteria and evidence

- **Configuration is reproducible.** `tools/run_misra.sh` is the
  sole entry point and is version-controlled.
- **Strict mode failure-on-finding.** Any new MISRA finding is a
  red CI gate. The `tools/cppcheck-suppressions.txt` is locked to
  the set of justified deviations under `docs/misra-deviations/`.
- **Deviation review.** Every entry in `cppcheck-suppressions.txt`
  has a corresponding `D<NNN>-*.md` file. Suppressions without
  documentation are forbidden by the deviation README (see
  `docs/misra-deviations/README.md`).
- **Rule 17.2 sanity check.** A standalone tool-qualification
  exercise: feed cppcheck a deliberately-recursive C source and
  confirm it flags 17.2. This check is **not** in CI (we do not
  ship the deliberately-broken source); it is performed as part
  of the qualification re-execution described below.

## Known limitations

- **Coverage of the MISRA standard.** The cppcheck `misra` addon
  implements a subset of MISRA C:2012; not every rule has a
  checker. Our use focuses on the rules where a checker exists.
  Rules without a checker (e.g. several advisory ones) are not
  enforced; the safety case does not claim full MISRA compliance,
  only compliance with the rules cppcheck enforces.
- **Configuration parsing.** cppcheck's `--undefine`
  (long-form) is not supported; we use the short `-U` flag (see
  commit `ff29436`). This affects analysis-configuration
  reproducibility across cppcheck versions.
- **Preprocessor-state-blind addon scans.** The misra addon scans
  `#include` directives regardless of `#ifdef` state; the
  `<stdio.h>` include in `bitstream.c` is reported even when
  gated. This is mitigated by an inline suppression — see
  `runtime/src/bitstream.c:30`.
- **Version drift.** Same situation as gcc — `ubuntu-latest` may
  bump the cppcheck version.

## Re-qualification triggers

1. A cppcheck version bump that changes the rule set or
   suppression syntax (verified by re-running the MISRA job after
   the runner image update).
2. A new MISRA rule finding requires a new deviation entry; no
   re-qualification of cppcheck itself is needed unless the
   finding is a known false negative we should have caught.
3. Addition of a new rule category to `cppcheck-suppressions.txt`
   triggers a review of the validation table above.

## Audit re-execution

```
.github/workflows/ci.yml → misra job
```

The job posts the full `cppcheck.log` as a commit comment, so the
exact tool output is preserved against the commit SHA. The
auditor can:

1. Inspect the commit comment to see the rule-by-rule output.
2. Cross-reference any finding against `cppcheck-suppressions.txt`
   and `docs/misra-deviations/`.
3. For the Rule 17.2 sanity check, run the off-CI deliberate-
   recursion script (one-page, kept in
   `tools/scratch/cppcheck-sanity-recursion.c` — to be added in
   a follow-up TQ re-execution commit).
