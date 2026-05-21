/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Klebert Engineering
 */

#include "zserio/primitives.h"

/*
 * Primitive reads are `static inline` in `zserio/primitives.h` so
 * each caller's translation unit gets a full inlining of the
 * bit-cache fast path. The reader path therefore has no per-
 * primitive function-call overhead.
 *
 * The write side stays out-of-line in this file. It is dominated by
 * the smart byte-walk inside `zserio_bitstream_write_bits` which is
 * itself complex enough that inlining each per-width wrapper has no
 * measurable effect (the inner loop dominates). If a future
 * profiling pass identifies a gain there, the write side moves to
 * the header the same way the reads did.
 */

/* ------------------------------------------------------------------------ */
/* Unsigned writes                                                          */
/* ------------------------------------------------------------------------ */

zserio_status_t zserio_write_uint8(zserio_bitstream_writer_t *bs, uint8_t v)
{
    return zserio_bitstream_write_bits(bs, 8U, (uint64_t)v);
}

zserio_status_t zserio_write_uint16(zserio_bitstream_writer_t *bs, uint16_t v)
{
    return zserio_bitstream_write_bits(bs, 16U, (uint64_t)v);
}

zserio_status_t zserio_write_uint32(zserio_bitstream_writer_t *bs, uint32_t v)
{
    return zserio_bitstream_write_bits(bs, 32U, (uint64_t)v);
}

zserio_status_t zserio_write_uint64(zserio_bitstream_writer_t *bs, uint64_t v)
{
    return zserio_bitstream_write_bits(bs, 64U, v);
}

/* ------------------------------------------------------------------------ */
/* Signed writes                                                            */
/* ------------------------------------------------------------------------ */

zserio_status_t zserio_write_int8(zserio_bitstream_writer_t *bs, int8_t v)
{
    return zserio_bitstream_write_signed(bs, 8U, (int64_t)v);
}

zserio_status_t zserio_write_int16(zserio_bitstream_writer_t *bs, int16_t v)
{
    return zserio_bitstream_write_signed(bs, 16U, (int64_t)v);
}

zserio_status_t zserio_write_int32(zserio_bitstream_writer_t *bs, int32_t v)
{
    return zserio_bitstream_write_signed(bs, 32U, (int64_t)v);
}

zserio_status_t zserio_write_int64(zserio_bitstream_writer_t *bs, int64_t v)
{
    return zserio_bitstream_write_signed(bs, 64U, v);
}

/* ------------------------------------------------------------------------ */
/* Bool                                                                     */
/* ------------------------------------------------------------------------ */

zserio_status_t zserio_write_bool(zserio_bitstream_writer_t *bs, bool v)
{
    return zserio_bitstream_write_bits(bs, 1U, v ? (uint64_t)1U : (uint64_t)0U);
}
