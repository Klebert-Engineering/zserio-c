# Compatibility policy

**Status:** normative for zserio-c. Discharges
[REQ-DM-007](../../requirements/domain/REQ-DM-007.md).

**Origin:** This document is adapted from upstream zserio's
`ZserioCompatibilityGuide.md` (BSD-3-Clause, © Navigation Data
Standard e.v.) at pinned version v2.18.1, restructured for the
zserio-c project's narrower scope (no JSON, no SQL).

**Audience:** integrators planning an upgrade; downstream
consumers writing support contracts.

---

## Five compatibility axes

zserio-c uses a **two-tier scheme**:

- Two **spec axes** with their own independent versions:
  schema language and wire format. These move on their own
  cadence — typically almost never.
- Three **tool axes** whose compatibility tracks the release
  version: runtime API, generated-code API, CLI. Breaks on
  these are permitted at release-major bumps.

| Axis | Version | Where it lives |
|---|---|---|
| **Release (zserio-c overall)** | `0.1.0` | `<zserio/version.h>`, `CMakeLists.txt`, README badge |
| **Wire format** | `1.0` (independent) | `<zserio/version.h>`, [`wire-format.md`](wire-format.md) header |
| **Schema language** | `1.0` (independent) | `<zserio/version.h>`, [`schema-language.md`](schema-language.md) header |
| Runtime library API | tracks release `0.1` | `<zserio/*.h>` |
| Generated-code API | tracks release `0.1` | [`docs/user-guide/generated-code.md`](../user-guide/generated-code.md) |
| CLI | tracks release `0.1` + deprecation cycle | [`docs/cli.md`](../cli.md) |

Compatibility on each spec axis follows the rule: **the major
version captures breaking changes; the minor/patch capture
additive and corrective changes.** Tool axes inherit
release-version semver — major bumps on the release version
imply potential breaks on the three tool axes.

## Current versions

| Axis | Current | Pinned reference |
|---|---|---|
| Release | `0.1.0` | pre-1.0; runtime / codegen / CLI APIs may change between minor releases |
| Wire format | `1.0` | compatible with upstream Zserio BIN 1.0; byte-equal with upstream zserio v2.18.1 |
| Schema language | `1.0` | subset of upstream Zserio language v2.18.1 (no `sql_table`, `sql_database`, no JSON) |

Programmatic access:

```sh
$ zserio_c_gen --version
zserio_c_gen 0.1.0
  schema language : 1.0   (subset of upstream Zserio v2.18.1)
  wire format     : 1.0   (compatible with upstream Zserio BIN 1.0)
  runtime API     : 0.1   (pre-1.0; may change between minor releases)
  generated code  : 0.1   (pre-1.0; may change between minor releases)
```

At build time, the same values are available as preprocessor
constants in `<zserio/version.h>` (`ZSERIO_C_VERSION_STRING`,
`ZSERIO_WIRE_FORMAT_VERSION_STRING`,
`ZSERIO_SCHEMA_LANGUAGE_VERSION_STRING`,
`ZSERIO_UPSTREAM_PIN_VERSION`).

The values in `<zserio/version.h>`, in `CMakeLists.txt`'s
`project()` clause, and in this document are kept in sync by
a CI gate (`tools/check_version_consistency.py`). Any
disagreement fails the build.

## Pre-1.0 semantics

While the release version is below `1.0.0`, the three tool
axes (runtime API, generated-code API, CLI) are **not** under
stability guarantees. A minor-version bump may rename, remove,
or alter the signature of a `<zserio/*.h>` function; regenerate
generated code with the new release; rebuild and recompile.

**The spec axes are not pre-release**, even at release `0.x.y`.
Wire format 1.0 and schema language 1.0 are stable from day
one of the project; they are tied to the upstream zserio
specification at v2.18.1, which has been stable since 2018.

Reaching `zserio-c 1.0.0` commits us to API stability on the
three tool axes for the duration of the `1.x` series.

## Schema-language compatibility

Defines whether a new `zserio-c` can compile a schema written
against an older `zserio-c`.

A schema-language major-version bump is required when:

- A keyword is added, removed, or repurposed.
- A previously-accepted construct is rejected.
- A semantic interpretation of an existing construct changes
  in a way that alters the parsed AST.

A minor or patch bump is sufficient when:

- A new optional construct is added that older schemas don't
  use.
- A diagnostic is improved or its message text is changed.
- A previously-rejected construct is now accepted (e.g.
  parser bug fix).

The schema-language version is recorded in the parser binary
(reachable via `zserio_c_gen --version`) and in the
[`schema-language.md`](schema-language.md) header.

## Wire-format compatibility

Defines whether a new `zserio-c` produces and consumes the
same bytes as an older `zserio-c` for the same schema and
values.

