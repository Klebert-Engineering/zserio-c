/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Fabian Klebert
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "unity.h"
#include "zserio/allocator.h"
#include "zserio/bitstream.h"
#include "zserio/extern.h"
#include "zserio/status.h"

/* ------------------------------------------------------------------------ */
/* Tracking allocator                                                        */
/* ------------------------------------------------------------------------ */

typedef struct {
    size_t alloc_count;
    size_t free_count;
    int    fail_alloc;
} alloc_state_t;

static void *t_alloc(void *ud, size_t size, size_t align)
{
    (void)align;
    alloc_state_t *s = (alloc_state_t *)ud;
    if (s->fail_alloc != 0) {
        s->fail_alloc = 0;
        return NULL;
    }
    void *p = malloc(size);
    if (p != NULL) {
        s->alloc_count++;
    }
    return p;
}

static void t_free(void *ud, void *ptr, size_t size, size_t align)
{
    (void)size;
    (void)align;
    if (ptr != NULL) {
        ((alloc_state_t *)ud)->free_count++;
        free(ptr);
    }
}

static const zserio_allocator_vtable_t TEST_VTABLE = { t_alloc, NULL, t_free };
static alloc_state_t g_alloc;

static zserio_allocator_t test_allocator(void)
{
    g_alloc.alloc_count = 0U;
    g_alloc.free_count  = 0U;
    g_alloc.fail_alloc  = 0;
    const zserio_allocator_t a = { &TEST_VTABLE, &g_alloc };
    return a;
}

void setUp(void) { (void)test_allocator(); }
void tearDown(void) {}

/* ------------------------------------------------------------------------ */
/* Encoding-Guide spot check                                                */
/* ------------------------------------------------------------------------ */

static void test_extern_encoding_guide_example(void)
{
    /* Verifies: REQ-FN-001
     *
     * 10-bit pattern 1010010111 encodes as `0a a5 c0`:
     * varsize 0x0a (= 10 bits) then the 10 bits MSB-aligned in the
     * next two bytes (top 10 of 16 are the payload, bottom 6 zero).
     */
    const uint8_t src_bits[2] = { 0xA5, 0xC0 }; /* 1010_0101 1100_0000 */
    zserio_bit_view_t v = { src_bits, 0U, 10U };

    uint8_t out[3] = { 0 };
    zserio_bitstream_writer_t w;
    (void)zserio_bitstream_writer_init(&w, out, 24U);
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_write_extern(&w, v));
    TEST_ASSERT_EQUAL_HEX8(0x0A, out[0]);
    TEST_ASSERT_EQUAL_HEX8(0xA5, out[1]);
    TEST_ASSERT_EQUAL_HEX8(0xC0, out[2]);
    /* 8 bits varsize + 10 bits payload = 18 bits. */
    TEST_ASSERT_EQUAL_size_t(18U, zserio_bitstream_writer_position(&w));
}

/* ------------------------------------------------------------------------ */
/* Round-trip via view path                                                  */
/* ------------------------------------------------------------------------ */

static void test_extern_round_trip_view_byte_aligned(void)
{
    /* Verifies: REQ-FN-001 — view path on byte-aligned source. */
    const uint8_t src[2] = { 0xDE, 0xAD };
    zserio_bit_view_t in_view = { src, 0U, 16U };

    uint8_t buf[16] = { 0 };
    zserio_bitstream_writer_t w;
    (void)zserio_bitstream_writer_init(&w, buf, 128U);
    (void)zserio_write_extern(&w, in_view);

    zserio_bitstream_reader_t r;
    (void)zserio_bitstream_reader_init(&r, buf, zserio_bitstream_writer_position(&w));
    zserio_bit_view_t out_view = { NULL, 0U, 0U };
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_read_extern_view(&r, &out_view));
    TEST_ASSERT_EQUAL_size_t(16U, out_view.bit_length);
    TEST_ASSERT_EQUAL_size_t(0U, out_view.bit_offset);
    TEST_ASSERT_EQUAL_HEX8(0xDE, out_view.base[0]);
    TEST_ASSERT_EQUAL_HEX8(0xAD, out_view.base[1]);
}

