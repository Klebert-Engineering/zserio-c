/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Fabian Klebert
 *
 * Integration tests for the recursive-descent parser. Each case
 * registers a hand-built schema buffer, parses it, asserts that
 * no errors were emitted, and inspects the AST shape.
 *
 * Verifies: REQ-FN-002 — parser accepts the schema language
 *                        (per-construct AST-shape assertions).
 */

#include <string.h>

#include "unity.h"

#include "zsparser/arena.h"
#include "zsparser/ast.h"
#include "zsparser/diag.h"
#include "zsparser/parser.h"
#include "zsparser/source.h"

static zserio_allocator_t libc_alloc;

void setUp(void)  { libc_alloc = zserio_allocator_libc(); }
void tearDown(void) {}

typedef struct fixture {
    zsp_source_t         src;
    zsp_arena_t          arena;
    zsp_diag_collector_t diag;
    zsp_file_id_t        file_id;
    zsp_ast_node_t      *unit;
} fixture_t;

static void fixture_parse(fixture_t *f, const uint8_t *bytes, size_t len)
{
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_source_init(&f->src, &libc_alloc));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_arena_init(&f->arena, &libc_alloc));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_diag_init(&f->diag, &libc_alloc, 0U));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_source_add_borrowed(
            &f->src, "test.zs", 7U, bytes, len, &f->file_id));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_parse_unit(
            &f->src, f->file_id, &f->arena, &f->diag, &f->unit));
}

static void fixture_dispose(fixture_t *f)
{
    zsp_diag_dispose(&f->diag);
    zsp_arena_dispose(&f->arena);
    zsp_source_dispose(&f->src);
}

static void assert_no_errors(const fixture_t *f)
{
    TEST_ASSERT_EQUAL_size_t(0U, zsp_diag_error_count(&f->diag));
}

/* ---------- Tests ---------- */

static void test_empty_input(void)
{
    fixture_t f;
    static const uint8_t INPUT[] = "";
    fixture_parse(&f, INPUT, 0U);
    assert_no_errors(&f);
    TEST_ASSERT_NOT_NULL(f.unit);
    TEST_ASSERT_EQUAL_INT(ZSP_AST_UNIT, f.unit->kind);
    TEST_ASSERT_NULL(f.unit->u.unit.package);
    TEST_ASSERT_EQUAL_size_t(0U, f.unit->u.unit.n_decls);
    fixture_dispose(&f);
}

static void test_package_only(void)
{
    fixture_t f;
    static const uint8_t INPUT[] = "package foo.bar;";
    fixture_parse(&f, INPUT, sizeof(INPUT) - 1U);
    assert_no_errors(&f);
    TEST_ASSERT_NOT_NULL(f.unit->u.unit.package);
    zsp_ast_node_t *const dotted = f.unit->u.unit.package->u.package.name;
    TEST_ASSERT_NOT_NULL(dotted);
    TEST_ASSERT_EQUAL_INT(ZSP_AST_DOTTED_NAME, dotted->kind);
    TEST_ASSERT_EQUAL_size_t(2U, dotted->u.dotted.n_parts);
    fixture_dispose(&f);
}

static void test_imports(void)
{
    fixture_t f;
    static const uint8_t INPUT[] =
            "package x;\n"
            "import a.b.*;\n"
            "import c.d.Foo;\n";
    fixture_parse(&f, INPUT, sizeof(INPUT) - 1U);
    assert_no_errors(&f);
    TEST_ASSERT_EQUAL_size_t(2U, f.unit->u.unit.n_imports);
    TEST_ASSERT_TRUE(f.unit->u.unit.imports[0]->u.import.wildcard);
    TEST_ASSERT_FALSE(f.unit->u.unit.imports[1]->u.import.wildcard);
    fixture_dispose(&f);
}

static void test_simple_struct(void)
{
    fixture_t f;
    static const uint8_t INPUT[] =
            "package telemetry;\n"
            "struct Frame {\n"
            "    uint32 sensor_id;\n"
            "    string device_label;\n"
            "};\n";
    fixture_parse(&f, INPUT, sizeof(INPUT) - 1U);
    assert_no_errors(&f);
    TEST_ASSERT_EQUAL_size_t(1U, f.unit->u.unit.n_decls);
    zsp_ast_node_t *const s = f.unit->u.unit.decls[0];
    TEST_ASSERT_EQUAL_INT(ZSP_AST_STRUCT, s->kind);
    TEST_ASSERT_EQUAL_size_t(2U, s->u.compound.n_members);
    zsp_ast_node_t *const f0 = s->u.compound.members[0];
    TEST_ASSERT_EQUAL_INT(ZSP_AST_FIELD, f0->kind);
    TEST_ASSERT_EQUAL_INT(ZSP_AST_TYPE_BUILTIN, f0->u.field.type->kind);
    TEST_ASSERT_EQUAL_INT(ZSP_TB_UINT32, f0->u.field.type->u.type_builtin.code);
    fixture_dispose(&f);
}

