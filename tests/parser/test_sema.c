/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Klebert Engineering
 *
 * Semantic-analyzer tests: drive parse → analyze and check the
 * `resolved` annotations + diagnostic emission.
 *
 * Verifies: REQ-FN-002 — parser accepts the schema language
 *                        (semantic validation of resolved schemas).
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
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_sema_analyze_unit(&f->sema, f->unit));
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
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_sema_analyze_unit(&f.sema, f.unit));
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
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_sema_analyze_unit(&f.sema, f.unit));
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

static void test_array_length_field_reference_resolves(void)
{
    /* `int32 items[count]` — `count` must resolve to the field
     * declared earlier in the same struct. */
    fixture_t f;
    static const uint8_t INPUT[] =
            "package x;\n"
            "struct List {\n"
            "    uint16 count;\n"
            "    int32 items[count];\n"
            "};\n";
    fixture_setup(&f, INPUT, sizeof(INPUT) - 1U);

    zsp_ast_node_t *const items = f.unit->u.unit.decls[0]->u.compound.members[1];
    zsp_ast_node_t *const arr   = items->u.field.type;
    TEST_ASSERT_EQUAL_INT(ZSP_AST_TYPE_ARRAY, arr->kind);
    zsp_ast_node_t *const len_expr = arr->u.type_array.length_expr;
    TEST_ASSERT_NOT_NULL(len_expr);
    TEST_ASSERT_EQUAL_INT(ZSP_AST_EXPR_IDENT, len_expr->kind);
    TEST_ASSERT_NOT_NULL(len_expr->resolved);
    /* Resolves to the count field. */
    zsp_ast_node_t *const count_field = f.unit->u.unit.decls[0]->u.compound.members[0];
    TEST_ASSERT_EQUAL_PTR(count_field, len_expr->resolved);

    fixture_teardown(&f);
}

static void test_struct_parameter_resolves(void)
{
    fixture_t f;
    static const uint8_t INPUT[] =
            "package x;\n"
            "struct Point(uint16 shift) {\n"
            "    int32 x;\n"
            "    int8 z if shift == 0;\n"
            "};\n";
    fixture_setup(&f, INPUT, sizeof(INPUT) - 1U);

    zsp_ast_node_t *const z_field  = f.unit->u.unit.decls[0]->u.compound.members[1];
    zsp_ast_node_t *const if_expr  = z_field->u.field.if_clause;
    TEST_ASSERT_NOT_NULL(if_expr);
    TEST_ASSERT_EQUAL_INT(ZSP_AST_EXPR_BINARY, if_expr->kind);
    zsp_ast_node_t *const lhs = if_expr->u.binop.lhs;
    TEST_ASSERT_EQUAL_INT(ZSP_AST_EXPR_IDENT, lhs->kind);
    TEST_ASSERT_NOT_NULL(lhs->resolved);
    TEST_ASSERT_EQUAL_INT(ZSP_AST_PARAMETER, lhs->resolved->kind);
    fixture_teardown(&f);
}

static void test_enum_item_value_can_reference_prior(void)
{
    fixture_t f;
    static const uint8_t INPUT[] =
            "package x;\n"
            "enum uint8 E {\n"
            "    A = 1,\n"
            "    B = A + 1\n"
            "};\n";
    fixture_setup(&f, INPUT, sizeof(INPUT) - 1U);

    zsp_ast_node_t *const enm = f.unit->u.unit.decls[0];
    zsp_ast_node_t *const b   = enm->u.enum_decl.items[1];
    zsp_ast_node_t *const expr = b->u.enum_item.value_expr;
    TEST_ASSERT_EQUAL_INT(ZSP_AST_EXPR_BINARY, expr->kind);
    zsp_ast_node_t *const lhs = expr->u.binop.lhs;
    TEST_ASSERT_EQUAL_INT(ZSP_AST_EXPR_IDENT, lhs->kind);
    TEST_ASSERT_NOT_NULL(lhs->resolved);
    TEST_ASSERT_EQUAL_PTR(enm->u.enum_decl.items[0], lhs->resolved);

    fixture_teardown(&f);
}

static void test_index_pseudo_ident_does_not_emit_unresolved(void)
{
    fixture_t f;
    static const uint8_t INPUT[] =
            "package x;\n"
            "struct Header { uint32 length; uint32 offset; };\n"
            "struct Item(Header h) { int64 data[h.length]; };\n"
            "struct Container {\n"
            "    uint16 count;\n"
            "    Header headers[count];\n"
            "    Item(headers[@index]) items[count];\n"
            "};\n";
    fixture_setup(&f, INPUT, sizeof(INPUT) - 1U);
    /* @index doesn't trigger a hard diagnostic, even though it's
     * not bound in any scope. */
    TEST_ASSERT_EQUAL_size_t(0U, zsp_diag_error_count(&f.diag));
    fixture_teardown(&f);
}

