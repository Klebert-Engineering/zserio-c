/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Fabian Klebert
 */

#ifndef ZSERIO_FLOAT_H_
#define ZSERIO_FLOAT_H_

#include <stdint.h>

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
/* Read / write                                                             */
/* ------------------------------------------------------------------------ */

zserio_status_t zserio_read_float16(zserio_bitstream_reader_t *bs, float  *out);
zserio_status_t zserio_read_float32(zserio_bitstream_reader_t *bs, float  *out);
zserio_status_t zserio_read_float64(zserio_bitstream_reader_t *bs, double *out);

zserio_status_t zserio_write_float16(zserio_bitstream_writer_t *bs, float  v);
zserio_status_t zserio_write_float32(zserio_bitstream_writer_t *bs, float  v);
zserio_status_t zserio_write_float64(zserio_bitstream_writer_t *bs, double v);

/* ------------------------------------------------------------------------ */
/* Bit-pattern conversion helpers                                           */
/*                                                                          */
/* These are public because tests and callers occasionally need to inspect  */
/* or build bit patterns directly. They never fail.                         */
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
float    zserio_float_from_u32(uint32_t v);

/** Reinterpret a `float` as its single-precision IEEE 754 bit pattern.
 *  Bit-exact `memcpy`. */
uint32_t zserio_float_to_u32  (float v);

/** Reinterpret a double-precision IEEE 754 bit pattern as a `double`. */
double   zserio_double_from_u64(uint64_t v);

/** Reinterpret a `double` as its double-precision IEEE 754 bit pattern. */
uint64_t zserio_double_to_u64  (double v);

#endif /* ZSERIO_FLOAT_H_ */
