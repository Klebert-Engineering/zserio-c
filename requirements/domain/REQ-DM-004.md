---
id: REQ-DM-004
title: Schema-language reference is owned by zserio-c
type: domain
status: accepted
phase_target: 1
verification: review
---

## Statement

`zserio-c` SHALL maintain a normative reference for the schema
language it accepts, residing in `docs/spec/schema-language.md`.
The reference SHALL be complete in the sense that an independent
implementer can construct a conforming parser from it alone,
without consulting any other implementation or document.

The reference SHALL cover, at minimum:

1. Lexical structure: tokens, identifiers, numeric and string
   literals, comments, whitespace.
2. Syntactic grammar: every production in the language, presented
   in an unambiguous notation (EBNF or equivalent).
3. Type system: every primitive type, compound type, subtype,
   enum, bitmask, and template form, with their permitted
   contexts.
4. Expression semantics: operator precedence, associativity,
   type promotion rules, and overflow behavior.
5. Schema semantics: packages, imports, name resolution, scope
   rules, and constraint evaluation timing.
6. Template semantics: parameter substitution, instantiation, and
   name mangling.

The reference SHALL match exactly what the implementation
accepts; divergence between the reference and the parser is a
defect in either the reference or the parser, never an
acceptable state.

## Rationale

For `zserio-c` to be usable as a standalone safety-relevant
asset, the schema language it processes must be defined by a
document that `zserio-c` itself owns. A reference that points
"see upstream" creates a dependency that an external auditor
cannot evaluate without expanding scope to the upstream project.

The "complete and unambiguous" criterion is the substantive
content of this requirement: a tool-qualification argument for
the parser rests on whether the language it accepts is fully
specified. A 2000-line reference that defers ten edge cases to
"undefined" is not sufficient.

## Verification

Reviewed when the schema language reference is first published,
and at every change to the parser that may affect accepted
schemas. The review checklist
(`docs/spec/schema-language-review-checklist.md`) enumerates the
six coverage areas above; sign-off requires every item present.

A "round-trip" verification mode is enforced in CI: every
language feature the parser accepts SHALL be exercised by at
least one schema under `tests/xvalidation/harnesses/`, and the
reference SHALL list every harness that exercises a given
feature in its corresponding section.

## Notes

This requirement is the schema-language counterpart of
[REQ-DM-005](REQ-DM-005.md), which covers the wire format.

The first version of `docs/spec/schema-language.md` is adapted
from upstream's BSD-3-Clause-licensed
`ZserioLanguageOverview.md`. The adapted content is explicitly
attributed in the document header. Once we declare a divergent
schema-language version under [REQ-DM-007](REQ-DM-007.md), the
adapted source becomes historical context only.
