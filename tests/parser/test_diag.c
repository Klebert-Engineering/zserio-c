/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Fabian Klebert
 *
 * Unit tests for zsparser/diag.
 */

#include <string.h>

#include "unity.h"

#include "zsparser/diag.h"
#include "zsparser/source.h"

static zserio_allocator_t libc_alloc;

void setUp(void)
{
    libc_alloc = zserio_allocator_libc();
}

void tearDown(void)
{
}

/* Sink that appends every chunk into a fixed-size buffer for
 * inspection. Truncates on overflow without aborting the test —
 * we assert on buffer prefix content. */
typedef struct sink {
    char   buf[2048];
    size_t len;
    bool   truncated;
} sink_t;

static void sink_init(sink_t *s)
{
    s->len = 0U;
    s->truncated = false;
    s->buf[0] = '\0';
}

static void sink_write(void *ud, const char *bytes, size_t len)
{
    sink_t *const s = (sink_t *)ud;
    const size_t avail = sizeof(s->buf) - 1U - s->len;
    if (len > avail) {
        (void)memcpy(&s->buf[s->len], bytes, avail);
        s->len += avail;
        s->truncated = true;
    }
    else {
        (void)memcpy(&s->buf[s->len], bytes, len);
        s->len += len;
    }
    s->buf[s->len] = '\0';
}

static const uint8_t SCHEMA[] =
        "package telemetry;\n"
        "\n"
        "struct Frame {\n"
        "    uint32 sensor_id\n"     /* missing semicolon — line 4 */
        "    string device_label;\n"
        "};\n";
static const size_t SCHEMA_LEN = sizeof(SCHEMA) - 1U;

static void load_schema(zsp_source_t *src, zsp_file_id_t *out_id)
{
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_source_init(src, &libc_alloc));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_source_add_borrowed(
            src, "telemetry.zs", 12U,
            SCHEMA, SCHEMA_LEN, out_id));
}

static void test_init_dispose(void)
{
    zsp_diag_collector_t c;
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_diag_init(&c, &libc_alloc, 0U));
    TEST_ASSERT_EQUAL_size_t(0U, c.count);
    TEST_ASSERT_EQUAL_size_t(ZSP_DIAG_DEFAULT_MAX, c.max);
    TEST_ASSERT_FALSE(c.truncated);
    zsp_diag_dispose(&c);
    TEST_ASSERT_NULL(c.diags);
}

static void test_emit_lit_records_diag(void)
{
    zsp_source_t src;
    zsp_file_id_t id;
    load_schema(&src, &id);

    zsp_diag_collector_t c;
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_diag_init(&c, &libc_alloc, 0U));

    /* Point at the location of "string" on line 5 — we treat the
     * line-4 missing-semicolon scenario as detected at "string". */
    const char *p = strstr((const char *)SCHEMA, "string");
    TEST_ASSERT_NOT_NULL(p);
    const uint32_t off = (uint32_t)(p - (const char *)SCHEMA);

    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_diag_emit_lit(
            &c, ZSP_SEV_ERROR, id, off,
            "expected ';' after field declaration"));
    TEST_ASSERT_EQUAL_size_t(1U, c.count);
    TEST_ASSERT_EQUAL_size_t(1U, zsp_diag_error_count(&c));

    zsp_diag_dispose(&c);
    zsp_source_dispose(&src);
}

static void test_format_gcc_includes_position_and_carat(void)
{
    zsp_source_t src;
    zsp_file_id_t id;
    load_schema(&src, &id);

    zsp_diag_collector_t c;
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_diag_init(&c, &libc_alloc, 0U));

    const char *p = strstr((const char *)SCHEMA, "string");
    const uint32_t off = (uint32_t)(p - (const char *)SCHEMA);
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_diag_emit_lit(
            &c, ZSP_SEV_ERROR, id, off,
            "expected ';' after field declaration"));

    sink_t sink;
    sink_init(&sink);
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_diag_format_gcc(&c, &src,
                                                    sink_write, &sink));

    /* The first line should be the gcc-style header. */
    TEST_ASSERT_NOT_NULL(strstr(sink.buf, "telemetry.zs:5:5: error:"));
    TEST_ASSERT_NOT_NULL(strstr(sink.buf, "expected ';' after field declaration"));
    /* The snippet line should contain the source line. */
    TEST_ASSERT_NOT_NULL(strstr(sink.buf, "5 |     string device_label;"));
    /* The carat line should be present. */
    TEST_ASSERT_NOT_NULL(strstr(sink.buf, "^"));

    zsp_diag_dispose(&c);
    zsp_source_dispose(&src);
}

