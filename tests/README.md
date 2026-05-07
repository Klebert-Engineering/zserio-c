# `tests/`

- `runtime/`     — Unity-based unit tests for `runtime/`.
- `generated/`   — round-trip tests against the upstream Java zserio
                   reference, exercising code emitted by `generator/`.
- `conformance/` — runs the upstream zserio test corpus through the C
                   generator.

**Phase 0 status: empty.** A test framework (Unity) will be vendored
when Phase 1 begins. Test files reference requirements with the marker
`/* Verifies: REQ-... */`; the traceability workflow scans for these.
