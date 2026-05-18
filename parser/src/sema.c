/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Fabian Klebert
 */

#include "zsparser/sema.h"

#include <stdio.h>
#include <string.h>

/*
 * Semantic analyser — first slice. Builds a symbol table over
 * top-level declarations, walks the AST resolving type-ref and
 * identifier references, emits diagnostics for duplicates and
 * unresolved names. Resolved declarations land on
 * `node->resolved`.
 *
 * Scope stack is allocated from the same arena as the AST.
 * Visitor is iterative-with-stack (no recursion past the
 * recursive-descent deviation set in Q5; the scope-tree walk
 * is bounded by AST nesting depth).
 */

static const uint8_t *source_bytes(const zsp_sema_t *s, zsp_file_id_t fid)
{
    const zsp_file_t *f = NULL;
    if (!zsp_source_get(s->source, fid, &f)) {
        return NULL;
    }
    return f->bytes;
}

/* ----- Scope construction ------------------------------------- */

static zserio_status_t scope_new(zsp_sema_t *s, zsp_sema_scope_t *parent,
                                 zsp_sema_scope_t **out)
{
    zsp_sema_scope_t *const sc = (zsp_sema_scope_t *)zsp_arena_alloc(
            s->arena, sizeof(zsp_sema_scope_t), sizeof(void *));
    if (sc == NULL) {
        return ZSERIO_ERR_OUT_OF_MEMORY;
    }
    sc->parent   = parent;
    sc->bindings = NULL;
    sc->count    = 0U;
    sc->cap      = 0U;
    *out = sc;
    return ZSERIO_OK;
}

static zserio_status_t scope_grow(zsp_sema_t *s, zsp_sema_scope_t *sc)
{
    if (sc->count < sc->cap) {
        return ZSERIO_OK;
    }
    const size_t new_cap = (sc->cap == 0U) ? 8U : (sc->cap * 2U);
    zsp_sema_binding_t *const grown = (zsp_sema_binding_t *)zsp_arena_alloc(
            s->arena, new_cap * sizeof(zsp_sema_binding_t),
            sizeof(zsp_sema_binding_t));
    if (grown == NULL) {
        return ZSERIO_ERR_OUT_OF_MEMORY;
    }
    if (sc->bindings != NULL) {
        (void)memcpy(grown, sc->bindings,
                     sc->count * sizeof(zsp_sema_binding_t));
    }
    sc->bindings = grown;
    sc->cap      = new_cap;
    return ZSERIO_OK;
}

/* ----- Name fetch from AST nodes ------------------------------ */

/* Extract `(bytes, len)` for an IDENT or DOTTED_NAME's last
 * segment. Returns false if `n` doesn't have a name we can
 * extract. */
static bool extract_simple_name(const zsp_sema_t *s,
                                const zsp_ast_node_t *n,
                                const uint8_t **out_bytes, size_t *out_len)
{
    if (n == NULL) {
        return false;
    }
    const zsp_ast_node_t *target = n;
    if (n->kind == ZSP_AST_DOTTED_NAME) {
        if (n->u.dotted.n_parts == 0U) {
            return false;
        }
        target = n->u.dotted.parts[n->u.dotted.n_parts - 1U];
    }
    if (target->kind != ZSP_AST_EXPR_IDENT) {
        return false;
    }
    const uint8_t *const src = source_bytes(s, target->file_id);
    if (src == NULL) {
        return false;
    }
    *out_bytes = &src[target->u.ident.span_off];
    *out_len   = (size_t)target->u.ident.span_len;
    return true;
}

zsp_ast_node_t *zsp_sema_scope_find(const zsp_sema_scope_t *scope,
                                    const uint8_t *name, size_t len)
{
    const zsp_sema_scope_t *sc = scope;
    while (sc != NULL) {
        for (size_t i = 0U; i < sc->count; ++i) {
            const zsp_sema_binding_t *const b = &sc->bindings[i];
            if ((b->name_len == len)
                    && (memcmp(b->name, name, len) == 0)) {
                return b->decl;
            }
        }
        sc = sc->parent;
    }
    return NULL;
}

/* True for decls that can stand as the target of a TYPE_REF. */
static bool is_type_decl(const zsp_ast_node_t *d)
{
    if (d == NULL) return false;
    switch (d->kind) {
    case ZSP_AST_STRUCT:
    case ZSP_AST_UNION:
    case ZSP_AST_CHOICE:
    case ZSP_AST_ENUM:
    case ZSP_AST_BITMASK:
    case ZSP_AST_SUBTYPE:
    case ZSP_AST_INSTANTIATE:
    case ZSP_AST_TEMPLATE_PARAM:
    case ZSP_AST_SQL_TABLE:
    case ZSP_AST_SQL_DATABASE:
        return true;
    default:
        return false;
    }
}

/* File-aware lookup: when there are multiple bindings for the
 * same name (cross-package conflict), prefer one whose decl
 * lives in the given file. Falls back to any match. */
static zsp_ast_node_t *scope_find_prefer(const zsp_sema_scope_t *scope,
                                         const uint8_t *name, size_t len,
                                         zsp_file_id_t preferred_fid,
                                         bool preferred_valid)
{
    if (!preferred_valid) {
        return zsp_sema_scope_find(scope, name, len);
    }
    const zsp_sema_scope_t *sc = scope;
    zsp_ast_node_t *fallback = NULL;
    while (sc != NULL) {
        for (size_t i = 0U; i < sc->count; ++i) {
            const zsp_sema_binding_t *const b = &sc->bindings[i];
            if ((b->name_len == len)
                    && (memcmp(b->name, name, len) == 0)) {
                if ((b->decl != NULL)
                        && (b->decl->file_id == preferred_fid)) {
                    return b->decl;
                }
                if (fallback == NULL) {
                    fallback = b->decl;
                }
            }
        }
        sc = sc->parent;
    }
    return fallback;
}

/* Type-position lookup: same as scope_find_prefer but skips
 * non-type bindings (fields, parameters, case bindings) when
 * searching. This is needed because zserio's grammar allows
 * `Type Name;` where Type and Name refer to the same identifier
 * but in different namespaces — the type position must skip the
 * locally-bound field of the same name and find the outer type
 * declaration instead. Type-aware bindings still see their first
 * type-decl match before climbing further. */
static zsp_ast_node_t *scope_find_type_prefer(const zsp_sema_scope_t *scope,
                                              const uint8_t *name, size_t len,
                                              zsp_file_id_t preferred_fid,
                                              bool preferred_valid)
{
    const zsp_sema_scope_t *sc = scope;
    zsp_ast_node_t *fallback     = NULL;
    zsp_ast_node_t *any_fallback = NULL;
    while (sc != NULL) {
        for (size_t i = 0U; i < sc->count; ++i) {
            const zsp_sema_binding_t *const b = &sc->bindings[i];
            if ((b->name_len != len)
                    || (memcmp(b->name, name, len) != 0)) {
                continue;
            }
            if (is_type_decl(b->decl)) {
                if (preferred_valid && (b->decl != NULL)
                        && (b->decl->file_id == preferred_fid)) {
                    return b->decl;
                }
                if (fallback == NULL) {
                    fallback = b->decl;
                }
            }
            else if (any_fallback == NULL) {
                any_fallback = b->decl;
            }
        }
        sc = sc->parent;
    }
    if (fallback != NULL) return fallback;
    return any_fallback;
}

static zserio_status_t scope_bind(zsp_sema_t *s, zsp_sema_scope_t *sc,
                                  const uint8_t *name, size_t len,
                                  zsp_ast_node_t *decl)
{
    for (size_t i = 0U; i < sc->count; ++i) {
        const zsp_sema_binding_t *const b = &sc->bindings[i];
        if ((b->name_len == len) && (memcmp(b->name, name, len) == 0)) {
            /* Same-file duplicate is an error. Cross-file
             * duplicate is silently accepted as an additional
             * binding — the file-aware lookup will pick the
             * right one based on the resolver's current file_id. */
            if (b->decl != NULL && b->decl->file_id == decl->file_id) {
                (void)zsp_diag_emit_lit(s->diag, ZSP_SEV_ERROR,
                                        decl->file_id, decl->offset,
                                        "duplicate top-level declaration");
                return ZSERIO_OK;
            }
            /* Cross-file: fall through to add as a new binding. */
            break;
        }
    }
    ZSERIO_TRY(scope_grow(s, sc));
    sc->bindings[sc->count].name     = name;
    sc->bindings[sc->count].name_len = len;
    sc->bindings[sc->count].decl     = decl;
    sc->count += 1U;
    return ZSERIO_OK;
}

/* ----- Pass 1: collect top-level declarations ----------------- */

/* Returns the IDENT node carrying the name a declaration binds. */
static const zsp_ast_node_t *decl_name_node(const zsp_ast_node_t *d)
{
    switch (d->kind) {
    case ZSP_AST_STRUCT:
    case ZSP_AST_UNION:        return d->u.compound.name;
    case ZSP_AST_CHOICE:       return d->u.choice.name;
    case ZSP_AST_ENUM:         return d->u.enum_decl.name;
    case ZSP_AST_BITMASK:      return d->u.bitmask_decl.name;
    case ZSP_AST_SUBTYPE:      return d->u.subtype.alias_name;
    case ZSP_AST_CONST:        return d->u.const_decl.name;
    case ZSP_AST_INSTANTIATE:  return d->u.instantiate.name;
    case ZSP_AST_RULE_GROUP:   return d->u.rule_group.name;
    case ZSP_AST_SERVICE:      return d->u.service.name;
    case ZSP_AST_PUBSUB:       return d->u.pubsub.name;
    case ZSP_AST_SQL_TABLE:    return d->u.sql_table.name;
    case ZSP_AST_SQL_DATABASE: return d->u.sql_database.name;
    default:                   return NULL;
    }
}

static zserio_status_t collect_top_level(zsp_sema_t *s, zsp_ast_node_t *unit)
{
    if (unit->kind != ZSP_AST_UNIT) {
        return ZSERIO_OK;
    }
    for (size_t i = 0U; i < unit->u.unit.n_decls; ++i) {
        zsp_ast_node_t *const d = unit->u.unit.decls[i];
        if (d == NULL) {
            continue;
        }
        /* For top-level decls, repurpose `resolved` to point to
         * their enclosing unit. The emit layer reads this to
         * derive the decl's package path for cross-package
         * include emission. Top-level decls never have a
         * "resolved declaration" of their own, so the slot is
         * otherwise unused. */
        if (d->resolved == NULL) {
            d->resolved = unit;
        }
        const zsp_ast_node_t *const name = decl_name_node(d);
        const uint8_t *bytes = NULL;
        size_t         len   = 0U;
        if (!extract_simple_name(s, name, &bytes, &len)) {
            continue;
        }
        ZSERIO_TRY(scope_bind(s, s->root, bytes, len, d));
    }
    return ZSERIO_OK;
}

/* ----- Pass 2: resolve type-refs and identifiers -------------- */

/* Resolve TYPE_REF by looking up its name in the current scope.
 * Falls back to the last segment of the dotted name; package-qualified
 * lookup is a follow-up slice. */
static void resolve_type_ref(zsp_sema_t *s, zsp_ast_node_t *t)
{
    if (t->resolved != NULL) {
        return;
    }
    const uint8_t *bytes = NULL;
    size_t         len   = 0U;
    if (!extract_simple_name(s, t->u.type_ref.name, &bytes, &len)) {
        return;
    }
    /* For fully-qualified type references (DOTTED_NAME with
     * package prefix), pick the candidate whose enclosing
     * package path matches the prefix. Without this, multiple
     * decls sharing a simple name (e.g. several `Coordinate`s
     * in different packages) all hash to the first lookup hit. */
    zsp_ast_node_t *decl = NULL;
    if ((t->u.type_ref.name != NULL)
            && (t->u.type_ref.name->kind == ZSP_AST_DOTTED_NAME)
            && (t->u.type_ref.name->u.dotted.n_parts > 1U)) {
        /* Collect prefix segments. */
        const zsp_ast_node_t *const dn = t->u.type_ref.name;
        const size_t n_prefix = dn->u.dotted.n_parts - 1U;
        /* Scan every binding for matches whose decl's unit package
         * ends with the requested prefix. */
        for (const zsp_sema_scope_t *sc = s->current; sc != NULL && decl == NULL; sc = sc->parent) {
            for (size_t i = 0U; i < sc->count; ++i) {
                const zsp_sema_binding_t *const b = &sc->bindings[i];
                if ((b->name_len != len) || (memcmp(b->name, bytes, len) != 0)) continue;
                if (!is_type_decl(b->decl)) continue;
                /* Compare package suffix. */
                const zsp_ast_node_t *unit = b->decl->resolved;
                if ((unit == NULL) || (unit->kind != ZSP_AST_UNIT)) continue;
                const zsp_ast_node_t *pname = (unit->u.unit.package != NULL)
                        ? unit->u.unit.package->u.package.name : NULL;
                if (pname == NULL) continue;
                const zsp_ast_node_t * const *pkg_parts = NULL;
                size_t pkg_n = 0U;
                if (pname->kind == ZSP_AST_DOTTED_NAME) {
                    pkg_parts = (const zsp_ast_node_t * const *)pname->u.dotted.parts;
                    pkg_n     = pname->u.dotted.n_parts;
                }
                else { pkg_parts = (const zsp_ast_node_t * const *)&pname; pkg_n = 1U; }
                if (pkg_n < n_prefix) continue;
                bool match = true;
                for (size_t k = 0U; k < n_prefix; ++k) {
                    const uint8_t *pa = NULL, *pb = NULL;
                    size_t la = 0U, lb = 0U;
                    extract_simple_name(s, dn->u.dotted.parts[k], &pa, &la);
                    extract_simple_name(s, pkg_parts[pkg_n - n_prefix + k], &pb, &lb);
                    if (la != lb || memcmp(pa, pb, la) != 0) { match = false; break; }
                }
                if (match) { decl = b->decl; break; }
            }
        }
    }
    if (decl == NULL) {
        decl = scope_find_type_prefer(s->current, bytes, len, t->file_id, true);
    }
    if (decl == NULL) {
        (void)zsp_diag_emit_lit(s->diag, ZSP_SEV_ERROR,
                                t->file_id, t->offset,
                                "reference to undeclared type");
        return;
    }
    t->resolved = decl;
}

