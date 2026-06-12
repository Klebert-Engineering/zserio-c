/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Klebert Engineering
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
 * Element values: callers always pass the bit pattern as `uint64_t`.
 * Signed interpretation is controlled by `is_signed` at init time.
 * For an `int8` value of -5, the caller passes
 * `(uint64_t)(int64_t)-5` (= `0xFFFFFFFFFFFFFFFBULL`).
 *
 * Two element-shape modes:
 *  - Fixed-width: caller passes `num_bits` at init; reads/writes use
 *    the matching fixed-width bitstream calls.
 *  - Trait-driven: caller passes a `zserio_delta_traits_t` of
 *    callbacks that drive the unpacked element read / write /
 *    bit-size. Used for varint-encoded elements (varuintN, varintN,
 *    varuint, varint, varsize) where the per-element wire size is
 *    value-dependent. The packed delta encoding (descriptor + signed
 *    bit-bag) is identical to the fixed-width path.
 */

/**
 * Element-shape traits for the trait-driven mode of
 * {@link zserio_delta_context_t}.
 *
 * `read_unpacked` / `write_unpacked` are the per-element codec used
 * when the context's packing decision lands on "unpacked" (and for
 * the very first element on the packed path). `bit_size_unpacked`
 * returns the wire size of one element under the unpacked codec —
 * this drives the packed-vs-unpacked cost comparison in
 * {@code delta_finalize}. Returning the actual per-value bit size
 * (e.g. {@code 8 * varuint32_byte_count(value)}) keeps the decision
 * consistent with upstream zserio's encoder, which is required for
 * byte-exact write-side round-trip.
 */
typedef struct zserio_delta_traits {
    zserio_status_t (*read_unpacked)(zserio_bitstream_reader_t *bs, uint64_t *out);
    zserio_status_t (*write_unpacked)(zserio_bitstream_writer_t *bs, uint64_t value);
    size_t          (*bit_size_unpacked)(uint64_t value);
    bool             is_signed;
} zserio_delta_traits_t;

typedef struct zserio_delta_context {
    /* All fields are an implementation detail. Do not access directly. */
    uint64_t previous;
    uint64_t first_value;     /* first observed element — used to size the leading
                                 unpacked element in trait-driven mode */
    uint64_t total_unpacked_bits;
    uint32_t num_elements;
    uint8_t  max_bit_number;
    uint8_t  flags;
    uint8_t  num_bits;       /* element bit width [1, 64] for fixed-width mode; 0 for traits mode */
    bool     is_signed;
    /* NULL → fixed-width mode (use num_bits / is_signed); non-NULL →
     * traits-driven mode (use callbacks for the unpacked codec). */
    const zserio_delta_traits_t *traits;
} zserio_delta_context_t;

/**
 * Initialize a context for an array of `num_bits`-wide elements with
 * the given signedness. `num_bits` MUST be in [1, 64].
 *
 * The context starts empty: 0 elements observed, packed flag clear.
 */
zserio_status_t zserio_delta_init(zserio_delta_context_t *ctx,
                                  uint8_t num_bits,
                                  bool is_signed);

/**
 * Initialize a context in trait-driven mode. {@code traits} MUST
 * point at a non-NULL struct with all callbacks populated; the
 * struct must remain valid for the lifetime of the context (typically
 * a static const instance per varint flavour).
 */
zserio_status_t zserio_delta_init_traits(zserio_delta_context_t *ctx,
                                         const zserio_delta_traits_t *traits);

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
 * Lifecycle flags. Exposed here only so the `zserio_delta_read` /
 * `zserio_delta_write` inline fast paths below can test them — the
 * bit values are an implementation detail and not part of the public
 * ABI contract.
 */
#define ZSERIO_DELTA_FLAG_INIT_STARTED  ((uint8_t)0x01U)
#define ZSERIO_DELTA_FLAG_IS_PACKED     ((uint8_t)0x02U)
#define ZSERIO_DELTA_FLAG_USE_STARTED   ((uint8_t)0x04U)
#define ZSERIO_DELTA_FLAG_FINALIZED     ((uint8_t)0x08U)

