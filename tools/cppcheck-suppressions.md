# cppcheck suppressions — rationale

`tools/cppcheck-suppressions.txt` is consumed by `cppcheck
--suppressions-list`. The format the tool accepts is **one ID per
line, no comments, no blank lines**, optionally followed by
`:file:line`. This document is the human-readable companion that
records why each entry exists.

## Format

```
<id>[:<file>[:<line>]]
```

For MISRA-rule-specific suppressions, ALSO add a corresponding
deviation document under `docs/misra-deviations/` recording rule,
scope, justification, mitigation, and approver. The deviation doc
and the suppression entry MUST cite each other.

## Current entries

| Suppression           | Why                                                                 |
|-----------------------|---------------------------------------------------------------------|
| `unusedFunction`      | The runtime is a library; its public symbols are called by callers outside our tree. cppcheck cannot see those callers and would otherwise flag every public function. |
| `missingIncludeSystem` | cppcheck warns when `<stddef.h>`, `<stdint.h>`, etc. can't be resolved on its include path. They're always present on supported toolchains; the warning is informational. |

(MISRA-rule entries land here as the deviation review process
produces them.)
