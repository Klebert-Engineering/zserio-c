/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Fabian Klebert
 *
 * Unit tests for zsparser/lexer.
 */

#include <string.h>

#include "unity.h"

#include "zsparser/diag.h"
#include "zsparser/lexer.h"
#include "zsparser/source.h"
#include "zsparser/token.h"

static zserio_allocator_t libc_alloc;

void setUp(void)
{
    libc_alloc = zserio_allocator_libc();
}

void tearDown(void)
{
}

/* Fixture: register a buffer, init lexer + diag collector over it. */
typedef struct lex_fixture {
    zsp_source_t         src;
    zsp_diag_collector_t diag;
    zsp_lexer_t          lexer;
    zsp_file_id_t        file_id;
} lex_fixture_t;

static void fixture_init(lex_fixture_t *f,
                         const uint8_t *bytes, size_t len)
{
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_source_init(&f->src, &libc_alloc));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_diag_init(&f->diag, &libc_alloc, 0U));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_source_add_borrowed(
            &f->src, "test.zs", 7U, bytes, len, &f->file_id));
    TEST_ASSERT_EQUAL(ZSERIO_OK,
                      zsp_lexer_init(&f->lexer, &f->src, f->file_id, &f->diag));
}

static void fixture_dispose(lex_fixture_t *f)
{
    zsp_diag_dispose(&f->diag);
    zsp_source_dispose(&f->src);
}

/* Pull one token + assert its kind. */
static void assert_next_kind(zsp_lexer_t *l, zsp_token_kind_t expected)
{
    zsp_token_t tok;
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_lexer_next(l, &tok));
    TEST_ASSERT_EQUAL_INT(expected, tok.kind);
}

static void assert_eof(zsp_lexer_t *l)
{
    zsp_token_t tok;
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_lexer_next(l, &tok));
    TEST_ASSERT_EQUAL_INT(ZSP_TOK_EOF, tok.kind);
}

/* --- Tests ----------------------------------------------------- */

static void test_empty_input_yields_eof(void)
{
    lex_fixture_t f;
    static const uint8_t INPUT[] = "";
    fixture_init(&f, INPUT, 0U);
    assert_eof(&f.lexer);
    assert_eof(&f.lexer);  /* repeated EOF is idempotent */
    fixture_dispose(&f);
}

static void test_whitespace_only(void)
{
    lex_fixture_t f;
    static const uint8_t INPUT[] = "   \t\n\r\f  ";
    fixture_init(&f, INPUT, sizeof(INPUT) - 1U);
    assert_eof(&f.lexer);
    fixture_dispose(&f);
}

static void test_line_comment(void)
{
    lex_fixture_t f;
    static const uint8_t INPUT[] = "// hello\npackage";
    fixture_init(&f, INPUT, sizeof(INPUT) - 1U);
    assert_next_kind(&f.lexer, ZSP_TOK_KW_PACKAGE);
    assert_eof(&f.lexer);
    fixture_dispose(&f);
}

static void test_block_comment(void)
{
    lex_fixture_t f;
    static const uint8_t INPUT[] = "/* multi\nline */ struct";
    fixture_init(&f, INPUT, sizeof(INPUT) - 1U);
    assert_next_kind(&f.lexer, ZSP_TOK_KW_STRUCT);
    assert_eof(&f.lexer);
    fixture_dispose(&f);
}

static void test_unterminated_block_comment_reports_error(void)
{
    lex_fixture_t f;
    static const uint8_t INPUT[] = "struct /* unterminated";
    fixture_init(&f, INPUT, sizeof(INPUT) - 1U);
    assert_next_kind(&f.lexer, ZSP_TOK_KW_STRUCT);
    assert_eof(&f.lexer);  /* swallows rest, reports diagnostic */
    TEST_ASSERT_EQUAL_size_t(1U, zsp_diag_error_count(&f.diag));
    fixture_dispose(&f);
}