/* Iterative AST walker. The work list holds nodes to visit; for
 * each we resolve and enqueue children. Order doesn't matter for
 * pure reference resolution. */
typedef struct walk_stack {
    zsp_ast_node_t **items;
    size_t            count;
    size_t            cap;
    zsp_arena_t      *arena;
    bool              oom;
} walk_stack_t;

static void walk_push(walk_stack_t *st, zsp_ast_node_t *n)
{
    if (n == NULL) {
        return;
    }
    if (st->count == st->cap) {
        const size_t new_cap = (st->cap == 0U) ? 32U : (st->cap * 2U);
        zsp_ast_node_t **const grown = (zsp_ast_node_t **)zsp_arena_alloc(
                st->arena, new_cap * sizeof(zsp_ast_node_t *),
                sizeof(zsp_ast_node_t *));
        if (grown == NULL) {
            st->oom = true;
            return;
        }
        if (st->items != NULL) {
            (void)memcpy(grown, st->items, st->count * sizeof(zsp_ast_node_t *));
        }
        st->items = grown;
        st->cap   = new_cap;
    }
    st->items[st->count] = n;
    st->count += 1U;
}

static void walk_push_list(walk_stack_t *st,
                           zsp_ast_node_t **items, size_t n)
{
    for (size_t i = 0U; i < n; ++i) {
        walk_push(st, items[i]);
    }
}

static void enqueue_children(walk_stack_t *st, zsp_ast_node_t *n)
{
    if (n == NULL) {
        return;
    }
    switch (n->kind) {
    case ZSP_AST_UNIT:
        walk_push(st, n->u.unit.package);
        walk_push(st, n->u.unit.compat_version);
        walk_push_list(st, n->u.unit.imports, n->u.unit.n_imports);
        walk_push_list(st, n->u.unit.decls,   n->u.unit.n_decls);
        break;
    case ZSP_AST_PACKAGE:
        walk_push(st, n->u.package.name);
        break;
    case ZSP_AST_IMPORT:
        walk_push(st, n->u.import.name);
        break;
    case ZSP_AST_DOTTED_NAME:
        walk_push_list(st, n->u.dotted.parts, n->u.dotted.n_parts);
        break;
    case ZSP_AST_STRUCT:
    case ZSP_AST_UNION:
        walk_push(st, n->u.compound.name);
        walk_push_list(st, n->u.compound.template_params,
                       n->u.compound.n_template_params);
        walk_push_list(st, n->u.compound.params, n->u.compound.n_params);
        walk_push_list(st, n->u.compound.members, n->u.compound.n_members);
        break;
    case ZSP_AST_CHOICE:
        walk_push(st, n->u.choice.name);
        walk_push_list(st, n->u.choice.template_params,
                       n->u.choice.n_template_params);
        walk_push_list(st, n->u.choice.params, n->u.choice.n_params);
        walk_push(st, n->u.choice.selector);
        walk_push_list(st, n->u.choice.cases, n->u.choice.n_cases);
        walk_push(st, n->u.choice.default_case);
        break;
    case ZSP_AST_CHOICE_CASE:
        walk_push_list(st, n->u.choice_case.labels, n->u.choice_case.n_labels);
        walk_push(st, n->u.choice_case.field);
        break;
    case ZSP_AST_CHOICE_DEFAULT:
        walk_push(st, n->u.choice_default.field);
        break;
    case ZSP_AST_ENUM:
        walk_push(st, n->u.enum_decl.name);
        walk_push(st, n->u.enum_decl.base_type);
        walk_push_list(st, n->u.enum_decl.items, n->u.enum_decl.n_items);
        break;
    case ZSP_AST_ENUM_ITEM:
        walk_push(st, n->u.enum_item.name);
        walk_push(st, n->u.enum_item.value_expr);
        break;
    case ZSP_AST_BITMASK:
        walk_push(st, n->u.bitmask_decl.name);
        walk_push(st, n->u.bitmask_decl.base_type);
        walk_push_list(st, n->u.bitmask_decl.items, n->u.bitmask_decl.n_items);
        break;
    case ZSP_AST_BITMASK_ITEM:
        walk_push(st, n->u.bitmask_item.name);
        walk_push(st, n->u.bitmask_item.value_expr);
        break;
    case ZSP_AST_SUBTYPE:
        walk_push(st, n->u.subtype.target_type);
        walk_push(st, n->u.subtype.alias_name);
        break;
    case ZSP_AST_CONST:
        walk_push(st, n->u.const_decl.type);
        walk_push(st, n->u.const_decl.name);
        walk_push(st, n->u.const_decl.value_expr);
        break;
    case ZSP_AST_INSTANTIATE:
        walk_push(st, n->u.instantiate.name);
        walk_push(st, n->u.instantiate.target);
        break;
    case ZSP_AST_RULE_GROUP:
        walk_push(st, n->u.rule_group.name);
        walk_push_list(st, n->u.rule_group.rules, n->u.rule_group.n_rules);
        break;
    case ZSP_AST_RULE:
        walk_push(st, n->u.rule.name);
        walk_push(st, n->u.rule.id_expr);
        walk_push(st, n->u.rule.body_expr);
        break;
    case ZSP_AST_FUNCTION:
        walk_push(st, n->u.function_decl.return_type);
        walk_push(st, n->u.function_decl.name);
        walk_push(st, n->u.function_decl.body_expr);
        break;
    case ZSP_AST_FIELD:
        walk_push_list(st, n->u.field.annotations, n->u.field.n_annotations);
        walk_push(st, n->u.field.align_expr);
        walk_push(st, n->u.field.offset_expr);
        walk_push(st, n->u.field.type);
        walk_push(st, n->u.field.name);
        walk_push(st, n->u.field.init_expr);
        walk_push(st, n->u.field.if_clause);
        walk_push(st, n->u.field.constraint_expr);
        break;
    case ZSP_AST_PARAMETER:
        walk_push(st, n->u.parameter.type);
        walk_push(st, n->u.parameter.name);
        break;
    case ZSP_AST_TYPE_REF:
        walk_push(st, n->u.type_ref.name);
        walk_push_list(st, n->u.type_ref.template_args,
                       n->u.type_ref.n_template_args);
        walk_push_list(st, n->u.type_ref.type_args, n->u.type_ref.n_type_args);
        break;
    case ZSP_AST_TYPE_BUILTIN:
        walk_push(st, n->u.type_builtin.width_expr);
        break;
    case ZSP_AST_TYPE_ARRAY:
        walk_push(st, n->u.type_array.element);
        walk_push(st, n->u.type_array.length_expr);
        break;
    case ZSP_AST_EXPR_BINARY:
        walk_push(st, n->u.binop.lhs);
        walk_push(st, n->u.binop.rhs);
        break;
    case ZSP_AST_EXPR_UNARY:
        walk_push(st, n->u.unop.operand);
        break;
    case ZSP_AST_EXPR_TERNARY:
        walk_push(st, n->u.ternary.cond);
        walk_push(st, n->u.ternary.then_expr);
        walk_push(st, n->u.ternary.else_expr);
        break;
    case ZSP_AST_EXPR_MEMBER:
        walk_push(st, n->u.member.lhs);
        /* The name on the RHS of a member access does not get
         * resolved against the global scope — it's looked up on
         * the LHS's type. Skip in this slice. */
        break;
    case ZSP_AST_EXPR_INDEX:
        walk_push(st, n->u.index.lhs);
        walk_push(st, n->u.index.index);
        break;
    case ZSP_AST_EXPR_CALL:
        walk_push(st, n->u.call.callee);
        walk_push_list(st, n->u.call.args, n->u.call.n_args);
        break;
    case ZSP_AST_SERVICE:
        walk_push(st, n->u.service.name);
        walk_push_list(st, n->u.service.methods, n->u.service.n_methods);
        break;
    case ZSP_AST_SERVICE_METHOD:
        walk_push(st, n->u.service_method.return_type);
        walk_push(st, n->u.service_method.name);
        walk_push(st, n->u.service_method.request_type);
        break;
    case ZSP_AST_PUBSUB:
        walk_push(st, n->u.pubsub.name);
        walk_push_list(st, n->u.pubsub.messages, n->u.pubsub.n_messages);
        break;
    case ZSP_AST_PUBSUB_MESSAGE:
        walk_push(st, n->u.pubsub_message.topic_expr);
        walk_push(st, n->u.pubsub_message.type);
        walk_push(st, n->u.pubsub_message.name);
        break;
    case ZSP_AST_SQL_TABLE:
        walk_push(st, n->u.sql_table.name);
        walk_push_list(st, n->u.sql_table.fields, n->u.sql_table.n_fields);
        walk_push_list(st, n->u.sql_table.constraints,
                       n->u.sql_table.n_constraints);
        walk_push(st, n->u.sql_table.virtual_using);
        break;
    case ZSP_AST_SQL_FIELD:
        walk_push(st, n->u.sql_field.type);
        walk_push(st, n->u.sql_field.name);
        walk_push(st, n->u.sql_field.constraint_expr);
        break;
    case ZSP_AST_SQL_DATABASE:
        walk_push(st, n->u.sql_database.name);
        walk_push_list(st, n->u.sql_database.table_refs,
                       n->u.sql_database.n_table_refs);
        break;
    case ZSP_AST_SQL_TABLE_REF:
        walk_push(st, n->u.sql_table_ref.type);
        walk_push(st, n->u.sql_table_ref.name);
        break;
    case ZSP_AST_ANNOTATION:
        walk_push(st, n->u.annotation.message);
        break;
    case ZSP_AST_TEMPLATE_PARAM:
        walk_push(st, n->u.template_param.name);
        break;
    case ZSP_AST_COMPAT_VERSION:
        walk_push(st, n->u.compat_version.version_string);
        break;
    default:
        /* Atomic literals + EXPR_IDENT have no children to push. */
        break;
    }
}

/* Bind a single field/parameter/template-param's name into the
 * given scope. Silently skips nodes we can't extract a name
 * from. Used during compound-scope construction. */
static zserio_status_t bind_member(zsp_sema_t *s, zsp_sema_scope_t *sc,
                                   zsp_ast_node_t *member)
{
    if (member == NULL) {
        return ZSERIO_OK;
    }
    const zsp_ast_node_t *name_node = NULL;
    switch (member->kind) {
    case ZSP_AST_FIELD:           name_node = member->u.field.name;            break;
    case ZSP_AST_PARAMETER:       name_node = member->u.parameter.name;        break;
    case ZSP_AST_TEMPLATE_PARAM:  name_node = member->u.template_param.name;   break;
    case ZSP_AST_FUNCTION:        name_node = member->u.function_decl.name;    break;
    case ZSP_AST_ENUM_ITEM:       name_node = member->u.enum_item.name;        break;
    case ZSP_AST_BITMASK_ITEM:    name_node = member->u.bitmask_item.name;     break;
    case ZSP_AST_SQL_FIELD:       name_node = member->u.sql_field.name;        break;
    case ZSP_AST_SQL_TABLE_REF:   name_node = member->u.sql_table_ref.name;    break;
    case ZSP_AST_SERVICE_METHOD:  name_node = member->u.service_method.name;   break;
    case ZSP_AST_PUBSUB_MESSAGE:  name_node = member->u.pubsub_message.name;   break;
    case ZSP_AST_CHOICE_CASE:     name_node = (member->u.choice_case.field != NULL)
                                              ? member->u.choice_case.field->u.field.name
                                              : NULL; break;
    case ZSP_AST_CHOICE_DEFAULT:  name_node = (member->u.choice_default.field != NULL)
                                              ? member->u.choice_default.field->u.field.name
                                              : NULL; break;
    default:                      break;
    }
    if (name_node == NULL) {
        return ZSERIO_OK;
    }
    const uint8_t *bytes = NULL;
    size_t         len   = 0U;
    if (!extract_simple_name(s, name_node, &bytes, &len)) {
        return ZSERIO_OK;
    }
    /* Per-compound scope: duplicate field names are a hard error
     * since members share a single namespace. */
    for (size_t i = 0U; i < sc->count; ++i) {
        const zsp_sema_binding_t *const b = &sc->bindings[i];
        if ((b->name_len == len) && (memcmp(b->name, bytes, len) == 0)) {
            (void)zsp_diag_emit_lit(s->diag, ZSP_SEV_ERROR,
                                    member->file_id, member->offset,
                                    "duplicate member name in compound type");
            return ZSERIO_OK;
        }
    }
    ZSERIO_TRY(scope_grow(s, sc));
    sc->bindings[sc->count].name     = bytes;
    sc->bindings[sc->count].name_len = len;
    sc->bindings[sc->count].decl     = member;
    sc->count += 1U;
    return ZSERIO_OK;
}

