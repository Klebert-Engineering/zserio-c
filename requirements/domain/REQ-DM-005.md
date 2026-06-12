---
id: REQ-DM-005
title: Wire-format reference is owned by zserio-c
type: domain
status: accepted
verification: review
---

## Statement

`zserio-c` SHALL maintain a normative reference for the binary
wire format it produces and consumes, residing in
`docs/spec/wire-format.md`. The reference SHALL be complete in
the sense that an independent implementer can construct a
conforming codec from it alone.

The reference SHALL cover, at minimum:

1. Bit endianness and bit-order conventions for the bitstream.
2. Per-primitive encoding for each primitive type in the schema
   language: `bit:N`, `int:N`, `bit<N>`, `int<N>`, the
   `uintN`/`intN` family, `varuintN`/`varintN`/`varsize`/`varint`,
   `bool`, `float16`/`float32`/`float64`, `string`, `extern`,
   `bytes`.
3. Compound type encoding: `struct`, `choice`, `union`, with the
   role of selector and tag fields.
4. Array encoding: fixed-length, length-prefixed, implicit,
   packed (scalar), packed (compound, with per-field delta
   contexts).
5. Optional encoding: `optional` with on-wire presence bit,
   conditional fields under `if`, `extend` with end-of-stream
   semantics.
6. Alignment and offset encoding: `align`, `@offset`, indexed
   offsets.
7. Parameter encoding: how parameter values affect wire-format
   decisions (conditional fields, dynamic-width bit fields, array
   lengths).
8. Constraint evaluation timing on the read path.
9. The 2³¹-1 array-length and bitstream-size caps.

The reference SHALL be the authoritative source for every byte
the encoder produces. Divergence between the reference and the
implementation is a defect in either, never an acceptable state.

## Rationale

The wire format is the contract that crosses the trust boundary
between implementations and across process/network boundaries. If
the format is not specified separately from any implementation,
the only thing a remote integrator can do to verify
interoperability is run an implementation; this is incompatible
with the safety case argument that the format is a stable
artifact independent of any particular tool.

The "complete and unambiguous" criterion is more demanding here
than for the schema language: a wire-format ambiguity manifests
silently as a decoding bug in the field, often years after
deployment. The reference must be precise enough that two
independent codec authors will produce byte-equal output without
needing to consult each other or any implementation.

## Verification

Reviewed when the wire format reference is first published, and
at every change to the runtime or generated emit/read code paths.
The review checklist
(`docs/spec/wire-format-review-checklist.md`) enumerates the
nine coverage areas above.

Cross-validated continuously: every wire-format decision in the
reference is exercised by at least one harness under
`tests/xvalidation/harnesses/`, with the cross-validation gate
proving byte-equal encoding against the upstream reference
implementation
([`tests/xvalidation/run_xvalidation.sh`](../../tests/xvalidation/run_xvalidation.sh)).

## Notes

This requirement is the wire-format counterpart of
[REQ-DM-004](REQ-DM-004.md) (schema language reference) and is
related to [REQ-DM-003](REQ-DM-003.md) (canonical encoding) —
canonicalization is a property of the wire format, not of the
implementation.

The first version of `docs/spec/wire-format.md` is adapted from
upstream's BSD-3-Clause-licensed `ZserioEncodingGuide.md`. The
adapted content is explicitly attributed in the document
header. Once we declare a divergent wire-format version under
[REQ-DM-007](REQ-DM-007.md), the adapted source becomes
historical context only.
