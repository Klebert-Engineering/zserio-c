/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Fabian Klebert
 *
 * Unit tests for zsparser/source.
 *
 * Verifies: REQ-FN-005 — import resolution rules
 *                        (source loading and parse-once-by-canonical-path).
 */

#include <string.h>

#include "unity.h"

#include "zsparser/source.h"

static zserio_allocator_t libc_alloc;

void setUp(void)
{
    libc_alloc = zserio_allocator_libc();
}

void tearDown(void)
{
}

static const uint8_t SCHEMA[] =
        "package telemetry;\n"
        "\n"
        "struct Frame {\n"
        "    uint32 sensor_id;\n"
        "    string device_label;\n"
        "};\n";

static const size_t SCHEMA_LEN = sizeof(SCHEMA) - 1U;

static void test_init_dispose_clean(void)
{
    zsp_source_t src;
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_source_init(&src, &libc_alloc));
    TEST_ASSERT_EQUAL_size_t(0U, src.count);
    TEST_ASSERT_NOT_NULL(src.files);
    TEST_ASSERT_TRUE(src.cap >= ZSP_SOURCE_INITIAL_CAP);
    zsp_source_dispose(&src);
    TEST_ASSERT_NULL(src.files);
    TEST_ASSERT_EQUAL_size_t(0U, src.count);
}

static void test_init_rejects_null(void)
{
    TEST_ASSERT_EQUAL(ZSERIO_ERR_INVALID_ARGUMENT,
                      zsp_source_init(NULL, &libc_alloc));
    zsp_source_t src;
    TEST_ASSERT_EQUAL(ZSERIO_ERR_INVALID_ARGUMENT,
                      zsp_source_init(&src, NULL));
}

static void test_add_borrowed_buffer(void)
{
    zsp_source_t src;
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_source_init(&src, &libc_alloc));

    zsp_file_id_t id = ZSP_FILE_ID_INVALID;
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_source_add_borrowed(
            &src, "telemetry.zs", 12U,
            SCHEMA, SCHEMA_LEN, &id));
    TEST_ASSERT_EQUAL_UINT16(0U, id);
    TEST_ASSERT_EQUAL_size_t(1U, src.count);

    const zsp_file_t *f = NULL;
    TEST_ASSERT_TRUE(zsp_source_get(&src, id, &f));
    TEST_ASSERT_EQUAL_size_t(SCHEMA_LEN, f->len);
    TEST_ASSERT_EQUAL_PTR(SCHEMA, f->bytes);

    zsp_source_dispose(&src);
}

static void test_line_col_recovery(void)
{
    zsp_source_t src;
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_source_init(&src, &libc_alloc));
    zsp_file_id_t id = ZSP_FILE_ID_INVALID;
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_source_add_borrowed(
            &src, "telemetry.zs", 12U,
            SCHEMA, SCHEMA_LEN, &id));

    uint32_t line = 0U;
    uint32_t col  = 0U;

    /* Offset 0 = start of "package" on line 1. */
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_source_line_col(&src, id, 0U, &line, &col));
    TEST_ASSERT_EQUAL_UINT32(1U, line);
    TEST_ASSERT_EQUAL_UINT32(1U, col);

    /* Offset of "struct" — the third line, column 1. */
    const char *p = strstr((const char *)SCHEMA, "struct");
    TEST_ASSERT_NOT_NULL(p);
    const uint32_t struct_off = (uint32_t)(p - (const char *)SCHEMA);
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_source_line_col(&src, id, struct_off,
                                                    &line, &col));
    TEST_ASSERT_EQUAL_UINT32(3U, line);
    TEST_ASSERT_EQUAL_UINT32(1U, col);

    /* Offset of "sensor_id" — line 4, column 12 (4 spaces + "uint32" + space). */
    const char *q = strstr((const char *)SCHEMA, "sensor_id");
    TEST_ASSERT_NOT_NULL(q);
    const uint32_t sensor_off = (uint32_t)(q - (const char *)SCHEMA);
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_source_line_col(&src, id, sensor_off,
                                                    &line, &col));
    TEST_ASSERT_EQUAL_UINT32(4U, line);
    TEST_ASSERT_EQUAL_UINT32(12U, col);

    /* EOF position resolves to the last line, last col + 1. */
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_source_line_col(&src, id, (uint32_t)SCHEMA_LEN,
                                                    &line, &col));
    TEST_ASSERT_EQUAL_UINT32(7U, line);
    TEST_ASSERT_EQUAL_UINT32(1U, col);

    zsp_source_dispose(&src);
}

