/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Fabian Klebert
 */

#ifndef ZSERIO_BYTES_H_
#define ZSERIO_BYTES_H_

#include <stddef.h>
#include <stdint.h>

#include "zserio/allocator.h"
#include "zserio/bitstream.h"
#include "zserio/status.h"

/**
 * @file
 * Read and write helpers for the zserio `bytes` type.
 *
 * Wire format: a `varsize` BYTE length followed by that many raw bytes
 * at the current bit position. The byte payload is byte-aligned
 * exactly when the cursor at the start of the field is byte-aligned
 * (the varsize itself is read in 8-bit chunks, preserving alignment).
 *
 * Note the unit difference from `extern`: bytes counts BYTES; extern
 * counts BITS.
 *
 * Two read flavors:
 * - `zserio_read_bytes_view` is O(1) and zero-copy. Requires byte-
 *   aligned source. Returns `ZSERIO_ERR_INVALID_ARGUMENT` and does not
 *   advance the cursor when the source is bit-misaligned (caller falls
 *   back to the materializing read).
 * - `zserio_read_bytes` always succeeds (subject to buffer state),
 *   allocates through the caller's vtable, copies byte-by-byte
 *   (handles bit-misalignment naturally).
 */

typedef struct zserio_bytes_view {
    const uint8_t *base;     /**< NULL iff length == 0. */
    size_t         length;   /**< Bytes. */
} zserio_bytes_view_t;

typedef struct zserio_bytes {
    uint8_t *bytes;          /**< Allocator-owned. NULL iff length == 0. */
    size_t   length;
} zserio_bytes_t;

/* ------------------------------------------------------------------------ */
/* Reads                                                                    */
/* ------------------------------------------------------------------------ */

zserio_status_t zserio_read_bytes_view(zserio_bitstream_reader_t *bs,
                                       zserio_bytes_view_t *out);

zserio_status_t zserio_read_bytes(zserio_bitstream_reader_t *bs,
                                  const zserio_allocator_t *alloc,
                                  zserio_bytes_t *out);

void zserio_bytes_free(zserio_bytes_t *b, const zserio_allocator_t *alloc);

/* ------------------------------------------------------------------------ */
/* Write                                                                    */
/* ------------------------------------------------------------------------ */

zserio_status_t zserio_write_bytes(zserio_bitstream_writer_t *bs,
                                   zserio_bytes_view_t v);

#endif /* ZSERIO_BYTES_H_ */
