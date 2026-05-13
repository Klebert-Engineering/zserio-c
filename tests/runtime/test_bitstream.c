/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Fabian Klebert
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "unity.h"
#include "zserio/bitstream.h"
#include "zserio/status.h"

void setUp(void) {}
void tearDown(void) {}

/* ------------------------------------------------------------------------ */
/* Reader: initialization                                                   */
/* ------------------------------------------------------------------------ */

static void test_reader_init_valid(void)
{
    /* Verifies: REQ-FS-006 */
    const uint8_t buf[16] = { 0 };
    zserio_bitstream_reader_t r;
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_bitstream_reader_init(&r, buf, 128U));
    TEST_ASSERT_EQUAL_size_t(0U, zserio_bitstream_reader_position(&r));
    TEST_ASSERT_EQUAL_size_t(128U, zserio_bitstream_reader_size(&r));
}

static void test_reader_init_null_bs(void)
{
    /* Verifies: REQ-FS-006 */
    const uint8_t buf[1] = { 0 };
    TEST_ASSERT_EQUAL(ZSERIO_ERR_INVALID_ARGUMENT,
                      zserio_bitstream_reader_init(NULL, buf, 8U));
}

static void test_reader_init_null_buffer_with_size(void)
{
    /* Verifies: REQ-FS-006 */
    zserio_bitstream_reader_t r;
    TEST_ASSERT_EQUAL(ZSERIO_ERR_INVALID_BUFFER,
                      zserio_bitstream_reader_init(&r, NULL, 8U));
}

static void test_reader_init_empty_buffer_ok(void)
{
    /* Verifies: REQ-FS-006 */
    zserio_bitstream_reader_t r;
    TEST_ASSERT_EQUAL(ZSERIO_OK,
                      zserio_bitstream_reader_init(&r, NULL, 0U));
    TEST_ASSERT_EQUAL_size_t(0U, zserio_bitstream_reader_size(&r));
}

static void test_reader_init_oversize(void)
{
    /* Verifies: REQ-FS-006 */
    const uint8_t buf[1] = { 0 };
    zserio_bitstream_reader_t r;
    TEST_ASSERT_EQUAL(ZSERIO_ERR_INVALID_BUFFER,
                      zserio_bitstream_reader_init(&r, buf, SIZE_MAX));
}

/* ------------------------------------------------------------------------ */
/* Reader: read_bits                                                        */
/* ------------------------------------------------------------------------ */

static void test_read_bits_basic(void)
{
    /* Single byte 0xAB read in 8 bits. */
    const uint8_t buf[1] = { 0xAB };
    zserio_bitstream_reader_t r;
    (void)zserio_bitstream_reader_init(&r, buf, 8U);

    uint64_t v = 0U;
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_bitstream_read_bits(&r, 8U, &v));
    TEST_ASSERT_EQUAL_UINT64(0xABU, v);
    TEST_ASSERT_EQUAL_size_t(8U, zserio_bitstream_reader_position(&r));
}

static void test_read_bits_one_bit(void)
{
    /* 0xA0 = 1010_0000. Read four 1-bit values: 1, 0, 1, 0. */
    const uint8_t buf[1] = { 0xA0 };
    zserio_bitstream_reader_t r;
    (void)zserio_bitstream_reader_init(&r, buf, 8U);

    uint64_t v = 0U;
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_bitstream_read_bits(&r, 1U, &v));
    TEST_ASSERT_EQUAL_UINT64(1U, v);
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_bitstream_read_bits(&r, 1U, &v));
    TEST_ASSERT_EQUAL_UINT64(0U, v);
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_bitstream_read_bits(&r, 1U, &v));
    TEST_ASSERT_EQUAL_UINT64(1U, v);
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_bitstream_read_bits(&r, 1U, &v));
    TEST_ASSERT_EQUAL_UINT64(0U, v);
}

static void test_read_bits_64_bit(void)
{
    /* Read a full uint64 from 8 bytes. */
    const uint8_t buf[8] = { 0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF };
    zserio_bitstream_reader_t r;
    (void)zserio_bitstream_reader_init(&r, buf, 64U);

    uint64_t v = 0U;
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_bitstream_read_bits(&r, 64U, &v));
    TEST_ASSERT_EQUAL_UINT64(0x0123456789ABCDEFULL, v);
}

static void test_read_bits_unaligned_simple_structure(void)
{
    /* Verifies: REQ-FN-001 (wire-compat with upstream SimpleStructure)
     *
     * Schema:
     *   struct SimpleStructure {
     *       bit:3 a;
     *       uint8 b;
     *       bit:7 c;
     *   };
     * With a=7, b=0xFF, c=0x7F: bits 111 11111111 1111111 packed MSB-first
     * gives 18 bits = 0x3FFFF, padded to 24 bits = 0xFFFFC0.
     */
    const uint8_t buf[3] = { 0xFF, 0xFF, 0xC0 };
    zserio_bitstream_reader_t r;
    (void)zserio_bitstream_reader_init(&r, buf, 18U);

    uint64_t a = 0U, b = 0U, c = 0U;
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_bitstream_read_bits(&r, 3U, &a));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_bitstream_read_bits(&r, 8U, &b));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_bitstream_read_bits(&r, 7U, &c));
    TEST_ASSERT_EQUAL_UINT64(0x07U, a);
    TEST_ASSERT_EQUAL_UINT64(0xFFU, b);
    TEST_ASSERT_EQUAL_UINT64(0x7FU, c);
    TEST_ASSERT_EQUAL_size_t(18U, zserio_bitstream_reader_position(&r));
}

