/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Klebert Engineering
 */

#ifndef ZSERIO_PUBSUB_H_
#define ZSERIO_PUBSUB_H_

#include <stddef.h>
#include <stdint.h>

#include "zserio/allocator.h"
#include "zserio/status.h"

/**
 * @file
 * Transport seam for generated zserio pub/sub clients (REQ-FN-027).
 *
 * As with services, zserio-c generates the typed (de)serialization glue
 * per topic message but never a broker/transport. This header defines
 * the byte-level seam, mirroring upstream zserio's `IPubsub`
 * (publish / subscribe / unsubscribe) in MISRA style: a function-pointer
 * vtable plus opaque `impl`, `zserio_status_t` returns, and an explicit
 * subscription-context pointer instead of `std::shared_ptr` callbacks.
 *
 * Interop: the topic string comes from the schema's `topic(...)`
 * definition and the payload is the plain zserio wire bytes of the
 * message type.
 */

/** Opaque subscription handle, unique per successful subscribe (matches
 *  upstream `IPubsub::SubscriptionId`). */
typedef size_t zserio_pubsub_subscription_id_t;

/**
 * Raw per-topic delivery callback installed on the transport.
 *
 * The generated `subscribe<Message>` wrapper installs its own trampoline
 * of this type which deserializes `data` into the typed message and
 * forwards it to the application callback. `callback_context` is the
 * pointer the wrapper passed to `subscribe`.
 *
 * `topic`/`data` are borrowed for the duration of the call only.
 */
typedef void (*zserio_pubsub_on_topic_t)(void *callback_context,
                                         const char *topic, size_t topic_len,
                                         const uint8_t *data, size_t data_size);

/**
 * Pub/Sub transport seam, integrator-implemented.
 *
 * - `publish` sends `data_size` serialized bytes on `topic`.
 * - `subscribe` registers `callback` for `topic` and writes the new
 *   subscription id through `subscription_id`. `callback_context` MUST
 *   be stored and passed back on every delivery; the integrator MUST NOT
 *   take ownership of it (the application owns its lifetime, see the
 *   generated subscription struct).
 * - `unsubscribe` cancels a prior subscription by id.
 *
 * Every entry returns `ZSERIO_OK` on success or a `zserio_status_t`
 * describing the failure; outputs are valid only on `ZSERIO_OK`.
 */
typedef struct zserio_pubsub {
    zserio_status_t (*publish)(void *impl,
                               const char *topic, size_t topic_len,
                               const uint8_t *data, size_t data_size,
                               void *context);
    zserio_status_t (*subscribe)(void *impl,
                                 const char *topic, size_t topic_len,
                                 zserio_pubsub_on_topic_t callback, void *callback_context,
                                 void *context,
                                 zserio_pubsub_subscription_id_t *subscription_id);
    zserio_status_t (*unsubscribe)(void *impl,
                                   zserio_pubsub_subscription_id_t subscription_id);
    void *impl; /**< Opaque; passed back to every callback. */
} zserio_pubsub_t;

#endif /* ZSERIO_PUBSUB_H_ */
