/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Fabian Klebert
 *
 * Parser robustness against adversarial / malformed schema input.
 * Each test feeds the parser a pathological construct and asserts it
 * TERMINATES (the test process completing is itself the assertion —
 * a regression would hang or crash) and, where applicable, reports a
 * diagnostic rather than corrupting state.
 *
 * Coverage:
 *   - Deeply nested parenthesised / unary expressions and nested
 *     template-argument types are rejected past a fixed depth instead
 *     of recursing until the C stack is exhausted (REQ-FS-003).
 *   - A malformed `rule_group` body cannot spin the body loop forever
 *     (REQ-FS-005 — all loops bounded).
 *   - A float literal with an enormous exponent neither overflows the
 *     exponent accumulator nor drives a multi-billion-iteration
 *     scaling loop.
 *
 * Verifies: REQ-FS-003 — bounded parser recursion.
 * Verifies: REQ-FS-005 — every parser loop has a bounded trip count.
 */

#define _POSIX_C_SOURCE 200809L

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "unity.h"

#include "zsparser/arena.h"
#include "zsparser/ast.h"
#include "zsparser/diag.h"
#include "zsparser/parser.h"
#include "zsparser/sema.h"
#include "zsparser/source.h"

static zserio_allocator_t libc_alloc;

void setUp(void)    { libc_alloc = zserio_allocator_libc(); }
void tearDown(void) {}

/* Parse `schema` (no semantic analysis). Returns the parse status and,
 * via *err_out, the number of diagnostics emitted. Always disposes. */
static zserio_status_t parse_only(const char *schema, size_t len,
                                  size_t *err_out)
{
    zsp_source_t         src;
    zsp_arena_t          arena;
    zsp_diag_collector_t diag;
    zsp_file_id_t        fid;
    zsp_ast_node_t      *unit = NULL;

    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_source_init(&src, &libc_alloc));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_arena_init(&arena, &libc_alloc));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_diag_init(&diag, &libc_alloc, 0U));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_source_add_borrowed(
            &src, "adversarial.zs", 14U, (const uint8_t *)schema, len, &fid));

    const zserio_status_t st = zsp_parse_unit(&src, fid, &arena, &diag, &unit);
    if (err_out != NULL) {
        *err_out = zsp_diag_error_count(&diag);
    }

    zsp_diag_dispose(&diag);
    zsp_arena_dispose(&arena);
    zsp_source_dispose(&src);
    return st;
}

/* Run the full front end (parse → analyse → instantiate templates) on
 * `schema`. Returns the diagnostic error count via *err_out. The point
 * is that the call RETURNS at all — a recursive template would
 * otherwise recurse/allocate without bound. */
static void analyse_and_instantiate(const char *schema, size_t len,
                                    size_t *err_out)
{
    zsp_source_t         src;
    zsp_arena_t          arena;
    zsp_diag_collector_t diag;
    zsp_file_id_t        fid;
    zsp_ast_node_t      *unit = NULL;
    zsp_sema_t           sema;

    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_source_init(&src, &libc_alloc));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_arena_init(&arena, &libc_alloc));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_diag_init(&diag, &libc_alloc, 0U));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_source_add_borrowed(
            &src, "adversarial.zs", 14U, (const uint8_t *)schema, len, &fid));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_parse_unit(&src, fid, &arena, &diag, &unit));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_sema_init(&sema, &src, &arena, &diag));
    (void)zsp_sema_analyse_unit(&sema, unit);
    (void)zsp_sema_instantiate_templates(&sema, unit);
    if (err_out != NULL) {
        *err_out = zsp_diag_error_count(&diag);
    }
    zsp_diag_dispose(&diag);
    zsp_arena_dispose(&arena);
    zsp_source_dispose(&src);
}

/* Build "<prefix>" + open*n + "<mid>" + close*n + "<suffix>".
 * Caller frees. */
static char *build_nested(const char *prefix, const char *open, size_t n,
                          const char *mid, const char *close,
                          const char *suffix, size_t *len_out)
{
    const size_t pl = strlen(prefix);
    const size_t ol = strlen(open);
    const size_t ml = strlen(mid);
    const size_t cl = strlen(close);
    const size_t sl = strlen(suffix);
    const size_t total = pl + (n * ol) + ml + (n * cl) + sl;
    char *const buf = (char *)malloc(total + 1U);
    TEST_ASSERT_NOT_NULL(buf);
    size_t pos = 0U;
    (void)memcpy(&buf[pos], prefix, pl); pos += pl;
    for (size_t i = 0U; i < n; ++i) { (void)memcpy(&buf[pos], open, ol); pos += ol; }
    (void)memcpy(&buf[pos], mid, ml); pos += ml;
    for (size_t i = 0U; i < n; ++i) { (void)memcpy(&buf[pos], close, cl); pos += cl; }
    (void)memcpy(&buf[pos], suffix, sl); pos += sl;
    buf[pos] = '\0';
    if (len_out != NULL) *len_out = pos;
    return buf;
}

