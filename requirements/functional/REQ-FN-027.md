---
id: REQ-FN-027
title: Service and pub/sub interface generation
type: functional
status: accepted
verification: test
---

## Statement

For a schema `service` declaration, the generator SHALL emit a
transport-agnostic C interface comprising:

1. A **client** wrapper per method that serializes the request type,
   invokes a caller-supplied transport (`zserio_service_t`) with the
   method's verbatim short name and the serialized request bytes, and
   deserializes the returned response bytes into the response type.
2. A **server** dispatch (`<service>_service_init` producing a
   `zserio_service_t`) that, given an incoming method name and request
   bytes, deserializes the request, invokes the matching application
   handler, and serializes the handler's response to bytes. An unknown
   method name SHALL return `ZSERIO_ERR_INVALID_ARGUMENT`.

For a schema `pubsub` declaration, the generator SHALL emit a
`publish<Message>` wrapper per published message (serialize + publish via
`zserio_pubsub_t`) and a `subscribe<Message>` wrapper per subscribed
message (register a generated raw callback that deserializes delivered
bytes into the typed message and forwards it to the application).

The generated code SHALL:

- be wire-interoperable with upstream zserio (`v2.18.1`): the service
  dispatch key is the **verbatim short method name**, payloads are the
  plain zserio wire bytes of the request/response/message types, and the
  pub/sub topic is the schema's `topic(...)` string;
- route every allocation through the caller-supplied
  `zserio_allocator_t`, and allocate nothing on the `_write` path
  ([REQ-FN-016](REQ-FN-016.md));
- report all failures through `zserio_status_t`
  ([REQ-FS-002](../safety/REQ-FS-002.md)), with outputs valid only on
  `ZSERIO_OK`;
- contain no transport implementation — the transport is bound by the
  integrator through the `zserio_service_t` / `zserio_pubsub_t` vtables.

## Rationale

Upstream zserio generates RPC/pub-sub *glue* (typed (de)serialization
and method dispatch) over an abstract transport seam, leaving the
transport (gRPC, sockets, DDS, AUTOSAR COM, …) to the integrator. That
separation is exactly right for an ASIL-relevant core: the transport is
a platform decision and a qualification boundary, while the
schema-derived (de)serialization is deterministic, bounded, and testable.

The C translation keeps the upstream contract (so a zserio-c peer and an
upstream C++ peer interoperate over one transport) while replacing C++
mechanisms with the patterns already used in this runtime: a
function-pointer vtable plus opaque `impl` for the seam (the same
construct as `zserio_allocator_t`, deviation D004), `zserio_status_t`
instead of exceptions, and explicit allocator threading. Method dispatch
is generated code rather than runtime reflection, which is simpler and
avoids the `ITypeInfo`/`Reflectable` machinery deliberately excluded from
the C target.

## Verification

- `tests/parser/test_service_pubsub.c` generates from a schema
  containing a `service` (multiple methods) and a `pubsub` (a published
  and a subscribed message), compiles the generated C against the
  runtime, and round-trips:
  - a client wrapper call through a mock `zserio_service_t` transport
    that loops the request bytes through the generated server dispatch
    into an application handler and back, asserting the decoded response;
  - a `publish<Message>` through a mock `zserio_pubsub_t` broker that
    delivers immediately to a `subscribe<Message>` subscriber, asserting
    the decoded typed message;
  - an unknown method name returns `ZSERIO_ERR_INVALID_ARGUMENT`.

## Notes

The design, the upstream contract it interoperates with, and the
ownership/lifetime model are recorded in
[`docs/design/service-pubsub.md`](../../docs/design/service-pubsub.md).

`bytes`-typed request/response/message payloads (upstream's `isBytes`
shortcut) are not specially handled in this first version; such payloads
round-trip as ordinary serialized types. A `topic(...)` expression that
is not a plain string literal (concatenation, const reference) is
rejected with an error diagnostic — generation fails rather than baking
a wrong topic into the glue. Support for constant-foldable topic
expressions is a possible extension.
