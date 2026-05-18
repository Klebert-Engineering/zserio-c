/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Fabian Klebert
 *
 * Acceptance test: parse a curated subset of the upstream
 * zserio test-data corpus and assert every schema parses to
 * zero errors. This is the milestone-7.2 sanity gate; the
 * full 182-schema xvalidation gate runs against the analyser
 * + emitter (later phases).
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "unity.h"

#include "zsparser/arena.h"
#include "zsparser/ast.h"
#include "zsparser/diag.h"
#include "zsparser/parser.h"
#include "zsparser/source.h"

/* Source-tree-relative path resolution. We invoke the test from
 * the build directory; the corpus lives at the configured-in
 * absolute path. ZSP_CORPUS_ROOT is set by the test's CMake
 * target_compile_definitions. */
#ifndef ZSP_CORPUS_ROOT
#define ZSP_CORPUS_ROOT ""
#endif

static zserio_allocator_t libc_alloc;

void setUp(void)  { libc_alloc = zserio_allocator_libc(); }
void tearDown(void) {}

/* Load a file's bytes via libc — host-side test code only, not
 * the parser library. Returns a malloc'd buffer; caller frees. */
static uint8_t *read_file(const char *path, size_t *out_len)
{
    FILE *const f = fopen(path, "rb");
    if (f == NULL) {
        return NULL;
    }
    if (fseek(f, 0L, SEEK_END) != 0) {
        (void)fclose(f);
        return NULL;
    }
    const long sz = ftell(f);
    if ((sz < 0) || (fseek(f, 0L, SEEK_SET) != 0)) {
        (void)fclose(f);
        return NULL;
    }
    uint8_t *const buf = (uint8_t *)malloc((size_t)sz + 1U);
    if (buf == NULL) {
        (void)fclose(f);
        return NULL;
    }
    const size_t got = fread(buf, 1U, (size_t)sz, f);
    (void)fclose(f);
    if (got != (size_t)sz) {
        free(buf);
        return NULL;
    }
    buf[sz] = (uint8_t)'\0';
    *out_len = (size_t)sz;
    return buf;
}

/* Format diagnostics from `diag` into a string sink so we can
 * print them on test failure. */
typedef struct sink {
    char   buf[4096];
    size_t len;
} sink_t;

static void sink_write(void *ud, const char *bytes, size_t len)
{
    sink_t *const s = (sink_t *)ud;
    const size_t avail = sizeof(s->buf) - 1U - s->len;
    const size_t to    = (len < avail) ? len : avail;
    (void)memcpy(&s->buf[s->len], bytes, to);
    s->len += to;
    s->buf[s->len] = '\0';
}

static void parse_and_assert_ok(const char *rel_path)
{
    char abs_path[1024];
    const int n = snprintf(abs_path, sizeof(abs_path), "%s/%s",
                           ZSP_CORPUS_ROOT, rel_path);
    if ((n < 0) || ((size_t)n >= sizeof(abs_path))) {
        TEST_FAIL_MESSAGE("path too long");
        return;
    }
    size_t len = 0U;
    uint8_t *const bytes = read_file(abs_path, &len);
    if (bytes == NULL) {
        /* Missing files mean the submodule isn't initialised — skip
         * gracefully rather than failing the test suite. */
        char msg[1200];
        (void)snprintf(msg, sizeof(msg),
                       "skipping: cannot read %s (submodule may be uninitialised)",
                       abs_path);
        TEST_IGNORE_MESSAGE(msg);
        return;
    }

    zsp_source_t         src;
    zsp_arena_t          arena;
    zsp_diag_collector_t diag;
    zsp_file_id_t        fid;
    zsp_ast_node_t      *unit = NULL;

    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_source_init(&src, &libc_alloc));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_arena_init(&arena, &libc_alloc));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_diag_init(&diag, &libc_alloc, 0U));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_source_add_borrowed(
            &src, rel_path, strlen(rel_path), bytes, len, &fid));

    const zserio_status_t st = zsp_parse_unit(&src, fid, &arena, &diag, &unit);
    TEST_ASSERT_EQUAL(ZSERIO_OK, st);
    TEST_ASSERT_NOT_NULL(unit);

    if (zsp_diag_error_count(&diag) != 0U) {
        sink_t sink;
        sink.len = 0U;
        sink.buf[0] = '\0';
        (void)zsp_diag_format_gcc(&diag, &src, sink_write, &sink);
        TEST_FAIL_MESSAGE(sink.buf);
    }

    zsp_diag_dispose(&diag);
    zsp_arena_dispose(&arena);
    zsp_source_dispose(&src);
    free(bytes);
}