static void test_read_bits_null_bs(void)
{
    /* Verifies: REQ-FS-006 */
    uint64_t v = 0U;
    TEST_ASSERT_EQUAL(ZSERIO_ERR_INVALID_ARGUMENT,
                      zserio_bitstream_read_bits(NULL, 8U, &v));
}

static void test_read_bits_null_out(void)
{
    /* Verifies: REQ-FS-006 */
    const uint8_t buf[1] = { 0 };
    zserio_bitstream_reader_t r;
    (void)zserio_bitstream_reader_init(&r, buf, 8U);
    TEST_ASSERT_EQUAL(ZSERIO_ERR_INVALID_ARGUMENT,
                      zserio_bitstream_read_bits(&r, 8U, NULL));
}

static void test_read_bits_zero_num_bits(void)
{
    /* Verifies: REQ-FS-006 */
    const uint8_t buf[1] = { 0 };
    zserio_bitstream_reader_t r;
    (void)zserio_bitstream_reader_init(&r, buf, 8U);
    uint64_t v = 0U;
    TEST_ASSERT_EQUAL(ZSERIO_ERR_INVALID_NUM_BITS,
                      zserio_bitstream_read_bits(&r, 0U, &v));
}

static void test_read_bits_too_many_bits(void)
{
    /* Verifies: REQ-FS-006 */
    const uint8_t buf[16] = { 0 };
    zserio_bitstream_reader_t r;
    (void)zserio_bitstream_reader_init(&r, buf, 128U);
    uint64_t v = 0U;
    TEST_ASSERT_EQUAL(ZSERIO_ERR_INVALID_NUM_BITS,
                      zserio_bitstream_read_bits(&r, 65U, &v));
}

static void test_read_bits_eof(void)
{
    /* Verifies: REQ-FS-005 (bounded reads) */
    const uint8_t buf[1] = { 0xFF };
    zserio_bitstream_reader_t r;
    (void)zserio_bitstream_reader_init(&r, buf, 8U);

    uint64_t v = 0U;
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_bitstream_read_bits(&r, 8U, &v));
    TEST_ASSERT_EQUAL(ZSERIO_ERR_END_OF_STREAM,
                      zserio_bitstream_read_bits(&r, 1U, &v));
    /* Position is unchanged on failure. */
    TEST_ASSERT_EQUAL_size_t(8U, zserio_bitstream_reader_position(&r));
}

/* ------------------------------------------------------------------------ */
/* Reader: read_signed                                                      */
/* ------------------------------------------------------------------------ */

static void test_read_signed_positive(void)
{
    /* 4-bit value 0b0101 = +5 */
    const uint8_t buf[1] = { 0x50 };
    zserio_bitstream_reader_t r;
    (void)zserio_bitstream_reader_init(&r, buf, 4U);

    int64_t v = 0;
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_bitstream_read_signed(&r, 4U, &v));
    TEST_ASSERT_EQUAL_INT64(5, v);
}

static void test_read_signed_negative(void)
{
    /* 4-bit value 0b1111 = -1 (two's complement, sign-extended) */
    const uint8_t buf[1] = { 0xF0 };
    zserio_bitstream_reader_t r;
    (void)zserio_bitstream_reader_init(&r, buf, 4U);

    int64_t v = 0;
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_bitstream_read_signed(&r, 4U, &v));
    TEST_ASSERT_EQUAL_INT64(-1, v);
}

static void test_read_signed_min_int8(void)
{
    /* 8-bit value 0x80 = -128 (INT8_MIN) */
    const uint8_t buf[1] = { 0x80 };
    zserio_bitstream_reader_t r;
    (void)zserio_bitstream_reader_init(&r, buf, 8U);

    int64_t v = 0;
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_bitstream_read_signed(&r, 8U, &v));
    TEST_ASSERT_EQUAL_INT64(-128, v);
}

static void test_read_signed_64_bit(void)
{
    /* 64-bit INT64_MIN = 0x8000000000000000 */
    const uint8_t buf[8] = { 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    zserio_bitstream_reader_t r;
    (void)zserio_bitstream_reader_init(&r, buf, 64U);

    int64_t v = 0;
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_bitstream_read_signed(&r, 64U, &v));
    TEST_ASSERT_EQUAL_INT64(INT64_MIN, v);
}