static void test_line_text_strips_newline(void)
{
    zsp_source_t src;
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_source_init(&src, &libc_alloc));
    zsp_file_id_t id = ZSP_FILE_ID_INVALID;
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_source_add_borrowed(
            &src, "telemetry.zs", 12U,
            SCHEMA, SCHEMA_LEN, &id));

    const uint8_t *bytes = NULL;
    size_t         len   = 0U;
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_source_line_text(&src, id, 1U,
                                                     &bytes, &len));
    TEST_ASSERT_EQUAL_size_t(18U, len);  /* "package telemetry;" */
    TEST_ASSERT_EQUAL_MEMORY("package telemetry;", bytes, 18U);

    /* Line 4 contains the sensor_id declaration with 4-space indent. */
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_source_line_text(&src, id, 4U,
                                                     &bytes, &len));
    TEST_ASSERT_EQUAL_MEMORY("    uint32 sensor_id;", bytes, 21U);

    zsp_source_dispose(&src);
}

static void test_line_text_out_of_range(void)
{
    zsp_source_t src;
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_source_init(&src, &libc_alloc));
    zsp_file_id_t id = ZSP_FILE_ID_INVALID;
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_source_add_borrowed(
            &src, "telemetry.zs", 12U,
            SCHEMA, SCHEMA_LEN, &id));

    const uint8_t *bytes = NULL;
    size_t         len   = 0U;
    TEST_ASSERT_EQUAL(ZSERIO_ERR_VALUE_OUT_OF_RANGE,
                      zsp_source_line_text(&src, id, 0U, &bytes, &len));
    TEST_ASSERT_EQUAL(ZSERIO_ERR_VALUE_OUT_OF_RANGE,
                      zsp_source_line_text(&src, id, 999U, &bytes, &len));

    zsp_source_dispose(&src);
}

static void test_get_rejects_invalid_id(void)
{
    zsp_source_t src;
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_source_init(&src, &libc_alloc));
    const zsp_file_t *f = NULL;
    TEST_ASSERT_FALSE(zsp_source_get(&src, ZSP_FILE_ID_INVALID, &f));
    TEST_ASSERT_FALSE(zsp_source_get(&src, (zsp_file_id_t)42U, &f));
    zsp_source_dispose(&src);
}

static void test_multiple_files(void)
{
    zsp_source_t src;
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_source_init(&src, &libc_alloc));

    static const uint8_t A[] = "package a;\n";
    static const uint8_t B[] = "package b;\nstruct X {};\n";

    zsp_file_id_t id_a = ZSP_FILE_ID_INVALID;
    zsp_file_id_t id_b = ZSP_FILE_ID_INVALID;
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_source_add_borrowed(
            &src, "a.zs", 4U,
            A, sizeof(A) - 1U, &id_a));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_source_add_borrowed(
            &src, "b.zs", 4U,
            B, sizeof(B) - 1U, &id_b));
    TEST_ASSERT_NOT_EQUAL(id_a, id_b);

    uint32_t line = 0U;
    uint32_t col  = 0U;
    TEST_ASSERT_EQUAL(ZSERIO_OK,
                      zsp_source_line_col(&src, id_b, 11U, &line, &col));
    TEST_ASSERT_EQUAL_UINT32(2U, line);   /* "struct X {};" */
    TEST_ASSERT_EQUAL_UINT32(1U, col);

    zsp_source_dispose(&src);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_init_dispose_clean);
    RUN_TEST(test_init_rejects_null);
    RUN_TEST(test_add_borrowed_buffer);
    RUN_TEST(test_line_col_recovery);
    RUN_TEST(test_line_text_strips_newline);
    RUN_TEST(test_line_text_out_of_range);
    RUN_TEST(test_get_rejects_invalid_id);
    RUN_TEST(test_multiple_files);
    return UNITY_END();
}
