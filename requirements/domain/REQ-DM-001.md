---
id: REQ-DM-001
title: System purpose — schema-driven binary serialization
type: domain
status: accepted
phase_target: 1
verification: review
---

## Statement

`zserio-c` SHALL implement schema-driven binary serialization,
defined as the bidirectional transformation between in-memory
structured values and a binary representation, where the
transformation rules — encoding layout, type interpretation,
constraint semantics, and evolution behavior — are determined
entirely by a separate declarative schema document expressed in
the schema language defined by [REQ-DM-004](REQ-DM-004.md).

## Rationale

This requirement fixes what `zserio-c` is, independent of any
particular implementation, target platform, or upstream
reference. It tells a safety integrator, an external auditor, or
a future maintainer the first thing they need to know about the
system: it consumes a schema, and that schema fully governs how
bytes relate to values. Nothing about the wire format or
generated API is allowed to be implementation-implicit.

The requirement deliberately does not name a language version, a
file format, or an implementation language. Those are
constrained by subsequent requirements
([REQ-DM-004](REQ-DM-004.md), [REQ-DM-005](REQ-DM-005.md),
[REQ-DM-007](REQ-DM-007.md)) so that this top-level statement
remains stable as the project evolves.

## Verification

Demonstrated by the existence of the parser/generator/runtime
triple and by the cross-validation harness
(`tests/xvalidation/run_xvalidation.sh`), which exercises 182
distinct schemas through the encode/decode path and verifies the
transformation is schema-determined.

Reviewed at each major release that the implementation has not
drifted from the schema-driven model (e.g. by leaking
configuration into runtime behavior beyond what the schema
describes).

## Notes

This is the parent requirement for every functional requirement
in `requirements/functional/`. The functional layer is the
*how*; this layer is the *what*.

Specifically not part of the system purpose: providing a
service-RPC layer, a SQL persistence layer, or a JSON
serialization layer. These exist in upstream zserio but are
deliberately out of scope for `zserio-c`
([H-09](../../docs/safety-case/hazard-analysis.md)).
