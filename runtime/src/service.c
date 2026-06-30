/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Klebert Engineering
 */

#include <string.h>

#include "zserio/service.h"

/* Verifies: REQ-FN-027 */

/*
 * Initial serialization buffer in bytes. Doubled on ZSERIO_ERR_BUFFER_
 * OVERFLOW until the object fits. Most request/response messages are
 * well under this, so the common path serializes in a single pass.
 */
#define ZSERIO_SERVICE_INITIAL_CAP ((size_t)64U)

zserio_status_t zserio_service_serialize(const zserio_allocator_t *alloc,
                                         zserio_status_t (*write_fn)(const void *obj,
                                                 zserio_bitstream_writer_t *bs),
                                         const void *obj,
                                         uint8_t **out_data, size_t *out_size)
{
    if ((alloc == NULL) || (alloc->vtable == NULL) || (write_fn == NULL)
            || (out_data == NULL) || (out_size == NULL)) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }

    *out_data = NULL;
    *out_size = 0U;

    size_t cap = ZSERIO_SERVICE_INITIAL_CAP;
    for (;;) {
        uint8_t *work = (uint8_t *)alloc->vtable->alloc(alloc->userdata, cap, 1U);
        if (work == NULL) {
            return ZSERIO_ERR_OUT_OF_MEMORY;
        }

        zserio_bitstream_writer_t writer;
        zserio_status_t st = zserio_bitstream_writer_init(&writer, work, cap * 8U);
        if (st == ZSERIO_OK) {
            st = write_fn(obj, &writer);
        }

        if (st == ZSERIO_OK) {
            const size_t bytes = (zserio_bitstream_writer_position(&writer) + 7U) / 8U;
            if (bytes == 0U) {
                /* Zero-length payload: nothing to hand back (the allocator
                 * contract forbids a zero-size allocation). */
                alloc->vtable->free(alloc->userdata, work, cap, 1U);
                return ZSERIO_OK;
            }
            /* Hand back an exact-size buffer so the caller can free with
             * (size, 1) per the seam contract. */
            uint8_t *exact = (uint8_t *)alloc->vtable->alloc(alloc->userdata, bytes, 1U);
            if (exact == NULL) {
                alloc->vtable->free(alloc->userdata, work, cap, 1U);
                return ZSERIO_ERR_OUT_OF_MEMORY;
            }
            (void)memcpy(exact, work, bytes);
            alloc->vtable->free(alloc->userdata, work, cap, 1U);
            *out_data = exact;
            *out_size = bytes;
            return ZSERIO_OK;
        }

        alloc->vtable->free(alloc->userdata, work, cap, 1U);

        if (st != ZSERIO_ERR_BUFFER_OVERFLOW) {
            return st;
        }

        /* Grow and retry. Guard against size_t overflow of both the byte
         * capacity and the bit count passed to the writer. */
        if (cap > ((SIZE_MAX / 8U) / 2U)) {
            return ZSERIO_ERR_BUFFER_OVERFLOW;
        }
        cap *= 2U;
    }
}