static void test_read_signed_invalid_args(void)
{
    /* Verifies: REQ-FS-006 */
    const uint8_t buf[1] = { 0 };
    zserio_bitstream_reader_t r;
    (void)zserio_bitstream_reader_init(&r, buf, 8U);

    int64_t v = 0;
    TEST_ASSERT_EQUAL(ZSERIO_ERR_INVALID_ARGUMENT,
                      zserio_bitstream_read_signed(&r, 8U, NULL));
    TEST_ASSERT_EQUAL(ZSERIO_ERR_INVALID_NUM_BITS,
                      zserio_bitstream_read_signed(&r, 0U, &v));
    TEST_ASSERT_EQUAL(ZSERIO_ERR_INVALID_NUM_BITS,
                      zserio_bitstream_read_signed(&r, 65U, &v));
}

/* ------------------------------------------------------------------------ */
/* Reader: align                                                            */
/* ------------------------------------------------------------------------ */

static void test_align_already_aligned(void)
{
    const uint8_t buf[2] = { 0xAA, 0xBB };
    zserio_bitstream_reader_t r;
    (void)zserio_bitstream_reader_init(&r, buf, 16U);
    /* position is 0, already aligned to 8 */
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_bitstream_reader_align(&r, 8U));
    TEST_ASSERT_EQUAL_size_t(0U, zserio_bitstream_reader_position(&r));
}

static void test_align_byte(void)
{
    /* Read 3 bits, align to next byte (8), should be at position 8. */
    const uint8_t buf[2] = { 0xAA, 0xBB };
    zserio_bitstream_reader_t r;
    (void)zserio_bitstream_reader_init(&r, buf, 16U);

    uint64_t v = 0U;
    (void)zserio_bitstream_read_bits(&r, 3U, &v);
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_bitstream_reader_align(&r, 8U));
    TEST_ASSERT_EQUAL_size_t(8U, zserio_bitstream_reader_position(&r));
}

static void test_align_invalid_alignment(void)
{
    /* Verifies: REQ-FS-006.
     *
     * Zserio's `align(N):` directive accepts any positive integer
     * (the upstream test corpus uses align(3), align(5), align(6),
     * align(7) in alignment/bit_alignment) — so the runtime rejects
     * only alignment == 0. Non-power-of-two alignments are valid. */
    const uint8_t buf[2] = { 0 };
    zserio_bitstream_reader_t r;
    (void)zserio_bitstream_reader_init(&r, buf, 16U);
    TEST_ASSERT_EQUAL(ZSERIO_ERR_INVALID_ARGUMENT,
                      zserio_bitstream_reader_align(&r, 0U));
}

static void test_align_past_eof(void)
{
    /* Buffer holds 11 bits, current position 5, align to 32 wants 32 ≤ 11: fail */
    const uint8_t buf[2] = { 0 };
    zserio_bitstream_reader_t r;
    (void)zserio_bitstream_reader_init(&r, buf, 11U);
    uint64_t v = 0U;
    (void)zserio_bitstream_read_bits(&r, 5U, &v);
    TEST_ASSERT_EQUAL(ZSERIO_ERR_END_OF_STREAM,
                      zserio_bitstream_reader_align(&r, 32U));
}

static void test_align_null_bs(void)
{
    /* Verifies: REQ-FS-006 */
    TEST_ASSERT_EQUAL(ZSERIO_ERR_INVALID_ARGUMENT,
                      zserio_bitstream_reader_align(NULL, 8U));
}

/* ------------------------------------------------------------------------ */
/* Reader: position / size / seek / aligned_eof                              */
/* ------------------------------------------------------------------------ */

static void test_position_size_null(void)
{
    /* Defensive getters return 0 on NULL. */
    TEST_ASSERT_EQUAL_size_t(0U, zserio_bitstream_reader_position(NULL));
    TEST_ASSERT_EQUAL_size_t(0U, zserio_bitstream_reader_size(NULL));
}

static void test_seek_valid(void)
{
    const uint8_t buf[2] = { 0xAA, 0xBB };
    zserio_bitstream_reader_t r;
    (void)zserio_bitstream_reader_init(&r, buf, 16U);
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_bitstream_reader_seek(&r, 12U));
    TEST_ASSERT_EQUAL_size_t(12U, zserio_bitstream_reader_position(&r));
}

static void test_seek_past_eof(void)
{
    const uint8_t buf[2] = { 0xAA, 0xBB };
    zserio_bitstream_reader_t r;
    (void)zserio_bitstream_reader_init(&r, buf, 16U);
    TEST_ASSERT_EQUAL(ZSERIO_ERR_INVALID_POSITION,
                      zserio_bitstream_reader_seek(&r, 17U));
}

static void test_seek_null(void)
{
    /* Verifies: REQ-FS-006 */
    TEST_ASSERT_EQUAL(ZSERIO_ERR_INVALID_ARGUMENT,
                      zserio_bitstream_reader_seek(NULL, 0U));
}

static void test_aligned_eof_at_end(void)
{
    /* Buffer with 16 bits, position 16: aligned_eof(8) should be true. */
    const uint8_t buf[2] = { 0 };
    zserio_bitstream_reader_t r;
    (void)zserio_bitstream_reader_init(&r, buf, 16U);
    (void)zserio_bitstream_reader_seek(&r, 16U);
    TEST_ASSERT_TRUE(zserio_bitstream_reader_aligned_eof(&r, 8U));
}

