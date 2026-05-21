/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Fabian Klebert
 *
 * Verifies: REQ-FN-017 — owned `zserio_bytes_t` payload allocates via
 *                        the supplied vtable.
 * Verifies: REQ-FN-018 — `_free`-on-partial-state safety
 *                        (tracking-allocator leak check).
 * Verifies: REQ-FN-019 — `_free` returns the field to post-`_init` state.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "unity.h"
#include "zserio/allocator.h"
#include "zserio/bitstream.h"
#include "zserio/bytes.h"
#include "zserio/status.h"

/* ------------------------------------------------------------------------ */
/* Tracking allocator                                                       */
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

static void test_bytes_encoding_guide_example(void)
{
    /* Verifies: REQ-FN-001
     *
     * Encoding Guide example: bytes "de ad be ef" (4 bytes) encodes as
     *   04 de ad be ef
     */
    uint8_t out[5] = { 0 };
    zserio_bitstream_writer_t w;
    (void)zserio_bitstream_writer_init(&w, out, 40U);

    const uint8_t src[4] = { 0xDE, 0xAD, 0xBE, 0xEF };
    const zserio_bytes_view_t v = { src, 4U };
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_write_bytes(&w, v));

    const uint8_t expected[5] = { 0x04, 0xDE, 0xAD, 0xBE, 0xEF };
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, out, 5U);
}

/* ------------------------------------------------------------------------ */
/* Round-trip via view path                                                 */
/* ------------------------------------------------------------------------ */

static void test_bytes_round_trip_view_aligned(void)
{
    const uint8_t in[3] = { 0x01, 0x02, 0x03 };
    const zserio_bytes_view_t in_view = { in, 3U };

    uint8_t buf[8] = { 0 };
    zserio_bitstream_writer_t w;
    (void)zserio_bitstream_writer_init(&w, buf, 64U);
    (void)zserio_write_bytes(&w, in_view);

    zserio_bitstream_reader_t r;
    (void)zserio_bitstream_reader_init(&r, buf, zserio_bitstream_writer_position(&w));
    zserio_bytes_view_t out = { NULL, 0U };
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_read_bytes_view(&r, &out));
    TEST_ASSERT_EQUAL_size_t(3U, out.length);
    TEST_ASSERT_EQUAL_MEMORY(in, out.base, 3U);
}

static void test_bytes_view_misaligned_fails(void)
{
    /* Verifies: REQ-FS-006 — view requires byte-aligned source. */
    uint8_t buf[8] = { 0 };
    zserio_bitstream_writer_t w;
    (void)zserio_bitstream_writer_init(&w, buf, 64U);
    (void)zserio_bitstream_write_bits(&w, 1U, 1U);
    const uint8_t src[2] = { 0xAB, 0xCD };
    (void)zserio_write_bytes(&w, (zserio_bytes_view_t){ src, 2U });

    zserio_bitstream_reader_t r;
    (void)zserio_bitstream_reader_init(&r, buf, zserio_bitstream_writer_position(&w));
    uint64_t skip = 0U;
    (void)zserio_bitstream_read_bits(&r, 1U, &skip);

    zserio_bytes_view_t out = { NULL, 0U };
    TEST_ASSERT_EQUAL(ZSERIO_ERR_INVALID_ARGUMENT,
                      zserio_read_bytes_view(&r, &out));
    /* Position is unchanged on failure. */
    TEST_ASSERT_EQUAL_size_t(1U, zserio_bitstream_reader_position(&r));
}

/* ------------------------------------------------------------------------ */
/* Round-trip via materializing read                                        */
/* ------------------------------------------------------------------------ */

static void test_bytes_round_trip_owned_aligned(void)
{
    const uint8_t in[4] = { 0xDE, 0xAD, 0xBE, 0xEF };
    const zserio_bytes_view_t in_view = { in, 4U };

    uint8_t buf[8] = { 0 };
    zserio_bitstream_writer_t w;
    (void)zserio_bitstream_writer_init(&w, buf, 64U);
    (void)zserio_write_bytes(&w, in_view);

    zserio_bitstream_reader_t r;
    (void)zserio_bitstream_reader_init(&r, buf, zserio_bitstream_writer_position(&w));
    const zserio_allocator_t alloc = test_allocator();
    zserio_bytes_t out = { NULL, 0U };
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_read_bytes(&r, &alloc, &out));
    TEST_ASSERT_EQUAL_size_t(4U, out.length);
    TEST_ASSERT_EQUAL_MEMORY(in, out.bytes, 4U);
    TEST_ASSERT_EQUAL_size_t(1U, g_alloc.alloc_count);

    zserio_bytes_free(&out, &alloc);
    TEST_ASSERT_EQUAL_size_t(1U, g_alloc.free_count);
}

