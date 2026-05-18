/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Fabian Klebert
 */

#include "zsparser/token.h"

/*
 * Token-kind metadata. Two functions:
 *   - zsp_token_is_keyword: range check
 *   - zsp_token_kind_name:  printable name for diagnostics
 *
 * Both are pure data lookups; no recursion, no heap. Adding a
 * new keyword to `zsp_token_kind_t` adds one new case to the
 * switch below and one row to the keyword table in lexer.c.
 */

bool zsp_token_is_keyword(zsp_token_kind_t k)
{
    return (k >= ZSP_TOK_KW_ALIGN) && (k <= ZSP_TOK_KW_VARUINT);
}

/* Full enumeration of token kinds with their printable spellings.
 * The switch's lack of a `default:` plus -Wswitch-enum makes
 * adding a kind without updating this function a build error
 * under our top-level warning flags. */
const char *zsp_token_kind_name(zsp_token_kind_t k)
{
    switch (k) {
    case ZSP_TOK_EOF:                  return "end of file";
    case ZSP_TOK_INVALID:              return "invalid token";
    case ZSP_TOK_IDENT:                return "identifier";
    case ZSP_TOK_INT_LIT:              return "integer literal";
    case ZSP_TOK_FLOAT_LIT:            return "float literal";
    case ZSP_TOK_STRING_LIT:           return "string literal";

    case ZSP_TOK_KW_ALIGN:             return "align";
    case ZSP_TOK_KW_BIT:               return "bit";
    case ZSP_TOK_KW_BITMASK:           return "bitmask";
    case ZSP_TOK_KW_BOOL:              return "bool";
    case ZSP_TOK_KW_BYTES:             return "bytes";
    case ZSP_TOK_KW_CASE:              return "case";
    case ZSP_TOK_KW_CHOICE:            return "choice";
    case ZSP_TOK_KW_COMPAT_VERSION:    return "zserio_compatibility_version";
    case ZSP_TOK_KW_CONST:             return "const";
    case ZSP_TOK_KW_DEFAULT:           return "default";
    case ZSP_TOK_KW_DEPRECATED:        return "@deprecated";
    case ZSP_TOK_KW_ENUM:              return "enum";
    case ZSP_TOK_KW_EXPLICIT:          return "explicit";
    case ZSP_TOK_KW_EXTEND:            return "extend";
    case ZSP_TOK_KW_EXTERN:            return "extern";
    case ZSP_TOK_KW_FALSE:             return "false";
    case ZSP_TOK_KW_FUNCTION:          return "function";
    case ZSP_TOK_KW_IF:                return "if";
    case ZSP_TOK_KW_IMPLICIT:          return "implicit";
    case ZSP_TOK_KW_IMPORT:            return "import";
    case ZSP_TOK_KW_INDEX:             return "@index";
    case ZSP_TOK_KW_INSTANTIATE:       return "instantiate";
    case ZSP_TOK_KW_INT:               return "int";
    case ZSP_TOK_KW_ISSET:             return "isset";
    case ZSP_TOK_KW_LENGTHOF:          return "lengthof";
    case ZSP_TOK_KW_NUMBITS:           return "numbits";
    case ZSP_TOK_KW_ON:                return "on";
    case ZSP_TOK_KW_OPTIONAL:          return "optional";
    case ZSP_TOK_KW_PACKAGE:           return "package";
    case ZSP_TOK_KW_PACKED:            return "packed";
    case ZSP_TOK_KW_PUBLISH:           return "publish";
    case ZSP_TOK_KW_PUBSUB:            return "pubsub";
    case ZSP_TOK_KW_REMOVED:           return "@removed";
    case ZSP_TOK_KW_RETURN:            return "return";
    case ZSP_TOK_KW_RULE:              return "rule";
    case ZSP_TOK_KW_RULE_GROUP:        return "rule_group";
    case ZSP_TOK_KW_SERVICE:           return "service";
    case ZSP_TOK_KW_SQL:               return "sql";
    case ZSP_TOK_KW_SQL_DATABASE:      return "sql_database";
    case ZSP_TOK_KW_SQL_TABLE:         return "sql_table";
    case ZSP_TOK_KW_SQL_VIRTUAL:       return "sql_virtual";
    case ZSP_TOK_KW_SQL_WITHOUT_ROWID: return "sql_without_rowid";
    case ZSP_TOK_KW_STRING:            return "string";
    case ZSP_TOK_KW_STRUCT:            return "struct";
    case ZSP_TOK_KW_SUBSCRIBE:         return "subscribe";
    case ZSP_TOK_KW_SUBTYPE:           return "subtype";
    case ZSP_TOK_KW_TOPIC:             return "topic";
    case ZSP_TOK_KW_TRUE:              return "true";
    case ZSP_TOK_KW_UNION:             return "union";
    case ZSP_TOK_KW_USING:             return "using";
    case ZSP_TOK_KW_VALUEOF:           return "valueof";
    case ZSP_TOK_KW_VARINT:            return "varint";
    case ZSP_TOK_KW_VARSIZE:           return "varsize";
    case ZSP_TOK_KW_VARUINT:           return "varuint";

    case ZSP_TOK_LPAREN:               return "(";
    case ZSP_TOK_RPAREN:               return ")";
    case ZSP_TOK_LBRACE:               return "{";
    case ZSP_TOK_RBRACE:               return "}";
    case ZSP_TOK_LBRACKET:             return "[";
    case ZSP_TOK_RBRACKET:             return "]";
    case ZSP_TOK_SEMICOLON:            return ";";
    case ZSP_TOK_COMMA:                return ",";
    case ZSP_TOK_DOT:                  return ".";
    case ZSP_TOK_COLON:                return ":";
    case ZSP_TOK_QUESTION:             return "?";

    case ZSP_TOK_ASSIGN:               return "=";
    case ZSP_TOK_EQ:                   return "==";
    case ZSP_TOK_NEQ:                  return "!=";
    case ZSP_TOK_LT:                   return "<";
    case ZSP_TOK_GT:                   return ">";
    case ZSP_TOK_LE:                   return "<=";
    case ZSP_TOK_GE:                   return ">=";
    case ZSP_TOK_PLUS:                 return "+";
    case ZSP_TOK_MINUS:                return "-";
    case ZSP_TOK_STAR:                 return "*";
    case ZSP_TOK_SLASH:                return "/";
    case ZSP_TOK_PERCENT:              return "%";
    case ZSP_TOK_BANG:                 return "!";
    case ZSP_TOK_TILDE:                return "~";
    case ZSP_TOK_AMP:                  return "&";
    case ZSP_TOK_PIPE:                 return "|";
    case ZSP_TOK_CARET:                return "^";
    case ZSP_TOK_LOGICAL_AND:          return "&&";
    case ZSP_TOK_LOGICAL_OR:           return "||";
    case ZSP_TOK_LSHIFT:               return "<<";
    case ZSP_TOK_RSHIFT:               return ">>";

    case ZSP_TOK_KIND_COUNT:           return "<invalid-token-kind>";
    }
    return "<invalid-token-kind>";
}