static void test_aligned_eof_with_data(void)
{
    /* Position 0, 16 bits available, aligned_eof(8) should be false. */
    const uint8_t buf[2] = { 0 };
    zserio_bitstream_reader_t r;
    (void)zserio_bitstream_reader_init(&r, buf, 16U);
    TEST_ASSERT_FALSE(zserio_bitstream_reader_aligned_eof(&r, 8U));
}

static void test_aligned_eof_extend_probe(void)
{
    /* The extend-decode probe scenario from upstream: at position 13 with
     * buffer size 16 and alignment 8, the next aligned position is 16,
     * which equals buffer size → aligned_eof returns true. */
    const uint8_t buf[2] = { 0 };
    zserio_bitstream_reader_t r;
    (void)zserio_bitstream_reader_init(&r, buf, 16U);
    (void)zserio_bitstream_reader_seek(&r, 13U);
    TEST_ASSERT_TRUE(zserio_bitstream_reader_aligned_eof(&r, 8U));
}

static void test_aligned_eof_defensive(void)
{
    /* Verifies: REQ-FS-006 (defensive: returns true on bad input).
     *
     * "Bad input" means NULL bitstream or alignment == 0. Non-power-of-two
     * alignments are valid per the zserio spec (the upstream corpus
     * uses align(3) and align(5)), so `aligned_eof(&r, 3U)` is *not*
     * a defensive case — it computes a valid aligned position. */
    TEST_ASSERT_TRUE(zserio_bitstream_reader_aligned_eof(NULL, 8U));
    const uint8_t buf[2] = { 0 };
    zserio_bitstream_reader_t r;
    (void)zserio_bitstream_reader_init(&r, buf, 16U);
    TEST_ASSERT_TRUE(zserio_bitstream_reader_aligned_eof(&r, 0U));
}

/* ------------------------------------------------------------------------ */
/* Writer: initialization                                                   */
/* ------------------------------------------------------------------------ */

static void test_writer_init_zeros_buffer(void)
{
    /* Init must zero the buffer so partial-byte writes start clean. */
    uint8_t buf[4] = { 0xDE, 0xAD, 0xBE, 0xEF };
    zserio_bitstream_writer_t w;
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_bitstream_writer_init(&w, buf, 32U));
    for (size_t i = 0U; i < 4U; i++) {
        TEST_ASSERT_EQUAL_HEX8(0x00, buf[i]);
    }
}

static void test_writer_init_null_bs(void)
{
    /* Verifies: REQ-FS-006 */
    uint8_t buf[1];
    TEST_ASSERT_EQUAL(ZSERIO_ERR_INVALID_ARGUMENT,
                      zserio_bitstream_writer_init(NULL, buf, 8U));
}

static void test_writer_init_null_buffer(void)
{
    /* Verifies: REQ-FS-006 */
    zserio_bitstream_writer_t w;
    TEST_ASSERT_EQUAL(ZSERIO_ERR_INVALID_ARGUMENT,
                      zserio_bitstream_writer_init(&w, NULL, 8U));
}

static void test_writer_init_zero_capacity(void)
{
    /* Verifies: REQ-FS-006 */
    uint8_t buf[1];
    zserio_bitstream_writer_t w;
    TEST_ASSERT_EQUAL(ZSERIO_ERR_INVALID_BUFFER,
                      zserio_bitstream_writer_init(&w, buf, 0U));
}

static void test_writer_init_oversize_capacity(void)
{
    /* Verifies: REQ-FS-006 — guard against bit_index + n overflowing size_t. */
    uint8_t buf[1];
    zserio_bitstream_writer_t w;
    TEST_ASSERT_EQUAL(ZSERIO_ERR_INVALID_BUFFER,
                      zserio_bitstream_writer_init(&w, buf, SIZE_MAX));
}

/* ------------------------------------------------------------------------ */
/* Writer: write_bits                                                       */
/* ------------------------------------------------------------------------ */

static void test_write_bits_basic(void)
{
    uint8_t buf[1] = { 0 };
    zserio_bitstream_writer_t w;
    (void)zserio_bitstream_writer_init(&w, buf, 8U);

    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_bitstream_write_bits(&w, 8U, 0xABU));
    TEST_ASSERT_EQUAL_HEX8(0xAB, buf[0]);
    TEST_ASSERT_EQUAL_size_t(8U, zserio_bitstream_writer_position(&w));
}