static void test_duplicate_field_name_is_diagnosed(void)
{
    fixture_t f;
    static const uint8_t INPUT[] =
            "package x;\n"
            "struct Bad {\n"
            "    uint32 v;\n"
            "    string v;\n"
            "};\n";
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_source_init(&f.src, &libc_alloc));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_arena_init(&f.arena, &libc_alloc));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_diag_init(&f.diag, &libc_alloc, 0U));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_source_add_borrowed(
            &f.src, "test.zs", 7U, INPUT, sizeof(INPUT) - 1U, &f.fid));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_parse_unit(
            &f.src, f.fid, &f.arena, &f.diag, &f.unit));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_sema_init(&f.sema, &f.src,
                                              &f.arena, &f.diag));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_sema_analyze_unit(&f.sema, f.unit));
    TEST_ASSERT_EQUAL_size_t(1U, zsp_diag_error_count(&f.diag));
    fixture_teardown(&f);
}

static void test_const_eval_enum_implicit_values(void)
{
    fixture_t f;
    static const uint8_t INPUT[] =
            "package x;\n"
            "enum uint8 Color {\n"
            "    RED,\n"
            "    GREEN,\n"
            "    BLUE\n"
            "};\n";
    fixture_setup(&f, INPUT, sizeof(INPUT) - 1U);
    zsp_ast_node_t *const enm = f.unit->u.unit.decls[0];

    const zsp_sema_const_t *const r = (const zsp_sema_const_t *)
            enm->u.enum_decl.items[0]->computed;
    const zsp_sema_const_t *const g = (const zsp_sema_const_t *)
            enm->u.enum_decl.items[1]->computed;
    const zsp_sema_const_t *const b = (const zsp_sema_const_t *)
            enm->u.enum_decl.items[2]->computed;
    TEST_ASSERT_NOT_NULL(r);
    TEST_ASSERT_EQUAL_UINT64(0U, r->v.u);
    TEST_ASSERT_NOT_NULL(g);
    TEST_ASSERT_EQUAL_UINT64(1U, g->v.u);
    TEST_ASSERT_NOT_NULL(b);
    TEST_ASSERT_EQUAL_UINT64(2U, b->v.u);
    fixture_teardown(&f);
}

static void test_const_eval_explicit_expr(void)
{
    fixture_t f;
    static const uint8_t INPUT[] =
            "package x;\n"
            "enum uint16 E {\n"
            "    A = 10,\n"
            "    B = A + 5,\n"
            "    C = (B << 1) | 0x01\n"
            "};\n";
    fixture_setup(&f, INPUT, sizeof(INPUT) - 1U);
    zsp_ast_node_t *const enm = f.unit->u.unit.decls[0];
    const zsp_sema_const_t *const a = (const zsp_sema_const_t *)enm->u.enum_decl.items[0]->computed;
    const zsp_sema_const_t *const b = (const zsp_sema_const_t *)enm->u.enum_decl.items[1]->computed;
    const zsp_sema_const_t *const c = (const zsp_sema_const_t *)enm->u.enum_decl.items[2]->computed;
    TEST_ASSERT_EQUAL_UINT64(10U, a->v.u);
    TEST_ASSERT_EQUAL_UINT64(15U, b->v.u);
    TEST_ASSERT_EQUAL_UINT64((15U << 1) | 1U, c->v.u);
    fixture_teardown(&f);
}

static void test_const_decl_eval(void)
{
    fixture_t f;
    static const uint8_t INPUT[] =
            "package x;\n"
            "const uint32 BASE = 100;\n"
            "const uint32 SCALED = BASE * 2 + 1;\n";
    fixture_setup(&f, INPUT, sizeof(INPUT) - 1U);
    zsp_ast_node_t *const base   = f.unit->u.unit.decls[0];
    zsp_ast_node_t *const scaled = f.unit->u.unit.decls[1];
    const zsp_sema_const_t *const bv = (const zsp_sema_const_t *)
            base->u.const_decl.value_expr->computed;
    const zsp_sema_const_t *const sv = (const zsp_sema_const_t *)
            scaled->u.const_decl.value_expr->computed;
    TEST_ASSERT_NOT_NULL(bv);
    TEST_ASSERT_EQUAL_UINT64(100U, bv->v.u);
    TEST_ASSERT_NOT_NULL(sv);
    TEST_ASSERT_EQUAL_UINT64(201U, sv->v.u);
    fixture_teardown(&f);
}

static void test_bitmask_implicit_powers_of_two(void)
{
    fixture_t f;
    static const uint8_t INPUT[] =
            "package x;\n"
            "bitmask uint8 Flags {\n"
            "    A,\n"   /* 1 */
            "    B,\n"   /* 2 */
            "    C,\n"   /* 4 */
            "    D\n"    /* 8 */
            "};\n";
    fixture_setup(&f, INPUT, sizeof(INPUT) - 1U);
    zsp_ast_node_t *const bm = f.unit->u.unit.decls[0];
    const uint64_t expect[] = { 1U, 2U, 4U, 8U };
    for (size_t i = 0U; i < 4U; ++i) {
        const zsp_sema_const_t *const c = (const zsp_sema_const_t *)
                bm->u.bitmask_decl.items[i]->computed;
        TEST_ASSERT_NOT_NULL(c);
        TEST_ASSERT_EQUAL_UINT64(expect[i], c->v.u);
    }
    fixture_teardown(&f);
}

