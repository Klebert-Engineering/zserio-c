/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Fabian Klebert
 *
 * zserio-c-gen: command-line driver for the C-native parser
 * pipeline. Reads a `.zs` schema, parses + analyses + emits
 * the generated C code to the specified output directory.
 *
 * Usage:
 *   zserio-c-gen <input.zs> -c <output-dir>
 *
 * This is the entry point integrators use in place of the
 * Java-based ZserioTool for the C target.
 */

#define _POSIX_C_SOURCE 200809L

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "zsparser/arena.h"
#include "zsparser/ast.h"
#include "zsparser/diag.h"
#include "zsparser/emit.h"
#include "zsparser/parser.h"
#include "zsparser/sema.h"
#include "zsparser/session.h"
#include "zsparser/source.h"

static void usage(const char *argv0)
{
    (void)fprintf(stderr,
                  "usage: %s <input.zs> -c <output-dir> [-src <root>...] "
                  "[-setTopLevelPackage <pkg>]\n"
                  "\n"
                  "  <input.zs>             schema file to compile\n"
                  "  -c <output>            directory for emitted .c/.h\n"
                  "  -src <root>            source-root for imports\n"
                  "  -setTopLevelPackage P  prepend dotted package P to\n"
                  "                         every loaded unit's `package`\n"
                  "                         (matches upstream's flag of\n"
                  "                         the same name)\n",
                  argv0);
}

/* Build a fresh IDENT node referring to `name` (length `len`),
 * registering a borrowed source view so diagnostics + ident_span
 * read consistent bytes. The buffer must live for the session's
 * lifetime — we copy into the arena. */
static zsp_ast_node_t *make_ident_node(zsp_session_t *s,
                                       const char *name, size_t len)
{
    char *buf = (char *)zsp_arena_alloc(&s->arena, len + 1U, 1U);
    if (buf == NULL) return NULL;
    (void)memcpy(buf, name, len);
    buf[len] = '\0';
    zsp_file_id_t fid = (zsp_file_id_t)0;
    if (zsp_source_add_borrowed(&s->source, buf, len,
                                (const uint8_t *)buf, len,
                                &fid) != ZSERIO_OK) {
        return NULL;
    }
    zsp_ast_node_t *n = (zsp_ast_node_t *)zsp_arena_alloc(
            &s->arena, sizeof(zsp_ast_node_t), sizeof(void *));
    if (n == NULL) return NULL;
    (void)memset(n, 0, sizeof(*n));
    n->kind             = ZSP_AST_EXPR_IDENT;
    n->file_id          = fid;
    n->offset           = 0U;
    n->length           = (uint32_t)len;
    n->u.ident.span_off = 0U;
    n->u.ident.span_len = (uint32_t)len;
    return n;
}

/* Prepend dotted prefix `prefix` (e.g. "index_workaround") to
 * every loaded unit's `package` declaration. Mutates the AST
 * in place — must run before zsp_session_analyse(). */
static int apply_top_level_prefix(zsp_session_t *s, const char *prefix)
{
    if ((prefix == NULL) || (*prefix == '\0')) return 0;
    /* Split prefix on `.`. */
    zsp_ast_node_t *prefix_parts[16];
    size_t n_prefix = 0U;
    const char *p = prefix;
    while ((*p != '\0') && (n_prefix < (sizeof(prefix_parts) / sizeof(prefix_parts[0])))) {
        const char *q = p;
        while ((*q != '\0') && (*q != '.')) ++q;
        zsp_ast_node_t *id = make_ident_node(s, p, (size_t)(q - p));
        if (id == NULL) return 1;
        prefix_parts[n_prefix++] = id;
        if (*q == '\0') break;
        p = q + 1;
    }
    for (size_t i = 0U; i < s->n_files; ++i) {
        zsp_ast_node_t *unit = s->files[i].unit;
        if ((unit == NULL) || (unit->kind != ZSP_AST_UNIT)) continue;
        zsp_ast_node_t *pkg = unit->u.unit.package;
        if (pkg == NULL) continue;
        zsp_ast_node_t *pname = pkg->u.package.name;
        if (pname == NULL) continue;
        zsp_ast_node_t **old_parts = NULL;
        size_t           n_old = 0U;
        if (pname->kind == ZSP_AST_DOTTED_NAME) {
            old_parts = pname->u.dotted.parts;
            n_old     = pname->u.dotted.n_parts;
        } else if (pname->kind == ZSP_AST_EXPR_IDENT) {
            old_parts = (zsp_ast_node_t **)zsp_arena_alloc(
                    &s->arena, sizeof(zsp_ast_node_t *), sizeof(void *));
            if (old_parts == NULL) return 1;
            old_parts[0] = pname;
            n_old = 1U;
        }
        const size_t n_total = n_prefix + n_old;
        zsp_ast_node_t **parts = (zsp_ast_node_t **)zsp_arena_alloc(
                &s->arena, n_total * sizeof(zsp_ast_node_t *), sizeof(void *));
        if (parts == NULL) return 1;
        (void)memcpy(parts, prefix_parts, n_prefix * sizeof(zsp_ast_node_t *));
        (void)memcpy(parts + n_prefix, old_parts, n_old * sizeof(zsp_ast_node_t *));
        zsp_ast_node_t *dotted = (zsp_ast_node_t *)zsp_arena_alloc(
                &s->arena, sizeof(zsp_ast_node_t), sizeof(void *));
        if (dotted == NULL) return 1;
        (void)memset(dotted, 0, sizeof(*dotted));
        dotted->kind             = ZSP_AST_DOTTED_NAME;
        dotted->file_id          = pname->file_id;
        dotted->offset           = pname->offset;
        dotted->length           = pname->length;
        dotted->u.dotted.parts   = parts;
        dotted->u.dotted.n_parts = n_total;
        pkg->u.package.name = dotted;
    }
    return 0;
}

