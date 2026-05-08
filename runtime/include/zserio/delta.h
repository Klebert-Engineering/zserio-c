/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Fabian Klebert
 */

#ifndef ZSERIO_DELTA_H_
#define ZSERIO_DELTA_H_

#include <stdbool.h>
#include <stdint.h>

#include "zserio/bitstream.h"
#include "zserio/status.h"

/**
 * @file
 * Delta context for `packed` arrays of fixed-width integers.
 *
 * The wire format and algorithm are documented in
 * `02-wire-format.md` §5.5. In summary: a packed array is a 1-bit
 * `isPacked` flag followed by, when packed, a 6-bit `maxBitNumber`
 * and a sequence of (first-element || delta-from-previous) values
 * where each delta is a signed integer of `maxBitNumber + 1` bits.
 * When packed encoding would not save space, `isPacked` is `0` and
 * elements are written unpacked.
 *
 * The context is one-shot stateful: the caller runs an init pass
 * (`zserio_delta_init` once + `zserio_delta_observe` per element) to
 * compute `maxBitNumber` and the packing decision, then a use pass
 * (`zserio_delta_write` or `zserio_delta_read` per element, in the
 * same order). One context cannot be reused for both write and read
 * over the same data; instantiate a fresh one.
 *
 * v1 covers fixed-width integer types: callers pass element bit
 * patterns as `uint64_t` and tell the context the bit width and
 * signedness at init time. Packed varint-encoded element types are
 * not supported by this module yet — the integer-type encoding is
 * the only path that benefits the most from delta packing in
 * practice.
 *
 * Element values: callers always pass the bit pattern as `uint64_t`.
 * Signed interpretation is controlled by `is_signed` at init time.
 * For an `int8` value of -5, the caller passes
 * `(uint64_t)(int64_t)-5` (= `0xFFFFFFFFFFFFFFFBULL`).
 */

typedef struct zserio_delta_context {
    /* All fields are an implementation detail. Do not access directly. */
    uint64_t previous;
    uint64_t total_unpacked_bits;
    uint32_t num_elements;
    uint8_t  max_bit_number;
    uint8_t  flags;
    uint8_t  num_bits;       /* element bit width [1, 64] */
    bool     is_signed;
} zserio_delta_context_t;

/**
 * Initialise a context for an array of `num_bits`-wide elements with
 * the given signedness. `num_bits` MUST be in [1, 64].
 *
 * The context starts empty: 0 elements observed, packed flag clear.
 */
zserio_status_t zserio_delta_init(zserio_delta_context_t *ctx,
                                  uint8_t num_bits,
                                  bool is_signed);

/**
 * Observe one element during the init pass. Call once per array
 * element, in encoding order, before any `_write` / `_read` call.
 * `element` is the integer value as a `uint64_t` bit pattern.
 *
 * Returns `ZSERIO_ERR_INVALID_ARGUMENT` for NULL `ctx`.
 */
zserio_status_t zserio_delta_observe(zserio_delta_context_t *ctx,
                                     uint64_t element);

/**
 * Write one element during the use pass. The first call also writes
 * the packing descriptor and the first element's full encoding;
 * subsequent calls write either the signed delta (when packed) or
 * the full element (when unpacked). Call once per array element,
 * in the same order as the init pass.
 *
 * Returns `ZSERIO_ERR_INVALID_ARGUMENT` for NULL `ctx` or NULL `bs`.
 * Underlying buffer / value-out-of-range errors propagate from the
 * `bitstream` calls.
 */
zserio_status_t zserio_delta_write(zserio_delta_context_t *ctx,
                                   zserio_bitstream_writer_t *bs,
                                   uint64_t element);

/**
 * Read one element during the use pass. The first call also reads
 * the packing descriptor and the first element. `*out` receives the
 * element bit pattern as a `uint64_t`; for signed types the caller
 * casts back through `int64_t`.
 */
zserio_status_t zserio_delta_read(zserio_delta_context_t *ctx,
                                  zserio_bitstream_reader_t *bs,
                                  uint64_t *out);

#endif /* ZSERIO_DELTA_H_ */