static void test_parameterised_struct_with_optional(void)
{
    fixture_t f;
    static const uint8_t INPUT[] =
            "package geo;\n"
            "struct Point(uint16 shift) {\n"
            "    int32 x;\n"
            "    int32 y;\n"
            "    int8 z if shift == 0;\n"
            "};\n";
    fixture_parse(&f, INPUT, sizeof(INPUT) - 1U);
    assert_no_errors(&f);
    zsp_ast_node_t *const s = f.unit->u.unit.decls[0];
    TEST_ASSERT_EQUAL_size_t(1U, s->u.compound.n_params);
    TEST_ASSERT_EQUAL_size_t(3U, s->u.compound.n_members);
    zsp_ast_node_t *const z = s->u.compound.members[2];
    TEST_ASSERT_NOT_NULL(z->u.field.if_clause);
    TEST_ASSERT_EQUAL_INT(ZSP_AST_EXPR_BINARY, z->u.field.if_clause->kind);
    TEST_ASSERT_EQUAL_INT(ZSP_OP_EQ, z->u.field.if_clause->u.binop.op);
    fixture_dispose(&f);
}

static void test_array_field(void)
{
    fixture_t f;
    static const uint8_t INPUT[] =
            "package x;\n"
            "struct List {\n"
            "    uint16 count;\n"
            "    uint32 items[count];\n"
            "};\n";
    fixture_parse(&f, INPUT, sizeof(INPUT) - 1U);
    assert_no_errors(&f);
    zsp_ast_node_t *const arr = f.unit->u.unit.decls[0]->u.compound.members[1];
    TEST_ASSERT_EQUAL_INT(ZSP_AST_TYPE_ARRAY, arr->u.field.type->kind);
    TEST_ASSERT_NOT_NULL(arr->u.field.type->u.type_array.length_expr);
    fixture_dispose(&f);
}

static void test_enum(void)
{
    fixture_t f;
    static const uint8_t INPUT[] =
            "package x;\n"
            "enum uint8 Direction {\n"
            "    NORTH = 0,\n"
            "    EAST  = 1,\n"
            "    SOUTH = 2,\n"
            "    WEST  = 3\n"
            "};\n";
    fixture_parse(&f, INPUT, sizeof(INPUT) - 1U);
    assert_no_errors(&f);
    zsp_ast_node_t *const e = f.unit->u.unit.decls[0];
    TEST_ASSERT_EQUAL_INT(ZSP_AST_ENUM, e->kind);
    TEST_ASSERT_EQUAL_size_t(4U, e->u.enum_decl.n_items);
    TEST_ASSERT_EQUAL_INT(ZSP_TB_UINT8, e->u.enum_decl.base_type->u.type_builtin.code);
    fixture_dispose(&f);
}

static void test_choice(void)
{
    fixture_t f;
    static const uint8_t INPUT[] =
            "package x;\n"
            "enum uint8 Tag { A = 0, B = 1, C = 2 };\n"
            "choice Body(Tag t) on (t) {\n"
            "    case Tag.A: uint32 a_value;\n"
            "    case Tag.B: string b_text;\n"
            "    default;\n"
            "};\n";
    fixture_parse(&f, INPUT, sizeof(INPUT) - 1U);
    assert_no_errors(&f);
    zsp_ast_node_t *const ch = f.unit->u.unit.decls[1];
    TEST_ASSERT_EQUAL_INT(ZSP_AST_CHOICE, ch->kind);
    TEST_ASSERT_EQUAL_size_t(2U, ch->u.choice.n_cases);
    TEST_ASSERT_NOT_NULL(ch->u.choice.default_case);
    fixture_dispose(&f);
}

static void test_const(void)
{
    fixture_t f;
    static const uint8_t INPUT[] =
            "package x;\n"
            "const uint32 MAGIC = 0xDEADBEEF;\n";
    fixture_parse(&f, INPUT, sizeof(INPUT) - 1U);
    assert_no_errors(&f);
    zsp_ast_node_t *const c = f.unit->u.unit.decls[0];
    TEST_ASSERT_EQUAL_INT(ZSP_AST_CONST, c->kind);
    TEST_ASSERT_EQUAL_INT(ZSP_AST_EXPR_INT_LIT, c->u.const_decl.value_expr->kind);
    TEST_ASSERT_EQUAL_UINT64(0xDEADBEEFULL,
            c->u.const_decl.value_expr->u.int_lit.value);
    fixture_dispose(&f);
}

