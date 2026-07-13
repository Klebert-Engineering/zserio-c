## Summary

<!-- What changes, and why? Link the requirement ID(s) touched (REQ-*), if any. -->

## Checklist

- [ ] Builds warning-clean with the `gcc-debug` and `clang-debug` presets
- [ ] `ctest` passes on both presets
- [ ] New or changed tests carry `/* Verifies: REQ-* */` markers where
      applicable, and `requirements/traceability.csv` was regenerated
      (`python3 tools/check_traceability.py --write-csv`) if markers changed
- [ ] Docs and `CHANGELOG.md` updated where behavior changed
- [ ] No direct `malloc`/`calloc`/`realloc`/`free` — allocation flows through
      `zserio_allocator_t`
- [ ] American English in comments, docs, and identifiers
