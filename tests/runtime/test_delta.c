/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Fabian Klebert
 */

#include <stdint.h>
#include <string.h>

#include "unity.h"
#include "zserio/bitstream.h"
#include "zserio/delta.h"
#include "zserio/status.h"

void setUp(void) {}
void tearDown(void) {}

/* Helper: encode a uint8 array via delta and return the position.
 *
 * Encoding lifecycle: init → observe (every element once) → write
 * (every element). `zserio_delta_init` is idempotent on an already-
 * configured context, so re-init between observe and write is a
 * harmless no-op. Repeated observes, however, accumulate
 * num_elements and pick up bogus inter-pass deltas — the encoded
 * size drifts and the read side desyncs. */
static size_t encode_delta_uint8(uint8_t *buf, size_t buf_bits,
                                 const uint8_t *elements, size_t n)
{
    zserio_bitstream_writer_t w;
    (void)zserio_bitstream_writer_init(&w, buf, buf_bits);

    zserio_delta_context_t ctx;
    (void)memset(&ctx, 0, sizeof(ctx));
    (void)zserio_delta_init(&ctx, 8U, false);
    for (size_t i = 0U; i < n; i++) {
        (void)zserio_delta_observe(&ctx, (uint64_t)elements[i]);
    }
    for (size_t i = 0U; i < n; i++) {
        (void)zserio_delta_write(&ctx, &w, (uint64_t)elements[i]);
    }
    return zserio_bitstream_writer_position(&w);
}

/* Helper: decode a uint8 array, return whether values match. */
static void decode_delta_uint8(const uint8_t *buf, size_t buf_bits,
                               uint8_t *out, size_t n)
{
    zserio_bitstream_reader_t r;
    (void)zserio_bitstream_reader_init(&r, buf, buf_bits);

    zserio_delta_context_t ctx;
    (void)memset(&ctx, 0, sizeof(ctx));
    (void)zserio_delta_init(&ctx, 8U, false);
    for (size_t i = 0U; i < n; i++) {
        uint64_t v = 0U;
        (void)zserio_delta_read(&ctx, &r, &v);
        out[i] = (uint8_t)v;
    }
}

/* ------------------------------------------------------------------------ */
/* Encoding-Guide examples                                                  */
/* ------------------------------------------------------------------------ */

static void test_delta_packed_5_elements_encoding_guide(void)
{
    /* Verifies: REQ-FN-001
     *
     * Encoding Guide example: [11, 12, 15, 22, 23] packed as
     *   isPacked=1, maxBitNumber=3, list[0]=11, then four 4-bit deltas.
     *   Expected bit stream `10000110 00010110 00100110 1110001`
     *   = bytes 86 16 26 e2 ... (rounded to 31 bits, last 1 bit padded).
     */
    const uint8_t in[5] = { 11U, 12U, 15U, 22U, 23U };
    uint8_t buf[8] = { 0 };
    const size_t pos = encode_delta_uint8(buf, 64U, in, 5U);
    TEST_ASSERT_EQUAL_size_t(31U, pos);
    TEST_ASSERT_EQUAL_HEX8(0x86, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(0x16, buf[1]);
    TEST_ASSERT_EQUAL_HEX8(0x26, buf[2]);
    TEST_ASSERT_EQUAL_HEX8(0xE2, buf[3]);

    uint8_t out[5] = { 0 };
    decode_delta_uint8(buf, pos, out, 5U);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(in, out, 5U);
}

static void test_delta_unpacked_5_elements_encoding_guide(void)
{
    /* Verifies: REQ-FN-001
     *
     * Encoding Guide example: [0, 250, 251, 252, 253] cannot be packed
     * efficiently (delta 250 needs 8 magnitude bits → 9-bit signed delta;
     * 4×9 + 1 + 6 + 8 = 51 bits, vs 1 + 5×8 = 41 bits unpacked).
     * Encoded as isPacked=0, then 5 raw uint8 values.
     */
    const uint8_t in[5] = { 0U, 250U, 251U, 252U, 253U };
    uint8_t buf[8] = { 0 };
    const size_t pos = encode_delta_uint8(buf, 64U, in, 5U);
    TEST_ASSERT_EQUAL_size_t(41U, pos);
    /* First bit is isPacked=0, then bytes are the raw values shifted right by 1. */
    /* Easier check: round-trip. */
    uint8_t out[5] = { 0 };
    decode_delta_uint8(buf, pos, out, 5U);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(in, out, 5U);
}

/* ------------------------------------------------------------------------ */
/* Round-trip — varying widths and signs                                    */
/* ------------------------------------------------------------------------ */

static void test_delta_round_trip_uint16(void)
{
    const uint16_t in[6] = { 100U, 110U, 120U, 130U, 140U, 150U };
    uint8_t buf[16] = { 0 };
    zserio_bitstream_writer_t w;
    (void)zserio_bitstream_writer_init(&w, buf, 128U);

    zserio_delta_context_t ctx;
    (void)memset(&ctx, 0, sizeof(ctx));
    (void)zserio_delta_init(&ctx, 16U, false);
    for (size_t i = 0U; i < 6U; i++) {
        (void)zserio_delta_observe(&ctx, (uint64_t)in[i]);
    }
    for (size_t i = 0U; i < 6U; i++) {
        TEST_ASSERT_EQUAL(ZSERIO_OK,
            zserio_delta_write(&ctx, &w, (uint64_t)in[i]));
    }
    const size_t total_bits = zserio_bitstream_writer_position(&w);

    zserio_bitstream_reader_t r;
    (void)zserio_bitstream_reader_init(&r, buf, total_bits);
    zserio_delta_context_t rctx;
    (void)memset(&rctx, 0, sizeof(rctx));
    (void)zserio_delta_init(&rctx, 16U, false);
    uint16_t out[6] = { 0 };
    for (size_t i = 0U; i < 6U; i++) {
        uint64_t v = 0U;
        TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_delta_read(&rctx, &r, &v));
        out[i] = (uint16_t)v;
    }
    for (size_t i = 0U; i < 6U; i++) {
        TEST_ASSERT_EQUAL_HEX16(in[i], out[i]);
    }
}