static void test_subtype(void)
{
    fixture_t f;
    static const uint8_t INPUT[] =
            "package x;\n"
            "subtype uint32 NodeId;\n";
    fixture_parse(&f, INPUT, sizeof(INPUT) - 1U);
    assert_no_errors(&f);
    TEST_ASSERT_EQUAL_INT(ZSP_AST_SUBTYPE, f.unit->u.unit.decls[0]->kind);
    fixture_dispose(&f);
}

static void test_bitmask(void)
{
    fixture_t f;
    static const uint8_t INPUT[] =
            "package x;\n"
            "bitmask uint8 Flags {\n"
            "    RED   = 1,\n"
            "    GREEN = 2,\n"
            "    BLUE  = 4\n"
            "};\n";
    fixture_parse(&f, INPUT, sizeof(INPUT) - 1U);
    assert_no_errors(&f);
    TEST_ASSERT_EQUAL_INT(ZSP_AST_BITMASK, f.unit->u.unit.decls[0]->kind);
    TEST_ASSERT_EQUAL_size_t(3U, f.unit->u.unit.decls[0]->u.bitmask_decl.n_items);
    fixture_dispose(&f);
}

static void test_expression_precedence(void)
{
    fixture_t f;
    /* The const initializer exercises the Pratt parser:
     *   (1 + 2 * 3) > 6 && (4 - 1)
     * → AND-of(GT-of(ADD-of(1, MUL-of(2,3)), 6), SUB-of(4, 1)) */
    static const uint8_t INPUT[] =
            "package x;\n"
            "const bool R = (1 + 2 * 3 > 6) && (4 - 1 != 0);\n";
    fixture_parse(&f, INPUT, sizeof(INPUT) - 1U);
    assert_no_errors(&f);
    zsp_ast_node_t *const v = f.unit->u.unit.decls[0]->u.const_decl.value_expr;
    TEST_ASSERT_EQUAL_INT(ZSP_AST_EXPR_BINARY, v->kind);
    TEST_ASSERT_EQUAL_INT(ZSP_OP_LOGICAL_AND, v->u.binop.op);
    fixture_dispose(&f);
}

static void test_bit_field(void)
{
    fixture_t f;
    static const uint8_t INPUT[] =
            "package x;\n"
            "struct B {\n"
            "    bit:4 nibble;\n"
            "    int:8 byte_;\n"
            "};\n";
    fixture_parse(&f, INPUT, sizeof(INPUT) - 1U);
    assert_no_errors(&f);
    zsp_ast_node_t *const s = f.unit->u.unit.decls[0];
    TEST_ASSERT_EQUAL_INT(ZSP_TB_BIT_FIELD,
            s->u.compound.members[0]->u.field.type->u.type_builtin.code);
    TEST_ASSERT_EQUAL_UINT32(4U,
            s->u.compound.members[0]->u.field.type->u.type_builtin.width);
    TEST_ASSERT_EQUAL_INT(ZSP_TB_INT_FIELD,
            s->u.compound.members[1]->u.field.type->u.type_builtin.code);
    fixture_dispose(&f);
}

static void test_template_struct(void)
{
    fixture_t f;
    static const uint8_t INPUT[] =
            "package x;\n"
            "struct Wrapper<T> {\n"
            "    T value;\n"
            "};\n";
    fixture_parse(&f, INPUT, sizeof(INPUT) - 1U);
    assert_no_errors(&f);
    zsp_ast_node_t *const w = f.unit->u.unit.decls[0];
    TEST_ASSERT_EQUAL_size_t(1U, w->u.compound.n_template_params);
    fixture_dispose(&f);
}

static void test_function_inside_compound(void)
{
    fixture_t f;
    static const uint8_t INPUT[] =
            "package x;\n"
            "struct S {\n"
            "    uint32 n;\n"
            "    function uint32 doubled() { return n * 2; }\n"
            "};\n";
    fixture_parse(&f, INPUT, sizeof(INPUT) - 1U);
    assert_no_errors(&f);
    zsp_ast_node_t *const s = f.unit->u.unit.decls[0];
    TEST_ASSERT_EQUAL_size_t(2U, s->u.compound.n_members);
    TEST_ASSERT_EQUAL_INT(ZSP_AST_FUNCTION, s->u.compound.members[1]->kind);
    fixture_dispose(&f);
}