static void test_corpus_builtin_types(void)
{ parse_and_assert_ok("language/builtin_types/zs/builtin_types.zs"); }
static void test_corpus_constants(void)
{ parse_and_assert_ok("language/constants/zs/constants.zs"); }
static void test_corpus_enumeration_types(void)
{ parse_and_assert_ok("language/enumeration_types/zs/enumeration_types.zs"); }
static void test_corpus_bitmask_types(void)
{ parse_and_assert_ok("language/bitmask_types/zs/bitmask_types.zs"); }
static void test_corpus_subtypes(void)
{ parse_and_assert_ok("language/subtypes/zs/subtypes.zs"); }
static void test_corpus_structure_types(void)
{ parse_and_assert_ok("language/structure_types/zs/structure_types.zs"); }
static void test_corpus_array_types(void)
{ parse_and_assert_ok("language/array_types/zs/array_types.zs"); }
static void test_corpus_optional_members(void)
{ parse_and_assert_ok("language/optional_members/zs/optional_members.zs"); }
static void test_corpus_parameterized_types(void)
{ parse_and_assert_ok("language/parameterized_types/zs/parameterized_types.zs"); }
static void test_corpus_choice_types(void)
{ parse_and_assert_ok("language/choice_types/zs/choice_types.zs"); }
static void test_corpus_union_types(void)
{ parse_and_assert_ok("language/union_types/zs/union_types.zs"); }
static void test_corpus_alignment(void)
{ parse_and_assert_ok("language/alignment/zs/alignment.zs"); }
static void test_corpus_constraints(void)
{ parse_and_assert_ok("language/constraints/zs/constraints.zs"); }
static void test_corpus_functions(void)
{ parse_and_assert_ok("language/functions/zs/functions.zs"); }
static void test_corpus_templates(void)
{ parse_and_assert_ok("language/templates/zs/templates.zs"); }
static void test_corpus_literals(void)
{ parse_and_assert_ok("language/literals/zs/literals.zs"); }
static void test_corpus_service_types(void)
{ parse_and_assert_ok("language/service_types/zs/service_types.zs"); }
static void test_corpus_sql_tables(void)
{ parse_and_assert_ok("language/sql_tables/zs/sql_tables.zs"); }
static void test_corpus_sql_databases(void)
{ parse_and_assert_ok("language/sql_databases/zs/sql_databases.zs"); }
static void test_corpus_default_values(void)
{ parse_and_assert_ok("language/default_values/zs/default_values.zs"); }
static void test_corpus_member_access(void)
{ parse_and_assert_ok("language/member_access/zs/member_access.zs"); }
static void test_corpus_identifiers(void)
{ parse_and_assert_ok("language/identifiers/zs/identifiers.zs"); }
static void test_corpus_extended_members(void)
{ parse_and_assert_ok("language/extended_members/zs/extended_members.zs"); }
static void test_corpus_offsets(void)
{ parse_and_assert_ok("language/offsets/zs/offsets.zs"); }

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_corpus_builtin_types);
    RUN_TEST(test_corpus_constants);
    RUN_TEST(test_corpus_enumeration_types);
    RUN_TEST(test_corpus_bitmask_types);
    RUN_TEST(test_corpus_subtypes);
    RUN_TEST(test_corpus_structure_types);
    RUN_TEST(test_corpus_array_types);
    RUN_TEST(test_corpus_optional_members);
    RUN_TEST(test_corpus_parameterized_types);
    RUN_TEST(test_corpus_choice_types);
    RUN_TEST(test_corpus_union_types);
    RUN_TEST(test_corpus_alignment);
    RUN_TEST(test_corpus_constraints);
    RUN_TEST(test_corpus_functions);
    RUN_TEST(test_corpus_templates);
    RUN_TEST(test_corpus_literals);
    RUN_TEST(test_corpus_service_types);
    RUN_TEST(test_corpus_sql_tables);
    RUN_TEST(test_corpus_sql_databases);
    RUN_TEST(test_corpus_default_values);
    RUN_TEST(test_corpus_member_access);
    RUN_TEST(test_corpus_identifiers);
    RUN_TEST(test_corpus_extended_members);
    RUN_TEST(test_corpus_offsets);
    return UNITY_END();
}