/* Build a fresh scope for a compound's body and bind its
 * template params + parameters + members. Returns the new scope
 * (which is also pushed as the current). */
static zserio_status_t enter_compound_scope(zsp_sema_t *s,
                                            zsp_ast_node_t *cmpd,
                                            zsp_sema_scope_t **out_scope)
{
    zsp_sema_scope_t *scope = NULL;
    ZSERIO_TRY(scope_new(s, s->current, &scope));

    if ((cmpd->kind == ZSP_AST_STRUCT) || (cmpd->kind == ZSP_AST_UNION)) {
        for (size_t i = 0U; i < cmpd->u.compound.n_template_params; ++i) {
            ZSERIO_TRY(bind_member(s, scope,
                                   cmpd->u.compound.template_params[i]));
        }
        for (size_t i = 0U; i < cmpd->u.compound.n_params; ++i) {
            ZSERIO_TRY(bind_member(s, scope, cmpd->u.compound.params[i]));
        }
        for (size_t i = 0U; i < cmpd->u.compound.n_members; ++i) {
            ZSERIO_TRY(bind_member(s, scope, cmpd->u.compound.members[i]));
        }
    }
    else if (cmpd->kind == ZSP_AST_CHOICE) {
        for (size_t i = 0U; i < cmpd->u.choice.n_template_params; ++i) {
            ZSERIO_TRY(bind_member(s, scope,
                                   cmpd->u.choice.template_params[i]));
        }
        for (size_t i = 0U; i < cmpd->u.choice.n_params; ++i) {
            ZSERIO_TRY(bind_member(s, scope, cmpd->u.choice.params[i]));
        }
        for (size_t i = 0U; i < cmpd->u.choice.n_cases; ++i) {
            ZSERIO_TRY(bind_member(s, scope, cmpd->u.choice.cases[i]));
        }
        if (cmpd->u.choice.default_case != NULL) {
            ZSERIO_TRY(bind_member(s, scope, cmpd->u.choice.default_case));
        }
    }
    else if (cmpd->kind == ZSP_AST_ENUM) {
        for (size_t i = 0U; i < cmpd->u.enum_decl.n_items; ++i) {
            ZSERIO_TRY(bind_member(s, scope, cmpd->u.enum_decl.items[i]));
        }
    }
    else if (cmpd->kind == ZSP_AST_BITMASK) {
        for (size_t i = 0U; i < cmpd->u.bitmask_decl.n_items; ++i) {
            ZSERIO_TRY(bind_member(s, scope, cmpd->u.bitmask_decl.items[i]));
        }
    }
    else if (cmpd->kind == ZSP_AST_SQL_TABLE) {
        for (size_t i = 0U; i < cmpd->u.sql_table.n_template_params; ++i) {
            ZSERIO_TRY(bind_member(s, scope,
                                   cmpd->u.sql_table.template_params[i]));
        }
        for (size_t i = 0U; i < cmpd->u.sql_table.n_fields; ++i) {
            ZSERIO_TRY(bind_member(s, scope, cmpd->u.sql_table.fields[i]));
        }
    }
    else if (cmpd->kind == ZSP_AST_SQL_DATABASE) {
        for (size_t i = 0U; i < cmpd->u.sql_database.n_table_refs; ++i) {
            ZSERIO_TRY(bind_member(s, scope, cmpd->u.sql_database.table_refs[i]));
        }
    }
    else if (cmpd->kind == ZSP_AST_SERVICE) {
        for (size_t i = 0U; i < cmpd->u.service.n_methods; ++i) {
            ZSERIO_TRY(bind_member(s, scope, cmpd->u.service.methods[i]));
        }
    }
    else if (cmpd->kind == ZSP_AST_PUBSUB) {
        for (size_t i = 0U; i < cmpd->u.pubsub.n_messages; ++i) {
            ZSERIO_TRY(bind_member(s, scope, cmpd->u.pubsub.messages[i]));
        }
    }

    s->current = scope;
    *out_scope = scope;
    return ZSERIO_OK;
}

static bool is_scope_owner(zsp_ast_kind_t k)
{
    return (k == ZSP_AST_STRUCT) || (k == ZSP_AST_UNION) || (k == ZSP_AST_CHOICE)
        || (k == ZSP_AST_ENUM) || (k == ZSP_AST_BITMASK)
        || (k == ZSP_AST_SQL_TABLE) || (k == ZSP_AST_SQL_DATABASE)
        || (k == ZSP_AST_SERVICE) || (k == ZSP_AST_PUBSUB);
}

/* Resolve EXPR_IDENT against the lexical scope chain. */
static void resolve_expr_ident(zsp_sema_t *s, zsp_ast_node_t *e)
{
    if (e->resolved != NULL) {
        return;
    }
    const uint8_t *const src = source_bytes(s, e->file_id);
    if (src == NULL) {
        return;
    }
    const uint8_t *const name = &src[e->u.ident.span_off];
    const size_t         len  = (size_t)e->u.ident.span_len;
    /* `@index` and a few other pseudo-idents start with '@' and
     * are not in any scope. Skip those. */
    if ((len > 0U) && (name[0] == (uint8_t)'@')) {
        return;
    }
    zsp_ast_node_t *const decl = scope_find_prefer(
            s->current, name, len, e->file_id, true);
    if (decl == NULL) {
        /* Identifier resolution is best-effort: it may legitimately
         * fail for member-access RHS, package-qualified references,
         * or template-parameter uses inside instantiations. We
         * emit no diagnostic here; only TYPE_REF (which definitely
         * names a type) gets the strict-error treatment. */
        return;
    }
    e->resolved = decl;
}

/* Resolve a member access `LHS.name` where LHS has resolved to
 * an enum, bitmask, or compound declaration. The RHS resolves
 * to one of the LHS's items. Sets `member->resolved` on success;
 * silently leaves NULL when LHS isn't enum/bitmask/compound
 * shape (which is fine — that case will be picked up by a
 * future expression-typing pass). */
static void resolve_member_access(zsp_sema_t *s, zsp_ast_node_t *m)
{
    if (m->resolved != NULL) {
        return;
    }
    zsp_ast_node_t *const lhs = m->u.member.lhs;
    zsp_ast_node_t *const rhs = m->u.member.name;
    if ((lhs == NULL) || (rhs == NULL)) {
        return;
    }
    /* The LHS must have resolved to a decl. EXPR_IDENT resolves
     * via lexical scope; EXPR_MEMBER recursively resolves via
     * this same function. */
    zsp_ast_node_t *base = NULL;
    if (lhs->kind == ZSP_AST_EXPR_IDENT) {
        base = lhs->resolved;
    }
    else if (lhs->kind == ZSP_AST_EXPR_MEMBER) {
        base = lhs->resolved;
    }
    /* Fully-qualified package path fallback: when LHS is an
     * unresolved dotted chain (e.g. `pkg1.pkg2.Color`), treat
     * the rightmost segment as a type name and look it up via
     * the root scope. This is how zserio's enum literals are
     * spelled across package boundaries. */
    if ((base == NULL) && (lhs->kind == ZSP_AST_EXPR_MEMBER)) {
        const zsp_ast_node_t *type_ident = lhs->u.member.name;
        if ((type_ident != NULL) && (type_ident->kind == ZSP_AST_EXPR_IDENT)) {
            const uint8_t *tb = NULL;
            size_t         tl = 0U;
            if (extract_simple_name(s, type_ident, &tb, &tl)) {
                zsp_ast_node_t *const found = scope_find_type_prefer(
                        s->root, tb, tl, m->file_id, true);
                if ((found != NULL)
                        && ((found->kind == ZSP_AST_ENUM)
                            || (found->kind == ZSP_AST_BITMASK)
                            || (found->kind == ZSP_AST_STRUCT)
                            || (found->kind == ZSP_AST_UNION)
                            || (found->kind == ZSP_AST_SUBTYPE))) {
                    base = found;
                    lhs->resolved = found;
                }
            }
        }
    }
    /* Top-level decl reached via package path: `pkg1.pkg2.CONST`
     * or `pkg1.pkg2.Type` where the LHS chain is just packages
     * (no nested type). Look up the rhs name at the root scope
     * and shortcut: resolve the member directly. */
    if (base == NULL) {
        const uint8_t *rb = NULL;
        size_t         rl = 0U;
        if (extract_simple_name(s, rhs, &rb, &rl)) {
            zsp_ast_node_t *const found = scope_find_prefer(
                    s->root, rb, rl, m->file_id, true);
            if (found != NULL) {
                m->resolved   = found;
                rhs->resolved = found;
                return;
            }
        }
        return;
    }

    const uint8_t *const src = source_bytes(s, rhs->file_id);
    if (src == NULL) {
        return;
    }
    const uint8_t *const name = &src[rhs->u.ident.span_off];
    const size_t         len  = (size_t)rhs->u.ident.span_len;

    /* Search items by name on enum/bitmask. For compound types
     * (struct/union/choice), search field names. */
    if (base->kind == ZSP_AST_ENUM) {
        for (size_t i = 0U; i < base->u.enum_decl.n_items; ++i) {
            zsp_ast_node_t *const item = base->u.enum_decl.items[i];
            const uint8_t *ib = NULL;
            size_t         il = 0U;
            if (extract_simple_name(s, item->u.enum_item.name, &ib, &il)
                    && (il == len) && (memcmp(ib, name, len) == 0)) {
                m->resolved   = item;
                rhs->resolved = item;
                return;
            }
        }
    }
    else if (base->kind == ZSP_AST_BITMASK) {
        for (size_t i = 0U; i < base->u.bitmask_decl.n_items; ++i) {
            zsp_ast_node_t *const item = base->u.bitmask_decl.items[i];
            const uint8_t *ib = NULL;
            size_t         il = 0U;
            if (extract_simple_name(s, item->u.bitmask_item.name, &ib, &il)
                    && (il == len) && (memcmp(ib, name, len) == 0)) {
                m->resolved   = item;
                rhs->resolved = item;
                return;
            }
        }
    }
    else if ((base->kind == ZSP_AST_STRUCT) || (base->kind == ZSP_AST_UNION)) {
        for (size_t i = 0U; i < base->u.compound.n_members; ++i) {
            zsp_ast_node_t *const mem = base->u.compound.members[i];
            const uint8_t *mb = NULL;
            size_t         ml = 0U;
            const zsp_ast_node_t *name_node = NULL;
            if (mem->kind == ZSP_AST_FIELD) name_node = mem->u.field.name;
            else if (mem->kind == ZSP_AST_FUNCTION) name_node = mem->u.function_decl.name;
            else if (mem->kind == ZSP_AST_PARAMETER) name_node = mem->u.parameter.name;
            if ((name_node != NULL)
                    && extract_simple_name(s, name_node, &mb, &ml)
                    && (ml == len) && (memcmp(mb, name, len) == 0)) {
                m->resolved   = mem;
                rhs->resolved = mem;
                return;
            }
        }
    }
    /* Field or parameter: look through the field/parameter's
     * type to the underlying struct/union/choice and search
     * its members. This lets `<field>.<member>` and
     * `<param>.<member>` (member-function calls included)
     * resolve. */
    else if ((base->kind == ZSP_AST_FIELD)
             || (base->kind == ZSP_AST_PARAMETER)) {
        zsp_ast_node_t *ty = (base->kind == ZSP_AST_FIELD)
                ? base->u.field.type : base->u.parameter.type;
        /* Peel SUBTYPE chains. */
        while ((ty != NULL) && (ty->kind == ZSP_AST_TYPE_REF)
                && (ty->resolved != NULL)
                && (ty->resolved->kind == ZSP_AST_SUBTYPE)
                && (ty->resolved->u.subtype.target_type != NULL)) {
            ty = ty->resolved->u.subtype.target_type;
        }
        zsp_ast_node_t *type_decl = NULL;
        if ((ty != NULL) && (ty->kind == ZSP_AST_TYPE_REF)) {
            type_decl = ty->resolved;
        }
        if ((type_decl != NULL)
                && ((type_decl->kind == ZSP_AST_STRUCT)
                    || (type_decl->kind == ZSP_AST_UNION)
                    || (type_decl->kind == ZSP_AST_CHOICE))) {
            zsp_ast_node_t **members = NULL;
            size_t n_members = 0U;
            if (type_decl->kind == ZSP_AST_CHOICE) {
                members   = type_decl->u.choice.params;  /* params then case fields */
                n_members = type_decl->u.choice.n_params;
                /* Iterate case-field children separately below. */
            } else {
                members   = type_decl->u.compound.members;
                n_members = type_decl->u.compound.n_members;
            }
            for (size_t i = 0U; i < n_members; ++i) {
                zsp_ast_node_t *const mem = members[i];
                const zsp_ast_node_t *name_node = NULL;
                if (mem->kind == ZSP_AST_FIELD)         name_node = mem->u.field.name;
                else if (mem->kind == ZSP_AST_FUNCTION) name_node = mem->u.function_decl.name;
                else if (mem->kind == ZSP_AST_PARAMETER) name_node = mem->u.parameter.name;
                const uint8_t *mb = NULL;
                size_t ml = 0U;
                if ((name_node != NULL)
                        && extract_simple_name(s, name_node, &mb, &ml)
                        && (ml == len) && (memcmp(mb, name, len) == 0)) {
                    m->resolved   = mem;
                    rhs->resolved = mem;
                    return;
                }
            }
        }
    }
    /* Subtype: peel one layer and try again. */
    else if (base->kind == ZSP_AST_SUBTYPE) {
        zsp_ast_node_t *target = base->u.subtype.target_type;
        if ((target != NULL) && (target->resolved != NULL)) {
            /* Construct a transient member access shape and recurse.
             * For now, just walk: temporarily swap base out. */
            zsp_ast_node_t *const inner = target->resolved;
            /* If the subtype's target resolves further, defer; the
             * recursion below covers single-level alias. */
            if ((inner->kind == ZSP_AST_ENUM) || (inner->kind == ZSP_AST_BITMASK)
                    || (inner->kind == ZSP_AST_STRUCT) || (inner->kind == ZSP_AST_UNION)) {
                /* Splice: temporarily set lhs->resolved = inner so a
                 * recursive call finds the items. */
                zsp_ast_node_t *const orig = lhs->resolved;
                lhs->resolved = inner;
                resolve_member_access(s, m);
                lhs->resolved = orig;
            }
        }
    }
}