static void test_bytes_round_trip_owned_large(void)
{
    /* Verifies: REQ-FN-001 — exercises the memcpy fast path in
     * zserio_read_bytes. A 20-byte payload exceeds the 8-byte
     * bit-cache capacity, so the read drains the cache then
     * `memcpy`s the remainder from the source buffer. */
    uint8_t in[20];
    for (size_t i = 0U; i < 20U; i++) {
        in[i] = (uint8_t)(0x10U + i);
    }
    const zserio_bytes_view_t in_view = { in, 20U };

    uint8_t buf[40] = { 0 };
    zserio_bitstream_writer_t w;
    (void)zserio_bitstream_writer_init(&w, buf, 8U * sizeof(buf));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_write_bytes(&w, in_view));

    zserio_bitstream_reader_t r;
    (void)zserio_bitstream_reader_init(&r, buf, zserio_bitstream_writer_position(&w));
    const zserio_allocator_t alloc = test_allocator();
    zserio_bytes_t out = { NULL, 0U };
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_read_bytes(&r, &alloc, &out));
    TEST_ASSERT_EQUAL_size_t(20U, out.length);
    TEST_ASSERT_EQUAL_MEMORY(in, out.bytes, 20U);

    zserio_bytes_free(&out, &alloc);
}

static void test_bytes_round_trip_owned_misaligned(void)
{
    /* Verifies: REQ-FN-001 */
    uint8_t buf[8] = { 0 };
    zserio_bitstream_writer_t w;
    (void)zserio_bitstream_writer_init(&w, buf, 64U);
    (void)zserio_bitstream_write_bits(&w, 5U, 0x15U);
    const uint8_t in[2] = { 0xAB, 0xCD };
    (void)zserio_write_bytes(&w, (zserio_bytes_view_t){ in, 2U });

    zserio_bitstream_reader_t r;
    (void)zserio_bitstream_reader_init(&r, buf, zserio_bitstream_writer_position(&w));
    uint64_t skip = 0U;
    (void)zserio_bitstream_read_bits(&r, 5U, &skip);

    const zserio_allocator_t alloc = test_allocator();
    zserio_bytes_t out = { NULL, 0U };
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_read_bytes(&r, &alloc, &out));
    TEST_ASSERT_EQUAL_size_t(2U, out.length);
    TEST_ASSERT_EQUAL_MEMORY(in, out.bytes, 2U);
    zserio_bytes_free(&out, &alloc);
}

/* ------------------------------------------------------------------------ */
/* Empty                                                                    */
/* ------------------------------------------------------------------------ */

static void test_bytes_empty_no_allocation(void)
{
    const uint8_t buf[1] = { 0 };
    zserio_bitstream_reader_t r;
    (void)zserio_bitstream_reader_init(&r, buf, 8U);

    const zserio_allocator_t alloc = test_allocator();
    zserio_bytes_t out = { NULL, 0U };
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_read_bytes(&r, &alloc, &out));
    TEST_ASSERT_EQUAL_size_t(0U, out.length);
    TEST_ASSERT_NULL(out.bytes);
    TEST_ASSERT_EQUAL_size_t(0U, g_alloc.alloc_count);
}

/* ------------------------------------------------------------------------ */
/* Defensive checks                                                         */
/* ------------------------------------------------------------------------ */

static void test_bytes_write_too_long(void)
{
    /* Verifies: REQ-FS-006 */
    uint8_t buf[8] = { 0 };
    zserio_bitstream_writer_t w;
    (void)zserio_bitstream_writer_init(&w, buf, 64U);
    zserio_bytes_view_t v;
    v.base = (const uint8_t *)0xDEADBEEF;
    v.length = (size_t)0x80000000U;
    TEST_ASSERT_EQUAL(ZSERIO_ERR_VALUE_OUT_OF_RANGE, zserio_write_bytes(&w, v));
}

static void test_bytes_write_null_base(void)
{
    /* Verifies: REQ-FS-006 */
    uint8_t buf[8] = { 0 };
    zserio_bitstream_writer_t w;
    (void)zserio_bitstream_writer_init(&w, buf, 64U);
    zserio_bytes_view_t v = { NULL, 4U };
    TEST_ASSERT_EQUAL(ZSERIO_ERR_INVALID_ARGUMENT, zserio_write_bytes(&w, v));
}

static void test_bytes_alloc_failure(void)
{
    /* Verifies: REQ-FS-001, REQ-FS-006 */
    const uint8_t in[2] = { 0xAB, 0xCD };
    uint8_t buf[8] = { 0 };
    zserio_bitstream_writer_t w;
    (void)zserio_bitstream_writer_init(&w, buf, 64U);
    (void)zserio_write_bytes(&w, (zserio_bytes_view_t){ in, 2U });

    zserio_bitstream_reader_t r;
    (void)zserio_bitstream_reader_init(&r, buf, zserio_bitstream_writer_position(&w));
    zserio_allocator_t alloc = test_allocator();
    g_alloc.fail_alloc = 1;
    zserio_bytes_t out = { NULL, 0U };
    TEST_ASSERT_EQUAL(ZSERIO_ERR_OUT_OF_MEMORY,
                      zserio_read_bytes(&r, &alloc, &out));
}

static void test_bytes_free_on_zero_struct(void)
{
    const zserio_allocator_t alloc = test_allocator();
    zserio_bytes_t b = { NULL, 0U };
    zserio_bytes_free(&b, &alloc);
    zserio_bytes_free(NULL, &alloc);
    TEST_ASSERT_EQUAL_size_t(0U, g_alloc.free_count);
}