This is the most stable axis. A wire-format major bump is
expected to be **extremely rare** and would require:

- A breaking change to a primitive type's encoding (e.g.
  changing `varint` continuation bit position).
- A change to compound-type field ordering rules.
- A change to alignment or offset semantics.

A wire-format minor or patch bump may accompany:

- A new schema construct whose wire format extends existing
  rules without changing them (e.g. a new packed-array
  flavor for a new primitive type added in a separate
  schema-language bump).
- A clarification of previously-underspecified behavior that
  matches the existing reference implementation's behavior.

The wire-format version is recorded in
[`wire-format.md`](wire-format.md). Integrators with safety
contracts pin both `zserio-c` release version and the wire
format axis explicitly.

## Generated-code API compatibility

Defines whether code generated by a new `zserio-c` is
source-compatible with code generated by an older `zserio-c`.

A generated-code-API major bump is required when:

- An emitted entry-point signature changes
  (`_init`, `_set_parameters`, `_read`, `_write`, `_free`).
- The struct layout changes in a way that breaks `memcmp`-based
  equality or sibling-flag positioning.
- The naming convention rules ([REQ-FN-009](../../requirements/functional/REQ-FN-009.md))
  change.
- The runtime symbols (`zserio_*`) called by generated code
  change incompatibly.

A minor or patch bump is sufficient when:

- An emitted entry point gains a new optional behavior
  (e.g. additional helper function).
- Generated identifier mangling changes in a way that
  introduces new names without breaking old ones (e.g.
  shortening hashes for cosmetic reasons — counts as breaking
  if downstream code referenced the old names).

Generated-code-API compatibility is a coupled axis: a major
bump here typically requires regenerating all downstream
code. Re-running the generator against the same schema and
recompiling is normally sufficient.

## Runtime library API compatibility

Defines whether existing generated code links against a new
runtime, and whether existing application code that calls
runtime API directly compiles against the new headers.

A runtime API major bump is required when:

- An existing function signature in `<zserio/*.h>` changes.
- A struct in `<zserio/types.h>` changes layout (any added
  field, removed field, reordered field).
- An enumerator in `<zserio/status.h>` is removed or
  renumbered. (Adding new error codes is **not** a major
  bump — switch-on-name is the safe pattern, see
  [`status.h`](../../runtime/include/zserio/status.h).)

A minor or patch bump is sufficient when:

- New entry points are added.
- A new enumerator is added to `zserio_status_t`.
- A new allocator vtable is added (existing vtables unchanged).

The runtime API version is consumed by generated code via
`<zserio/version.h>` (build artifact); a generated-code-API
major version pins a corresponding runtime-API major version.

## CLI compatibility

Defines whether `zserio_c_gen` invoked with old arguments
continues to work on a new release.

A CLI major bump is required when:

- An existing flag is removed or its semantics change.
- The exit-code meanings change.
- An accepted input form is rejected.

A minor or patch bump is sufficient when:

- A new flag is added.
- A flag gains additional permitted values.
- A diagnostic format is improved.

CLI changes are released with a deprecation cycle: an existing
flag that will be removed in version N is deprecated (with a
warning on use) in version N-1 or earlier.

## Pinning for safety arguments

A safety-case argument that pins a "zserio-c release version"
pins all axes implicitly. For long-lived deployments where one
axis needs to be pinned more tightly than the release (e.g.
wire format pinned across an OTA upgrade boundary), the
integrator pins the axis directly. Example pin statement:

> "This safety case is qualified against zserio-c release 0.1.0:
> schema language 1.0, wire format 1.0, runtime / generated /
> CLI APIs at 0.1. Channel A pins wire format 1.0 across all
> deployed ECUs and tolerates release version drift up to but
> not including 1.0.0."

A future release bump on the release axis (e.g. 0.1.x → 0.2.0)
may change the tool axes; the integrator re-verifies against
the new tool-axis state but the wire-format pin still holds
unless a deliberate wire-format major bump occurred.

## What this document does NOT cover

- The schema-evolution constructs (`optional`, `extend`,
  `@removed`) — those are about evolving a single schema,
  not about evolving the language or the format. See
  [`schema-evolution.md`](schema-evolution.md).
- JSON encoding — out of scope for zserio-c.
- SQL extensions — out of scope for zserio-c.
- Service/RPC and Pub/Sub — supported (transport-agnostic interface
  generation); see [REQ-FN-027](../../requirements/functional/REQ-FN-027.md)
  and [`docs/design/service-pubsub.md`](../design/service-pubsub.md).

## Reading compatibility info

`zserio_c_gen --version` prints the five axis versions and the
release version. The same information is in
`<zserio/version.h>` for build-time consumption.