/* Resolve every TYPE_REF + EXPR_IDENT under `n`, with scope
 * push/pop around compound bodies. Recursive — bounded by AST
 * nesting depth, covered by the Q5 / Rule 17.2 deviation. */
static void resolve_label_expr(zsp_sema_t *s, zsp_ast_node_t *e,
                               zsp_ast_node_t *enum_decl);

static zserio_status_t resolve_walk(zsp_sema_t *s, zsp_ast_node_t *n)
{
    if (n == NULL) {
        return ZSERIO_OK;
    }

    zsp_sema_scope_t *saved = NULL;
    if (is_scope_owner(n->kind)) {
        saved = s->current;
        zsp_sema_scope_t *new_scope = NULL;
        ZSERIO_TRY(enter_compound_scope(s, n, &new_scope));
        (void)new_scope;
    }

    if (n->kind == ZSP_AST_TYPE_REF) {
        resolve_type_ref(s, n);
    }
    else if (n->kind == ZSP_AST_EXPR_IDENT) {
        resolve_expr_ident(s, n);
    }
    else if (n->kind == ZSP_AST_EXPR_MEMBER) {
        /* Defer until LHS is resolved; we walk children first
         * below, then resolve member access on the way back. */
    }

    /* Walk children. We can't reuse the iterative enqueue_children
     * verbatim here because we need recursive descent — recurse
     * into each child to maintain scope discipline. */
    walk_stack_t st = { NULL, 0U, 0U, s->arena, false };
    enqueue_children(&st, n);
    if (st.oom) {
        if (saved != NULL) {
            s->current = saved;
        }
        return ZSERIO_ERR_OUT_OF_MEMORY;
    }
    for (size_t i = 0U; i < st.count; ++i) {
        ZSERIO_TRY(resolve_walk(s, st.items[i]));
    }

    /* Member-access resolves after the LHS has had its chance to
     * resolve via the child walk. */
    if (n->kind == ZSP_AST_EXPR_MEMBER) {
        resolve_member_access(s, n);
    }
    /* `isset(target, FLAG)`: zserio allows bare flag spellings
     * inside the second arg when the target's type is a bitmask
     * or enum. After child walk has resolved the target, look
     * for unresolved IDENTs in the second arg and bind them
     * to the matching item on the target's type. */
    if ((n->kind == ZSP_AST_EXPR_CALL)
            && (n->u.call.callee != NULL)
            && (n->u.call.callee->kind == ZSP_AST_EXPR_IDENT)
            && (n->u.call.n_args == 2U)) {
        const zsp_file_t *fc = NULL;
        if (zsp_source_get(s->source, n->u.call.callee->file_id, &fc)) {
            const uint8_t *kw = &fc->bytes[n->u.call.callee->u.ident.span_off];
            const size_t   kl = (size_t)n->u.call.callee->u.ident.span_len;
            if ((kl == 5U) && (memcmp(kw, "isset", 5U) == 0)) {
                /* Find target's enum/bitmask decl. */
                zsp_ast_node_t *const tgt = n->u.call.args[0];
                zsp_ast_node_t *type_decl = NULL;
                if ((tgt != NULL) && (tgt->resolved != NULL)) {
                    zsp_ast_node_t *d = tgt->resolved;
                    zsp_ast_node_t *ty = NULL;
                    if (d->kind == ZSP_AST_FIELD)         ty = d->u.field.type;
                    else if (d->kind == ZSP_AST_PARAMETER) ty = d->u.parameter.type;
                    while ((ty != NULL) && (ty->kind == ZSP_AST_TYPE_REF)
                            && (ty->resolved != NULL)
                            && (ty->resolved->kind == ZSP_AST_SUBTYPE)
                            && (ty->resolved->u.subtype.target_type != NULL)) {
                        ty = ty->resolved->u.subtype.target_type;
                    }
                    if ((ty != NULL) && (ty->kind == ZSP_AST_TYPE_REF)
                            && (ty->resolved != NULL)
                            && ((ty->resolved->kind == ZSP_AST_BITMASK)
                                || (ty->resolved->kind == ZSP_AST_ENUM))) {
                        type_decl = ty->resolved;
                    }
                }
                /* Walk the flag expression and resolve bare
                 * IDENTs against type_decl's items. Reuse the
                 * existing label-resolution helper. */
                if (type_decl != NULL) {
                    resolve_label_expr(s, n->u.call.args[1], type_decl);
                }
            }
        }
    }

    if (saved != NULL) {
        s->current = saved;
    }
    return ZSERIO_OK;
}

/* Get the resolved enum/bitmask declaration that a selector
 * expression's value would have. Walks through field/parameter
 * resolution to find the type's resolved decl. */
static zsp_ast_node_t *selector_enum_decl(const zsp_ast_node_t *selector)
{
    if (selector == NULL) return NULL;
    const zsp_ast_node_t *d = NULL;
    if (selector->kind == ZSP_AST_EXPR_IDENT) {
        d = selector->resolved;
    }
    else if (selector->kind == ZSP_AST_EXPR_MEMBER) {
        d = selector->resolved;
    }
    if (d == NULL) return NULL;
    const zsp_ast_node_t *type = NULL;
    if (d->kind == ZSP_AST_FIELD) {
        type = d->u.field.type;
    }
    else if (d->kind == ZSP_AST_PARAMETER) {
        type = d->u.parameter.type;
    }
    if ((type != NULL) && (type->kind == ZSP_AST_TYPE_REF)
            && (type->resolved != NULL)) {
        zsp_ast_node_t *target = type->resolved;
        /* Peel subtypes. */
        while ((target != NULL) && (target->kind == ZSP_AST_SUBTYPE)
                && (target->u.subtype.target_type != NULL)
                && (target->u.subtype.target_type->resolved != NULL)) {
            target = target->u.subtype.target_type->resolved;
        }
        if (target && ((target->kind == ZSP_AST_ENUM)
                || (target->kind == ZSP_AST_BITMASK))) {
            return target;
        }
    }
    return NULL;
}

/* Look up `name` among an enum/bitmask's items. */
static zsp_ast_node_t *enum_find_item(const zsp_sema_t *s,
                                      const zsp_ast_node_t *decl,
                                      const uint8_t *name, size_t len)
{
    const zsp_ast_node_t * const *items;
    size_t n;
    if (decl->kind == ZSP_AST_ENUM) {
        items = (const zsp_ast_node_t * const *)decl->u.enum_decl.items;
        n     = decl->u.enum_decl.n_items;
    }
    else if (decl->kind == ZSP_AST_BITMASK) {
        items = (const zsp_ast_node_t * const *)decl->u.bitmask_decl.items;
        n     = decl->u.bitmask_decl.n_items;
    }
    else {
        return NULL;
    }
    for (size_t i = 0U; i < n; ++i) {
        const zsp_ast_node_t *item = items[i];
        const zsp_ast_node_t *iname = (decl->kind == ZSP_AST_ENUM)
                ? item->u.enum_item.name : item->u.bitmask_item.name;
        const uint8_t *ib = NULL;
        size_t         il = 0U;
        if (extract_simple_name(s, iname, &ib, &il)
                && (il == len) && (memcmp(ib, name, len) == 0)) {
            return (zsp_ast_node_t *)(uintptr_t)item;
        }
    }
    return NULL;
}

/* Post-pass: for each CHOICE, resolve any unresolved case-label
 * idents against the selector's enum/bitmask items. */
/* Walk an expression and resolve any bare IDENT that names an
 * item of `enum_decl`. The bitmask flag-OR pattern `case (RED |
 * GREEN):` lands here so each operand resolves to its item. */
static void resolve_label_expr(zsp_sema_t *s, zsp_ast_node_t *e,
                               zsp_ast_node_t *enum_decl)
{
    if ((e == NULL) || (enum_decl == NULL)) return;
    if ((e->kind == ZSP_AST_EXPR_IDENT) && (e->resolved == NULL)) {
        const zsp_file_t *f = NULL;
        if (zsp_source_get(s->source, e->file_id, &f)) {
            const uint8_t *nm = &f->bytes[e->u.ident.span_off];
            const size_t   nl = (size_t)e->u.ident.span_len;
            zsp_ast_node_t *item = enum_find_item(s, enum_decl, nm, nl);
            if (item != NULL) {
                e->resolved = item;
            }
        }
        return;
    }
    switch (e->kind) {
    case ZSP_AST_EXPR_BINARY:
        resolve_label_expr(s, e->u.binop.lhs, enum_decl);
        resolve_label_expr(s, e->u.binop.rhs, enum_decl);
        break;
    case ZSP_AST_EXPR_UNARY:
        resolve_label_expr(s, e->u.unop.operand, enum_decl);
        break;
    case ZSP_AST_EXPR_TERNARY:
        resolve_label_expr(s, e->u.ternary.cond, enum_decl);
        resolve_label_expr(s, e->u.ternary.then_expr, enum_decl);
        resolve_label_expr(s, e->u.ternary.else_expr, enum_decl);
        break;
    case ZSP_AST_EXPR_MEMBER:
        /* `<Type>.<ITEM>` references resolve through the regular
         * member-access path; nothing to do here. */
        break;
    default:
        break;
    }
}

static void resolve_choice_labels(zsp_sema_t *s, zsp_ast_node_t *n)
{
    if (n == NULL) return;
    if (n->kind == ZSP_AST_CHOICE) {
        zsp_ast_node_t *const enum_decl = selector_enum_decl(n->u.choice.selector);
        if (enum_decl != NULL) {
            for (size_t i = 0U; i < n->u.choice.n_cases; ++i) {
                zsp_ast_node_t *c = n->u.choice.cases[i];
                for (size_t j = 0U; j < c->u.choice_case.n_labels; ++j) {
                    resolve_label_expr(s, c->u.choice_case.labels[j], enum_decl);
                }
            }
        }
    }

    /* Recurse into children — small, bounded. */
    walk_stack_t st = { NULL, 0U, 0U, s->arena, false };
    enqueue_children(&st, n);
    for (size_t i = 0U; i < st.count; ++i) {
        resolve_choice_labels(s, st.items[i]);
    }
}