static void test_const_eval_cycle_detected(void)
{
    fixture_t f;
    static const uint8_t INPUT[] =
            "package x;\n"
            "const uint32 X = Y + 1;\n"
            "const uint32 Y = X + 1;\n";
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_source_init(&f.src, &libc_alloc));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_arena_init(&f.arena, &libc_alloc));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_diag_init(&f.diag, &libc_alloc, 0U));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_source_add_borrowed(
            &f.src, "test.zs", 7U, INPUT, sizeof(INPUT) - 1U, &f.fid));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_parse_unit(
            &f.src, f.fid, &f.arena, &f.diag, &f.unit));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_sema_init(&f.sema, &f.src,
                                              &f.arena, &f.diag));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_sema_analyze_unit(&f.sema, f.unit));
    /* At least one cyclic-constant diagnostic should have been
     * emitted; the analyzer detects the cycle via the in-flight
     * sentinel and emits a diagnostic before unwinding. */
    TEST_ASSERT_TRUE(zsp_diag_error_count(&f.diag) >= 1U);
    fixture_teardown(&f);
}

static void test_cross_file_type_reference_resolves(void)
{
    /* Two files: `outer.zs` defines a struct that references
     * a type defined in `inner.zs`. We register inner first,
     * then analyze outer — outer's reference should resolve to
     * inner's declaration through the shared root scope. */
    zsp_source_t         src;
    zsp_arena_t          arena;
    zsp_diag_collector_t diag;
    zsp_sema_t           sema;
    zsp_file_id_t        inner_fid, outer_fid;
    zsp_ast_node_t      *inner_unit = NULL;
    zsp_ast_node_t      *outer_unit = NULL;

    static const uint8_t INNER[] =
            "package geometry;\n"
            "struct Point { int32 x; int32 y; };\n";
    static const uint8_t OUTER[] =
            "package roads;\n"
            "import geometry.*;\n"
            "struct Stop { uint32 id; Point location; };\n";

    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_source_init(&src, &libc_alloc));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_arena_init(&arena, &libc_alloc));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_diag_init(&diag, &libc_alloc, 0U));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_source_add_borrowed(
            &src, "inner.zs", 8U, INNER, sizeof(INNER) - 1U, &inner_fid));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_source_add_borrowed(
            &src, "outer.zs", 8U, OUTER, sizeof(OUTER) - 1U, &outer_fid));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_parse_unit(
            &src, inner_fid, &arena, &diag, &inner_unit));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_parse_unit(
            &src, outer_fid, &arena, &diag, &outer_unit));
    TEST_ASSERT_EQUAL_size_t(0U, zsp_diag_error_count(&diag));

    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_sema_init(&sema, &src, &arena, &diag));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_sema_register_unit(&sema, inner_unit));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_sema_analyze_unit(&sema, outer_unit));
    TEST_ASSERT_EQUAL_size_t(0U, zsp_diag_error_count(&diag));

    /* Outer's `Stop.location` field's type should resolve to
     * inner's `Point` decl. */
    zsp_ast_node_t *const stop = outer_unit->u.unit.decls[0];
    zsp_ast_node_t *const loc  = stop->u.compound.members[1];
    TEST_ASSERT_EQUAL_INT(ZSP_AST_TYPE_REF, loc->u.field.type->kind);
    TEST_ASSERT_NOT_NULL(loc->u.field.type->resolved);
    TEST_ASSERT_EQUAL_PTR(inner_unit->u.unit.decls[0],
                          loc->u.field.type->resolved);

    zsp_diag_dispose(&diag);
    zsp_arena_dispose(&arena);
    zsp_source_dispose(&src);
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
    RUN_TEST(test_array_length_field_reference_resolves);
    RUN_TEST(test_struct_parameter_resolves);
    RUN_TEST(test_enum_item_value_can_reference_prior);
    RUN_TEST(test_index_pseudo_ident_does_not_emit_unresolved);
    RUN_TEST(test_duplicate_field_name_is_diagnosed);
    RUN_TEST(test_const_eval_enum_implicit_values);
    RUN_TEST(test_const_eval_explicit_expr);
    RUN_TEST(test_const_decl_eval);
    RUN_TEST(test_bitmask_implicit_powers_of_two);
    RUN_TEST(test_const_eval_cycle_detected);
    RUN_TEST(test_cross_file_type_reference_resolves);
    RUN_TEST(test_builtins_are_not_type_refs);
    return UNITY_END();
}