static void test_extern_view_succeeds_on_misaligned_source(void)
{
    /* Verifies: REQ-FN-001 — extern view always succeeds, exposing
     * the bit_offset to the caller. Distinguishes extern from bytes/
     * string whose views require byte alignment. */
    uint8_t buf[16] = { 0 };
    zserio_bitstream_writer_t w;
    (void)zserio_bitstream_writer_init(&w, buf, 128U);
    /* 5 bits then an 8-bit extern → starts at bit 5. */
    (void)zserio_bitstream_write_bits(&w, 5U, 0x15U);
    const uint8_t src[1] = { 0xAB };
    const zserio_bit_view_t in_view = { src, 0U, 8U };
    (void)zserio_write_extern(&w, in_view);

    zserio_bitstream_reader_t r;
    (void)zserio_bitstream_reader_init(&r, buf, zserio_bitstream_writer_position(&w));
    uint64_t skip = 0U;
    (void)zserio_bitstream_read_bits(&r, 5U, &skip);

    zserio_bit_view_t out_view = { NULL, 0U, 0U };
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_read_extern_view(&r, &out_view));
    TEST_ASSERT_EQUAL_size_t(8U, out_view.bit_length);
    /* The view captures whatever bit_offset the source ended up at after
     * the varsize. */
    TEST_ASSERT_TRUE(out_view.bit_offset <= 7U);
}

/* ------------------------------------------------------------------------ */
/* Round-trip via materializing read                                        */
/* ------------------------------------------------------------------------ */

static void test_extern_materialize_large_byte_aligned(void)
{
    /* Verifies: REQ-FS-001, REQ-FN-001 — exercises the memcpy fast
     * path in zserio_read_extern. A 20-byte payload exceeds the
     * 8-byte (64-bit) bit-cache capacity, so the path drains the
     * cache byte-by-byte and then `memcpy`s the rest directly from
     * the source buffer. The small-payload tests above only exercise
     * the cache-drain branch. */
    uint8_t src[20];
    for (size_t i = 0U; i < 20U; i++) {
        src[i] = (uint8_t)(0xA0U + i);
    }
    const zserio_bit_view_t in_view = { src, 0U, 20U * 8U };

    uint8_t buf[40] = { 0 };
    zserio_bitstream_writer_t w;
    (void)zserio_bitstream_writer_init(&w, buf, 8U * sizeof(buf));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_write_extern(&w, in_view));

    zserio_bitstream_reader_t r;
    (void)zserio_bitstream_reader_init(&r, buf, zserio_bitstream_writer_position(&w));
    const zserio_allocator_t alloc = test_allocator();
    zserio_extern_t out = { NULL, 0U };
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_read_extern(&r, &alloc, &out));
    TEST_ASSERT_EQUAL_size_t(20U * 8U, out.bit_length);
    for (size_t i = 0U; i < 20U; i++) {
        TEST_ASSERT_EQUAL_HEX8(src[i], out.bytes[i]);
    }
    zserio_extern_free(&out, &alloc);
}

static void test_extern_round_trip_materialize_byte_aligned(void)
{
    /* Verifies: REQ-FS-001, REQ-FN-001 — materialize aligns the bits
     * at byte 0 bit 0 of the output buffer. */
    const uint8_t src[2] = { 0xCA, 0xFE };
    const zserio_bit_view_t in_view = { src, 0U, 16U };

    uint8_t buf[16] = { 0 };
    zserio_bitstream_writer_t w;
    (void)zserio_bitstream_writer_init(&w, buf, 128U);
    (void)zserio_write_extern(&w, in_view);

    zserio_bitstream_reader_t r;
    (void)zserio_bitstream_reader_init(&r, buf, zserio_bitstream_writer_position(&w));
    const zserio_allocator_t alloc = test_allocator();
    zserio_extern_t out = { NULL, 0U };
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_read_extern(&r, &alloc, &out));
    TEST_ASSERT_EQUAL_size_t(16U, out.bit_length);
    TEST_ASSERT_EQUAL_HEX8(0xCA, out.bytes[0]);
    TEST_ASSERT_EQUAL_HEX8(0xFE, out.bytes[1]);

    zserio_extern_free(&out, &alloc);
    TEST_ASSERT_EQUAL_size_t(g_alloc.alloc_count, g_alloc.free_count);
}