static void test_identifier_basic(void)
{
    lex_fixture_t f;
    static const uint8_t INPUT[] = "frame_id  _internal foo123";
    fixture_init(&f, INPUT, sizeof(INPUT) - 1U);

    zsp_token_t tok;
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_lexer_next(&f.lexer, &tok));
    TEST_ASSERT_EQUAL_INT(ZSP_TOK_IDENT, tok.kind);
    TEST_ASSERT_EQUAL_UINT32(0U, tok.offset);
    TEST_ASSERT_EQUAL_UINT32(8U, tok.length);

    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_lexer_next(&f.lexer, &tok));
    TEST_ASSERT_EQUAL_INT(ZSP_TOK_IDENT, tok.kind);
    TEST_ASSERT_EQUAL_UINT32(10U, tok.offset);
    TEST_ASSERT_EQUAL_UINT32(9U, tok.length);

    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_lexer_next(&f.lexer, &tok));
    TEST_ASSERT_EQUAL_INT(ZSP_TOK_IDENT, tok.kind);
    TEST_ASSERT_EQUAL_UINT32(20U, tok.offset);
    TEST_ASSERT_EQUAL_UINT32(6U, tok.length);

    assert_eof(&f.lexer);
    fixture_dispose(&f);
}

static void test_keywords_lookup(void)
{
    lex_fixture_t f;
    static const uint8_t INPUT[] =
            "package import struct union choice enum bitmask "
            "subtype const optional implicit packed align "
            "if case default on function return rule rule_group "
            "instantiate using true false bool string bytes extern "
            "varint varuint varsize float32 float64 ";
    /* Note: "float32"/"float64" are NOT keywords today — they
     * should lex as identifiers. */
    fixture_init(&f, INPUT, sizeof(INPUT) - 1U);

    const zsp_token_kind_t expected[] = {
        ZSP_TOK_KW_PACKAGE, ZSP_TOK_KW_IMPORT, ZSP_TOK_KW_STRUCT,
        ZSP_TOK_KW_UNION, ZSP_TOK_KW_CHOICE, ZSP_TOK_KW_ENUM,
        ZSP_TOK_KW_BITMASK, ZSP_TOK_KW_SUBTYPE, ZSP_TOK_KW_CONST,
        ZSP_TOK_KW_OPTIONAL, ZSP_TOK_KW_IMPLICIT, ZSP_TOK_KW_PACKED,
        ZSP_TOK_KW_ALIGN, ZSP_TOK_KW_IF, ZSP_TOK_KW_CASE,
        ZSP_TOK_KW_DEFAULT, ZSP_TOK_KW_ON, ZSP_TOK_KW_FUNCTION,
        ZSP_TOK_KW_RETURN, ZSP_TOK_KW_RULE, ZSP_TOK_KW_RULE_GROUP,
        ZSP_TOK_KW_INSTANTIATE, ZSP_TOK_KW_USING, ZSP_TOK_KW_TRUE,
        ZSP_TOK_KW_FALSE, ZSP_TOK_KW_BOOL, ZSP_TOK_KW_STRING,
        ZSP_TOK_KW_BYTES, ZSP_TOK_KW_EXTERN, ZSP_TOK_KW_VARINT,
        ZSP_TOK_KW_VARUINT, ZSP_TOK_KW_VARSIZE,
        ZSP_TOK_IDENT,   /* float32 — identifier */
        ZSP_TOK_IDENT,   /* float64 — identifier */
    };
    const size_t n = sizeof(expected) / sizeof(expected[0]);
    for (size_t i = 0U; i < n; ++i) {
        assert_next_kind(&f.lexer, expected[i]);
    }
    assert_eof(&f.lexer);
    fixture_dispose(&f);
}