static void test_write_bits_simple_structure(void)
{
    /* Verifies: REQ-FN-001 (wire-compat with upstream SimpleStructure)
     *
     * Writes a=7 (3 bits), b=0xFF (8 bits), c=0x7F (7 bits) and asserts
     * the resulting bytes match the hand-computed expected sequence
     * { 0xFF, 0xFF, 0xC0 }. Same example as test_read_bits_unaligned_simple_structure.
     */
    uint8_t buf[3] = { 0 };
    zserio_bitstream_writer_t w;
    (void)zserio_bitstream_writer_init(&w, buf, 24U);

    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_bitstream_write_bits(&w, 3U, 0x07U));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_bitstream_write_bits(&w, 8U, 0xFFU));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_bitstream_write_bits(&w, 7U, 0x7FU));

    TEST_ASSERT_EQUAL_HEX8(0xFF, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, buf[1]);
    TEST_ASSERT_EQUAL_HEX8(0xC0, buf[2]);
    TEST_ASSERT_EQUAL_size_t(18U, zserio_bitstream_writer_position(&w));
}

static void test_write_bits_one_bit(void)
{
    /* Write 1010_0000 one bit at a time. Result byte = 0xA0. */
    uint8_t buf[1] = { 0 };
    zserio_bitstream_writer_t w;
    (void)zserio_bitstream_writer_init(&w, buf, 8U);

    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_bitstream_write_bits(&w, 1U, 1U));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_bitstream_write_bits(&w, 1U, 0U));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_bitstream_write_bits(&w, 1U, 1U));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_bitstream_write_bits(&w, 1U, 0U));
    /* Remaining 4 bits stay 0 from init. */
    TEST_ASSERT_EQUAL_HEX8(0xA0, buf[0]);
}

static void test_write_bits_64_bit(void)
{
    uint8_t buf[8] = { 0 };
    zserio_bitstream_writer_t w;
    (void)zserio_bitstream_writer_init(&w, buf, 64U);

    TEST_ASSERT_EQUAL(ZSERIO_OK,
                      zserio_bitstream_write_bits(&w, 64U, 0x0123456789ABCDEFULL));
    const uint8_t expected[8] = { 0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF };
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, buf, 8U);
}

static void test_write_bits_null_bs(void)
{
    /* Verifies: REQ-FS-006 */
    TEST_ASSERT_EQUAL(ZSERIO_ERR_INVALID_ARGUMENT,
                      zserio_bitstream_write_bits(NULL, 8U, 0U));
}

static void test_write_bits_zero_num_bits(void)
{
    /* Verifies: REQ-FS-006 */
    uint8_t buf[1] = { 0 };
    zserio_bitstream_writer_t w;
    (void)zserio_bitstream_writer_init(&w, buf, 8U);
    TEST_ASSERT_EQUAL(ZSERIO_ERR_INVALID_NUM_BITS,
                      zserio_bitstream_write_bits(&w, 0U, 0U));
}

static void test_write_bits_too_many_bits(void)
{
    /* Verifies: REQ-FS-006 */
    uint8_t buf[16] = { 0 };
    zserio_bitstream_writer_t w;
    (void)zserio_bitstream_writer_init(&w, buf, 128U);
    TEST_ASSERT_EQUAL(ZSERIO_ERR_INVALID_NUM_BITS,
                      zserio_bitstream_write_bits(&w, 65U, 0U));
}

static void test_write_bits_overflow(void)
{
    /* Verifies: REQ-FS-005 (bounded writes), REQ-FS-006 */
    uint8_t buf[1] = { 0 };
    zserio_bitstream_writer_t w;
    (void)zserio_bitstream_writer_init(&w, buf, 8U);
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_bitstream_write_bits(&w, 8U, 0xABU));
    TEST_ASSERT_EQUAL(ZSERIO_ERR_BUFFER_OVERFLOW,
                      zserio_bitstream_write_bits(&w, 1U, 0U));
}

static void test_write_bits_value_out_of_range(void)
{
    /* Verifies: REQ-FS-006 */
    uint8_t buf[1] = { 0 };
    zserio_bitstream_writer_t w;
    (void)zserio_bitstream_writer_init(&w, buf, 8U);
    /* 0x100 doesn't fit in 8 bits. */
    TEST_ASSERT_EQUAL(ZSERIO_ERR_VALUE_OUT_OF_RANGE,
                      zserio_bitstream_write_bits(&w, 8U, 0x100U));
    /* And the position is unchanged (no partial write). */
    TEST_ASSERT_EQUAL_size_t(0U, zserio_bitstream_writer_position(&w));
}

/* ------------------------------------------------------------------------ */
/* Writer: write_signed                                                     */
/* ------------------------------------------------------------------------ */

static void test_write_signed_positive(void)
{
    uint8_t buf[1] = { 0 };
    zserio_bitstream_writer_t w;
    (void)zserio_bitstream_writer_init(&w, buf, 8U);
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_bitstream_write_signed(&w, 8U, 5));
    TEST_ASSERT_EQUAL_HEX8(0x05, buf[0]);
}

static void test_write_signed_negative_one(void)
{
    /* -1 in 4 bits = 0b1111. Top 4 bits of byte = 0xF0. */
    uint8_t buf[1] = { 0 };
    zserio_bitstream_writer_t w;
    (void)zserio_bitstream_writer_init(&w, buf, 4U);
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_bitstream_write_signed(&w, 4U, -1));
    TEST_ASSERT_EQUAL_HEX8(0xF0, buf[0]);
}