static zserio_status_t resolve_pass(zsp_sema_t *s, zsp_ast_node_t *unit)
{
    ZSERIO_TRY(resolve_walk(s, unit));
    resolve_choice_labels(s, unit);
    return ZSERIO_OK;
}

/* ============================================================ */
/* Constant evaluation                                           */
/* ============================================================ */

static zsp_sema_const_t CYCLE_SENTINEL;

const zsp_sema_const_t *zsp_sema_const_cycle_marker(void)
{
    return &CYCLE_SENTINEL;
}

static zsp_sema_const_t *alloc_const(zsp_sema_t *s, zsp_const_kind_t k)
{
    zsp_sema_const_t *const c = (zsp_sema_const_t *)zsp_arena_alloc(
            s->arena, sizeof(zsp_sema_const_t), sizeof(void *));
    if (c == NULL) {
        return NULL;
    }
    c->kind = k;
    return c;
}

static const zsp_sema_const_t *eval_uint(zsp_sema_t *s, uint64_t v)
{
    zsp_sema_const_t *const c = alloc_const(s, ZSP_CONST_UINT);
    if (c == NULL) return NULL;
    c->v.u = v;
    return c;
}

static const zsp_sema_const_t *eval_bool(zsp_sema_t *s, bool v)
{
    zsp_sema_const_t *const c = alloc_const(s, ZSP_CONST_BOOL);
    if (c == NULL) return NULL;
    c->v.b = v;
    return c;
}

static const zsp_sema_const_t *eval_float(zsp_sema_t *s, double v)
{
    zsp_sema_const_t *const c = alloc_const(s, ZSP_CONST_FLOAT);
    if (c == NULL) return NULL;
    c->v.f = v;
    return c;
}

static uint64_t to_u64(const zsp_sema_const_t *c)
{
    if (c->kind == ZSP_CONST_BOOL) return c->v.b ? 1U : 0U;
    if (c->kind == ZSP_CONST_INT)  return (uint64_t)c->v.i;
    return c->v.u;
}

static const zsp_sema_const_t *eval_binop(zsp_sema_t *s, zsp_op_code_t op,
                                          const zsp_sema_const_t *l,
                                          const zsp_sema_const_t *r)
{
    if ((l->kind == ZSP_CONST_FLOAT) || (r->kind == ZSP_CONST_FLOAT)) {
        const double lv = (l->kind == ZSP_CONST_FLOAT) ? l->v.f : (double)to_u64(l);
        const double rv = (r->kind == ZSP_CONST_FLOAT) ? r->v.f : (double)to_u64(r);
        /* Float division-by-zero check: compare via bit pattern
         * rather than `==` to keep -Wfloat-equal clean. The
         * runtime's own `zserio_float_eq_bits` helper is the
         * reference for this idiom (REQ-FS-002 spirit). */
        const uint64_t rv_bits = ((const uint64_t *)&rv)[0];
        const uint64_t zero_bits = 0U;
        switch (op) {
        case ZSP_OP_ADD: return eval_float(s, lv + rv);
        case ZSP_OP_SUB: return eval_float(s, lv - rv);
        case ZSP_OP_MUL: return eval_float(s, lv * rv);
        case ZSP_OP_DIV: return (rv_bits != zero_bits) ? eval_float(s, lv / rv) : NULL;
        case ZSP_OP_EQ:  return eval_bool(s, !(lv <  rv) && !(lv >  rv));
        case ZSP_OP_NEQ: return eval_bool(s, (lv <  rv) ||  (lv >  rv));
        case ZSP_OP_LT:  return eval_bool(s, lv <  rv);
        case ZSP_OP_LE:  return eval_bool(s, lv <= rv);
        case ZSP_OP_GT:  return eval_bool(s, lv >  rv);
        case ZSP_OP_GE:  return eval_bool(s, lv >= rv);
        default:         return NULL;
        }
    }
    const uint64_t lu = to_u64(l);
    const uint64_t ru = to_u64(r);
    switch (op) {
    case ZSP_OP_ADD:         return eval_uint(s, lu + ru);
    case ZSP_OP_SUB:         return eval_uint(s, lu - ru);
    case ZSP_OP_MUL:         return eval_uint(s, lu * ru);
    case ZSP_OP_DIV:         return (ru != 0U) ? eval_uint(s, lu / ru) : NULL;
    case ZSP_OP_MOD:         return (ru != 0U) ? eval_uint(s, lu % ru) : NULL;
    case ZSP_OP_LSHIFT:      return eval_uint(s, lu << (ru & 63U));
    case ZSP_OP_RSHIFT:      return eval_uint(s, lu >> (ru & 63U));
    case ZSP_OP_BIT_AND:     return eval_uint(s, lu & ru);
    case ZSP_OP_BIT_OR:      return eval_uint(s, lu | ru);
    case ZSP_OP_BIT_XOR:     return eval_uint(s, lu ^ ru);
    case ZSP_OP_LOGICAL_AND: return eval_bool(s, (lu != 0U) && (ru != 0U));
    case ZSP_OP_LOGICAL_OR:  return eval_bool(s, (lu != 0U) || (ru != 0U));
    case ZSP_OP_EQ:          return eval_bool(s, lu == ru);
    case ZSP_OP_NEQ:         return eval_bool(s, lu != ru);
    case ZSP_OP_LT:          return eval_bool(s, lu <  ru);
    case ZSP_OP_LE:          return eval_bool(s, lu <= ru);
    case ZSP_OP_GT:          return eval_bool(s, lu >  ru);
    case ZSP_OP_GE:          return eval_bool(s, lu >= ru);
    default:                 return NULL;
    }
}

static const zsp_sema_const_t *eval_unop(zsp_sema_t *s, zsp_op_code_t op,
                                         const zsp_sema_const_t *o)
{
    switch (op) {
    case ZSP_OP_NEG:
        if (o->kind == ZSP_CONST_FLOAT) {
            return eval_float(s, -o->v.f);
        }
        return eval_uint(s, (uint64_t)(-(int64_t)to_u64(o)));
    case ZSP_OP_BIT_NOT:
        return eval_uint(s, ~to_u64(o));
    case ZSP_OP_LOGICAL_NOT:
        return eval_bool(s, to_u64(o) == 0U);
    default:
        return NULL;
    }
}

const zsp_sema_const_t *zsp_sema_eval(zsp_sema_t *s, zsp_ast_node_t *expr)
{
    if ((s == NULL) || (expr == NULL)) {
        return NULL;
    }
    if (expr->computed != NULL) {
        if (expr->computed == &CYCLE_SENTINEL) {
            (void)zsp_diag_emit_lit(s->diag, ZSP_SEV_ERROR,
                                    expr->file_id, expr->offset,
                                    "cyclic constant expression");
            return NULL;
        }
        return (const zsp_sema_const_t *)expr->computed;
    }

    /* Mark in-flight to detect cycles. */
    expr->computed = &CYCLE_SENTINEL;

    const zsp_sema_const_t *result = NULL;
    switch (expr->kind) {
    case ZSP_AST_EXPR_INT_LIT:
        result = eval_uint(s, expr->u.int_lit.value);
        break;
    case ZSP_AST_EXPR_BOOL_LIT:
        result = eval_bool(s, expr->u.bool_lit.value);
        break;
    case ZSP_AST_EXPR_FLOAT_LIT:
        result = eval_float(s, expr->u.float_lit.value);
        break;
    case ZSP_AST_EXPR_IDENT:
    {
        zsp_ast_node_t *const d = expr->resolved;
        if (d == NULL) {
            result = NULL;
        }
        else if (d->kind == ZSP_AST_ENUM_ITEM) {
            /* Enum item: evaluate its value_expr if present;
             * otherwise use the implicit "previous value + 1"
             * computation handled in compute_enum_values. */
            if (d->u.enum_item.value_expr != NULL) {
                result = zsp_sema_eval(s, d->u.enum_item.value_expr);
            }
            else if (d->computed != NULL) {
                result = (const zsp_sema_const_t *)d->computed;
            }
        }
        else if (d->kind == ZSP_AST_BITMASK_ITEM) {
            if (d->u.bitmask_item.value_expr != NULL) {
                result = zsp_sema_eval(s, d->u.bitmask_item.value_expr);
            }
            else if (d->computed != NULL) {
                result = (const zsp_sema_const_t *)d->computed;
            }
        }
        else if (d->kind == ZSP_AST_CONST) {
            if (d->u.const_decl.value_expr != NULL) {
                result = zsp_sema_eval(s, d->u.const_decl.value_expr);
            }
        }
        break;
    }
    case ZSP_AST_EXPR_MEMBER:
    {
        /* A member access that resolved to an enum/bitmask item
         * inherits the item's computed value. */
        zsp_ast_node_t *const d = expr->resolved;
        if (d != NULL) {
            if ((d->kind == ZSP_AST_ENUM_ITEM)
                    || (d->kind == ZSP_AST_BITMASK_ITEM)) {
                if (d->computed != NULL) {
                    result = (const zsp_sema_const_t *)d->computed;
                }
            }
            else if (d->kind == ZSP_AST_CONST) {
                if (d->u.const_decl.value_expr != NULL) {
                    result = zsp_sema_eval(s, d->u.const_decl.value_expr);
                }
            }
        }
        break;
    }
    case ZSP_AST_EXPR_BINARY:
    {
        const zsp_sema_const_t *const l = zsp_sema_eval(s, expr->u.binop.lhs);
        const zsp_sema_const_t *const r = zsp_sema_eval(s, expr->u.binop.rhs);
        if ((l != NULL) && (r != NULL)) {
            result = eval_binop(s, expr->u.binop.op, l, r);
        }
        break;
    }
    case ZSP_AST_EXPR_UNARY:
    {
        const zsp_sema_const_t *const o = zsp_sema_eval(s, expr->u.unop.operand);
        if (o != NULL) {
            result = eval_unop(s, expr->u.unop.op, o);
        }
        break;
    }
    case ZSP_AST_EXPR_TERNARY:
    {
        const zsp_sema_const_t *const c = zsp_sema_eval(s, expr->u.ternary.cond);
        if (c != NULL) {
            const bool cond = (to_u64(c) != 0U);
            result = zsp_sema_eval(s, cond ? expr->u.ternary.then_expr
                                           : expr->u.ternary.else_expr);
        }
        break;
    }
    case ZSP_AST_EXPR_CALL:
    {
        /* Schema-builtin calls (valueof/numbits/lengthof/isset)
         * are parsed as EXPR_CALL with a synthetic IDENT callee.
         * Each is a thin wrapper around its single arg's value
         * for const-eval purposes. `isset(a, b)` evaluates to
         * `(a & b) == b`. */
        const zsp_ast_node_t *const callee = expr->u.call.callee;
        if ((callee != NULL) && (callee->kind == ZSP_AST_EXPR_IDENT)) {
            const zsp_file_t *f = NULL;
            if (zsp_source_get(s->source, callee->file_id, &f)) {
                const uint8_t *n = &f->bytes[callee->u.ident.span_off];
                const size_t   nl = (size_t)callee->u.ident.span_len;
                if (((nl == 7U) && (memcmp(n, "valueof", 7U) == 0))
                        || ((nl == 7U) && (memcmp(n, "numbits", 7U) == 0))
                        || ((nl == 8U) && (memcmp(n, "lengthof", 8U) == 0))) {
                    if (expr->u.call.n_args == 1U) {
                        const zsp_sema_const_t *a =
                                zsp_sema_eval(s, expr->u.call.args[0]);
                        if (a != NULL) {
                            if ((nl == 7U) && (memcmp(n, "numbits", 7U) == 0)) {
                                const uint64_t v = to_u64(a);
                                uint64_t bits = 0U;
                                if (v <= 1U) { bits = 1U; }
                                else {
                                    uint64_t x = v - 1U;
                                    while (x > 0U) { bits++; x >>= 1U; }
                                }
                                result = eval_uint(s, bits);
                            } else {
                                /* valueof / lengthof: pass through. */
                                result = a;
                            }
                        }
                    }
                }
                else if ((nl == 5U) && (memcmp(n, "isset", 5U) == 0)
                        && (expr->u.call.n_args == 2U)) {
                    const zsp_sema_const_t *a =
                            zsp_sema_eval(s, expr->u.call.args[0]);
                    const zsp_sema_const_t *b =
                            zsp_sema_eval(s, expr->u.call.args[1]);
                    if ((a != NULL) && (b != NULL)) {
                        const uint64_t va = to_u64(a);
                        const uint64_t vb = to_u64(b);
                        result = eval_bool(s, (va & vb) == vb);
                    }
                }
            }
        }
        break;
    }
    default:
        result = NULL;
        break;
    }

    /* Replace the in-flight sentinel with the real result (or
     * clear it if non-constant). */
    if (result != NULL) {
        expr->computed = (void *)(uintptr_t)result;
    }
    else {
        expr->computed = NULL;
    }
    return result;
}