static void test_annotation_keywords(void)
{
    lex_fixture_t f;
    static const uint8_t INPUT[] = "@deprecated @index @removed";
    fixture_init(&f, INPUT, sizeof(INPUT) - 1U);
    assert_next_kind(&f.lexer, ZSP_TOK_KW_DEPRECATED);
    assert_next_kind(&f.lexer, ZSP_TOK_KW_INDEX);
    assert_next_kind(&f.lexer, ZSP_TOK_KW_REMOVED);
    assert_eof(&f.lexer);
    fixture_dispose(&f);
}

static void test_decimal_integer(void)
{
    lex_fixture_t f;
    static const uint8_t INPUT[] = "0 42 1234567890";
    fixture_init(&f, INPUT, sizeof(INPUT) - 1U);

    zsp_token_t tok;

    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_lexer_next(&f.lexer, &tok));
    TEST_ASSERT_EQUAL_INT(ZSP_TOK_INT_LIT, tok.kind);
    TEST_ASSERT_EQUAL_UINT64(0U, tok.value.u64);

    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_lexer_next(&f.lexer, &tok));
    TEST_ASSERT_EQUAL_INT(ZSP_TOK_INT_LIT, tok.kind);
    TEST_ASSERT_EQUAL_UINT64(42U, tok.value.u64);

    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_lexer_next(&f.lexer, &tok));
    TEST_ASSERT_EQUAL_INT(ZSP_TOK_INT_LIT, tok.kind);
    TEST_ASSERT_EQUAL_UINT64(1234567890ULL, tok.value.u64);

    assert_eof(&f.lexer);
    fixture_dispose(&f);
}

static void test_decimal_integer_overflow_reports_error(void)
{
    lex_fixture_t f;
    /* 2^64 = 18446744073709551616 — overflows uint64. */
    static const uint8_t INPUT[] = "99999999999999999999";
    fixture_init(&f, INPUT, sizeof(INPUT) - 1U);

    zsp_token_t tok;
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_lexer_next(&f.lexer, &tok));
    TEST_ASSERT_EQUAL_INT(ZSP_TOK_INVALID, tok.kind);
    TEST_ASSERT_EQUAL_size_t(1U, zsp_diag_error_count(&f.diag));

    assert_eof(&f.lexer);
    fixture_dispose(&f);
}

static void test_hex_integer(void)
{
    lex_fixture_t f;
    static const uint8_t INPUT[] = "0x0 0xFF 0xdeadBEEF";
    fixture_init(&f, INPUT, sizeof(INPUT) - 1U);

    zsp_token_t tok;
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_lexer_next(&f.lexer, &tok));
    TEST_ASSERT_EQUAL_INT(ZSP_TOK_INT_LIT, tok.kind);
    TEST_ASSERT_EQUAL_UINT64(0U, tok.value.u64);

    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_lexer_next(&f.lexer, &tok));
    TEST_ASSERT_EQUAL_INT(ZSP_TOK_INT_LIT, tok.kind);
    TEST_ASSERT_EQUAL_UINT64(0xFFU, tok.value.u64);

    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_lexer_next(&f.lexer, &tok));
    TEST_ASSERT_EQUAL_INT(ZSP_TOK_INT_LIT, tok.kind);
    TEST_ASSERT_EQUAL_UINT64(0xDEADBEEFU, tok.value.u64);

    fixture_dispose(&f);
}

static void test_binary_integer(void)
{
    lex_fixture_t f;
    static const uint8_t INPUT[] = "0b 1b 1010B 11111111b";
    fixture_init(&f, INPUT, sizeof(INPUT) - 1U);

    zsp_token_t tok;
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_lexer_next(&f.lexer, &tok));
    TEST_ASSERT_EQUAL_INT(ZSP_TOK_INT_LIT, tok.kind);
    TEST_ASSERT_EQUAL_UINT64(0U, tok.value.u64);

    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_lexer_next(&f.lexer, &tok));
    TEST_ASSERT_EQUAL_INT(ZSP_TOK_INT_LIT, tok.kind);
    TEST_ASSERT_EQUAL_UINT64(1U, tok.value.u64);

    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_lexer_next(&f.lexer, &tok));
    TEST_ASSERT_EQUAL_INT(ZSP_TOK_INT_LIT, tok.kind);
    TEST_ASSERT_EQUAL_UINT64(10U, tok.value.u64);

    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_lexer_next(&f.lexer, &tok));
    TEST_ASSERT_EQUAL_INT(ZSP_TOK_INT_LIT, tok.kind);
    TEST_ASSERT_EQUAL_UINT64(255U, tok.value.u64);

    fixture_dispose(&f);
}