static void mkdirs_for(const char *path)
{
    char buf[2048];
    (void)snprintf(buf, sizeof(buf), "%s", path);
    size_t len = strlen(buf);
    while ((len > 0U) && (buf[len - 1U] != '/')) {
        len -= 1U;
    }
    if (len == 0U) return;
    buf[len - 1U] = '\0';
    for (size_t i = 1U; i < len; ++i) {
        if (buf[i] == '/') {
            buf[i] = '\0';
            (void)mkdir(buf, 0755);
            buf[i] = '/';
        }
    }
    (void)mkdir(buf, 0755);
}

typedef struct out_ctx {
    const char *base_dir;
    bool        ok;
    size_t      files;
} out_ctx_t;

static bool out_write(void *ud, const char *rel,
                      const char *bytes, size_t len)
{
    out_ctx_t *const c = (out_ctx_t *)ud;
    char full[4096];
    (void)snprintf(full, sizeof(full), "%s/%s", c->base_dir, rel);
    mkdirs_for(full);
    FILE *const f = fopen(full, "wb");
    if (f == NULL) { c->ok = false; return false; }
    const size_t wrote = fwrite(bytes, 1U, len, f);
    (void)fclose(f);
    if (wrote != len) { c->ok = false; return false; }
    c->files += 1U;
    return true;
}

/* Stdout sink for diagnostics. */
static void diag_to_stderr(void *ud, const char *bytes, size_t len)
{
    (void)ud;
    (void)fwrite(bytes, 1U, len, stderr);
}

int main(int argc, char **argv)
{
    if (argc < 4) { usage(argv[0]); return 2; }

    const char *input  = NULL;
    const char *outdir = NULL;
    const char *roots[16];
    size_t      n_roots = 0U;
    const char *top_level_pkg = NULL;

    for (int i = 1; i < argc; ++i) {
        if ((strcmp(argv[i], "-c") == 0) && ((i + 1) < argc)) {
            outdir = argv[i + 1];
            i += 1;
        }
        else if ((strcmp(argv[i], "-src") == 0) && ((i + 1) < argc)
                 && (n_roots < (sizeof(roots) / sizeof(roots[0])))) {
            roots[n_roots++] = argv[i + 1];
            i += 1;
        }
        else if ((strcmp(argv[i], "-setTopLevelPackage") == 0)
                 && ((i + 1) < argc)) {
            top_level_pkg = argv[i + 1];
            i += 1;
        }
        else {
            input = argv[i];
        }
    }
    if ((input == NULL) || (outdir == NULL)) {
        usage(argv[0]);
        return 2;
    }

    /* If no -src roots supplied, derive a default from the
     * input file's parent directory. */
    char default_root[2048];
    if (n_roots == 0U) {
        (void)snprintf(default_root, sizeof(default_root), "%s", input);
        size_t pos = strlen(default_root);
        while ((pos > 0U) && (default_root[pos - 1U] != '/')) {
            pos -= 1U;
        }
        if (pos > 0U) {
            default_root[pos - 1U] = '\0';
        }
        else {
            (void)snprintf(default_root, sizeof(default_root), ".");
        }
        roots[n_roots++] = default_root;
    }

    zsp_session_t session;
    if (zsp_session_init(&session, roots, n_roots) != ZSERIO_OK) {
        (void)fprintf(stderr, "error: session init failed\n");
        return 1;
    }

    zsp_ast_node_t *top_unit = NULL;
    int rc = 0;
    if (zsp_session_load(&session, input, &top_unit) != ZSERIO_OK) {
        (void)fprintf(stderr, "error: cannot load '%s'\n", input);
        rc = 1;
    }
    if ((rc == 0) && (top_level_pkg != NULL)) {
        if (apply_top_level_prefix(&session, top_level_pkg) != 0) {
            (void)fprintf(stderr, "error: top-level package rewrite failed\n");
            rc = 1;
        }
    }
    if (rc == 0) {
        (void)zsp_session_analyse(&session);
    }
    if ((rc == 0) && (top_unit != NULL)) {
        /* Emit every loaded unit (top + transitively imported
         * dependencies) so cross-package references resolve in
         * the generated output tree. Diagnostics are kept and
         * still gate the exit code; emission proceeds best-
         * effort when only cross-file conflicts are present. */
        out_ctx_t out = { outdir, true, 0U };
        for (size_t i = 0U; i < session.n_files; ++i) {
            if (session.files[i].unit == NULL) continue;
            if (zsp_emit_unit(session.files[i].unit, &session.arena,
                              &session.diag, &session.source,
                              out_write, &out) != ZSERIO_OK) {
                (void)fprintf(stderr, "error: emit failed\n");
                rc = 1;
                break;
            }
        }
        if ((rc == 0) && !out.ok) {
            (void)fprintf(stderr, "error: file write failed\n");
            rc = 1;
        }
        else if (rc == 0) {
            (void)fprintf(stderr, "wrote %zu files under %s\n",
                          out.files, outdir);
        }
    }

    (void)zsp_diag_format_gcc(&session.diag, &session.source,
                              diag_to_stderr, NULL);

    if (zsp_diag_error_count(&session.diag) > 0U) {
        rc = 1;
    }

    zsp_session_dispose(&session);
    return rc;
}