static void test_delta_round_trip_signed_with_negative_deltas(void)
{
    /* Verifies: REQ-FN-001 — signed elements with mixed-sign deltas. */
    const int8_t in[5] = { 10, -5, 20, -30, 0 };
    uint8_t buf[16] = { 0 };
    zserio_bitstream_writer_t w;
    (void)zserio_bitstream_writer_init(&w, buf, 128U);

    zserio_delta_context_t ctx;
    (void)memset(&ctx, 0, sizeof(ctx));
    (void)zserio_delta_init(&ctx, 8U, true);
    for (size_t i = 0U; i < 5U; i++) {
        (void)zserio_delta_observe(&ctx, (uint64_t)(int64_t)in[i]);
    }
    for (size_t i = 0U; i < 5U; i++) {
        TEST_ASSERT_EQUAL(ZSERIO_OK,
            zserio_delta_write(&ctx, &w, (uint64_t)(int64_t)in[i]));
    }

    zserio_bitstream_reader_t r;
    (void)zserio_bitstream_reader_init(&r, buf,
                                       zserio_bitstream_writer_position(&w));
    zserio_delta_context_t rctx;
    (void)memset(&rctx, 0, sizeof(rctx));
    (void)zserio_delta_init(&rctx, 8U, true);
    int8_t out[5] = { 0 };
    for (size_t i = 0U; i < 5U; i++) {
        uint64_t v = 0U;
        (void)zserio_delta_read(&rctx, &r, &v);
        out[i] = (int8_t)(int64_t)v;
    }
    for (size_t i = 0U; i < 5U; i++) {
        TEST_ASSERT_EQUAL_INT8(in[i], out[i]);
    }
}

static void test_delta_round_trip_uint32(void)
{
    const uint32_t in[4] = { 1000000U, 1000001U, 1000010U, 999999U };
    uint8_t buf[32] = { 0 };
    zserio_bitstream_writer_t w;
    (void)zserio_bitstream_writer_init(&w, buf, 256U);

    zserio_delta_context_t ctx;
    (void)memset(&ctx, 0, sizeof(ctx));
    (void)zserio_delta_init(&ctx, 32U, false);
    for (size_t i = 0U; i < 4U; i++) {
        (void)zserio_delta_observe(&ctx, (uint64_t)in[i]);
    }
    for (size_t i = 0U; i < 4U; i++) {
        (void)zserio_delta_write(&ctx, &w, (uint64_t)in[i]);
    }

    zserio_bitstream_reader_t r;
    (void)zserio_bitstream_reader_init(&r, buf,
                                       zserio_bitstream_writer_position(&w));
    zserio_delta_context_t rctx;
    (void)memset(&rctx, 0, sizeof(rctx));
    (void)zserio_delta_init(&rctx, 32U, false);
    uint32_t out[4] = { 0 };
    for (size_t i = 0U; i < 4U; i++) {
        uint64_t v = 0U;
        (void)zserio_delta_read(&rctx, &r, &v);
        out[i] = (uint32_t)v;
    }
    for (size_t i = 0U; i < 4U; i++) {
        TEST_ASSERT_EQUAL_HEX32(in[i], out[i]);
    }
}

