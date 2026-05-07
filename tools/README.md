# `tools/`

Repository tooling — coverage reports, MISRA scans, requirements
traceability.

**Phase 0 status: one tool seeded.**

- `check_traceability.py` — scans `requirements/` and `tests/` and fails
  if any `accepted` requirement is not referenced by a `Verifies:` marker
  in at least one test file. Used by `.github/workflows/traceability.yml`.

Future additions:

- `coverage.sh` — gcov/lcov post-processing.
- `misra-check.sh` — wraps `cppcheck --addon=misra`.
- `track_size.py` — for REQ-PF-002 size regression gating.
