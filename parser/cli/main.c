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
                  "usage: %s <input.zs> -c <output-dir> [-src <root>...]\n"
                  "\n"
                  "  <input.zs>     schema file to compile\n"
                  "  -c <output>    directory for emitted .c / .h files\n"
                  "  -src <root>    source-root for resolving imports.\n"
                  "                 may be repeated; defaults to the\n"
                  "                 directory containing the input file.\n",
                  argv0);
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
    if (rc == 0) {
        (void)zsp_session_analyse(&session);
    }
    if ((rc == 0) && (zsp_diag_error_count(&session.diag) == 0U)
            && (top_unit != NULL)) {
        out_ctx_t out = { outdir, true, 0U };
        if (zsp_emit_unit(top_unit, &session.arena, &session.diag,
                          &session.source, out_write, &out) != ZSERIO_OK) {
            (void)fprintf(stderr, "error: emit failed\n");
            rc = 1;
        }
        else if (!out.ok) {
            (void)fprintf(stderr, "error: file write failed\n");
            rc = 1;
        }
        else {
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