/* ------------------------------------------------------------------------ */
/* Edge cases                                                               */
/* ------------------------------------------------------------------------ */

static void test_delta_two_equal_elements_packed(void)
{
    /* All-equal elements give max_bit_number = 0. The runtime elides
     * the per-element delta entirely in that case (matches upstream
     * cpp's wire format — see the `max_bit_number == 0` branch in
     * zserio_delta_write). Bits: 1 (isPacked) + 6 (maxBitNumber=0)
     * + 8 (first element) + 0 (deltas elided) = 15 bits. */
    const uint8_t in[3] = { 42U, 42U, 42U };
    uint8_t buf[8] = { 0 };
    const size_t pos = encode_delta_uint8(buf, 64U, in, 3U);
    TEST_ASSERT_EQUAL_size_t(15U, pos);

    uint8_t out[3] = { 0 };
    decode_delta_uint8(buf, pos, out, 3U);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(in, out, 3U);
}

static void test_delta_single_element_unpacked(void)
{
    /* Single element: packed encoding's 6-bit maxBitNumber is pure
     * overhead, so finalise picks unpacked. Bits: 1 + 8 = 9. */
    const uint8_t in[1] = { 0xABU };
    uint8_t buf[2] = { 0 };
    const size_t pos = encode_delta_uint8(buf, 16U, in, 1U);
    TEST_ASSERT_EQUAL_size_t(9U, pos);

    uint8_t out[1] = { 0 };
    decode_delta_uint8(buf, pos, out, 1U);
    TEST_ASSERT_EQUAL_HEX8(in[0], out[0]);
}

/* ------------------------------------------------------------------------ */
/* Defensive checks                                                         */
/* ------------------------------------------------------------------------ */

static void test_delta_init_invalid_args(void)
{
    /* Verifies: REQ-FS-006 */
    zserio_delta_context_t ctx;
    (void)memset(&ctx, 0, sizeof(ctx));
    TEST_ASSERT_EQUAL(ZSERIO_ERR_INVALID_ARGUMENT,
                      zserio_delta_init(NULL, 8U, false));
    /* num_bits == 0 is the deferred-init signal used by dynamic-width
     * generated code: the runtime returns OK and the caller re-invokes
     * delta_init with the resolved width at observe / read / write
     * time. */
    TEST_ASSERT_EQUAL(ZSERIO_OK,
                      zserio_delta_init(&ctx, 0U, false));
    TEST_ASSERT_EQUAL(ZSERIO_ERR_INVALID_NUM_BITS,
                      zserio_delta_init(&ctx, 65U, false));
}

static void test_delta_observe_after_finalise(void)
{
    /* After write begins, finalise has run; observe must reject. */
    zserio_delta_context_t ctx;
    (void)memset(&ctx, 0, sizeof(ctx));
    (void)zserio_delta_init(&ctx, 8U, false);
    (void)zserio_delta_observe(&ctx, 1U);
    (void)zserio_delta_observe(&ctx, 2U);

    uint8_t buf[2] = { 0 };
    zserio_bitstream_writer_t w;
    (void)zserio_bitstream_writer_init(&w, buf, 16U);
    (void)zserio_delta_write(&ctx, &w, 1U);
    /* finalise has run inside delta_write. observe must now reject. */
    TEST_ASSERT_EQUAL(ZSERIO_ERR_INVALID_ARGUMENT,
                      zserio_delta_observe(&ctx, 3U));
}

static void test_delta_observe_null_ctx(void)
{
    /* Verifies: REQ-FS-006 */
    TEST_ASSERT_EQUAL(ZSERIO_ERR_INVALID_ARGUMENT, zserio_delta_observe(NULL, 0U));
}

static void test_delta_write_without_observe_takes_unpacked_path(void)
{
    /* Skipping the init-pass observes lands the finalise() call on the
     * "IS_PACKED was never set" early-return branch. Subsequent write
     * proceeds through the unpacked path. */
    zserio_delta_context_t ctx;
    (void)memset(&ctx, 0, sizeof(ctx));
    (void)zserio_delta_init(&ctx, 8U, false);
    uint8_t buf[2] = { 0 };
    zserio_bitstream_writer_t w;
    (void)zserio_bitstream_writer_init(&w, buf, 16U);
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_delta_write(&ctx, &w, 0xABU));
    /* Decode it back: should be unpacked (1 bit = 0) + 8 bits of value. */
    zserio_bitstream_reader_t r;
    (void)zserio_bitstream_reader_init(&r, buf, zserio_bitstream_writer_position(&w));
    zserio_delta_context_t rctx;
    (void)memset(&rctx, 0, sizeof(rctx));
    (void)zserio_delta_init(&rctx, 8U, false);
    uint64_t v = 0U;
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_delta_read(&rctx, &r, &v));
    TEST_ASSERT_EQUAL_HEX64(0xABU, v);
}

