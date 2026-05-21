---
id: REQ-FN-009
title: Schema-to-C identifier mapping is total, deterministic, and documented
type: functional
status: accepted
phase_target: 1
verification: test
---

## Statement

The generator SHALL emit C identifiers for every schema-declared
entity (types, fields, enum items, bitmask items, constants,
function fields, parameter names, choice/union case names)
according to a mapping that is:

1. **Total** — every schema entity reachable by the parser has a
   defined C identifier.
2. **Deterministic** — the same schema entity produces the same
   C identifier across runs, hosts, and `zserio-c` versions
   sharing the same generated-code API version (per
   [REQ-DM-007](../domain/REQ-DM-007.md)).
3. **Documented** — the mapping rules are stated in
   [`docs/spec/types-mapping.md`](../../docs/spec/types-mapping.md)
   and [`docs/user-guide/generated-code.md`](../../docs/user-guide/generated-code.md).

The mapping SHALL produce identifiers that satisfy:

- Valid C99 identifier syntax.
- No collision with C99 keywords or with reserved identifiers in
  C99 § 7.1.3.
- No collision with public runtime symbols (the `zserio_` prefix
  family).
- No collision between two distinct schema entities reachable in
  the same translation unit.

When two schema entities at different scopes would produce
colliding identifiers under the documented rules, the generator
SHALL disambiguate by appending a suffix derived from the
entity's qualified schema name, with the disambiguation rule
itself documented.

## Rationale

Identifier mapping is the most user-visible interface of the
generator after the wire format itself. A non-total mapping
means the generator can produce a schema that has no C
representation (and crashes the build silently or loudly). A
non-deterministic mapping means re-running the generator may
break downstream code that referenced the old name. A
non-documented mapping means an integrator who needs to call a
particular generated function has no way to know its name
without running the generator.

The collision rules matter because the C symbol space is a flat
namespace per translation unit. Two schema types in different
packages can have the same short name; without disambiguation
they would shadow each other.

Explicitly forbidding collision with `zserio_*` (the runtime
symbol prefix) is a hygiene rule: a schema author should never
accidentally call a runtime function by writing what they think
is their own type's `_init`.

## Verification

- `tests/parser/test_identifier_mapping.c` exercises the
  snake_case conversion rules, the enum-item prefix
  construction, the `_present` suffix for optionals/conditionals,
  the union tag prefix, and the disambiguation suffix on
  forced-collision inputs.
- The cross-validation harness implicitly exercises every
  identifier path: each of 182 driver programs successfully
  references generated symbols by the documented name.
- `tests/parser/test_no_runtime_collision.c` exercises a schema
  whose type name spells `zserio_something` and verifies the
  generator either accepts it with disambiguation or rejects it
  with a defined diagnostic.

## Notes

This requirement is the *contract* side of the identifier
mapping; [REQ-FN-010](REQ-FN-010.md) is the *physical-limit*
side (NAME_MAX truncation).

The mapping rules are summarised at user-guide voice in
`docs/user-guide/generated-code.md` § Naming, and given as a
normative table in `docs/spec/types-mapping.md`.