/* Walk all enum + bitmask + const decls and pre-evaluate item
 * values so that downstream lookups via EXPR_IDENT find them. */
static zserio_status_t evaluate_known_constants(zsp_sema_t *s,
                                                zsp_ast_node_t *unit)
{
    if (unit->kind != ZSP_AST_UNIT) {
        return ZSERIO_OK;
    }
    for (size_t i = 0U; i < unit->u.unit.n_decls; ++i) {
        zsp_ast_node_t *const d = unit->u.unit.decls[i];
        if (d == NULL) continue;
        if (d->kind == ZSP_AST_ENUM) {
            uint64_t prev = 0U;
            bool     first = true;
            for (size_t j = 0U; j < d->u.enum_decl.n_items; ++j) {
                zsp_ast_node_t *const item = d->u.enum_decl.items[j];
                if (item->u.enum_item.value_expr != NULL) {
                    const zsp_sema_const_t *const v = zsp_sema_eval(
                            s, item->u.enum_item.value_expr);
                    if (v != NULL) {
                        prev = to_u64(v);
                        first = false;
                    }
                    item->computed = (void *)(uintptr_t)v;
                }
                else {
                    /* Implicit: previous + 1, or 0 for the first. */
                    const uint64_t v = first ? 0U : (prev + 1U);
                    zsp_sema_const_t *const c = alloc_const(s, ZSP_CONST_UINT);
                    if (c != NULL) {
                        c->v.u = v;
                        item->computed = c;
                    }
                    prev  = v;
                    first = false;
                }
            }
        }
        else if (d->kind == ZSP_AST_BITMASK) {
            uint64_t prev = 0U;
            bool     first = true;
            for (size_t j = 0U; j < d->u.bitmask_decl.n_items; ++j) {
                zsp_ast_node_t *const item = d->u.bitmask_decl.items[j];
                if (item->u.bitmask_item.value_expr != NULL) {
                    const zsp_sema_const_t *const v = zsp_sema_eval(
                            s, item->u.bitmask_item.value_expr);
                    if (v != NULL) {
                        prev = to_u64(v);
                        first = false;
                    }
                    item->computed = (void *)(uintptr_t)v;
                }
                else {
                    /* Bitmask implicit: previous << 1, or 1 for first. */
                    const uint64_t v = first ? 1U : (prev << 1U);
                    zsp_sema_const_t *const c = alloc_const(s, ZSP_CONST_UINT);
                    if (c != NULL) {
                        c->v.u = v;
                        item->computed = c;
                    }
                    prev  = v;
                    first = false;
                }
            }
        }
        else if (d->kind == ZSP_AST_CONST) {
            if (d->u.const_decl.value_expr != NULL) {
                (void)zsp_sema_eval(s, d->u.const_decl.value_expr);
            }
        }
        else if ((d->kind == ZSP_AST_STRUCT) || (d->kind == ZSP_AST_UNION)) {
            /* Pre-evaluate every field's array length expression so
             * the emitter can decide between fixed-size (T name[N])
             * and dynamic (T *name; size_t name_count) storage. */
            for (size_t j = 0U; j < d->u.compound.n_members; ++j) {
                zsp_ast_node_t *const mem = d->u.compound.members[j];
                if ((mem == NULL) || (mem->kind != ZSP_AST_FIELD)) continue;
                zsp_ast_node_t *t = mem->u.field.type;
                if ((t != NULL) && (t->kind == ZSP_AST_TYPE_ARRAY)
                        && (t->u.type_array.length_expr != NULL)) {
                    (void)zsp_sema_eval(s, t->u.type_array.length_expr);
                }
            }
        }
    }
    return ZSERIO_OK;
}

/* ----- Public API ------------------------------------------- */

zserio_status_t zsp_sema_init(zsp_sema_t           *s,
                              const zsp_source_t   *src,
                              zsp_arena_t          *arena,
                              zsp_diag_collector_t *diag)
{
    if ((s == NULL) || (src == NULL) || (arena == NULL) || (diag == NULL)) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }
    s->source  = src;
    s->arena   = arena;
    s->diag    = diag;
    s->root    = NULL;
    s->current = NULL;
    ZSERIO_TRY(scope_new(s, NULL, &s->root));
    s->current = s->root;
    return ZSERIO_OK;
}

zserio_status_t zsp_sema_register_unit(zsp_sema_t *s, zsp_ast_node_t *unit)
{
    if ((s == NULL) || (unit == NULL)) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }
    if (unit->kind != ZSP_AST_UNIT) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }
    return collect_top_level(s, unit);
}

/* ============================================================ */
/* Template instantiation                                        */
/* ============================================================ */

/* Substitution table for cloning a templated decl. Each entry
 * maps a TEMPLATE_PARAM decl node to the concrete TYPE_REF
 * argument it should resolve to. */
typedef struct subst {
    const zsp_ast_node_t *param;   /* TEMPLATE_PARAM */
    const zsp_ast_node_t *arg;     /* concrete TYPE_REF (cloned-or-borrowed) */
} subst_t;

typedef struct subst_table {
    const subst_t *entries;
    size_t         n;
} subst_table_t;

static const zsp_ast_node_t *find_subst(const subst_table_t *tbl,
                                        const zsp_ast_node_t *param)
{
    if (tbl == NULL) return NULL;
    for (size_t i = 0U; i < tbl->n; ++i) {
        if (tbl->entries[i].param == param) {
            return tbl->entries[i].arg;
        }
    }
    return NULL;
}

/* Clone a single AST node. Recursively clones referenced
 * children with substitution applied at TYPE_REF nodes that
 * resolve to template parameters in `tbl`. */
static zsp_ast_node_t *clone_node(zsp_arena_t *arena,
                                  const zsp_ast_node_t *src,
                                  const subst_table_t *tbl);

/* Clone an array of node pointers. Returns NULL on OOM. */
static zsp_ast_node_t **clone_list(zsp_arena_t *arena,
                                   zsp_ast_node_t * const *src, size_t n,
                                   const subst_table_t *tbl)
{
    if (n == 0U) return NULL;
    zsp_ast_node_t **out = (zsp_ast_node_t **)zsp_arena_alloc(
            arena, n * sizeof(zsp_ast_node_t *), sizeof(zsp_ast_node_t *));
    if (out == NULL) return NULL;
    for (size_t i = 0U; i < n; ++i) {
        out[i] = clone_node(arena, src[i], tbl);
    }
    return out;
}

static zsp_ast_node_t *clone_node(zsp_arena_t *arena,
                                  const zsp_ast_node_t *src,
                                  const subst_table_t *tbl)
{
    if (src == NULL) return NULL;

    /* Substitution: a TYPE_REF whose resolved decl is a template
     * parameter is replaced by a fresh TYPE_REF cloned from the
     * substitution target. Carry the original site's runtime
     * type_args (zserio runtime parameters like `CHOICE(selector)`)
     * onto the substituted clone — the target TYPE_REF from the
     * `instantiate` decl doesn't have them. */
    if ((src->kind == ZSP_AST_TYPE_REF) && (src->resolved != NULL)
            && (src->resolved->kind == ZSP_AST_TEMPLATE_PARAM)) {
        const zsp_ast_node_t *const arg = find_subst(tbl, src->resolved);
        if (arg != NULL) {
            zsp_ast_node_t *const subst = clone_node(arena, arg, NULL);
            if ((subst != NULL) && (subst->kind == ZSP_AST_TYPE_REF)
                    && (src->u.type_ref.n_type_args > 0U)) {
                subst->u.type_ref.type_args = clone_list(
                        arena, src->u.type_ref.type_args,
                        src->u.type_ref.n_type_args, tbl);
                subst->u.type_ref.n_type_args = src->u.type_ref.n_type_args;
            }
            return subst;
        }
    }

    zsp_ast_node_t *dst = (zsp_ast_node_t *)zsp_arena_alloc(
            arena, sizeof(zsp_ast_node_t), sizeof(void *));
    if (dst == NULL) return NULL;
    (void)memcpy(dst, src, sizeof(zsp_ast_node_t));

    /* Per-kind recursive clone of children that may contain
     * template-param references. Other children stay shared. */
    switch (src->kind) {
    case ZSP_AST_TYPE_REF:
        dst->u.type_ref.template_args = clone_list(
                arena, src->u.type_ref.template_args,
                src->u.type_ref.n_template_args, tbl);
        dst->u.type_ref.type_args = clone_list(
                arena, src->u.type_ref.type_args,
                src->u.type_ref.n_type_args, tbl);
        break;
    case ZSP_AST_TYPE_ARRAY:
        dst->u.type_array.element = clone_node(arena, src->u.type_array.element, tbl);
        dst->u.type_array.length_expr = clone_node(arena, src->u.type_array.length_expr, tbl);
        break;
    case ZSP_AST_FIELD:
        dst->u.field.type        = clone_node(arena, src->u.field.type, tbl);
        dst->u.field.if_clause   = clone_node(arena, src->u.field.if_clause, tbl);
        dst->u.field.init_expr   = clone_node(arena, src->u.field.init_expr, tbl);
        dst->u.field.constraint_expr = clone_node(arena, src->u.field.constraint_expr, tbl);
        dst->u.field.align_expr  = clone_node(arena, src->u.field.align_expr, tbl);
        dst->u.field.offset_expr = clone_node(arena, src->u.field.offset_expr, tbl);
        break;
    case ZSP_AST_PARAMETER:
        dst->u.parameter.type = clone_node(arena, src->u.parameter.type, tbl);
        break;
    case ZSP_AST_STRUCT:
    case ZSP_AST_UNION:
        /* The compound's template_params don't need cloning
         * — we strip them in the concrete instance. */
        dst->u.compound.template_params   = NULL;
        dst->u.compound.n_template_params = 0U;
        dst->u.compound.params = clone_list(
                arena, src->u.compound.params, src->u.compound.n_params, tbl);
        dst->u.compound.members = clone_list(
                arena, src->u.compound.members, src->u.compound.n_members, tbl);
        /* Clear `resolved` (was pointing at template unit) */
        dst->resolved = NULL;
        break;
    case ZSP_AST_CHOICE:
        dst->u.choice.template_params   = NULL;
        dst->u.choice.n_template_params = 0U;
        dst->u.choice.params = clone_list(
                arena, src->u.choice.params, src->u.choice.n_params, tbl);
        dst->u.choice.selector = clone_node(arena, src->u.choice.selector, tbl);
        dst->u.choice.cases = clone_list(
                arena, src->u.choice.cases, src->u.choice.n_cases, tbl);
        dst->u.choice.default_case = clone_node(arena, src->u.choice.default_case, tbl);
        dst->resolved = NULL;
        break;
    case ZSP_AST_CHOICE_CASE:
        dst->u.choice_case.labels = clone_list(
                arena, src->u.choice_case.labels, src->u.choice_case.n_labels, tbl);
        dst->u.choice_case.field = clone_node(arena, src->u.choice_case.field, tbl);
        break;
    case ZSP_AST_CHOICE_DEFAULT:
        dst->u.choice_default.field = clone_node(arena, src->u.choice_default.field, tbl);
        break;
    case ZSP_AST_EXPR_BINARY:
        dst->u.binop.lhs = clone_node(arena, src->u.binop.lhs, tbl);
        dst->u.binop.rhs = clone_node(arena, src->u.binop.rhs, tbl);
        break;
    case ZSP_AST_EXPR_UNARY:
        dst->u.unop.operand = clone_node(arena, src->u.unop.operand, tbl);
        break;
    case ZSP_AST_EXPR_TERNARY:
        dst->u.ternary.cond      = clone_node(arena, src->u.ternary.cond, tbl);
        dst->u.ternary.then_expr = clone_node(arena, src->u.ternary.then_expr, tbl);
        dst->u.ternary.else_expr = clone_node(arena, src->u.ternary.else_expr, tbl);
        break;
    case ZSP_AST_EXPR_MEMBER:
        dst->u.member.lhs = clone_node(arena, src->u.member.lhs, tbl);
        /* RHS name node stays shared */
        /* If the source LHS is a template-param IDENT
         * (e.g. `E.BLACK`), retarget the cloned LHS's
         * `resolved` to the substitution arg's concrete
         * type decl, and clear our own `resolved` so the
         * emitter re-looks-up the named item in the new
         * decl. */
        if ((src->u.member.lhs != NULL)
                && (src->u.member.lhs->kind == ZSP_AST_EXPR_IDENT)
                && (src->u.member.lhs->resolved != NULL)
                && (src->u.member.lhs->resolved->kind == ZSP_AST_TEMPLATE_PARAM)
                && (tbl != NULL)) {
            const zsp_ast_node_t *const arg =
                    find_subst(tbl, src->u.member.lhs->resolved);
            if ((arg != NULL) && (arg->kind == ZSP_AST_TYPE_REF)
                    && (arg->resolved != NULL)
                    && (dst->u.member.lhs != NULL)) {
                zsp_ast_node_t *d = arg->resolved;
                while ((d != NULL) && (d->kind == ZSP_AST_SUBTYPE)
                        && (d->u.subtype.target_type != NULL)) {
                    d = d->u.subtype.target_type->resolved;
                }
                dst->u.member.lhs->resolved = d;
            }
            dst->resolved = NULL;
        }
        /* For non-template-param chains (e.g. a fully-qualified
         * `pkg1.pkg2.E.BLACK`), the source's `resolved` already
         * points at the right global decl — preserving it lets
         * the emitter use the cached pointer directly. */
        break;
    case ZSP_AST_EXPR_INDEX:
        dst->u.index.lhs   = clone_node(arena, src->u.index.lhs, tbl);
        dst->u.index.index = clone_node(arena, src->u.index.index, tbl);
        break;
    case ZSP_AST_EXPR_CALL:
        dst->u.call.callee = clone_node(arena, src->u.call.callee, tbl);
        dst->u.call.args = clone_list(arena, src->u.call.args, src->u.call.n_args, tbl);
        break;
    default:
        /* Atomic kinds: shallow copy is sufficient. */
        break;
    }
    return dst;
}