static void test_write_signed_int8_min(void)
{
    /* -128 in 8 bits = 0x80 */
    uint8_t buf[1] = { 0 };
    zserio_bitstream_writer_t w;
    (void)zserio_bitstream_writer_init(&w, buf, 8U);
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_bitstream_write_signed(&w, 8U, -128));
    TEST_ASSERT_EQUAL_HEX8(0x80, buf[0]);
}

static void test_write_signed_int64_min(void)
{
    /* INT64_MIN in 64 bits */
    uint8_t buf[8] = { 0 };
    zserio_bitstream_writer_t w;
    (void)zserio_bitstream_writer_init(&w, buf, 64U);
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_bitstream_write_signed(&w, 64U, INT64_MIN));
    const uint8_t expected[8] = { 0x80, 0, 0, 0, 0, 0, 0, 0 };
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, buf, 8U);
}

static void test_write_signed_out_of_range(void)
{
    /* Verifies: REQ-FS-006 */
    uint8_t buf[1] = { 0 };
    zserio_bitstream_writer_t w;
    (void)zserio_bitstream_writer_init(&w, buf, 8U);
    /* 4-bit signed range is [-8, 7]. 8 is out, -9 is out. */
    TEST_ASSERT_EQUAL(ZSERIO_ERR_VALUE_OUT_OF_RANGE,
                      zserio_bitstream_write_signed(&w, 4U, 8));
    TEST_ASSERT_EQUAL(ZSERIO_ERR_VALUE_OUT_OF_RANGE,
                      zserio_bitstream_write_signed(&w, 4U, -9));
}

static void test_write_signed_invalid_args(void)
{
    /* Verifies: REQ-FS-006 */
    uint8_t buf[1] = { 0 };
    zserio_bitstream_writer_t w;
    (void)zserio_bitstream_writer_init(&w, buf, 8U);
    TEST_ASSERT_EQUAL(ZSERIO_ERR_INVALID_ARGUMENT,
                      zserio_bitstream_write_signed(NULL, 4U, 0));
    TEST_ASSERT_EQUAL(ZSERIO_ERR_INVALID_NUM_BITS,
                      zserio_bitstream_write_signed(&w, 0U, 0));
    TEST_ASSERT_EQUAL(ZSERIO_ERR_INVALID_NUM_BITS,
                      zserio_bitstream_write_signed(&w, 65U, 0));
}

/* ------------------------------------------------------------------------ */
/* Writer: align / seek / position                                          */
/* ------------------------------------------------------------------------ */

static void test_writer_align_pads_with_zero(void)
{
    /* Write 3 bits then align to 8: byte 0 should be 111 followed by 5 zeros. */
    uint8_t buf[1] = { 0 };
    zserio_bitstream_writer_t w;
    (void)zserio_bitstream_writer_init(&w, buf, 8U);
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_bitstream_write_bits(&w, 3U, 0x07U));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_bitstream_writer_align(&w, 8U));
    TEST_ASSERT_EQUAL_size_t(8U, zserio_bitstream_writer_position(&w));
    TEST_ASSERT_EQUAL_HEX8(0xE0, buf[0]); /* 111_00000 */
}

static void test_writer_align_no_op(void)
{
    uint8_t buf[2] = { 0 };
    zserio_bitstream_writer_t w;
    (void)zserio_bitstream_writer_init(&w, buf, 16U);
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_bitstream_writer_align(&w, 8U));
    TEST_ASSERT_EQUAL_size_t(0U, zserio_bitstream_writer_position(&w));
}

static void test_writer_align_invalid_alignment(void)
{
    /* Verifies: REQ-FS-006.
     *
     * Mirrors test_align_invalid_alignment on the reader side: only
     * alignment == 0 (and a NULL writer) are invalid; non-power-of-
     * two alignments are valid per the zserio spec. */
    uint8_t buf[1] = { 0 };
    zserio_bitstream_writer_t w;
    (void)zserio_bitstream_writer_init(&w, buf, 8U);
    TEST_ASSERT_EQUAL(ZSERIO_ERR_INVALID_ARGUMENT,
                      zserio_bitstream_writer_align(&w, 0U));
    TEST_ASSERT_EQUAL(ZSERIO_ERR_INVALID_ARGUMENT,
                      zserio_bitstream_writer_align(NULL, 8U));
}

static void test_writer_align_overflow(void)
{
    /* 11-bit capacity, position 5, align(32) wants 32 > 11. */
    uint8_t buf[2] = { 0 };
    zserio_bitstream_writer_t w;
    (void)zserio_bitstream_writer_init(&w, buf, 11U);
    (void)zserio_bitstream_write_bits(&w, 5U, 0U);
    TEST_ASSERT_EQUAL(ZSERIO_ERR_BUFFER_OVERFLOW,
                      zserio_bitstream_writer_align(&w, 32U));
}

