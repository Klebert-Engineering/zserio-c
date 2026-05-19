---
id: REQ-DM-007
title: Versioning policy for schema language and wire format
type: domain
status: proposed
phase_target: 2
verification: review
---

## Statement

`zserio-c` SHALL publish, and observe, a versioning policy
covering at least the following independent axes:

1. **Schema language version** — the version of the language
   accepted by the parser, as defined in
   [`docs/spec/schema-language.md`](../../docs/spec/schema-language.md).
2. **Wire format version** — the version of the binary encoding
   produced and consumed by the codec, as defined in
   [`docs/spec/wire-format.md`](../../docs/spec/wire-format.md).
3. **Generated-code API version** — the version of the C entry
   points emitted by the generator (`_init`, `_set_parameters`,
   `_read`, `_write`, `_free`, parameter packing context types).
4. **Runtime library API version** — the version of the
   `<zserio/*.h>` public headers.
5. **Command-line interface version** — the version of the
   `zserio_c_gen` flag set and exit-code semantics.

The policy SHALL state, for each axis:

- How versions are identified (e.g. `MAJOR.MINOR.PATCH`).
- What constitutes a breaking change on that axis.
- Whether breaking changes are permitted within a major version
  and under what conditions.
- The relationship between the axis and the `zserio-c` release
  version.

The policy SHALL reside in
[`docs/spec/compatibility.md`](../../docs/spec/compatibility.md).

## Status: proposed

This requirement is `proposed` rather than `accepted` because
the assignment of the *initial version numbers* on each axis is
deferred to a separate review. The policy itself is committed;
the starting numbers and the relationship to `zserio-c` release
versions need a structured discussion that the requirement text
alone is not the right place for.

The next step on this requirement is a separate PR that:

1. Selects initial version numbers for each axis.
2. Defines the relationship between axes (e.g. "wire format
   version X.Y is consumed by runtime version X.*").
3. Promotes this requirement from `proposed` to `accepted`.

## Rationale

Without an explicit versioning policy:

- Integrators cannot reason about whether two `zserio-c`
  releases are interoperable.
- Schema authors cannot tell whether a new feature they want to
  use is available on the integrator's target.
- A safety-case argument that pins a "version" cannot identify
  what is being pinned.

Five axes is more than a single SemVer triple, but they are not
all moving together. The wire format is far more stable than the
CLI; the runtime API is more stable than the generated-code API.
Conflating them into one version would either force
unnecessarily frequent major-version bumps or hide breaking
changes.

The axes mirror upstream zserio's compatibility model
(`ZserioCompatibilityGuide.md`); aligning the axes makes
cross-implementation comparisons concrete and unambiguous.

## Verification

Reviewed at every release: each axis SHALL declare a version,
each version change SHALL link to the policy clause that
justifies the bump.

Automated check: a CI gate
(`tools/check_version_consistency.py`) SHALL fail the build if
the version numbers in `docs/spec/compatibility.md`,
`runtime/include/zserio/version.h`, and `parser/cli/version.h`
disagree.

## Notes

The compatibility policy document this requirement points to is
the artifact a downstream user reads when deciding whether to
upgrade. It is normative for support contracts and for
ISO 26262 component-qualification arguments.

Note the interaction with [REQ-DM-006](REQ-DM-006.md): schema
evolution is the user's lever for tolerating wire-format
*non-changes*; the wire format version under this requirement is
the project's lever for owning *wire-format changes*. Both must
exist.
