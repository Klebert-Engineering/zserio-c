---
id: REQ-FN-002
title: Parser accepts the schema language defined in docs/spec/schema-language.md
type: functional
status: accepted
phase_target: 1
verification: test
---

## Statement

The parser SHALL accept any schema text that conforms to the
schema language defined in
[`docs/spec/schema-language.md`](../../docs/spec/schema-language.md),
and SHALL reject any schema text that does not.

Acceptance means:

1. The parser produces an abstract syntax tree representing the
   schema's declarations, types, and expressions.
2. Semantic analysis succeeds (every name resolves, every type
   check passes, every constraint expression is well-typed).
3. The schema is available to the generator for code emission.

Rejection means:

1. The parser emits at least one diagnostic at severity `error`,
   meeting the format requirements of
   [REQ-FN-003](REQ-FN-003.md) and the precision requirements of
   [REQ-FN-004](REQ-FN-004.md).
2. The process exits with non-zero status.
3. No partial or speculative output is emitted to the generator
   stage.

## Rationale

The parser is the boundary between user input (`*.zs` files)
and the rest of the system. Defining acceptance against a single
normative document — the schema language reference — is what
makes the parser's behaviour reviewable and what discharges
[REQ-DM-004](../domain/REQ-DM-004.md) at the implementation
layer.

A parser that accepts schemas the reference rejects (or vice
versa) is a defect in one or the other; the requirement does
not specify which. This is deliberate: it forces the spec and
implementation to be kept in sync at every change.

## Verification

- The cross-validation harness exercises 182 schemas covering
  every language feature. Each schema is parsed successfully and
  the resulting C output round-trips byte-equal against
  upstream.
- The NDS Live conformance corpus
  (`conformance/ndslive/`) exercises 86 production schemas
  yielding 1823 generated `.c`/`.h` files. All parse and
  generate cleanly.
- Negative tests under `tests/parser/test_reject_*.c` exercise
  each documented rejection case at least once.

## Notes

The schema language defined in `docs/spec/schema-language.md`
is, at the time of writing, adapted from upstream zserio
v2.18.1's `ZserioLanguageOverview.md` (per
[REQ-DM-004](../domain/REQ-DM-004.md) notes). The interoperability
guarantee with that upstream version is the subject of
[REQ-FN-001](REQ-FN-001.md).

A feature reaching parser acceptance under this requirement does
not automatically mean it is supported by the generator — that
is a separate requirement ([REQ-FN-013](REQ-FN-013.md) for
templates, the implicit "every accepted construct emits" for the
rest, gated by the xvalidation harness).