/* Construct a concrete instance name from a template name +
 * its argument types. Writes into `out` (snake-cased). */
static size_t mangle_name(const zsp_sema_t *s, const uint8_t *base,
                          size_t base_len,
                          zsp_ast_node_t * const *args, size_t n_args,
                          const zsp_ast_node_t * const *template_decl_pkg_parts,
                          size_t template_decl_pkg_n,
                          char *out, size_t out_cap)
{
    size_t pos = 0U;
    if (base_len >= out_cap) base_len = out_cap - 1U;
    (void)memcpy(out, base, base_len);
    pos = base_len;
    for (size_t i = 0U; i < n_args; ++i) {
        if ((pos + 1U) >= out_cap) break;
        out[pos++] = '_';
        const zsp_ast_node_t *arg = args[i];
        const uint8_t *arg_name = NULL;
        size_t         arg_len  = 0U;
        if ((arg != NULL) && (arg->kind == ZSP_AST_TYPE_REF)) {
            const zsp_ast_node_t *n = arg->u.type_ref.name;
            if ((n != NULL) && (n->kind == ZSP_AST_DOTTED_NAME)) {
                n = n->u.dotted.parts[n->u.dotted.n_parts - 1U];
            }
            (void)extract_simple_name(s, n, &arg_name, &arg_len);
            /* Disambiguate same-named types in different packages
             * by prepending the resolved unit's package segments
             * that lie BEYOND the template's own package (so a
             * same-package arg stays compact). */
            if ((arg->resolved != NULL)
                    && (arg->resolved->resolved != NULL)
                    && (arg->resolved->resolved->kind == ZSP_AST_UNIT)) {
                const zsp_ast_node_t *arg_unit = arg->resolved->resolved;
                const zsp_ast_node_t *arg_pname =
                        (arg_unit->u.unit.package != NULL)
                        ? arg_unit->u.unit.package->u.package.name : NULL;
                const zsp_ast_node_t * const *ap = NULL;
                size_t an = 0U;
                if (arg_pname != NULL) {
                    if (arg_pname->kind == ZSP_AST_DOTTED_NAME) {
                        ap = (const zsp_ast_node_t * const *)
                             arg_pname->u.dotted.parts;
                        an = arg_pname->u.dotted.n_parts;
                    } else { ap = &arg_pname; an = 1U; }
                }
                /* Skip the longest common prefix with the
                 * template's own package, then encode the
                 * remaining segments. */
                size_t skip = 0U;
                if (template_decl_pkg_parts != NULL) {
                    const size_t tn = template_decl_pkg_n;
                    const size_t lim = (tn < an) ? tn : an;
                    for (size_t k = 0U; k < lim; ++k) {
                        const uint8_t *pa=NULL,*pb=NULL; size_t la=0U,lb=0U;
                        extract_simple_name(s, template_decl_pkg_parts[k], &pa, &la);
                        extract_simple_name(s, ap[k], &pb, &lb);
                        if (la != lb || memcmp(pa, pb, la) != 0) break;
                        ++skip;
                    }
                }
                for (size_t k = skip; k < an; ++k) {
                    const uint8_t *pb=NULL; size_t lb=0U;
                    extract_simple_name(s, ap[k], &pb, &lb);
                    if (pb == NULL) continue;
                    if ((pos + lb + 1U) >= out_cap) break;
                    (void)memcpy(&out[pos], pb, lb);
                    pos += lb;
                    if ((pos + 1U) >= out_cap) break;
                    out[pos++] = '_';
                }
            }
        }
        else if ((arg != NULL) && (arg->kind == ZSP_AST_TYPE_BUILTIN)) {
            static const char *names[] = {
                [ZSP_TB_BOOL]="bool", [ZSP_TB_INT8]="int8",
                [ZSP_TB_INT16]="int16", [ZSP_TB_INT32]="int32",
                [ZSP_TB_INT64]="int64", [ZSP_TB_UINT8]="uint8",
                [ZSP_TB_UINT16]="uint16", [ZSP_TB_UINT32]="uint32",
                [ZSP_TB_UINT64]="uint64", [ZSP_TB_FLOAT16]="float16",
                [ZSP_TB_FLOAT32]="float32", [ZSP_TB_FLOAT64]="float64",
                [ZSP_TB_STRING]="string", [ZSP_TB_BYTES]="bytes",
                [ZSP_TB_EXTERN]="extern",
                [ZSP_TB_VARINT]="varint",   [ZSP_TB_VARINT16]="varint16",
                [ZSP_TB_VARINT32]="varint32", [ZSP_TB_VARINT64]="varint64",
                [ZSP_TB_VARUINT]="varuint", [ZSP_TB_VARUINT16]="varuint16",
                [ZSP_TB_VARUINT32]="varuint32", [ZSP_TB_VARUINT64]="varuint64",
                [ZSP_TB_VARSIZE]="varsize",
            };
            if ((size_t)arg->u.type_builtin.code < sizeof(names)/sizeof(names[0])
                    && names[arg->u.type_builtin.code] != NULL) {
                arg_name = (const uint8_t *)names[arg->u.type_builtin.code];
                arg_len  = strlen(names[arg->u.type_builtin.code]);
            }
        }
        if ((arg_name != NULL) && (arg_len > 0U)) {
            const size_t copy = (arg_len < (out_cap - pos - 1U))
                    ? arg_len : (out_cap - pos - 1U);
            (void)memcpy(&out[pos], arg_name, copy);
            pos += copy;
        }
    }
    out[pos] = '\0';
    return pos;
}

/* Tracking of already-instantiated combinations. */
typedef struct inst_record {
    const zsp_ast_node_t  *template_decl;
    zsp_ast_node_t       **args;      /* the arg list (borrowed) */
    size_t                 n_args;
    zsp_ast_node_t        *concrete;  /* the cloned decl */
    char                   mangled[256];
    size_t                 mangled_len;
} inst_record_t;

static bool type_arg_equal(const zsp_ast_node_t *a, const zsp_ast_node_t *b)
{
    if (a == b) return true;
    if ((a == NULL) || (b == NULL)) return false;
    if (a->kind != b->kind) return false;
    if (a->kind == ZSP_AST_TYPE_BUILTIN) {
        return a->u.type_builtin.code == b->u.type_builtin.code;
    }
    if (a->kind == ZSP_AST_TYPE_REF) {
        if ((a->resolved != NULL) && (a->resolved == b->resolved)) {
            return true;
        }
        return false;
    }
    return false;
}

static bool args_equal(zsp_ast_node_t * const *a, size_t na,
                       zsp_ast_node_t * const *b, size_t nb)
{
    if (na != nb) return false;
    for (size_t i = 0U; i < na; ++i) {
        if (!type_arg_equal(a[i], b[i])) return false;
    }
    return true;
}

static zsp_ast_node_t *get_decl_name_node(zsp_ast_node_t *d)
{
    switch (d->kind) {
    case ZSP_AST_STRUCT:
    case ZSP_AST_UNION:  return d->u.compound.name;
    case ZSP_AST_CHOICE: return d->u.choice.name;
    default:             return NULL;
    }
}

static void set_decl_name(zsp_ast_node_t *d, zsp_ast_node_t *name_node)
{
    switch (d->kind) {
    case ZSP_AST_STRUCT:
    case ZSP_AST_UNION:  d->u.compound.name = name_node; break;
    case ZSP_AST_CHOICE: d->u.choice.name   = name_node; break;
    default:             break;
    }
}

/* Build a synthetic IDENT node whose `span_off`/`span_len`
 * reference a freshly arena-allocated string. */
static zsp_ast_node_t *make_synthetic_ident(zsp_sema_t *s,
                                            zsp_ast_node_t *template_decl,
                                            const char *mangled, size_t len)
{
    /* Copy into arena so the storage outlives this call. */
    char *buf = (char *)zsp_arena_alloc(s->arena, len + 1U, 1U);
    if (buf == NULL) return NULL;
    (void)memcpy(buf, mangled, len);
    buf[len] = '\0';

    /* Add a synthetic "file" entry so the IDENT's span references
     * real bytes the source manager can look up. We register the
     * string itself as a borrowed file in the source manager. */
    /* Simpler: use the template's existing file_id and a sentinel
     * span_off that points beyond the file. Source-manager lookup
     * for diagnostics will fail gracefully, but emit just memcpys
     * via ident_span which reads file.bytes[span_off] — which
     * would be UB on out-of-range.
     *
     * Solution: register the mangled name as a borrowed buffer
     * in the source manager and use the returned file_id. */
    zsp_file_id_t fid = (zsp_file_id_t)0;
    /* Borrowed registration (we own `buf` in the arena, source
     * manager won't free it). */
    if (zsp_source_add_borrowed(
            (zsp_source_t *)(uintptr_t)s->source,
            buf, len, (const uint8_t *)buf, len, &fid) != ZSERIO_OK) {
        return NULL;
    }
    zsp_ast_node_t *id = (zsp_ast_node_t *)zsp_arena_alloc(
            s->arena, sizeof(zsp_ast_node_t), sizeof(void *));
    if (id == NULL) return NULL;
    (void)memset(id, 0, sizeof(*id));
    id->kind            = ZSP_AST_EXPR_IDENT;
    id->file_id         = fid;
    id->offset          = 0U;
    id->length          = (uint32_t)len;
    id->u.ident.span_off = 0U;
    id->u.ident.span_len = (uint32_t)len;
    (void)template_decl;
    return id;
}

/* Per-unit context for the instantiation pass. */
typedef struct inst_ctx {
    zsp_sema_t      *s;
    zsp_ast_node_t  *unit;
    inst_record_t  **records;
    size_t           n_records;
    size_t           cap;
} inst_ctx_t;

static zsp_ast_node_t *get_or_instantiate(inst_ctx_t *ctx,
                                          zsp_ast_node_t *template_decl,
                                          zsp_ast_node_t * const *args,
                                          size_t n_args);

static void replace_template_refs(inst_ctx_t *ctx, zsp_ast_node_t *node);

/* Build a sub-decl from a template, returning its (cached or new)
 * concrete clone. Mutates context's records list. */