static void test_octal_integer(void)
{
    lex_fixture_t f;
    static const uint8_t INPUT[] = "01 0777";
    fixture_init(&f, INPUT, sizeof(INPUT) - 1U);

    zsp_token_t tok;
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_lexer_next(&f.lexer, &tok));
    TEST_ASSERT_EQUAL_INT(ZSP_TOK_INT_LIT, tok.kind);
    TEST_ASSERT_EQUAL_UINT64(1U, tok.value.u64);

    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_lexer_next(&f.lexer, &tok));
    TEST_ASSERT_EQUAL_INT(ZSP_TOK_INT_LIT, tok.kind);
    TEST_ASSERT_EQUAL_UINT64(0777U, tok.value.u64);

    fixture_dispose(&f);
}

static void test_float_literal(void)
{
    lex_fixture_t f;
    static const uint8_t INPUT[] = "1.5 0.25 3.14f 1e2 2.5e-3";
    fixture_init(&f, INPUT, sizeof(INPUT) - 1U);

    zsp_token_t tok;
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_lexer_next(&f.lexer, &tok));
    TEST_ASSERT_EQUAL_INT(ZSP_TOK_FLOAT_LIT, tok.kind);
    TEST_ASSERT_FLOAT_WITHIN(1e-6F, (float)1.5, (float)tok.value.f64);

    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_lexer_next(&f.lexer, &tok));
    TEST_ASSERT_EQUAL_INT(ZSP_TOK_FLOAT_LIT, tok.kind);
    TEST_ASSERT_FLOAT_WITHIN(1e-6F, (float)0.25, (float)tok.value.f64);

    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_lexer_next(&f.lexer, &tok));
    TEST_ASSERT_EQUAL_INT(ZSP_TOK_FLOAT_LIT, tok.kind);
    TEST_ASSERT_FLOAT_WITHIN(1e-6F, (float)3.14, (float)tok.value.f64);

    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_lexer_next(&f.lexer, &tok));
    TEST_ASSERT_EQUAL_INT(ZSP_TOK_FLOAT_LIT, tok.kind);
    TEST_ASSERT_FLOAT_WITHIN(1e-6F, (float)100.0, (float)tok.value.f64);

    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_lexer_next(&f.lexer, &tok));
    TEST_ASSERT_EQUAL_INT(ZSP_TOK_FLOAT_LIT, tok.kind);
    TEST_ASSERT_FLOAT_WITHIN(1e-6F, (float)0.0025, (float)tok.value.f64);

    fixture_dispose(&f);
}

static void test_string_literal(void)
{
    lex_fixture_t f;
    static const uint8_t INPUT[] = "\"hello\" \"with \\\"quotes\\\"\"";
    fixture_init(&f, INPUT, sizeof(INPUT) - 1U);

    zsp_token_t tok;
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_lexer_next(&f.lexer, &tok));
    TEST_ASSERT_EQUAL_INT(ZSP_TOK_STRING_LIT, tok.kind);
    TEST_ASSERT_EQUAL_UINT32(7U, tok.length);   /* "hello" including quotes */

    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_lexer_next(&f.lexer, &tok));
    TEST_ASSERT_EQUAL_INT(ZSP_TOK_STRING_LIT, tok.kind);
    /* "with \"quotes\"" — 17 source bytes including outer quotes and the 2 escape pairs */
    TEST_ASSERT_EQUAL_UINT32(17U, tok.length);

    fixture_dispose(&f);
}