static void test_error_recovery(void)
{
    fixture_t f;
    /* Missing semicolon after sensor_id should be diagnosed but
     * parsing should continue. */
    static const uint8_t INPUT[] =
            "package x;\n"
            "struct Frame {\n"
            "    uint32 sensor_id\n"
            "    string device_label;\n"
            "};\n";
    fixture_parse(&f, INPUT, sizeof(INPUT) - 1U);
    TEST_ASSERT_TRUE(zsp_diag_error_count(&f.diag) >= 1U);
    TEST_ASSERT_NOT_NULL(f.unit);
    fixture_dispose(&f);
}

static void test_service(void)
{
    fixture_t f;
    static const uint8_t INPUT[] =
            "package x;\n"
            "struct Req { uint32 id; };\n"
            "struct Resp { uint32 value; };\n"
            "service Calculator {\n"
            "    Resp compute(Req);\n"
            "};\n";
    fixture_parse(&f, INPUT, sizeof(INPUT) - 1U);
    assert_no_errors(&f);
    TEST_ASSERT_EQUAL_size_t(3U, f.unit->u.unit.n_decls);
    zsp_ast_node_t *const svc = f.unit->u.unit.decls[2];
    TEST_ASSERT_EQUAL_INT(ZSP_AST_SERVICE, svc->kind);
    TEST_ASSERT_EQUAL_size_t(1U, svc->u.service.n_methods);
    fixture_dispose(&f);
}

static void test_pubsub(void)
{
    fixture_t f;
    static const uint8_t INPUT[] =
            "package x;\n"
            "struct Msg { uint32 v; };\n"
            "pubsub Bus {\n"
            "    topic(\"x/y\") publish Msg send;\n"
            "    topic(\"a/b\") subscribe Msg recv;\n"
            "};\n";
    fixture_parse(&f, INPUT, sizeof(INPUT) - 1U);
    assert_no_errors(&f);
    zsp_ast_node_t *const ps = f.unit->u.unit.decls[1];
    TEST_ASSERT_EQUAL_INT(ZSP_AST_PUBSUB, ps->kind);
    TEST_ASSERT_EQUAL_size_t(2U, ps->u.pubsub.n_messages);
    TEST_ASSERT_TRUE(ps->u.pubsub.messages[0]->u.pubsub_message.publish);
    TEST_ASSERT_TRUE(ps->u.pubsub.messages[1]->u.pubsub_message.subscribe);
    fixture_dispose(&f);
}

static void test_sql_table_and_database(void)
{
    fixture_t f;
    static const uint8_t INPUT[] =
            "package x;\n"
            "sql_table Stops {\n"
            "    uint32 id;\n"
            "    string name;\n"
            "};\n"
            "sql_database TransitDb {\n"
            "    Stops stops;\n"
            "};\n";
    fixture_parse(&f, INPUT, sizeof(INPUT) - 1U);
    assert_no_errors(&f);
    TEST_ASSERT_EQUAL_size_t(2U, f.unit->u.unit.n_decls);
    zsp_ast_node_t *const t = f.unit->u.unit.decls[0];
    TEST_ASSERT_EQUAL_INT(ZSP_AST_SQL_TABLE, t->kind);
    TEST_ASSERT_EQUAL_size_t(2U, t->u.sql_table.n_fields);
    zsp_ast_node_t *const db = f.unit->u.unit.decls[1];
    TEST_ASSERT_EQUAL_INT(ZSP_AST_SQL_DATABASE, db->kind);
    TEST_ASSERT_EQUAL_size_t(1U, db->u.sql_database.n_table_refs);
    fixture_dispose(&f);
}

static void test_compatibility_version(void)
{
    fixture_t f;
    static const uint8_t INPUT[] =
            "zserio_compatibility_version(\"2.18.1\");\n"
            "package x;\n"
            "struct Empty {};\n";
    fixture_parse(&f, INPUT, sizeof(INPUT) - 1U);
    assert_no_errors(&f);
    TEST_ASSERT_NOT_NULL(f.unit->u.unit.compat_version);
    fixture_dispose(&f);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_empty_input);
    RUN_TEST(test_package_only);
    RUN_TEST(test_imports);
    RUN_TEST(test_simple_struct);
    RUN_TEST(test_parameterised_struct_with_optional);
    RUN_TEST(test_array_field);
    RUN_TEST(test_enum);
    RUN_TEST(test_choice);
    RUN_TEST(test_const);
    RUN_TEST(test_subtype);
    RUN_TEST(test_bitmask);
    RUN_TEST(test_expression_precedence);
    RUN_TEST(test_bit_field);
    RUN_TEST(test_template_struct);
    RUN_TEST(test_function_inside_compound);
    RUN_TEST(test_error_recovery);
    RUN_TEST(test_service);
    RUN_TEST(test_pubsub);
    RUN_TEST(test_sql_table_and_database);
    RUN_TEST(test_compatibility_version);
    return UNITY_END();
}
