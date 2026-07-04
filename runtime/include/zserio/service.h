/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Klebert Engineering
 */

#ifndef ZSERIO_SERVICE_H_
#define ZSERIO_SERVICE_H_

#include <stddef.h>
#include <stdint.h>

#include "zserio/allocator.h"
#include "zserio/bitstream.h"
#include "zserio/status.h"

/**
 * @file
 * Transport seam for generated zserio services (REQ-FN-027).
 *
 * zserio-c generates the typed (de)serialization glue and method
 * dispatch for a `service`; it never implements a transport. This
 * header defines the single byte-level seam both ends bind to, mirroring
 * upstream zserio's `IServiceClient` (client) and `IService` (server)
 * but expressed MISRA-style: a function-pointer vtable plus an opaque
 * `impl` (the same shape as `zserio_allocator_t`; see
 * docs/misra-deviations/D004), `zserio_status_t` instead of exceptions,
 * and explicit allocator threading instead of `std::allocator`.
 *
 * Interop contract (byte-for-byte with upstream over the same transport):
 * - the dispatch key is the *short* method name (no package/service
 *   qualification), passed as `(method_name, method_name_len)`;
 * - the request and response payloads are the plain zserio wire bytes
 *   of the method's request/response types (`_write` / `_read` output).
 */

/**
 * Client- and server-side transport seam.
 *
 * On the client, the integrator implements this to ship `request` to a
 * peer and return the peer's serialized response. On the server, the
 * generated `<service>_service_init` fills this in so an integrator's
 * server transport can feed arriving requests to the dispatch glue.
 *
 * `call_method` contract:
 * - `method_name` has `method_name_len` bytes and is NOT guaranteed
 *   NUL-terminated; compare by span.
 * - `request` holds `request_size` serialized bytes (may be NULL only
 *   when `request_size == 0`).
 * - On `ZSERIO_OK`, `*response` points to `*response_size` bytes
 *   allocated through `alloc->vtable->alloc(alloc->userdata,
 *   *response_size, 1u)`; ownership transfers to the caller, which MUST
 *   release it via `alloc->vtable->free(userdata, *response,
 *   *response_size, 1u)`.
 * - On any non-OK status, `*response` MUST be set to NULL and
 *   `*response_size` to 0.
 * - An unknown method name MUST return `ZSERIO_ERR_INVALID_ARGUMENT`.
 */
typedef struct zserio_service {
    zserio_status_t (*call_method)(void *impl,
                                   const char *method_name, size_t method_name_len,
                                   const uint8_t *request, size_t request_size,
                                   const zserio_allocator_t *alloc,
                                   uint8_t **response, size_t *response_size);
    void *impl; /**< Opaque; passed back to `call_method`. */
} zserio_service_t;

/**
 * Serialize a zserio object into a freshly allocated byte buffer.
 *
 * The generated code has no `bitSizeOf`, so this grows a buffer and
 * retries `write_fn` until the object fits (the object's own content
 * bounds the loop; `write_fn` is a pure serialization with no side
 * effects). Shared by the generated service and pubsub glue.
 *
 * `write_fn` MUST invoke the object's generated `_write` after casting
 * `obj` back to its concrete type (a `void *`-to-object conversion, see
 * docs/misra-deviations/D004). On `ZSERIO_OK`, `*out_data` holds
 * `*out_size` bytes allocated through `alloc` with alignment 1; release
 * with the matching `(*out_size, 1u)`. On failure `*out_data` is NULL
 * and `*out_size` is 0.
 */
zserio_status_t zserio_service_serialize(const zserio_allocator_t *alloc,
                                         zserio_status_t (*write_fn)(const void *obj,
                                                 zserio_bitstream_writer_t *bs),
                                         const void *obj,
                                         uint8_t **out_data, size_t *out_size);

#endif /* ZSERIO_SERVICE_H_ */