static void test_unterminated_string_reports_error(void)
{
    lex_fixture_t f;
    static const uint8_t INPUT[] = "\"oh no\nstruct";
    fixture_init(&f, INPUT, sizeof(INPUT) - 1U);

    zsp_token_t tok;
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_lexer_next(&f.lexer, &tok));
    TEST_ASSERT_EQUAL_INT(ZSP_TOK_INVALID, tok.kind);
    TEST_ASSERT_EQUAL_size_t(1U, zsp_diag_error_count(&f.diag));

    /* Lexer should recover and produce the struct keyword after
     * the newline. */
    assert_next_kind(&f.lexer, ZSP_TOK_KW_STRUCT);

    fixture_dispose(&f);
}

static void test_punctuation(void)
{
    lex_fixture_t f;
    static const uint8_t INPUT[] = "() {} [] ; , . : ?";
    fixture_init(&f, INPUT, sizeof(INPUT) - 1U);

    const zsp_token_kind_t expected[] = {
        ZSP_TOK_LPAREN, ZSP_TOK_RPAREN,
        ZSP_TOK_LBRACE, ZSP_TOK_RBRACE,
        ZSP_TOK_LBRACKET, ZSP_TOK_RBRACKET,
        ZSP_TOK_SEMICOLON, ZSP_TOK_COMMA,
        ZSP_TOK_DOT, ZSP_TOK_COLON,
        ZSP_TOK_QUESTION,
    };
    const size_t n = sizeof(expected) / sizeof(expected[0]);
    for (size_t i = 0U; i < n; ++i) {
        assert_next_kind(&f.lexer, expected[i]);
    }
    assert_eof(&f.lexer);
    fixture_dispose(&f);
}

static void test_single_and_double_char_operators(void)
{
    lex_fixture_t f;
    static const uint8_t INPUT[] =
            "= == != < > <= >= << >> "
            "+ - * / % "
            "! ~ & | ^ "
            "&& ||";
    fixture_init(&f, INPUT, sizeof(INPUT) - 1U);

    const zsp_token_kind_t expected[] = {
        ZSP_TOK_ASSIGN, ZSP_TOK_EQ, ZSP_TOK_NEQ,
        ZSP_TOK_LT, ZSP_TOK_GT, ZSP_TOK_LE, ZSP_TOK_GE,
        ZSP_TOK_LSHIFT, ZSP_TOK_RSHIFT,
        ZSP_TOK_PLUS, ZSP_TOK_MINUS, ZSP_TOK_STAR,
        ZSP_TOK_SLASH, ZSP_TOK_PERCENT,
        ZSP_TOK_BANG, ZSP_TOK_TILDE,
        ZSP_TOK_AMP, ZSP_TOK_PIPE, ZSP_TOK_CARET,
        ZSP_TOK_LOGICAL_AND, ZSP_TOK_LOGICAL_OR,
    };
    const size_t n = sizeof(expected) / sizeof(expected[0]);
    for (size_t i = 0U; i < n; ++i) {
        assert_next_kind(&f.lexer, expected[i]);
    }
    assert_eof(&f.lexer);
    fixture_dispose(&f);
}

static void test_unexpected_character_reports_error_and_recovers(void)
{
    lex_fixture_t f;
    /* '$' is not in zserio's vocabulary. The lexer should emit a
     * diagnostic, produce INVALID for the offending byte, then
     * recover and continue lexing the rest. */
    static const uint8_t INPUT[] = "struct $ frame";
    fixture_init(&f, INPUT, sizeof(INPUT) - 1U);

    assert_next_kind(&f.lexer, ZSP_TOK_KW_STRUCT);
    assert_next_kind(&f.lexer, ZSP_TOK_INVALID);
    assert_next_kind(&f.lexer, ZSP_TOK_IDENT);  /* "frame" */
    assert_eof(&f.lexer);
    TEST_ASSERT_EQUAL_size_t(1U, zsp_diag_error_count(&f.diag));

    fixture_dispose(&f);
}