static void test_delta_max_bit_number_exceeds_limit(void)
{
    /* Verifies: REQ-FN-001 — when the delta width exceeds the 62-bit
     * MAX_BIT_NUMBER_LIMIT, the encoder falls back to unpacked. */
    zserio_delta_context_t ctx;
    (void)memset(&ctx, 0, sizeof(ctx));
    (void)zserio_delta_init(&ctx, 64U, false);
    /* observe 0 and UINT64_MAX → abs_delta = UINT64_MAX, bit_length
     * = 64 > 62. The observe path clears the tentative IS_PACKED
     * flag, so finalise takes the unpacked branch. Single observe
     * pass (see encode_delta_uint8 helper for why double-observe is
     * a bug). */
    (void)zserio_delta_observe(&ctx, 0U);
    (void)zserio_delta_observe(&ctx, UINT64_MAX);

    uint8_t buf[24] = { 0 };
    zserio_bitstream_writer_t w;
    (void)zserio_bitstream_writer_init(&w, buf, 192U);
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_delta_write(&ctx, &w, 0U));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_delta_write(&ctx, &w, UINT64_MAX));

    zserio_bitstream_reader_t r;
    (void)zserio_bitstream_reader_init(&r, buf, zserio_bitstream_writer_position(&w));
    zserio_delta_context_t rctx;
    (void)memset(&rctx, 0, sizeof(rctx));
    (void)zserio_delta_init(&rctx, 64U, false);
    uint64_t a = 0U, b = 0U;
    (void)zserio_delta_read(&rctx, &r, &a);
    (void)zserio_delta_read(&rctx, &r, &b);
    TEST_ASSERT_EQUAL_HEX64(UINT64_C(0), a);
    TEST_ASSERT_EQUAL_HEX64(UINT64_MAX, b);
}

static void test_delta_write_read_null_args(void)
{
    /* Verifies: REQ-FS-006 */
    zserio_delta_context_t ctx;
    (void)memset(&ctx, 0, sizeof(ctx));
    (void)zserio_delta_init(&ctx, 8U, false);
    uint8_t buf[1] = { 0 };
    zserio_bitstream_writer_t w;
    (void)zserio_bitstream_writer_init(&w, buf, 8U);
    zserio_bitstream_reader_t r;
    (void)zserio_bitstream_reader_init(&r, buf, 8U);
    uint64_t v;

    TEST_ASSERT_EQUAL(ZSERIO_ERR_INVALID_ARGUMENT,
                      zserio_delta_write(NULL, &w, 0U));
    TEST_ASSERT_EQUAL(ZSERIO_ERR_INVALID_ARGUMENT,
                      zserio_delta_write(&ctx, NULL, 0U));
    TEST_ASSERT_EQUAL(ZSERIO_ERR_INVALID_ARGUMENT,
                      zserio_delta_read(NULL, &r, &v));
    TEST_ASSERT_EQUAL(ZSERIO_ERR_INVALID_ARGUMENT,
                      zserio_delta_read(&ctx, NULL, &v));
    TEST_ASSERT_EQUAL(ZSERIO_ERR_INVALID_ARGUMENT,
                      zserio_delta_read(&ctx, &r, NULL));
}

/* ------------------------------------------------------------------------ */
/* Test runner                                                              */
/* ------------------------------------------------------------------------ */

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_delta_packed_5_elements_encoding_guide);
    RUN_TEST(test_delta_unpacked_5_elements_encoding_guide);
    RUN_TEST(test_delta_round_trip_uint16);
    RUN_TEST(test_delta_round_trip_signed_with_negative_deltas);
    RUN_TEST(test_delta_round_trip_uint32);
    RUN_TEST(test_delta_two_equal_elements_packed);
    RUN_TEST(test_delta_single_element_unpacked);
    RUN_TEST(test_delta_init_invalid_args);
    RUN_TEST(test_delta_observe_after_finalise);
    RUN_TEST(test_delta_observe_null_ctx);
    RUN_TEST(test_delta_write_without_observe_takes_unpacked_path);
    RUN_TEST(test_delta_max_bit_number_exceeds_limit);
    RUN_TEST(test_delta_write_read_null_args);

    return UNITY_END();
}