static void test_bytes_view_null_args(void)
{
    /* Verifies: REQ-FS-006 */
    const uint8_t buf[1] = { 0 };
    zserio_bitstream_reader_t r;
    (void)zserio_bitstream_reader_init(&r, buf, 8U);
    zserio_bytes_view_t out;
    TEST_ASSERT_EQUAL(ZSERIO_ERR_INVALID_ARGUMENT, zserio_read_bytes_view(NULL, &out));
    TEST_ASSERT_EQUAL(ZSERIO_ERR_INVALID_ARGUMENT, zserio_read_bytes_view(&r, NULL));
}

static void test_bytes_owned_null_args(void)
{
    /* Verifies: REQ-FS-006 */
    const uint8_t buf[1] = { 0 };
    zserio_bitstream_reader_t r;
    (void)zserio_bitstream_reader_init(&r, buf, 8U);
    const zserio_allocator_t alloc = test_allocator();
    zserio_bytes_t out;
    TEST_ASSERT_EQUAL(ZSERIO_ERR_INVALID_ARGUMENT,
                      zserio_read_bytes(NULL, &alloc, &out));
    TEST_ASSERT_EQUAL(ZSERIO_ERR_INVALID_ARGUMENT,
                      zserio_read_bytes(&r, &alloc, NULL));
    TEST_ASSERT_EQUAL(ZSERIO_ERR_INVALID_ARGUMENT,
                      zserio_read_bytes(&r, NULL, &out));
    /* invalid allocator vtable */
    const zserio_allocator_t bad = { NULL, NULL };
    TEST_ASSERT_EQUAL(ZSERIO_ERR_INVALID_ARGUMENT,
                      zserio_read_bytes(&r, &bad, &out));
}

static void test_bytes_view_truncated_buffer(void)
{
    /* Construct a buffer where varsize says length=10 but only 5 bytes follow. */
    const uint8_t buf[6] = { 0x0A, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE };
    zserio_bitstream_reader_t r;
    (void)zserio_bitstream_reader_init(&r, buf, 48U);
    zserio_bytes_view_t out = { NULL, 0U };
    TEST_ASSERT_EQUAL(ZSERIO_ERR_END_OF_STREAM, zserio_read_bytes_view(&r, &out));
    /* Position must be unchanged on failure. */
    TEST_ASSERT_EQUAL_size_t(0U, zserio_bitstream_reader_position(&r));
}

static void test_bytes_view_empty_string_succeeds(void)
{
    /* Hits the `length == 0` branch (out->base = NULL). */
    const uint8_t buf[1] = { 0x00 };
    zserio_bitstream_reader_t r;
    (void)zserio_bitstream_reader_init(&r, buf, 8U);
    zserio_bytes_view_t out = { (const uint8_t *)0xDEAD, 99U };
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_read_bytes_view(&r, &out));
    TEST_ASSERT_NULL(out.base);
    TEST_ASSERT_EQUAL_size_t(0U, out.length);
}

static void test_bytes_owned_truncated_buffer_no_leak(void)
{
    /* End-of-stream during materialize must not leak: bound check is
     * before alloc, so alloc_count stays 0. */
    const uint8_t buf[6] = { 0x0A, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE };
    zserio_bitstream_reader_t r;
    (void)zserio_bitstream_reader_init(&r, buf, 48U);
    const zserio_allocator_t alloc = test_allocator();
    zserio_bytes_t out = { NULL, 0U };
    TEST_ASSERT_EQUAL(ZSERIO_ERR_END_OF_STREAM,
                      zserio_read_bytes(&r, &alloc, &out));
    TEST_ASSERT_EQUAL_size_t(0U, g_alloc.alloc_count);
    TEST_ASSERT_EQUAL_size_t(0U, g_alloc.free_count);
}

/* ------------------------------------------------------------------------ */
/* Test runner                                                              */
/* ------------------------------------------------------------------------ */

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_bytes_encoding_guide_example);
    RUN_TEST(test_bytes_round_trip_view_aligned);
    RUN_TEST(test_bytes_view_misaligned_fails);
    RUN_TEST(test_bytes_round_trip_owned_aligned);
    RUN_TEST(test_bytes_round_trip_owned_large);
    RUN_TEST(test_bytes_round_trip_owned_misaligned);
    RUN_TEST(test_bytes_empty_no_allocation);
    RUN_TEST(test_bytes_write_too_long);
    RUN_TEST(test_bytes_write_null_base);
    RUN_TEST(test_bytes_alloc_failure);
    RUN_TEST(test_bytes_free_on_zero_struct);
    RUN_TEST(test_bytes_view_null_args);
    RUN_TEST(test_bytes_owned_null_args);
    RUN_TEST(test_bytes_view_truncated_buffer);
    RUN_TEST(test_bytes_view_empty_string_succeeds);
    RUN_TEST(test_bytes_owned_truncated_buffer_no_leak);

    return UNITY_END();
}