static void test_realistic_schema_tokens(void)
{
    lex_fixture_t f;
    static const uint8_t INPUT[] =
            "package telemetry;\n"
            "\n"
            "struct Frame {\n"
            "    uint32 sensor_id;\n"     /* uint32 is an identifier in slice A — keyword table doesn't include it yet */
            "    string device_label;\n"
            "};\n";
    fixture_init(&f, INPUT, sizeof(INPUT) - 1U);

    const zsp_token_kind_t expected[] = {
        ZSP_TOK_KW_PACKAGE, ZSP_TOK_IDENT /* telemetry */, ZSP_TOK_SEMICOLON,
        ZSP_TOK_KW_STRUCT, ZSP_TOK_IDENT /* Frame */, ZSP_TOK_LBRACE,
        ZSP_TOK_IDENT /* uint32 — not yet a keyword */, ZSP_TOK_IDENT /* sensor_id */, ZSP_TOK_SEMICOLON,
        ZSP_TOK_KW_STRING, ZSP_TOK_IDENT /* device_label */, ZSP_TOK_SEMICOLON,
        ZSP_TOK_RBRACE, ZSP_TOK_SEMICOLON,
    };
    const size_t n = sizeof(expected) / sizeof(expected[0]);
    for (size_t i = 0U; i < n; ++i) {
        assert_next_kind(&f.lexer, expected[i]);
    }
    assert_eof(&f.lexer);
    fixture_dispose(&f);
}

static void test_token_kind_name_is_static(void)
{
    /* Sanity check on the metadata table. */
    TEST_ASSERT_EQUAL_STRING("struct", zsp_token_kind_name(ZSP_TOK_KW_STRUCT));
    TEST_ASSERT_EQUAL_STRING("==", zsp_token_kind_name(ZSP_TOK_EQ));
    TEST_ASSERT_EQUAL_STRING("identifier", zsp_token_kind_name(ZSP_TOK_IDENT));
    TEST_ASSERT_EQUAL_STRING("end of file", zsp_token_kind_name(ZSP_TOK_EOF));

    TEST_ASSERT_TRUE(zsp_token_is_keyword(ZSP_TOK_KW_STRUCT));
    TEST_ASSERT_TRUE(zsp_token_is_keyword(ZSP_TOK_KW_DEPRECATED));
    TEST_ASSERT_FALSE(zsp_token_is_keyword(ZSP_TOK_IDENT));
    TEST_ASSERT_FALSE(zsp_token_is_keyword(ZSP_TOK_LPAREN));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_empty_input_yields_eof);
    RUN_TEST(test_whitespace_only);
    RUN_TEST(test_line_comment);
    RUN_TEST(test_block_comment);
    RUN_TEST(test_unterminated_block_comment_reports_error);
    RUN_TEST(test_identifier_basic);
    RUN_TEST(test_keywords_lookup);
    RUN_TEST(test_annotation_keywords);
    RUN_TEST(test_decimal_integer);
    RUN_TEST(test_decimal_integer_overflow_reports_error);
    RUN_TEST(test_hex_integer);
    RUN_TEST(test_binary_integer);
    RUN_TEST(test_octal_integer);
    RUN_TEST(test_float_literal);
    RUN_TEST(test_string_literal);
    RUN_TEST(test_unterminated_string_reports_error);
    RUN_TEST(test_punctuation);
    RUN_TEST(test_single_and_double_char_operators);
    RUN_TEST(test_unexpected_character_reports_error_and_recovers);
    RUN_TEST(test_realistic_schema_tokens);
    RUN_TEST(test_token_kind_name_is_static);
    return UNITY_END();
}
