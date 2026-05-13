/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Fabian Klebert
 */

#ifndef ZSERIO_VARINT_H_
#define ZSERIO_VARINT_H_

#include <stdint.h>

#include "zserio/bitstream.h"
#include "zserio/delta.h"
#include "zserio/status.h"

/**
 * @file
 * Variable-length integer encoding and decoding.
 *
 * The encoded ranges are NARROWER than the underlying C types
 * because the encoding allocates a sign / continuation bit per byte:
 *
 * Type        | Bytes | Magnitude bits | Encoded range
 * ------------|-------|----------------|---------------------------------
 * varuint16   | 1..2  | 7  + 8 = 15    | [0, 32767]
 * varuint32   | 1..4  | 7×3 + 8 = 29   | [0, 2^29 - 1]
 * varuint64   | 1..8  | 7×7 + 8 = 57   | [0, 2^57 - 1]
 * varuint     | 1..9  | 7×8 + 8 = 64   | [0, 2^64 - 1]
 * varsize     | 1..5  | capped at 31   | [0, 2^31 - 1]
 * varint16    | 1..2  | 6  + 8 = 14    | [-(2^14 - 1), 2^14 - 1]
 * varint32    | 1..4  | 6 + 7×2 + 8    | [-(2^28 - 1), 2^28 - 1]
 * varint64    | 1..8  | 6 + 7×6 + 8    | [-(2^56 - 1), 2^56 - 1]
 * varint      | 1..9  | 6 + 7×7 + 8    | [-(2^63 - 1), 2^63 - 1]   *
 *
 * (*) `varint` additionally encodes INT64_MIN via a 1-byte special
 *     form (sign=1, magnitude=0). No other variant supports a value
 *     outside its symmetric range.
 *
 * Writers reject out-of-range inputs with `ZSERIO_ERR_VALUE_OUT_OF_RANGE`.
 * Readers return `ZSERIO_ERR_INVALID_ENCODING` for varsize values that
 * exceed 2^31 - 1 (the only type whose encoding admits over-range
 * values). Other types' encodings are bit-budgeted such that any
 * sequence of bytes the reader accepts produces a value within range.
 */

/* ------------------------------------------------------------------------ */
/* Reads                                                                    */
/* ------------------------------------------------------------------------ */

zserio_status_t zserio_read_varuint16(zserio_bitstream_reader_t *bs, uint16_t *out);
zserio_status_t zserio_read_varuint32(zserio_bitstream_reader_t *bs, uint32_t *out);
zserio_status_t zserio_read_varuint64(zserio_bitstream_reader_t *bs, uint64_t *out);
zserio_status_t zserio_read_varuint  (zserio_bitstream_reader_t *bs, uint64_t *out);
zserio_status_t zserio_read_varsize  (zserio_bitstream_reader_t *bs, uint32_t *out);

zserio_status_t zserio_read_varint16(zserio_bitstream_reader_t *bs, int16_t *out);
zserio_status_t zserio_read_varint32(zserio_bitstream_reader_t *bs, int32_t *out);
zserio_status_t zserio_read_varint64(zserio_bitstream_reader_t *bs, int64_t *out);
zserio_status_t zserio_read_varint  (zserio_bitstream_reader_t *bs, int64_t *out);

/* ------------------------------------------------------------------------ */
/* Writes                                                                   */
/* ------------------------------------------------------------------------ */

zserio_status_t zserio_write_varuint16(zserio_bitstream_writer_t *bs, uint16_t v);
zserio_status_t zserio_write_varuint32(zserio_bitstream_writer_t *bs, uint32_t v);
zserio_status_t zserio_write_varuint64(zserio_bitstream_writer_t *bs, uint64_t v);
zserio_status_t zserio_write_varuint  (zserio_bitstream_writer_t *bs, uint64_t v);
zserio_status_t zserio_write_varsize  (zserio_bitstream_writer_t *bs, uint32_t v);

zserio_status_t zserio_write_varint16(zserio_bitstream_writer_t *bs, int16_t v);
zserio_status_t zserio_write_varint32(zserio_bitstream_writer_t *bs, int32_t v);
zserio_status_t zserio_write_varint64(zserio_bitstream_writer_t *bs, int64_t v);
zserio_status_t zserio_write_varint  (zserio_bitstream_writer_t *bs, int64_t v);

/* ------------------------------------------------------------------------ */
/* Bit-size helpers                                                         */
/* ------------------------------------------------------------------------ */

/**
 * Wire bit-size of a value when encoded with the corresponding
 * write_varX function. Used by the packing-decision math in
 * `zserio_delta` (trait-driven mode) — the per-element unpacked cost
 * of a varint depends on the value, not just the type.
 *
 * Inputs MUST be in the encoded range listed in the file-level doc;
 * out-of-range inputs return the maximum encoded size (a safe
 * over-estimate that won't change the eventual write_varX outcome,
 * which would error with VALUE_OUT_OF_RANGE).
 */
size_t zserio_bitsize_varuint16(uint16_t v);
size_t zserio_bitsize_varuint32(uint32_t v);
size_t zserio_bitsize_varuint64(uint64_t v);
size_t zserio_bitsize_varuint  (uint64_t v);
size_t zserio_bitsize_varsize  (uint32_t v);

size_t zserio_bitsize_varint16(int16_t v);
size_t zserio_bitsize_varint32(int32_t v);
size_t zserio_bitsize_varint64(int64_t v);
size_t zserio_bitsize_varint  (int64_t v);

/* ------------------------------------------------------------------------ */
/* Delta-context traits — one static const per varint flavour.              */
/* Defined in varint.c. Plug into                                            */
/* `zserio_delta_init_traits(&ctx, &zserio_delta_traits_varuintXX)` for      */
/* packed arrays whose element shape is the matching varint family.         */
/* ------------------------------------------------------------------------ */

extern const zserio_delta_traits_t zserio_delta_traits_varuint16;
extern const zserio_delta_traits_t zserio_delta_traits_varuint32;
extern const zserio_delta_traits_t zserio_delta_traits_varuint64;
extern const zserio_delta_traits_t zserio_delta_traits_varuint;
extern const zserio_delta_traits_t zserio_delta_traits_varsize;
extern const zserio_delta_traits_t zserio_delta_traits_varint16;
extern const zserio_delta_traits_t zserio_delta_traits_varint32;
extern const zserio_delta_traits_t zserio_delta_traits_varint64;
extern const zserio_delta_traits_t zserio_delta_traits_varint;

#endif /* ZSERIO_VARINT_H_ */