static void test_extern_materialize_misaligned_source(void)
{
    /* Verifies: REQ-FN-001 — materialize handles bit-misaligned source. */
    uint8_t buf[16] = { 0 };
    zserio_bitstream_writer_t w;
    (void)zserio_bitstream_writer_init(&w, buf, 128U);
    (void)zserio_bitstream_write_bits(&w, 3U, 0x05U);
    const uint8_t src[2] = { 0xCA, 0xFE };
    const zserio_bit_view_t in_view = { src, 0U, 16U };
    (void)zserio_write_extern(&w, in_view);

    zserio_bitstream_reader_t r;
    (void)zserio_bitstream_reader_init(&r, buf, zserio_bitstream_writer_position(&w));
    uint64_t skip = 0U;
    (void)zserio_bitstream_read_bits(&r, 3U, &skip);

    const zserio_allocator_t alloc = test_allocator();
    zserio_extern_t out = { NULL, 0U };
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_read_extern(&r, &alloc, &out));
    TEST_ASSERT_EQUAL_size_t(16U, out.bit_length);
    TEST_ASSERT_EQUAL_HEX8(0xCA, out.bytes[0]);
    TEST_ASSERT_EQUAL_HEX8(0xFE, out.bytes[1]);
    zserio_extern_free(&out, &alloc);
}

static void test_extern_partial_byte_trailing_zeros(void)
{
    /* A 10-bit extern materializes into 2 bytes; the trailing 6 bits
     * of the second byte must be zero. */
    const uint8_t src[2] = { 0xFF, 0xFF }; /* 16 set bits */
    const zserio_bit_view_t in_view = { src, 0U, 10U };

    uint8_t buf[3] = { 0 };
    zserio_bitstream_writer_t w;
    (void)zserio_bitstream_writer_init(&w, buf, 24U);
    (void)zserio_write_extern(&w, in_view);

    zserio_bitstream_reader_t r;
    (void)zserio_bitstream_reader_init(&r, buf, zserio_bitstream_writer_position(&w));
    const zserio_allocator_t alloc = test_allocator();
    zserio_extern_t out = { NULL, 0U };
    (void)zserio_read_extern(&r, &alloc, &out);
    TEST_ASSERT_EQUAL_size_t(10U, out.bit_length);
    TEST_ASSERT_EQUAL_HEX8(0xFF, out.bytes[0]);
    TEST_ASSERT_EQUAL_HEX8(0xC0, out.bytes[1]); /* top 2 bits set, bottom 6 zero */
    zserio_extern_free(&out, &alloc);
}

/* ------------------------------------------------------------------------ */
/* Empty extern                                                             */
/* ------------------------------------------------------------------------ */

static void test_extern_empty_view(void)
{
    uint8_t buf[1] = { 0 };
    zserio_bitstream_writer_t w;
    (void)zserio_bitstream_writer_init(&w, buf, 8U);
    const zserio_bit_view_t empty = { NULL, 0U, 0U };
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_write_extern(&w, empty));
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[0]); /* varsize 0 */

    zserio_bitstream_reader_t r;
    (void)zserio_bitstream_reader_init(&r, buf, 8U);
    zserio_bit_view_t out_view = { (const uint8_t *)0xDEAD, 7U, 99U };
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_read_extern_view(&r, &out_view));
    TEST_ASSERT_EQUAL_size_t(0U, out_view.bit_length);
    TEST_ASSERT_NULL(out_view.base);
}