static void test_writer_seek_offset_patch(void)
{
    /*
     * Offset patch pattern: write a placeholder, write the dependent
     * field, seek back, overwrite the placeholder, seek forward.
     */
    uint8_t buf[3] = { 0 };
    zserio_bitstream_writer_t w;
    (void)zserio_bitstream_writer_init(&w, buf, 24U);

    /* Reserve 8 bits for a placeholder offset. */
    const size_t patch_pos = zserio_bitstream_writer_position(&w);
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_bitstream_write_bits(&w, 8U, 0U));

    /* Write the dependent field. */
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_bitstream_write_bits(&w, 16U, 0xABCDU));
    const size_t end_pos = zserio_bitstream_writer_position(&w);

    /* Seek back, write the actual offset, seek forward. */
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_bitstream_writer_seek(&w, patch_pos));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_bitstream_write_bits(&w, 8U, 0xEFU));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_bitstream_writer_seek(&w, end_pos));

    /* Bytes 0,1,2 should be EF AB CD. */
    TEST_ASSERT_EQUAL_HEX8(0xEF, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(0xAB, buf[1]);
    TEST_ASSERT_EQUAL_HEX8(0xCD, buf[2]);
}

static void test_writer_seek_past_capacity(void)
{
    uint8_t buf[1] = { 0 };
    zserio_bitstream_writer_t w;
    (void)zserio_bitstream_writer_init(&w, buf, 8U);
    TEST_ASSERT_EQUAL(ZSERIO_ERR_INVALID_POSITION,
                      zserio_bitstream_writer_seek(&w, 9U));
}

static void test_writer_seek_null(void)
{
    /* Verifies: REQ-FS-006 */
    TEST_ASSERT_EQUAL(ZSERIO_ERR_INVALID_ARGUMENT,
                      zserio_bitstream_writer_seek(NULL, 0U));
}

static void test_writer_position_null(void)
{
    TEST_ASSERT_EQUAL_size_t(0U, zserio_bitstream_writer_position(NULL));
}

/* ------------------------------------------------------------------------ */
/* Round-trip tests                                                         */
/* ------------------------------------------------------------------------ */

static void test_round_trip_simple_structure(void)
{
    /* Verifies: REQ-FN-001 */
    uint8_t buf[3] = { 0 };
    zserio_bitstream_writer_t w;
    (void)zserio_bitstream_writer_init(&w, buf, 24U);
    (void)zserio_bitstream_write_bits(&w, 3U, 0x07U);
    (void)zserio_bitstream_write_bits(&w, 8U, 0xFFU);
    (void)zserio_bitstream_write_bits(&w, 7U, 0x7FU);

    zserio_bitstream_reader_t r;
    (void)zserio_bitstream_reader_init(&r, buf, 18U);
    uint64_t a = 0U, b = 0U, c = 0U;
    (void)zserio_bitstream_read_bits(&r, 3U, &a);
    (void)zserio_bitstream_read_bits(&r, 8U, &b);
    (void)zserio_bitstream_read_bits(&r, 7U, &c);
    TEST_ASSERT_EQUAL_UINT64(0x07U, a);
    TEST_ASSERT_EQUAL_UINT64(0xFFU, b);
    TEST_ASSERT_EQUAL_UINT64(0x7FU, c);
}

static void test_round_trip_random(void)
{
    /* Verifies: REQ-FN-001 — round-trip 50 PRNG-seeded value pairs. */
    uint64_t state = 0xC001CAFE12345678ULL;
    uint8_t buf[1024] = { 0 };

    zserio_bitstream_writer_t w;
    (void)zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8U);

    /* Generate a sequence of (num_bits, value) pairs, write them. */
    uint8_t  widths[50];
    uint64_t values[50];
    size_t total_bits = 0U;
    for (size_t i = 0U; i < 50U; i++) {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        widths[i] = (uint8_t)((state >> 56) % 64U) + 1U; /* 1..64 */
        if (total_bits + widths[i] > sizeof(buf) * 8U) {
            widths[i] = (uint8_t)((sizeof(buf) * 8U) - total_bits);
            if (widths[i] == 0U) {
                widths[i] = 1U; /* shouldn't happen with size=1024 */
            }
        }
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        const uint64_t mask = (widths[i] == 64U) ? ~(uint64_t)0U
                                                 : (((uint64_t)1U << widths[i]) - 1U);
        values[i] = state & mask;
        TEST_ASSERT_EQUAL(ZSERIO_OK,
                          zserio_bitstream_write_bits(&w, widths[i], values[i]));
        total_bits += widths[i];
    }

    zserio_bitstream_reader_t r;
    (void)zserio_bitstream_reader_init(&r, buf, total_bits);
    for (size_t i = 0U; i < 50U; i++) {
        uint64_t got = 0U;
        TEST_ASSERT_EQUAL(ZSERIO_OK,
                          zserio_bitstream_read_bits(&r, widths[i], &got));
        TEST_ASSERT_EQUAL_UINT64(values[i], got);
    }
}

/* ------------------------------------------------------------------------ */
/* Freedom from interference (REQ-FS-007)                                   */
/* ------------------------------------------------------------------------ */

