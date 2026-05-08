/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Fabian Klebert
 */

#ifndef ZSERIO_PRIMITIVES_H_
#define ZSERIO_PRIMITIVES_H_

#include <stdbool.h>
#include <stdint.h>

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
/* ------------------------------------------------------------------------ */

zserio_status_t zserio_read_uint8 (zserio_bitstream_reader_t *bs, uint8_t  *out);
zserio_status_t zserio_read_uint16(zserio_bitstream_reader_t *bs, uint16_t *out);
zserio_status_t zserio_read_uint32(zserio_bitstream_reader_t *bs, uint32_t *out);
zserio_status_t zserio_read_uint64(zserio_bitstream_reader_t *bs, uint64_t *out);

zserio_status_t zserio_read_int8 (zserio_bitstream_reader_t *bs, int8_t  *out);
zserio_status_t zserio_read_int16(zserio_bitstream_reader_t *bs, int16_t *out);
zserio_status_t zserio_read_int32(zserio_bitstream_reader_t *bs, int32_t *out);
zserio_status_t zserio_read_int64(zserio_bitstream_reader_t *bs, int64_t *out);

/**
 * Read a single bit as a boolean. `1` becomes `true`, `0` becomes `false`.
 */
zserio_status_t zserio_read_bool(zserio_bitstream_reader_t *bs, bool *out);

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
