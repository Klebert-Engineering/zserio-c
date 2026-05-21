/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Klebert Engineering
 */

#ifndef ZSERIO_EXTERN_H_
#define ZSERIO_EXTERN_H_

#include <stddef.h>
#include <stdint.h>

#include "zserio/allocator.h"
#include "zserio/bitstream.h"
#include "zserio/status.h"

/**
 * @file
 * Read and write helpers for the zserio `extern` type.
 *
 * Wire format: a `varsize` BIT length followed by that many raw bits
 * at the current bit position. The bits are NOT byte-aligned — they
 * begin wherever the cursor is, and the last byte may have unused
 * trailing bits.
 *
 * Two read flavors:
 * - `zserio_read_extern_view` always succeeds (subject to buffer
 *   bounds) and is O(1). Returns a `zserio_bit_view_t` that natively
 *   expresses the bit offset within the first byte. The view points
 *   into the source bitstream's buffer; the caller must keep the
 *   buffer alive for the view's lifetime.
 * - `zserio_read_extern` materializes the bits into a fresh
 *   contiguous buffer aligned at byte 0 bit 0. Allocates through the
 *   caller's allocator. Free via `zserio_extern_free`.
 *
 * The "borrow always works" property of `extern` is the answer to
 * "can we parse externs in one go?" — yes, we can: the view path
 * does it in O(1).
 */

/** Borrowed bit view. The bits are at `base[0]`, starting at bit
 *  position `bit_offset` (0..7), running for `bit_length` bits.
 *  `base` is NULL iff `bit_length == 0`. */
typedef struct zserio_bit_view {
    const uint8_t *base;
    size_t         bit_offset;   /**< 0..7 */
    size_t         bit_length;
} zserio_bit_view_t;

/** Owned bit sequence, materialized at byte 0 bit 0 of `bytes`. The
 *  last byte may have unused trailing bits (kept zero). Free via
 *  `zserio_extern_free`. */
typedef struct zserio_extern {
    uint8_t *bytes;
    size_t   bit_length;
} zserio_extern_t;

/* ------------------------------------------------------------------------ */
/* Reads                                                                    */
/* ------------------------------------------------------------------------ */

/**
 * Zero-copy read. Always succeeds (subject to buffer bounds). Sets
 * `out->bit_offset` to the current cursor's bit-within-byte (0..7).
 * Advances the cursor past the bit sequence.
 */
zserio_status_t zserio_read_extern_view(zserio_bitstream_reader_t *bs,
                                        zserio_bit_view_t *out);

/**
 * Materializing read. Allocates `ceil(bit_length / 8)` bytes through
 * `alloc` and copies the bits into a byte-0/bit-0-aligned position.
 * Free with `zserio_extern_free`.
 */
zserio_status_t zserio_read_extern(zserio_bitstream_reader_t *bs,
                                   const zserio_allocator_t *alloc,
                                   zserio_extern_t *out);

/**
 * Free an owned extern. Safe on a zero-initialized struct and on NULL.
 * After the call, `e->bytes` is NULL and `e->bit_length` is 0.
 */
void zserio_extern_free(zserio_extern_t *e, const zserio_allocator_t *alloc);

/* ------------------------------------------------------------------------ */
/* Write                                                                    */
/* ------------------------------------------------------------------------ */

/**
 * Write a bit sequence: a `varsize` length prefix followed by the
 * bits. `v.bit_offset` must be in [0, 7]. The source bits are read
 * from `v.base[0]` starting at `v.bit_offset` for `v.bit_length` bits.
 *
 * Returns:
 * - `ZSERIO_ERR_INVALID_ARGUMENT` for `bit_offset > 7`, or
 *   `bit_length > 0` with NULL `base`
 * - `ZSERIO_ERR_VALUE_OUT_OF_RANGE` if `bit_length` exceeds the varsize cap
 * - `ZSERIO_ERR_BUFFER_OVERFLOW` if the writer cannot hold all the bits
 */
zserio_status_t zserio_write_extern(zserio_bitstream_writer_t *bs,
                                    zserio_bit_view_t v);

#endif /* ZSERIO_EXTERN_H_ */
