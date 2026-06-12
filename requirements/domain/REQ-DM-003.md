---
id: REQ-DM-003
title: Canonical encoding
type: domain
status: accepted
verification: test
---

## Statement

For a fixed schema `S` and a fixed value `v`, the encoder SHALL
produce a bitstream that is byte-identical across:

1. Distinct invocations on the same host,
2. Distinct hosts running the same `zserio-c` version,
3. Distinct `zserio-c` versions that share the same wire format
   version (per [REQ-DM-007](REQ-DM-007.md)),
4. Distinct compilers and optimization levels applied to
   `zserio-c`.

There SHALL be no encoder-internal source of variation
(timestamps, random padding, address-dependent layout, build-id
embedding, or otherwise) that would cause the same `(S, v)` pair
to produce different byte sequences.

## Rationale

Canonical encoding is the property that lets a bitstream be used
as a content-addressed identifier, a cache key, a transmission
checksum input, or a regression-test fixture. Without it none of
those uses are sound.

Canonical encoding also makes interoperability testable: a
second implementation can be validated by byte-comparison
against ours without needing to negotiate "equivalent but
different" encodings.

The requirement is *value-canonical*, not *struct-canonical*: an
in-memory C `struct` may contain padding bytes or other
representation details that the encoder is required to ignore.
The encoder canonicalizes on the schema-defined value, not on
the host's memory representation.

## Verification

The cross-validation harness pairs every encode operation with a
byte-diff against the upstream reference. The "no encoder
variation" property is verified by the harness itself running
multiple seeds against the same value and confirming byte-equal
output.

Reproducibility across hosts is gated on every commit by the CI
build matrix: gcc on x86_64-linux, gcc on aarch64-linux, clang
on x86_64-linux.

## Notes

Discharges Safety Goal SG-7
([`docs/safety-case/functional-safety-concept.md`](../../docs/safety-case/functional-safety-concept.md)).

Implementations of the schema language constraint expression
that surface platform-dependent semantics (e.g. integer overflow
in expression evaluation) would violate this requirement. The
wire format spec ([REQ-DM-005](REQ-DM-005.md)) defines
expression semantics in two's-complement modular arithmetic to
keep the encoding canonical.
