# MISRA C:2012 deviations

Each deviation from a MISRA C:2012 rule used by the zserio-c runtime
or generated code is documented here, one file per deviation. The
file name is `D<NNN>-<short-slug>.md`, where `<NNN>` is the next
available three-digit number.

## File template

```markdown
---
id: D001
rule: MISRA C:2012 Rule X.Y
classification: required | advisory | mandatory
status: proposed | accepted | superseded
approvers: <names>
---

## Statement of the rule

(One-paragraph paraphrase. Do NOT reproduce the proprietary rule
text from the MISRA standard verbatim — cite by number only.)

## Where it is deviated

- File:line citations for each affected location.

## Justification

Why the rule cannot be followed at this site, with reference to
specific design decisions (e.g. REQ-FS-005 bounded-loops, performance
considerations, language limitations).

## Mitigation

How risk is contained: code review, unit-test coverage, defensive
checks, runtime contracts, etc.

## Reviewer notes

(Optional, populated during review.)
```

## Process

1. A new finding from `tools/run_misra.sh` is reviewed.
2. If the finding is a real bug, it is fixed in the code.
3. If the finding is a known acceptable deviation, a deviation file
   is created here AND a corresponding suppression entry is added to
   `tools/cppcheck-suppressions.txt`. Both entries cite each other.
4. If the finding is a cppcheck false positive, only the suppression
   entry is added (with rationale).

Suppressions added without deviation documentation MUST be temporary
and tracked in an issue.

## Current deviations

(none yet)
