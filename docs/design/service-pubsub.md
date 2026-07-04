# Service and Pub/Sub interface generation

Status: implemented (REQ-FN-027). This document records the upstream
contract we interoperate with, the C / MISRA translation, the ownership
model, and the open decisions that were settled.

## What upstream zserio (C++) does

Read first-class from the pinned upstream `v2.18.1`
(`compiler/extensions/cpp/runtime/src/zserio/IService.h`, `IPubsub.h`,
and the `Service.*.ftl` / `Pubsub.*.ftl` templates). Upstream generates
the serialization + dispatch glue and an abstract transport seam; it
never implements a transport. Three layers:

1. **Runtime interfaces.** `IServiceClient::callMethod(methodName,
   requestData, context) -> responseBytes` (client seam, integrator-
   implemented); `IService::callMethod(...)` (server seam); `IPubsub`
   with `publish` / `subscribe(onTopic)` / `unsubscribe`.
2. **Generated service code.** A `Client` (typed per-method wrappers
   that serialize the request, call the transport, deserialize the
   response) and a `Service` (dispatches on the *short method-name
   string* to pure-virtual typed `*Impl` handlers).
3. **Generated pubsub code.** `publish<Msg>` / `subscribe<Msg>` wrappers
   that (de)serialize and forward to/from `IPubsub`; the topic string
   comes from the schema `topic(...)` definition.

**Interop contract** (byte-for-byte over the same transport):
- service dispatch key = the **short** method name, e.g. `"powerOfTwo"`
  (not package/service qualified). `serviceFullName` (`pkg.Service`) is
  exposed separately, for transports/diagnostics, not for dispatch.
- request/response payload = the plain zserio wire bytes of the request/
  response type (`write()` / read-ctor).
- pubsub topic = the schema's topic string; message payload = the wire
  bytes of the message type.

## The C / MISRA translation

Every C++ mechanism maps onto a pattern already used in this codebase:

| C++ | zserio-c |
|---|---|
| abstract interface (pure virtual) | function-pointer vtable + opaque `impl` (same as `zserio_allocator_t`, deviation D004) |
| `ObjectServiceData<T>` template | the type's generated `_write` / `_read` |
| exceptions | `zserio_status_t` + out-params |
| `std::vector<uint8_t>` response | bytes allocated via the allocator vtable, ownership handed to the caller |
| `bitSizeOf` for buffer sizing | not generated in the C target → `zserio_service_serialize` grows-and-retries |
| `std::shared_ptr<OnTopicCallback>` | caller-owned subscription struct + a generated raw trampoline |

### Runtime seam (stable, hand-written)

`runtime/include/zserio/service.h`:
- `zserio_service_t` — one byte-level vtable used by both ends:
  `call_method(impl, method_name, len, request, request_size, alloc,
  response, response_size)`.
- `zserio_service_serialize(alloc, write_fn, obj, out_data, out_size)` —
  grow-and-retry serialization into an alloc'd, **exact-size** buffer
  (so the caller frees with `(size, 1)`); shared by service + pubsub.

`runtime/include/zserio/pubsub.h`:
- `zserio_pubsub_t` — `publish` / `subscribe` / `unsubscribe` vtable.
- `zserio_pubsub_on_topic_t` — raw delivery callback type.
- `zserio_pubsub_subscription_id_t` (= `size_t`, matches upstream).

### Generated service code (per `service Svc` in package `pkg`)

Header (`pkg/svc.h`):
```c
/* application implements these typed handlers */
typedef struct {
    void *context;
    zserio_status_t (*<method>)(void *context,
            const <pkg>_<request>_t *request,
            <pkg>_<response>_t *response /* out */,
            const zserio_allocator_t *alloc);
    /* ... one per method ... */
} <pkg>_<svc>_handlers_t;

/* build a server seam backed by the handlers */
void            <pkg>_<svc>_service_init(zserio_service_t *self,
                    const <pkg>_<svc>_handlers_t *handlers);
const char     *<pkg>_<svc>_service_full_name(void);   /* "pkg.Svc" */

/* typed client wrapper over an integrator transport */
zserio_status_t <pkg>_<svc>_<method>(const zserio_service_t *transport,
                    const <pkg>_<request>_t *request,
                    <pkg>_<response>_t *response_out,
                    const zserio_allocator_t *alloc);
```
Source (`pkg/svc.c`):
- per method, a `static` `_write` thunk (`const void*` → typed, D004) so
  `zserio_service_serialize` can drive the request/response `_write`;