static void test_format_json(void)
{
    zsp_source_t src;
    zsp_file_id_t id;
    load_schema(&src, &id);

    zsp_diag_collector_t c;
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_diag_init(&c, &libc_alloc, 0U));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_diag_emit_lit(
            &c, ZSP_SEV_ERROR, id, 0U,
            "unexpected \"quote\""));   /* exercise JSON escaping */

    sink_t sink;
    sink_init(&sink);
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_diag_format_json(&c, &src,
                                                     sink_write, &sink));

    TEST_ASSERT_NOT_NULL(strstr(sink.buf, "\"file\":\"telemetry.zs\""));
    TEST_ASSERT_NOT_NULL(strstr(sink.buf, "\"line\":1"));
    TEST_ASSERT_NOT_NULL(strstr(sink.buf, "\"col\":1"));
    TEST_ASSERT_NOT_NULL(strstr(sink.buf, "\"severity\":\"error\""));
    /* JSON-escaped quotes around "quote". */
    TEST_ASSERT_NOT_NULL(strstr(sink.buf, "\\\"quote\\\""));

    zsp_diag_dispose(&c);
    zsp_source_dispose(&src);
}

static void test_cap_truncates_but_counts_all_errors(void)
{
    zsp_source_t src;
    zsp_file_id_t id;
    load_schema(&src, &id);

    zsp_diag_collector_t c;
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_diag_init(&c, &libc_alloc, 3U));

    for (size_t i = 0U; i < 5U; ++i) {
        TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_diag_emit_lit(
                &c, ZSP_SEV_ERROR, id, 0U,
                "synthetic error"));
    }

    TEST_ASSERT_EQUAL_size_t(3U, c.count);                  /* recorded */
    TEST_ASSERT_EQUAL_size_t(5U, zsp_diag_error_count(&c)); /* counted */
    TEST_ASSERT_TRUE(zsp_diag_truncated(&c));

    sink_t sink;
    sink_init(&sink);
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_diag_format_gcc(&c, &src,
                                                    sink_write, &sink));
    TEST_ASSERT_NOT_NULL(strstr(sink.buf, "additional diagnostics suppressed (cap=3)"));

    zsp_diag_dispose(&c);
    zsp_source_dispose(&src);
}

static void test_emit_copy_takes_ownership(void)
{
    zsp_source_t src;
    zsp_file_id_t id;
    load_schema(&src, &id);

    zsp_diag_collector_t c;
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_diag_init(&c, &libc_alloc, 0U));

    char scratch[64];
    (void)memcpy(scratch, "unknown identifier 'Foo'", 24U);
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_diag_emit_copy(
            &c, ZSP_SEV_ERROR, id, 0U, scratch, 24U));

    /* Mutate the source buffer to confirm the diagnostic took its
     * own copy. */
    (void)memset(scratch, 'X', sizeof(scratch));

    TEST_ASSERT_EQUAL_size_t(24U, c.diags[0].message_len);
    TEST_ASSERT_EQUAL_MEMORY("unknown identifier 'Foo'",
                             c.diags[0].message, 24U);

    zsp_diag_dispose(&c);
    zsp_source_dispose(&src);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_init_dispose);
    RUN_TEST(test_emit_lit_records_diag);
    RUN_TEST(test_format_gcc_includes_position_and_carat);
    RUN_TEST(test_format_json);
    RUN_TEST(test_cap_truncates_but_counts_all_errors);
    RUN_TEST(test_emit_copy_takes_ownership);
    return UNITY_END();
}