/* ------------------------------------------------------------------ */
/* Tests                                                              */
/* ------------------------------------------------------------------ */

/* 4000 nested parentheses — far past the depth cap. Must terminate
 * with a diagnostic, not overflow the stack. */
static void test_deep_paren_nesting_terminates(void)
{
    size_t len = 0U;
    char *const schema = build_nested(
            "package p;\nconst int32 X = ", "(", 4000U, "1", ")", ";\n", &len);
    size_t errs = 0U;
    (void)parse_only(schema, len, &errs);
    TEST_ASSERT_GREATER_THAN_size_t(0U, errs);
    free(schema);
}

/* 4000 stacked unary `!` operators recurse through parse_primary. */
static void test_deep_unary_nesting_terminates(void)
{
    size_t len = 0U;
    char *const schema = build_nested(
            "package p;\nconst int32 X = ", "!", 4000U, "1", "", ";\n", &len);
    size_t errs = 0U;
    (void)parse_only(schema, len, &errs);
    TEST_ASSERT_GREATER_THAN_size_t(0U, errs);
    free(schema);
}

/* 4000 nested template arguments recurse through parse_type. */
static void test_deep_template_nesting_terminates(void)
{
    size_t len = 0U;
    char *const schema = build_nested(
            "package p;\nstruct S { ", "Foo<", 4000U, "uint32", ">", " f; };\n",
            &len);
    size_t errs = 0U;
    (void)parse_only(schema, len, &errs);
    TEST_ASSERT_GREATER_THAN_size_t(0U, errs);
    free(schema);
}

/* A rule_group body that starts with a token that begins neither a
 * rule, an expression, nor a `;` must not spin the body loop. The
 * test completing is the assertion; a regression hangs here. */
static void test_malformed_rule_group_terminates(void)
{
    static const char SCHEMA[] = "package p;\nrule_group G { , };\n";
    size_t errs = 0U;
    (void)parse_only(SCHEMA, sizeof(SCHEMA) - 1U, &errs);
    TEST_ASSERT_GREATER_THAN_size_t(0U, errs);
}

/* A second malformed body, this time with a run of stray punctuation,
 * to exercise repeated force-advances through the body loop. */
static void test_malformed_rule_group_run_terminates(void)
{
    static const char SCHEMA[] =
            "package p;\nrule_group G { ) ] = , % };\n";
    size_t errs = 0U;
    (void)parse_only(SCHEMA, sizeof(SCHEMA) - 1U, &errs);
    TEST_ASSERT_GREATER_THAN_size_t(0U, errs);
}

/* A float literal with a gigantic exponent must lex in bounded time
 * (no exponent-accumulator overflow, no ~2e9-iteration scaling loop).
 * Reaching the assertion at all proves termination. */
static void test_huge_float_exponent_terminates(void)
{
    static const char SCHEMA[] =
            "package p;\nconst float64 X = 1e999999999;\n";
    size_t errs = 0U;
    const zserio_status_t st = parse_only(SCHEMA, sizeof(SCHEMA) - 1U, &errs);
    TEST_ASSERT_EQUAL(ZSERIO_OK, st);
    TEST_ASSERT_EQUAL_size_t(0U, errs);
}

/* A self-referential template expands to a structurally larger
 * instantiation at every level, which never hits the memo cache.
 * Instantiation must stop at the depth cap with a diagnostic rather
 * than recursing and allocating without bound. Completing the call is
 * the core assertion. */
static void test_recursive_template_instantiation_terminates(void)
{
    static const char SCHEMA[] =
            "package p;\n"
            "struct Foo<T> { Foo<Foo<T>> inner; };\n"
            "struct Root { Foo<uint32> r; };\n";
    size_t errs = 0U;
    analyse_and_instantiate(SCHEMA, sizeof(SCHEMA) - 1U, &errs);
    TEST_ASSERT_GREATER_THAN_size_t(0U, errs);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_deep_paren_nesting_terminates);
    RUN_TEST(test_deep_unary_nesting_terminates);
    RUN_TEST(test_deep_template_nesting_terminates);
    RUN_TEST(test_malformed_rule_group_terminates);
    RUN_TEST(test_malformed_rule_group_run_terminates);
    RUN_TEST(test_huge_float_exponent_terminates);
    RUN_TEST(test_recursive_template_instantiation_terminates);
    return UNITY_END();
}