static zsp_ast_node_t *get_or_instantiate(inst_ctx_t *ctx,
                                          zsp_ast_node_t *template_decl,
                                          zsp_ast_node_t * const *args,
                                          size_t n_args)
{
    /* Lookup existing in per-unit records. */
    for (size_t i = 0U; i < ctx->n_records; ++i) {
        inst_record_t *r = ctx->records[i];
        if ((r->template_decl == template_decl)
                && args_equal(r->args, r->n_args,
                              (zsp_ast_node_t * const *)args, n_args)) {
            return r->concrete;
        }
    }
    /* Determine the template decl's own package parts (used as
     * a common-prefix to keep same-package arg mangles compact). */
    const zsp_ast_node_t * const *tpl_pkg_parts = NULL;
    size_t tpl_pkg_n = 0U;
    if ((template_decl->resolved != NULL)
            && (template_decl->resolved->kind == ZSP_AST_UNIT)
            && (template_decl->resolved->u.unit.package != NULL)) {
        const zsp_ast_node_t *p =
                template_decl->resolved->u.unit.package->u.package.name;
        if (p != NULL) {
            if (p->kind == ZSP_AST_DOTTED_NAME) {
                tpl_pkg_parts = (const zsp_ast_node_t * const *)p->u.dotted.parts;
                tpl_pkg_n     = p->u.dotted.n_parts;
            } else { tpl_pkg_parts = &p; tpl_pkg_n = 1U; }
        }
    }
    /* Mangle name first so we can check global scope for an
     * already-instantiated concrete clone (e.g., produced when
     * another unit's pass visited this same template). */
    char    pre_mangled[256];
    size_t  pre_mangled_len = 0U;
    const zsp_ast_node_t *pre_base_name = get_decl_name_node(template_decl);
    const uint8_t *pre_base_bytes = NULL;
    size_t         pre_base_len   = 0U;
    if (extract_simple_name(ctx->s, pre_base_name, &pre_base_bytes,
                            &pre_base_len)) {
        pre_mangled_len = mangle_name(ctx->s, pre_base_bytes, pre_base_len,
                                      (zsp_ast_node_t * const *)args, n_args,
                                      tpl_pkg_parts, tpl_pkg_n,
                                      pre_mangled, sizeof(pre_mangled));
        zsp_ast_node_t *existing = zsp_sema_scope_find(
                ctx->s->root, (const uint8_t *)pre_mangled,
                pre_mangled_len);
        if ((existing != NULL) && (existing != template_decl)
                && (existing->kind == template_decl->kind)) {
            return existing;
        }
    }
    /* Grow records list. */
    if (ctx->n_records == ctx->cap) {
        const size_t nc = (ctx->cap == 0U) ? 16U : (ctx->cap * 2U);
        inst_record_t **g = (inst_record_t **)zsp_arena_alloc(
                ctx->s->arena, nc * sizeof(inst_record_t *), sizeof(void *));
        if (g == NULL) return NULL;
        if (ctx->records != NULL) {
            (void)memcpy(g, ctx->records, ctx->n_records * sizeof(inst_record_t *));
        }
        ctx->records = g;
        ctx->cap     = nc;
    }
    inst_record_t *rec = (inst_record_t *)zsp_arena_alloc(
            ctx->s->arena, sizeof(inst_record_t), sizeof(void *));
    if (rec == NULL) return NULL;
    (void)memset(rec, 0, sizeof(*rec));
    rec->template_decl = template_decl;
    rec->args = (zsp_ast_node_t **)zsp_arena_alloc(
            ctx->s->arena, n_args * sizeof(zsp_ast_node_t *), sizeof(void *));
    if (rec->args == NULL) return NULL;
    (void)memcpy(rec->args, args, n_args * sizeof(zsp_ast_node_t *));
    rec->n_args = n_args;

    /* Get template params count. */
    const size_t n_template_params =
            (template_decl->kind == ZSP_AST_STRUCT
             || template_decl->kind == ZSP_AST_UNION)
            ? template_decl->u.compound.n_template_params
            : (template_decl->kind == ZSP_AST_CHOICE
               ? template_decl->u.choice.n_template_params : 0U);
    if (n_template_params != n_args) {
        return NULL;
    }
    zsp_ast_node_t **tparams =
            (template_decl->kind == ZSP_AST_CHOICE)
            ? template_decl->u.choice.template_params
            : template_decl->u.compound.template_params;

    /* Build substitution table. */
    subst_t *entries = (subst_t *)zsp_arena_alloc(
            ctx->s->arena, n_args * sizeof(subst_t), sizeof(void *));
    if (entries == NULL) return NULL;
    for (size_t i = 0U; i < n_args; ++i) {
        entries[i].param = tparams[i];
        entries[i].arg   = args[i];
    }
    subst_table_t tbl = { entries, n_args };

    /* Clone the decl with substitution. */
    zsp_ast_node_t *concrete = clone_node(ctx->s->arena, template_decl, &tbl);
    if (concrete == NULL) return NULL;

    /* Compute mangled name + replace the name node. */
    const zsp_ast_node_t *base_name = get_decl_name_node(template_decl);
    const uint8_t *base_bytes = NULL;
    size_t         base_len   = 0U;
    if (extract_simple_name(ctx->s, base_name, &base_bytes, &base_len)) {
        rec->mangled_len = mangle_name(ctx->s, base_bytes, base_len,
                                       (zsp_ast_node_t * const *)args, n_args,
                                       tpl_pkg_parts, tpl_pkg_n,
                                       rec->mangled, sizeof(rec->mangled));
        /* Disambiguate against any prior decl binding the same
         * name (e.g. an explicit `instantiate Foo<X> SomeName;`
         * that already reserved our auto-mangled identifier).
         * Append `_<n>` until free; upstream's cpp generator uses
         * a hash suffix here but a small counter works equally
         * well for our purposes since the mangle is internal. */
        {
            zsp_ast_node_t *clash = zsp_sema_scope_find(
                    ctx->s->root, (const uint8_t *)rec->mangled,
                    rec->mangled_len);
            uint32_t suffix = 0U;
            while ((clash != NULL) && (clash != template_decl)) {
                char tail[16];
                int wn = snprintf(tail, sizeof(tail), "_%u", (unsigned)suffix);
                if ((wn < 0) || ((size_t)(rec->mangled_len + (size_t)wn) >= sizeof(rec->mangled))) {
                    break;
                }
                char trial[256];
                (void)memcpy(trial, rec->mangled, rec->mangled_len);
                (void)memcpy(trial + rec->mangled_len, tail, (size_t)wn);
                const size_t trial_len = rec->mangled_len + (size_t)wn;
                clash = zsp_sema_scope_find(
                        ctx->s->root, (const uint8_t *)trial, trial_len);
                if (clash == NULL) {
                    (void)memcpy(rec->mangled, trial, trial_len);
                    rec->mangled_len = trial_len;
                    rec->mangled[trial_len] = '\0';
                    break;
                }
                ++suffix;
                if (suffix > 1000U) break; /* safety */
            }
        }
        zsp_ast_node_t *id = make_synthetic_ident(ctx->s, template_decl,
                                                  rec->mangled,
                                                  rec->mangled_len);
        if (id != NULL) {
            set_decl_name(concrete, id);
        }
    }
    rec->concrete = concrete;
    ctx->records[ctx->n_records++] = rec;

    /* Add concrete to the template_decl's enclosing unit. We
     * find it via template_decl->resolved (which we set at
     * register-time to point at the unit). */
    zsp_ast_node_t *target_unit = template_decl->resolved;
    if ((target_unit == NULL) || (target_unit->kind != ZSP_AST_UNIT)) {
        target_unit = ctx->unit;
    }
    /* Grow the unit's decls array. */
    const size_t old_n = target_unit->u.unit.n_decls;
    zsp_ast_node_t **grown = (zsp_ast_node_t **)zsp_arena_alloc(
            ctx->s->arena, (old_n + 1U) * sizeof(zsp_ast_node_t *),
            sizeof(void *));
    if (grown == NULL) return concrete;
    (void)memcpy(grown, target_unit->u.unit.decls,
                 old_n * sizeof(zsp_ast_node_t *));
    grown[old_n] = concrete;
    target_unit->u.unit.decls   = grown;
    target_unit->u.unit.n_decls = old_n + 1U;
    /* Also bind the concrete into the root scope so future
     * references to its mangled name resolve. */
    concrete->resolved = target_unit;
    (void)scope_bind(ctx->s, ctx->s->root,
                     (const uint8_t *)rec->mangled,
                     rec->mangled_len, concrete);

    /* Recursively scan the concrete for further template uses. */
    replace_template_refs(ctx, concrete);

    return concrete;
}

/* Walk a node and replace any TYPE_REF that has template args
 * by setting its `resolved` to the concrete clone and clearing
 * its template_args (so the emitter sees a plain reference).
 * Also dereferences TYPE_REFs that resolve to an INSTANTIATE
 * alias into the underlying concrete clone. */
static void replace_template_refs(inst_ctx_t *ctx, zsp_ast_node_t *node)
{
    if (node == NULL) return;
    if ((node->kind == ZSP_AST_TYPE_REF)
            && (node->u.type_ref.n_template_args > 0U)
            && (node->resolved != NULL)
            && ((node->resolved->kind == ZSP_AST_STRUCT)
                || (node->resolved->kind == ZSP_AST_UNION)
                || (node->resolved->kind == ZSP_AST_CHOICE))) {
        zsp_ast_node_t *templ = node->resolved;
        zsp_ast_node_t **args = node->u.type_ref.template_args;
        size_t           n    = node->u.type_ref.n_template_args;
        /* Recurse into args first so nested template uses get
         * resolved. */
        for (size_t i = 0U; i < n; ++i) {
            replace_template_refs(ctx, args[i]);
        }
        /* Skip if any arg is still a template parameter — the
         * enclosing template hasn't been instantiated yet. The
         * outer instantiation pass will revisit this use through
         * the substituted body. */
        bool has_open_param = false;
        for (size_t i = 0U; i < n; ++i) {
            if ((args[i] != NULL)
                    && (args[i]->kind == ZSP_AST_TYPE_REF)
                    && (args[i]->resolved != NULL)
                    && (args[i]->resolved->kind == ZSP_AST_TEMPLATE_PARAM)) {
                has_open_param = true;
                break;
            }
        }
        if (has_open_param) {
            return;
        }
        zsp_ast_node_t *concrete = get_or_instantiate(ctx, templ, args, n);
        if (concrete != NULL) {
            node->resolved = concrete;
            node->u.type_ref.template_args   = NULL;
            node->u.type_ref.n_template_args = 0U;
            /* Rewrite the type-ref's name to the concrete's
             * mangled name so the emitter's name-based code
             * generation produces the right C symbol. */
            zsp_ast_node_t *new_name = get_decl_name_node(concrete);
            if (new_name != NULL) {
                node->u.type_ref.name = new_name;
            }
        }
    }
    /* Dereference TYPE_REFs that resolve to an INSTANTIATE alias.
     * The INSTANTIATE's target is itself a templated TYPE_REF;
     * after processing that target the chain yields the concrete
     * clone we should point at. */
    else if ((node->kind == ZSP_AST_TYPE_REF)
            && (node->resolved != NULL)
            && (node->resolved->kind == ZSP_AST_INSTANTIATE)) {
        zsp_ast_node_t *inst = node->resolved;
        zsp_ast_node_t *tgt  = inst->u.instantiate.target;
        if (tgt != NULL) {
            replace_template_refs(ctx, tgt);
            if ((tgt->kind == ZSP_AST_TYPE_REF)
                    && (tgt->resolved != NULL)
                    && ((tgt->resolved->kind == ZSP_AST_STRUCT)
                        || (tgt->resolved->kind == ZSP_AST_UNION)
                        || (tgt->resolved->kind == ZSP_AST_CHOICE))) {
                node->resolved = tgt->resolved;
                zsp_ast_node_t *new_name = get_decl_name_node(tgt->resolved);
                if (new_name != NULL) {
                    node->u.type_ref.name = new_name;
                }
            }
        }
    }

    /* Recurse into all children. */
    walk_stack_t st = { NULL, 0U, 0U, ctx->s->arena, false };
    enqueue_children(&st, node);
    for (size_t i = 0U; i < st.count; ++i) {
        replace_template_refs(ctx, st.items[i]);
    }
}

zserio_status_t zsp_sema_instantiate_templates(zsp_sema_t *s,
                                               zsp_ast_node_t *unit)
{
    if ((s == NULL) || (unit == NULL)) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }
    if (unit->kind != ZSP_AST_UNIT) {
        return ZSERIO_OK;
    }
    inst_ctx_t ctx = { s, unit, NULL, 0U, 0U };
    /* Snapshot the original decl count so newly-added clones
     * aren't iterated again (their internals are already
     * processed inside get_or_instantiate). */
    const size_t orig_n = unit->u.unit.n_decls;
    for (size_t i = 0U; i < orig_n; ++i) {
        zsp_ast_node_t *d = unit->u.unit.decls[i];
        if (d == NULL) continue;
        /* Skip templated decls themselves — only walk their uses
         * (from non-templated decls). The concrete clones added
         * inside get_or_instantiate ARE walked recursively from
         * within that function. */
        const size_t n_tp =
                ((d->kind == ZSP_AST_STRUCT) || (d->kind == ZSP_AST_UNION))
                ? d->u.compound.n_template_params
                : (d->kind == ZSP_AST_CHOICE)
                ? d->u.choice.n_template_params : 0U;
        if (n_tp > 0U) continue;
        replace_template_refs(&ctx, d);
    }
    return ZSERIO_OK;
}

zserio_status_t zsp_sema_resolve_unit(zsp_sema_t *s, zsp_ast_node_t *unit)
{
    if ((s == NULL) || (unit == NULL)) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }
    ZSERIO_TRY(resolve_pass(s, unit));
    ZSERIO_TRY(evaluate_known_constants(s, unit));
    /* Template instantiation runs in its own session-level
     * phase so that it sees fully-resolved AST across all
     * units (templates and their uses may live in different
     * files). See `zsp_session_analyse`. */
    return ZSERIO_OK;
}

zserio_status_t zsp_sema_analyse_unit(zsp_sema_t *s, zsp_ast_node_t *unit)
{
    if ((s == NULL) || (unit == NULL)) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }
    ZSERIO_TRY(collect_top_level(s, unit));
    ZSERIO_TRY(resolve_pass(s, unit));
    ZSERIO_TRY(evaluate_known_constants(s, unit));
    return ZSERIO_OK;
}
