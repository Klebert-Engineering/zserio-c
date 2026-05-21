/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Klebert Engineering
 */

#ifndef ZSERIO_FLOAT_H_
#define ZSERIO_FLOAT_H_

#include <stdint.h>
#include <string.h>

#include "zserio/bitstream.h"
#include "zserio/status.h"

/**
 * @file
 * IEEE 754 floating-point read and write helpers, and the integer-only
 * conversion routines that back them.
 *
 * Per REQ-FS-004, no `float` or `double` value participates in branch
 * conditions, loop bounds, indexing, or sizing anywhere in the runtime
 * or in code that links against it. The conversion routines below are
 * the only place where the host's `float`/`double` types appear at all
 * — and even there, all logic is on the bit pattern (`uint16_t`,
 * `uint32_t`, `uint64_t`). The `float`/`double` is just the boundary
 * representation that callers of the runtime see.
 *
 * The wire format is the bit-pattern integer encoded big-endian
 * (`02-wire-format.md` §2.4). float16 is the IEEE 754 half-precision
 * format, narrowed from / widened to a host `float` via the upstream
 * algorithm.
 */

/* ------------------------------------------------------------------------ */
/* Bit-pattern conversion helpers                                           */
/*                                                                          */
/* These are reachable by tests and callers that need to inspect or build   */
/* bit patterns directly. They never fail. The float32/float64 widen-from-  */
/* integer pair is a bare `memcpy`, so it is `static inline` here for the   */
/* same reason the read helpers below are — a caller's hot decode loop      */
/* sees the whole path without a TU-crossing call. The float16 widen and    */
/* the narrow-to-integer direction stay out-of-line: float16 widening is a  */
/* genuine decomposition algorithm, and the narrow direction is on the      */
/* (already-faster) encode path where inlining bought nothing.              */
/* ------------------------------------------------------------------------ */

/** Reinterpret a half-precision IEEE 754 bit pattern as a single-precision
 *  float. Subnormal half values are normalized; infinity/NaN are propagated. */
float    zserio_float_from_u16(uint16_t v);

/** Reinterpret a single-precision float as a half-precision bit pattern.
 *  Round-to-nearest on truncation; underflow may produce subnormals or zero;
 *  overflow produces infinity. */
uint16_t zserio_float_to_u16  (float v);

/** Reinterpret a single-precision IEEE 754 bit pattern as a `float`.
 *  Bit-exact `memcpy` — no arithmetic on the value. */
static inline float zserio_float_from_u32(uint32_t v)
{
    float f = 0.0F;
    (void)memcpy(&f, &v, sizeof(f));
    return f;
}

/** Reinterpret a `float` as its single-precision IEEE 754 bit pattern.
 *  Bit-exact `memcpy`. */
uint32_t zserio_float_to_u32  (float v);

/** Reinterpret a double-precision IEEE 754 bit pattern as a `double`.
 *  Bit-exact `memcpy`. */
static inline double zserio_double_from_u64(uint64_t v)
{
    double d = 0.0;
    (void)memcpy(&d, &v, sizeof(d));
    return d;
}

/** Reinterpret a `double` as its double-precision IEEE 754 bit pattern. */
uint64_t zserio_double_to_u64  (double v);

/* ------------------------------------------------------------------------ */
/* Read                                                                     */
/*                                                                          */
/* `static inline`, mirroring the integer primitive reads in primitives.h:  */
/* each caller's translation unit gets the cache fast path inlined, with no */
/* TU-crossing call for the common case. The shared validation pattern is:  */
/*   1. NULL out / NULL bs → INVALID_ARGUMENT.                              */
/*   2. bit-index + width past the buffer → END_OF_STREAM.                  */
/* Otherwise the read funnels straight into zserio_bitstream_read_bits_impl */
/* — the same unchecked fast path the integer reads use — skipping the     */
/* num_bits range re-check and the trace hook of the public read_bits.      */
/* ------------------------------------------------------------------------ */

static inline zserio_status_t zserio_read_float16(zserio_bitstream_reader_t *bs, float *out)
{
    if (out == NULL) { return ZSERIO_ERR_INVALID_ARGUMENT; }
    if (bs == NULL)  { return ZSERIO_ERR_INVALID_ARGUMENT; }
    if ((bs->bit_index + 16U) > bs->buffer_bit_size) { return ZSERIO_ERR_END_OF_STREAM; }
    *out = zserio_float_from_u16((uint16_t)zserio_bitstream_read_bits_impl(bs, 16U));
    return ZSERIO_OK;
}

static inline zserio_status_t zserio_read_float32(zserio_bitstream_reader_t *bs, float *out)
{
    if (out == NULL) { return ZSERIO_ERR_INVALID_ARGUMENT; }
    if (bs == NULL)  { return ZSERIO_ERR_INVALID_ARGUMENT; }
    if ((bs->bit_index + 32U) > bs->buffer_bit_size) { return ZSERIO_ERR_END_OF_STREAM; }
    *out = zserio_float_from_u32((uint32_t)zserio_bitstream_read_bits_impl(bs, 32U));
    return ZSERIO_OK;
}

static inline zserio_status_t zserio_read_float64(zserio_bitstream_reader_t *bs, double *out)
{
    if (out == NULL) { return ZSERIO_ERR_INVALID_ARGUMENT; }
    if (bs == NULL)  { return ZSERIO_ERR_INVALID_ARGUMENT; }
    if ((bs->bit_index + 64U) > bs->buffer_bit_size) { return ZSERIO_ERR_END_OF_STREAM; }
    *out = zserio_double_from_u64(zserio_bitstream_read_bits_impl(bs, 64U));
    return ZSERIO_OK;
}

/* ------------------------------------------------------------------------ */
/* Write                                                                    */
/* ------------------------------------------------------------------------ */

zserio_status_t zserio_write_float16(zserio_bitstream_writer_t *bs, float  v);
zserio_status_t zserio_write_float32(zserio_bitstream_writer_t *bs, float  v);
zserio_status_t zserio_write_float64(zserio_bitstream_writer_t *bs, double v);

#endif /* ZSERIO_FLOAT_H_ */