/**
 * Out-of-line slow path that handles every state of the read state
 * machine. The {@link zserio_delta_read} inline wrapper short-
 * circuits the packed steady-state hot loop and only calls this for
 * the first element and for unpacked-fallback reads, where the
 * function-pointer trait dispatch is unavoidable. Not for direct
 * caller use — go through {@link zserio_delta_read}.
 */
zserio_status_t zserio_delta_read_slow(zserio_delta_context_t *ctx,
                                       zserio_bitstream_reader_t *bs,
                                       uint64_t *out);

/**
 * Same shape as {@link zserio_delta_read_slow} for the write path:
 * handles first-element descriptor write + first element + unpacked
 * fallback. Steady-state packed delta writes go inline via
 * {@link zserio_delta_write}.
 */
zserio_status_t zserio_delta_write_slow(zserio_delta_context_t *ctx,
                                        zserio_bitstream_writer_t *bs,
                                        uint64_t element);

/**
 * Read one element during the use pass. The first call also reads
 * the packing descriptor and the first element. `*out` receives the
 * element bit pattern as a `uint64_t`; for signed types the caller
 * casts back through `int64_t`.
 *
 * The packed steady-state branch is open-coded here so the per-
 * element decode in a long packed array is a tight inline sequence:
 * argument check, flag test, signed bit-cache read, sign-extend,
 * accumulate. The cold paths (very first element of any array, plus
 * the unpacked fallback that has to invoke the trait callback) defer
 * to the out-of-line {@link zserio_delta_read_slow} so the call site
 * doesn't pay code-size for paths it rarely takes.
 */
static inline zserio_status_t zserio_delta_read(zserio_delta_context_t *ctx,
                                                zserio_bitstream_reader_t *bs,
                                                uint64_t *out)
{
    if ((ctx == NULL) || (bs == NULL) || (out == NULL)) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }
    /* Fast path: USE_STARTED is set (not the very first call) AND
     * IS_PACKED is set (packed encoding was chosen). Anything else —
     * first call, unpacked steady state — falls to the slow path. */
    const uint8_t fast_mask = ZSERIO_DELTA_FLAG_USE_STARTED | ZSERIO_DELTA_FLAG_IS_PACKED;
    if ((ctx->flags & fast_mask) == fast_mask) {
        if (ctx->max_bit_number > 0U) {
            int64_t delta = 0;
            ZSERIO_TRY(zserio_bitstream_read_signed_checked(
                bs, (uint8_t)(ctx->max_bit_number + 1U), &delta));
            ctx->previous = ctx->previous + (uint64_t)delta;
        }
        /* max_bit_number == 0 → all observed deltas were zero, no
         * bits per element on the wire; value stays at `previous`. */
        *out = ctx->previous;
        return ZSERIO_OK;
    }
    return zserio_delta_read_slow(ctx, bs, out);
}

/**
 * Write one element during the use pass. The fast path (packed
 * steady-state, finalized) is inlined to mirror the read shape; cold
 * paths defer to {@link zserio_delta_write_slow}. The first write()
 * call has to write the packing descriptor AND finalize the
 * decision, so it always takes the slow path; subsequent calls on
 * the same context can hit the inlined branch.
 */
static inline zserio_status_t zserio_delta_write(zserio_delta_context_t *ctx,
                                                 zserio_bitstream_writer_t *bs,
                                                 uint64_t element)
{
    if ((ctx == NULL) || (bs == NULL)) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }
    const uint8_t fast_mask = ZSERIO_DELTA_FLAG_USE_STARTED
                            | ZSERIO_DELTA_FLAG_IS_PACKED
                            | ZSERIO_DELTA_FLAG_FINALIZED;
    if ((ctx->flags & fast_mask) == fast_mask) {
        if (ctx->max_bit_number > 0U) {
            const uint64_t delta_bits = element - ctx->previous;
            ZSERIO_TRY(zserio_bitstream_write_signed(bs,
                (uint8_t)(ctx->max_bit_number + 1U), (int64_t)delta_bits));
        }
        ctx->previous = element;
        return ZSERIO_OK;
    }
    return zserio_delta_write_slow(ctx, bs, element);
}

#endif /* ZSERIO_DELTA_H_ */
