/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Fabian Klebert
 */

#ifndef ZSERIO_PRIMITIVES_H_
#define ZSERIO_PRIMITIVES_H_

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "zserio/bitstream.h"
#include "zserio/status.h"

/**
 * @file
 * Read and write helpers for the fixed-width built-in zserio types
 * (`uint8`/`uint16`/`uint32`/`uint64`, `int8`/`int16`/`int32`/`int64`, `bool`).
 *
 * Each helper is a typed wrapper over `zserio_bitstream_read_bits` /
 * `read_signed` (and their writer counterparts) with a `(num_bits)` of
 * 8/16/32/64 (or 1 for `bool`). The wrappers are here so generated
 * code stays self-documenting — `zserio_read_uint16(&bs, &v)` reads
 * better than `zserio_bitstream_read_bits(&bs, 16, &raw)` + cast.
 *
 * Bit-field accessors of variable width (`bit:N`, `int:N`,
 * `bit<expr>`, `int<expr>`) are exposed by `bitstream.h` directly
 * as `zserio_bitstream_read_bits` / `read_signed` / `write_bits` /
 * `write_signed`. There is no separate "bits.h" — that would be pure
 * aliases.
 *
 * All functions return `ZSERIO_ERR_INVALID_ARGUMENT` when `out` (read
 * functions) is NULL. NULL `bs` is detected by the underlying
 * `bitstream` call and surfaced unchanged. Other errors propagate
 * from the underlying call (`ZSERIO_ERR_END_OF_STREAM`,
 * `ZSERIO_ERR_BUFFER_OVERFLOW`).
 */

/* ------------------------------------------------------------------------ */
/* Reads                                                                    */
/*                                                                          */
/* These are `static inline` so each caller's translation unit gets a       */
/* full inlining of the cache fast path — no TU-crossing call for the     */
/* common case. The shared validation pattern is:                           */
/*   1. NULL out → INVALID_ARGUMENT.                                        */
/*   2. NULL bs or bit-index overflow → fall through to                     */
/*      `zserio_bitstream_read_bits` which surfaces the correct error.     */
/*   3. End-of-stream → INVALID error code from the impl.                  */
/* The hot path on a valid call is ~5 instructions.                         */
/* ------------------------------------------------------------------------ */

#define ZSERIO_PRIMITIVES_DEFINE_READ_UNSIGNED(SUF, T, N)                              \
static inline zserio_status_t zserio_read_##SUF(zserio_bitstream_reader_t *bs, T *out) \
{                                                                                      \
    if (out == NULL) {                                                                 \
        return ZSERIO_ERR_INVALID_ARGUMENT;                                            \
    }                                                                                  \
    if (bs == NULL) {                                                                  \
        return ZSERIO_ERR_INVALID_ARGUMENT;                                            \
    }                                                                                  \
    if ((bs->bit_index + (N)) > bs->buffer_bit_size) {                                 \
        return ZSERIO_ERR_END_OF_STREAM;                                               \
    }                                                                                  \
    *out = (T)zserio_bitstream_read_bits_impl(bs, (N));                                \
    return ZSERIO_OK;                                                                  \
}

ZSERIO_PRIMITIVES_DEFINE_READ_UNSIGNED(uint8,  uint8_t,   8U)
ZSERIO_PRIMITIVES_DEFINE_READ_UNSIGNED(uint16, uint16_t, 16U)
ZSERIO_PRIMITIVES_DEFINE_READ_UNSIGNED(uint32, uint32_t, 32U)
ZSERIO_PRIMITIVES_DEFINE_READ_UNSIGNED(uint64, uint64_t, 64U)

/* Signed reads sign-extend the value before narrowing. For 64-bit
 * the extracted value already occupies all bits so no extension. */
#define ZSERIO_PRIMITIVES_DEFINE_READ_SIGNED(SUF, T, N)                                \
static inline zserio_status_t zserio_read_##SUF(zserio_bitstream_reader_t *bs, T *out) \
{                                                                                      \
    if (out == NULL) {                                                                 \
        return ZSERIO_ERR_INVALID_ARGUMENT;                                            \
    }                                                                                  \
    if (bs == NULL) {                                                                  \
        return ZSERIO_ERR_INVALID_ARGUMENT;                                            \
    }                                                                                  \
    if ((bs->bit_index + (N)) > bs->buffer_bit_size) {                                 \
        return ZSERIO_ERR_END_OF_STREAM;                                               \
    }                                                                                  \
    uint64_t raw = zserio_bitstream_read_bits_impl(bs, (N));                           \
    /* Sign-extend if the MSB of the read bits is set. For N == 64 the value           \
     * already occupies all bits; the mask table at [64] is UINT64_MAX so              \
     * ~mask_table[64] == 0 and the OR is a no-op (also guarded by the if). */        \
    if ((N) < 64U) {                                                                   \
        const uint64_t sign_bit = (uint64_t)1U << (uint64_t)((N) - 1U);                \
        if ((raw & sign_bit) != 0U) {                                                  \
            raw |= ~zserio_bitstream_mask_table[(N)];                                  \
        }                                                                              \
    }                                                                                  \
    int64_t signed_value;                                                              \
    (void)memcpy(&signed_value, &raw, sizeof(signed_value));                           \
    *out = (T)signed_value;                                                            \
    return ZSERIO_OK;                                                                  \
}

ZSERIO_PRIMITIVES_DEFINE_READ_SIGNED(int8,  int8_t,   8U)
ZSERIO_PRIMITIVES_DEFINE_READ_SIGNED(int16, int16_t, 16U)
ZSERIO_PRIMITIVES_DEFINE_READ_SIGNED(int32, int32_t, 32U)
ZSERIO_PRIMITIVES_DEFINE_READ_SIGNED(int64, int64_t, 64U)

/**
 * Read a single bit as a boolean. `1` becomes `true`, `0` becomes `false`.
 */
static inline zserio_status_t zserio_read_bool(zserio_bitstream_reader_t *bs, bool *out)
{
    if (out == NULL) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }
    if (bs == NULL) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }
    if ((bs->bit_index + 1U) > bs->buffer_bit_size) {
        return ZSERIO_ERR_END_OF_STREAM;
    }
    *out = (zserio_bitstream_read_bits_impl(bs, 1U) != 0U);
    return ZSERIO_OK;
}

/* ------------------------------------------------------------------------ */
/* Writes                                                                   */
/* ------------------------------------------------------------------------ */

zserio_status_t zserio_write_uint8 (zserio_bitstream_writer_t *bs, uint8_t  v);
zserio_status_t zserio_write_uint16(zserio_bitstream_writer_t *bs, uint16_t v);
zserio_status_t zserio_write_uint32(zserio_bitstream_writer_t *bs, uint32_t v);
zserio_status_t zserio_write_uint64(zserio_bitstream_writer_t *bs, uint64_t v);

zserio_status_t zserio_write_int8 (zserio_bitstream_writer_t *bs, int8_t  v);
zserio_status_t zserio_write_int16(zserio_bitstream_writer_t *bs, int16_t v);
zserio_status_t zserio_write_int32(zserio_bitstream_writer_t *bs, int32_t v);
zserio_status_t zserio_write_int64(zserio_bitstream_writer_t *bs, int64_t v);

/**
 * Write a boolean as a single bit. `false` → `0`, `true` → `1`.
 */
zserio_status_t zserio_write_bool(zserio_bitstream_writer_t *bs, bool v);

#endif /* ZSERIO_PRIMITIVES_H_ */