static void test_two_readers_independent(void)
{
    /* Verifies: REQ-FS-007 — two readers on the same buffer maintain
     * fully independent state. */
    const uint8_t buf[2] = { 0xAB, 0xCD };
    zserio_bitstream_reader_t r1;
    zserio_bitstream_reader_t r2;
    (void)zserio_bitstream_reader_init(&r1, buf, 16U);
    (void)zserio_bitstream_reader_init(&r2, buf, 16U);

    /* r1 reads 8 bits, r2 should still be at position 0. */
    uint64_t v = 0U;
    (void)zserio_bitstream_read_bits(&r1, 8U, &v);
    TEST_ASSERT_EQUAL_size_t(8U, zserio_bitstream_reader_position(&r1));
    TEST_ASSERT_EQUAL_size_t(0U, zserio_bitstream_reader_position(&r2));

    /* Read same bits from r2: must still see 0xAB. */
    (void)zserio_bitstream_read_bits(&r2, 8U, &v);
    TEST_ASSERT_EQUAL_UINT64(0xABU, v);
    TEST_ASSERT_EQUAL_size_t(8U, zserio_bitstream_reader_position(&r2));
}

/* ------------------------------------------------------------------------ */
/* Test runner                                                              */
/* ------------------------------------------------------------------------ */

int main(void)
{
    UNITY_BEGIN();

    /* Reader: init */
    RUN_TEST(test_reader_init_valid);
    RUN_TEST(test_reader_init_null_bs);
    RUN_TEST(test_reader_init_null_buffer_with_size);
    RUN_TEST(test_reader_init_empty_buffer_ok);
    RUN_TEST(test_reader_init_oversize);

    /* Reader: read_bits */
    RUN_TEST(test_read_bits_basic);
    RUN_TEST(test_read_bits_one_bit);
    RUN_TEST(test_read_bits_64_bit);
    RUN_TEST(test_read_bits_unaligned_simple_structure);
    RUN_TEST(test_read_bits_null_bs);
    RUN_TEST(test_read_bits_null_out);
    RUN_TEST(test_read_bits_zero_num_bits);
    RUN_TEST(test_read_bits_too_many_bits);
    RUN_TEST(test_read_bits_eof);

    /* Reader: read_signed */
    RUN_TEST(test_read_signed_positive);
    RUN_TEST(test_read_signed_negative);
    RUN_TEST(test_read_signed_min_int8);
    RUN_TEST(test_read_signed_64_bit);
    RUN_TEST(test_read_signed_invalid_args);

    /* Reader: align */
    RUN_TEST(test_align_already_aligned);
    RUN_TEST(test_align_byte);
    RUN_TEST(test_align_invalid_alignment);
    RUN_TEST(test_align_past_eof);
    RUN_TEST(test_align_null_bs);

    /* Reader: position / seek / aligned_eof */
    RUN_TEST(test_position_size_null);
    RUN_TEST(test_seek_valid);
    RUN_TEST(test_seek_past_eof);
    RUN_TEST(test_seek_null);
    RUN_TEST(test_aligned_eof_at_end);
    RUN_TEST(test_aligned_eof_with_data);
    RUN_TEST(test_aligned_eof_extend_probe);
    RUN_TEST(test_aligned_eof_defensive);

    /* Writer: init */
    RUN_TEST(test_writer_init_zeros_buffer);
    RUN_TEST(test_writer_init_null_bs);
    RUN_TEST(test_writer_init_null_buffer);
    RUN_TEST(test_writer_init_zero_capacity);
    RUN_TEST(test_writer_init_oversize_capacity);

    /* Writer: write_bits */
    RUN_TEST(test_write_bits_basic);
    RUN_TEST(test_write_bits_simple_structure);
    RUN_TEST(test_write_bits_one_bit);
    RUN_TEST(test_write_bits_64_bit);
    RUN_TEST(test_write_bits_null_bs);
    RUN_TEST(test_write_bits_zero_num_bits);
    RUN_TEST(test_write_bits_too_many_bits);
    RUN_TEST(test_write_bits_overflow);
    RUN_TEST(test_write_bits_value_out_of_range);

    /* Writer: write_signed */
    RUN_TEST(test_write_signed_positive);
    RUN_TEST(test_write_signed_negative_one);
    RUN_TEST(test_write_signed_int8_min);
    RUN_TEST(test_write_signed_int64_min);
    RUN_TEST(test_write_signed_out_of_range);
    RUN_TEST(test_write_signed_invalid_args);

    /* Writer: align / seek / position */
    RUN_TEST(test_writer_align_pads_with_zero);
    RUN_TEST(test_writer_align_no_op);
    RUN_TEST(test_writer_align_invalid_alignment);
    RUN_TEST(test_writer_align_overflow);
    RUN_TEST(test_writer_seek_offset_patch);
    RUN_TEST(test_writer_seek_past_capacity);
    RUN_TEST(test_writer_seek_null);
    RUN_TEST(test_writer_position_null);

    /* Round-trip and FFI */
    RUN_TEST(test_round_trip_simple_structure);
    RUN_TEST(test_round_trip_random);
    RUN_TEST(test_two_readers_independent);

    return UNITY_END();
}