- a `static` dispatch `call_method` that span-compares `method_name`
  against each short name, `_read`s the request (via `alloc`), calls the
  handler, serializes the response, and returns the bytes; unknown method
  → `ZSERIO_ERR_INVALID_ARGUMENT`;
- client wrappers that serialize the request, call
  `transport->call_method("<method>", ...)`, then `_read` the response.

### Generated pubsub code (per `pubsub Ps`)

For each published message: `zserio_status_t <pkg>_<ps>_publish_<msg>(
const zserio_pubsub_t*, const <msg>_t*, const zserio_allocator_t*, void
*context)` — serialize + `publish(topic, ...)`.

For each subscribed message: a caller-owned subscription struct holding
the typed application callback + allocator, plus `subscribe_<msg>(const
zserio_pubsub_t*, <sub>_t *sub /* lifetime >= subscription */, void
*context, id*)`. A generated raw trampoline (`zserio_pubsub_on_topic_t`)
casts the context back to the subscription, `_read`s the message (via
the stored allocator), invokes the application callback with a borrowed
typed message, then `_free`s it. `unsubscribe` forwards by id.

## Ownership / lifetime

- **Request bytes** (client) and **response bytes** (server) are produced
  by `zserio_service_serialize`: alloc'd via the caller's allocator with
  alignment 1, exact size, ownership handed across the seam; the receiver
  frees with `(size, 1)`.
- **Decoded request/response/message objects** are owned by whoever
  `_read` them and released with the matching `_free`. The server frees
  the decoded request and the response object it serialized; the client
  hands the decoded response to the caller (freed via `_free`). A pubsub
  delivery's typed message is borrowed for the callback only, then freed.
- **No hidden heap on the write path:** the generated `_write` functions
  still take no allocator and allocate nothing; only the service/pubsub
  *glue* allocates (via the explicit allocator), exactly like `_read`.
  `tools/check_no_alloc_in_writers.py` continues to hold (it scans
  `_write` functions, which are unchanged).

## MISRA / ASIL

- The transport vtables are the same construct as `zserio_allocator_t`;
  the function-pointer-call and `void *`→object conversions are covered
  by the existing deviation **D004** (allocator void-cast) — see
  `docs/misra-deviations/`. No new deviation class is introduced.
- No exceptions, no recursion in the glue (dispatch is a flat span
  compare), no globals — REQ-FS-002/003/007 hold.
- Reflection is intentionally absent (consistent with
  `docs/upstream-analysis/03-plugin-spi.md`: no `ITypeInfo`/`Reflectable`).
  Dispatch is generated code, which is simpler and more ASIL-friendly
  than runtime reflection.

## Settled decisions

- **Method/topic identity = upstream strings** (short method name; schema
  topic string) so a zserio-c peer interoperates with an upstream peer
  over the same transport.
- **Response sizing** via grow-and-retry (no `bitSizeOf` in the C target).
- **Pubsub subscription lifetime** = caller-owned struct (no hidden heap,
  no shared_ptr), released by the application after `unsubscribe`.
- **`bytes` request/response/message** (upstream's `isBytes` shortcut) is
  not specially handled in this first version; such payloads round-trip
  as ordinary serialized types. Tracked as a follow-up.
- **Topics must be plain string literals.** The generated glue bakes the
  topic into the code, so a non-literal `topic(...)` expression
  (concatenation, const reference) is rejected with an error diagnostic
  at generation time — failing loudly beats silently publishing on a
  wrong topic. Constant folding of topic expressions is a possible
  extension.