static void test_extern_empty_owned_no_allocation(void)
{
    const uint8_t buf[1] = { 0 };
    zserio_bitstream_reader_t r;
    (void)zserio_bitstream_reader_init(&r, buf, 8U);

    const zserio_allocator_t alloc = test_allocator();
    zserio_extern_t out = { NULL, 0U };
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_read_extern(&r, &alloc, &out));
    TEST_ASSERT_EQUAL_size_t(0U, out.bit_length);
    TEST_ASSERT_NULL(out.bytes);
    TEST_ASSERT_EQUAL_size_t(0U, g_alloc.alloc_count);
}

/* ------------------------------------------------------------------------ */
/* Defensive checks                                                         */
/* ------------------------------------------------------------------------ */

static void test_extern_write_invalid_bit_offset(void)
{
    /* Verifies: REQ-FS-006 */
    uint8_t buf[1] = { 0 };
    zserio_bitstream_writer_t w;
    (void)zserio_bitstream_writer_init(&w, buf, 8U);
    const uint8_t src[1] = { 0 };
    zserio_bit_view_t v = { src, 8U, 1U }; /* bit_offset = 8 is invalid */
    TEST_ASSERT_EQUAL(ZSERIO_ERR_INVALID_ARGUMENT, zserio_write_extern(&w, v));
}

static void test_extern_write_null_base_with_length(void)
{
    /* Verifies: REQ-FS-006 */
    uint8_t buf[1] = { 0 };
    zserio_bitstream_writer_t w;
    (void)zserio_bitstream_writer_init(&w, buf, 8U);
    zserio_bit_view_t v = { NULL, 0U, 8U };
    TEST_ASSERT_EQUAL(ZSERIO_ERR_INVALID_ARGUMENT, zserio_write_extern(&w, v));
}

static void test_extern_alloc_failure(void)
{
    /* Verifies: REQ-FS-001, REQ-FS-006 */
    const uint8_t src[2] = { 0xFF, 0xFF };
    const zserio_bit_view_t in_view = { src, 0U, 16U };

    uint8_t buf[16] = { 0 };
    zserio_bitstream_writer_t w;
    (void)zserio_bitstream_writer_init(&w, buf, 128U);
    (void)zserio_write_extern(&w, in_view);

    zserio_bitstream_reader_t r;
    (void)zserio_bitstream_reader_init(&r, buf, zserio_bitstream_writer_position(&w));
    zserio_allocator_t alloc = test_allocator();
    g_alloc.fail_alloc = 1;

    zserio_extern_t out = { NULL, 0U };
    TEST_ASSERT_EQUAL(ZSERIO_ERR_OUT_OF_MEMORY, zserio_read_extern(&r, &alloc, &out));
    TEST_ASSERT_EQUAL_size_t(0U, g_alloc.alloc_count);
    TEST_ASSERT_EQUAL_size_t(0U, g_alloc.free_count);
}

static void test_extern_free_on_zero_struct(void)
{
    const zserio_allocator_t alloc = test_allocator();
    zserio_extern_t e = { NULL, 0U };
    zserio_extern_free(&e, &alloc);
    zserio_extern_free(NULL, &alloc); /* no-op */
    TEST_ASSERT_EQUAL_size_t(0U, g_alloc.free_count);
}

static void test_extern_view_null_args(void)
{
    /* Verifies: REQ-FS-006 */
    const uint8_t buf[1] = { 0 };
    zserio_bitstream_reader_t r;
    (void)zserio_bitstream_reader_init(&r, buf, 8U);
    zserio_bit_view_t out;
    TEST_ASSERT_EQUAL(ZSERIO_ERR_INVALID_ARGUMENT, zserio_read_extern_view(NULL, &out));
    TEST_ASSERT_EQUAL(ZSERIO_ERR_INVALID_ARGUMENT, zserio_read_extern_view(&r, NULL));
}

