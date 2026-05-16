/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Fabian Klebert
 *
 * Semantic-analyser tests: drive parse → analyse and check the
 * `resolved` annotations + diagnostic emission.
 */

#include <string.h>

#include "unity.h"

#include "zsparser/arena.h"
#include "zsparser/ast.h"
#include "zsparser/diag.h"
#include "zsparser/parser.h"
#include "zsparser/sema.h"
#include "zsparser/source.h"

static zserio_allocator_t libc_alloc;

void setUp(void)  { libc_alloc = zserio_allocator_libc(); }
void tearDown(void) {}

typedef struct fixture {
    zsp_source_t         src;
    zsp_arena_t          arena;
    zsp_diag_collector_t diag;
    zsp_file_id_t        fid;
    zsp_ast_node_t      *unit;
    zsp_sema_t           sema;
} fixture_t;

static void fixture_setup(fixture_t *f, const uint8_t *bytes, size_t len)
{
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_source_init(&f->src, &libc_alloc));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_arena_init(&f->arena, &libc_alloc));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_diag_init(&f->diag, &libc_alloc, 0U));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_source_add_borrowed(
            &f->src, "test.zs", 7U, bytes, len, &f->fid));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_parse_unit(
            &f->src, f->fid, &f->arena, &f->diag, &f->unit));
    TEST_ASSERT_EQUAL_size_t(0U, zsp_diag_error_count(&f->diag));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_sema_init(&f->sema, &f->src,
                                              &f->arena, &f->diag));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_sema_analyse_unit(&f->sema, f->unit));
}

static void fixture_teardown(fixture_t *f)
{
    zsp_diag_dispose(&f->diag);
    zsp_arena_dispose(&f->arena);
    zsp_source_dispose(&f->src);
}

static void test_top_level_binding(void)
{
    fixture_t f;
    static const uint8_t INPUT[] =
            "package x;\n"
            "struct Frame { uint32 id; };\n"
            "enum uint8 Color { RED = 0 };\n";
    fixture_setup(&f, INPUT, sizeof(INPUT) - 1U);

    TEST_ASSERT_EQUAL_size_t(0U, zsp_diag_error_count(&f.diag));
    TEST_ASSERT_NOT_NULL(zsp_sema_scope_find(f.sema.root,
                            (const uint8_t *)"Frame", 5U));
    TEST_ASSERT_NOT_NULL(zsp_sema_scope_find(f.sema.root,
                            (const uint8_t *)"Color", 5U));
    TEST_ASSERT_NULL(zsp_sema_scope_find(f.sema.root,
                            (const uint8_t *)"Nope", 4U));
    fixture_teardown(&f);
}

static void test_duplicate_declaration_is_diagnosed(void)
{
    fixture_t f;
    static const uint8_t INPUT[] =
            "package x;\n"
            "struct Frame { uint32 a; };\n"
            "struct Frame { uint32 b; };\n";
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_source_init(&f.src, &libc_alloc));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_arena_init(&f.arena, &libc_alloc));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_diag_init(&f.diag, &libc_alloc, 0U));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_source_add_borrowed(
            &f.src, "test.zs", 7U, INPUT, sizeof(INPUT) - 1U, &f.fid));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_parse_unit(
            &f.src, f.fid, &f.arena, &f.diag, &f.unit));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_sema_init(&f.sema, &f.src,
                                              &f.arena, &f.diag));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_sema_analyse_unit(&f.sema, f.unit));
    TEST_ASSERT_EQUAL_size_t(1U, zsp_diag_error_count(&f.diag));

    fixture_teardown(&f);
}

static void test_unresolved_type_reference_is_diagnosed(void)
{
    fixture_t f;
    static const uint8_t INPUT[] =
            "package x;\n"
            "struct Holder { UnknownType field; };\n";
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_source_init(&f.src, &libc_alloc));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_arena_init(&f.arena, &libc_alloc));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_diag_init(&f.diag, &libc_alloc, 0U));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_source_add_borrowed(
            &f.src, "test.zs", 7U, INPUT, sizeof(INPUT) - 1U, &f.fid));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_parse_unit(
            &f.src, f.fid, &f.arena, &f.diag, &f.unit));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_sema_init(&f.sema, &f.src,
                                              &f.arena, &f.diag));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_sema_analyse_unit(&f.sema, f.unit));
    TEST_ASSERT_EQUAL_size_t(1U, zsp_diag_error_count(&f.diag));
    fixture_teardown(&f);
}

static void test_type_ref_resolution(void)
{
    fixture_t f;
    static const uint8_t INPUT[] =
            "package x;\n"
            "struct Inner { uint32 v; };\n"
            "struct Outer { Inner inner_field; };\n";
    fixture_setup(&f, INPUT, sizeof(INPUT) - 1U);

    /* Walk Outer's first field and check that its type ref is
     * resolved to the Inner struct decl. */
    zsp_ast_node_t *const outer = f.unit->u.unit.decls[1];
    TEST_ASSERT_EQUAL_INT(ZSP_AST_STRUCT, outer->kind);
    zsp_ast_node_t *const field = outer->u.compound.members[0];
    TEST_ASSERT_EQUAL_INT(ZSP_AST_FIELD, field->kind);
    zsp_ast_node_t *const ty = field->u.field.type;
    TEST_ASSERT_EQUAL_INT(ZSP_AST_TYPE_REF, ty->kind);
    TEST_ASSERT_NOT_NULL(ty->resolved);
    TEST_ASSERT_EQUAL_INT(ZSP_AST_STRUCT, ty->resolved->kind);
    /* The resolved decl is the Inner struct. */
    TEST_ASSERT_EQUAL_PTR(f.unit->u.unit.decls[0], ty->resolved);

    fixture_teardown(&f);
}

static void test_resolve_across_decl_order(void)
{
    /* `Outer` references `Inner` declared LATER in the file. The
     * symbol table is built in a separate pass before resolution,
     * so forward references must work. */
    fixture_t f;
    static const uint8_t INPUT[] =
            "package x;\n"
            "struct Outer { Inner field; };\n"
            "struct Inner { uint32 v; };\n";
    fixture_setup(&f, INPUT, sizeof(INPUT) - 1U);

    zsp_ast_node_t *const outer = f.unit->u.unit.decls[0];
    zsp_ast_node_t *const ty    = outer->u.compound.members[0]->u.field.type;
    TEST_ASSERT_NOT_NULL(ty->resolved);
    TEST_ASSERT_EQUAL_PTR(f.unit->u.unit.decls[1], ty->resolved);
    fixture_teardown(&f);
}

static void test_builtins_are_not_type_refs(void)
{
    /* uint32, string, etc. lex as identifiers but parse as
     * TYPE_BUILTIN — they don't reach the sema's type-ref
     * resolution and don't trigger an unresolved-name diagnostic. */
    fixture_t f;
    static const uint8_t INPUT[] =
            "package x;\n"
            "struct Frame {\n"
            "    uint32 a;\n"
            "    string b;\n"
            "    varuint c;\n"
            "    float32 d;\n"
            "};\n";
    fixture_setup(&f, INPUT, sizeof(INPUT) - 1U);
    TEST_ASSERT_EQUAL_size_t(0U, zsp_diag_error_count(&f.diag));
    fixture_teardown(&f);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_top_level_binding);
    RUN_TEST(test_duplicate_declaration_is_diagnosed);
    RUN_TEST(test_unresolved_type_reference_is_diagnosed);
    RUN_TEST(test_type_ref_resolution);
    RUN_TEST(test_resolve_across_decl_order);
    RUN_TEST(test_builtins_are_not_type_refs);
    return UNITY_END();
}
