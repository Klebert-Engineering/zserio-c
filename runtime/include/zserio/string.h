/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Klebert Engineering
 */

#ifndef ZSERIO_STRING_H_
#define ZSERIO_STRING_H_

#include <stddef.h>
#include <stdint.h>

#include "zserio/allocator.h"
#include "zserio/bitstream.h"
#include "zserio/status.h"

/**
 * @file
 * Read and write helpers for the zserio `string` type.
 *
 * Wire format: a `varsize` byte length followed by that many UTF-8 bytes
 * at the current bit position. Bytes are NOT auto-aligned by themselves
 * (they start wherever the bit cursor is). When the cursor is byte-
 * aligned at the start of the field, the bytes are byte-aligned too,
 * which enables the zero-copy borrowed-view path below.
 *
 * UTF-8 well-formedness is NOT validated by the runtime, matching
 * upstream behavior. Callers concerned about malformed UTF-8 must
 * validate after decode.
 *
 * Two read flavors:
 * - `zserio_read_string_view` is O(1), no allocator. It returns a
 *   pointer into the source bitstream's buffer. Succeeds only when the
 *   reader's bit position is byte-aligned at entry. The view is valid
 *   for as long as the source buffer lives.
 * - `zserio_read_string` always succeeds (subject to buffer state). It
 *   allocates a fresh contiguous buffer through the caller's allocator
 *   and copies bit-by-bit if the source is misaligned. Caller frees
 *   with `zserio_string_free`.
 *
 * `zserio_string_view_t.bytes` is NOT NUL-terminated. `length` counts
 * bytes, not code points.
 */

/** Borrowed UTF-8 string view. Caller must keep the underlying buffer
 *  alive for the view's lifetime. */
typedef struct zserio_string_view {
    const char *bytes;   /**< UTF-8, NOT NUL-terminated. NULL iff length == 0. */
    size_t      length;  /**< Length in bytes (not code points). */
} zserio_string_view_t;

/** Owned UTF-8 string. Free via `zserio_string_free`. */
typedef struct zserio_string {
    char  *bytes;        /**< Allocator-owned. NULL iff length == 0. */
    size_t length;       /**< Length in bytes. */
} zserio_string_t;

/**
 * Construct a view over a NUL-terminated C string. `cstr` may be NULL,
 * in which case the view is empty (length 0). Convenience for tests
 * and call sites that have a literal.
 */
zserio_string_view_t zserio_string_view_from_cstr(const char *cstr);

/* ------------------------------------------------------------------------ */
/* Reads                                                                    */
/* ------------------------------------------------------------------------ */

/**
 * Zero-copy read. The reader MUST be byte-aligned on entry; otherwise
 * `ZSERIO_ERR_INVALID_ARGUMENT` is returned and the position is not
 * advanced. On success the reader advances past the string and `*out`
 * references the source buffer.
 */
zserio_status_t zserio_read_string_view(zserio_bitstream_reader_t *bs,
                                        zserio_string_view_t *out);

/**
 * Materializing read. Allocates a fresh buffer through `alloc` and
 * copies the bytes (possibly bit-shifted from the source). Always
 * succeeds when the buffer state allows, regardless of bit alignment.
 * Caller frees with `zserio_string_free`.
 */
zserio_status_t zserio_read_string(zserio_bitstream_reader_t *bs,
                                   const zserio_allocator_t *alloc,
                                   zserio_string_t *out);

/**
 * Free an owned string previously returned from `zserio_read_string`.
 * Safe to call on a zero-initialized `zserio_string_t`. After the call,
 * `s->bytes` is NULL and `s->length` is 0.
 */
void zserio_string_free(zserio_string_t *s, const zserio_allocator_t *alloc);

/* ------------------------------------------------------------------------ */
/* Write                                                                    */
/* ------------------------------------------------------------------------ */

/**
 * Write the varsize length prefix followed by the bytes. Returns
 * `ZSERIO_ERR_VALUE_OUT_OF_RANGE` if `s.length` exceeds the varsize
 * cap (2^31 - 1), and `ZSERIO_ERR_INVALID_ARGUMENT` if `s.length > 0`
 * but `s.bytes` is NULL.
 */
zserio_status_t zserio_write_string(zserio_bitstream_writer_t *bs,
                                    zserio_string_view_t s);

#endif /* ZSERIO_STRING_H_ */