static void test_extern_view_truncated_buffer(void)
{
    /* varsize 16 bits, buffer has only 8 more bits after the prefix. */
    const uint8_t buf[2] = { 0x10, 0xAA };
    zserio_bitstream_reader_t r;
    (void)zserio_bitstream_reader_init(&r, buf, 16U);
    zserio_bit_view_t out = { NULL, 0U, 0U };
    TEST_ASSERT_EQUAL(ZSERIO_ERR_END_OF_STREAM, zserio_read_extern_view(&r, &out));
    /* Position must be unchanged on failure. */
    TEST_ASSERT_EQUAL_size_t(0U, zserio_bitstream_reader_position(&r));
}

static void test_extern_owned_null_args(void)
{
    /* Verifies: REQ-FS-006 */
    const uint8_t buf[1] = { 0 };
    zserio_bitstream_reader_t r;
    (void)zserio_bitstream_reader_init(&r, buf, 8U);
    const zserio_allocator_t alloc = test_allocator();
    zserio_extern_t out;
    TEST_ASSERT_EQUAL(ZSERIO_ERR_INVALID_ARGUMENT, zserio_read_extern(NULL, &alloc, &out));
    TEST_ASSERT_EQUAL(ZSERIO_ERR_INVALID_ARGUMENT, zserio_read_extern(&r, &alloc, NULL));
    TEST_ASSERT_EQUAL(ZSERIO_ERR_INVALID_ARGUMENT, zserio_read_extern(&r, NULL, &out));
    const zserio_allocator_t bad = { NULL, NULL };
    TEST_ASSERT_EQUAL(ZSERIO_ERR_INVALID_ARGUMENT, zserio_read_extern(&r, &bad, &out));
}

static void test_extern_owned_truncated_buffer_no_leak(void)
{
    /* varsize 16 bits, buffer too short. Bound check before alloc. */
    const uint8_t buf[2] = { 0x10, 0xAA };
    zserio_bitstream_reader_t r;
    (void)zserio_bitstream_reader_init(&r, buf, 16U);
    const zserio_allocator_t alloc = test_allocator();
    zserio_extern_t out = { NULL, 0U };
    TEST_ASSERT_EQUAL(ZSERIO_ERR_END_OF_STREAM, zserio_read_extern(&r, &alloc, &out));
    TEST_ASSERT_EQUAL_size_t(0U, g_alloc.alloc_count);
}

static void test_extern_write_too_long(void)
{
    /* Verifies: REQ-FS-006 */
    uint8_t buf[1] = { 0 };
    zserio_bitstream_writer_t w;
    (void)zserio_bitstream_writer_init(&w, buf, 8U);
    const uint8_t src[1] = { 0 };
    /* bit_length larger than ZSERIO_EXTERN_MAX_BITS triggers VALUE_OUT_OF_RANGE. */
    zserio_bit_view_t v = { src, 0U, (size_t)0x80000000U };
    TEST_ASSERT_EQUAL(ZSERIO_ERR_VALUE_OUT_OF_RANGE, zserio_write_extern(&w, v));
}

/* ------------------------------------------------------------------------ */
/* Test runner                                                              */
/* ------------------------------------------------------------------------ */

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_extern_encoding_guide_example);
    RUN_TEST(test_extern_round_trip_view_byte_aligned);
    RUN_TEST(test_extern_view_succeeds_on_misaligned_source);
    RUN_TEST(test_extern_round_trip_materialize_byte_aligned);
    RUN_TEST(test_extern_materialize_misaligned_source);
    RUN_TEST(test_extern_partial_byte_trailing_zeros);
    RUN_TEST(test_extern_materialize_large_byte_aligned);

    RUN_TEST(test_extern_empty_view);
    RUN_TEST(test_extern_empty_owned_no_allocation);

    RUN_TEST(test_extern_write_invalid_bit_offset);
    RUN_TEST(test_extern_write_null_base_with_length);
    RUN_TEST(test_extern_alloc_failure);
    RUN_TEST(test_extern_free_on_zero_struct);
    RUN_TEST(test_extern_view_null_args);
    RUN_TEST(test_extern_view_truncated_buffer);
    RUN_TEST(test_extern_owned_null_args);
    RUN_TEST(test_extern_owned_truncated_buffer_no_leak);
    RUN_TEST(test_extern_write_too_long);

    return UNITY_END();
}
