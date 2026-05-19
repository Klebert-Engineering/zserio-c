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

#include "zserio/version.h"
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
                  "       %s --version\n"
                  "\n"
                  "  <input.zs>             schema file to compile\n"
                  "  -c <output>            directory for emitted .c/.h\n"
                  "  -src <root>            source-root for imports\n"
                  "  -setTopLevelPackage P  prepend dotted package P to\n"
                  "                         every loaded unit's `package`\n"
                  "                         (matches upstream's flag of\n"
                  "                         the same name)\n"
                  "  --version              print version information and exit\n",
                  argv0, argv0);
}

static void print_version(void)
{
    (void)printf("zserio_c_gen %s\n", ZSERIO_C_VERSION_STRING);
    (void)printf("  schema language : %s   (subset of upstream Zserio %s)\n",
                 ZSERIO_SCHEMA_LANGUAGE_VERSION_STRING,
                 ZSERIO_UPSTREAM_PIN_VERSION);
    (void)printf("  wire format     : %s   (compatible with upstream Zserio BIN %s)\n",
                 ZSERIO_WIRE_FORMAT_VERSION_STRING,
                 ZSERIO_WIRE_FORMAT_VERSION_STRING);
    (void)printf("  runtime API     : %d.%d   (%s)\n",
                 ZSERIO_C_VERSION_MAJOR, ZSERIO_C_VERSION_MINOR,
                 (ZSERIO_C_VERSION_MAJOR == 0)
                     ? "pre-1.0; may change between minor releases"
                     : "stable within this major release");
    (void)printf("  generated code  : %d.%d   (%s)\n",
                 ZSERIO_C_VERSION_MAJOR, ZSERIO_C_VERSION_MINOR,
                 (ZSERIO_C_VERSION_MAJOR == 0)
                     ? "pre-1.0; may change between minor releases"
                     : "stable within this major release");
}

/* The top-level-package rewrite is implemented in the parser
 * library as `zsp_session_set_top_level_package`. */

static void mkdirs_for(const char *path)
{
    /* Match out_write's 4096-byte cap so -Wformat-truncation can
     * statically see that the snprintf never truncates. */
    char buf[4096];
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
    /* --version is honored regardless of other arguments and
     * must work with no `.zs` input. Handled before the argc
     * minimum check. */
    for (int i = 1; i < argc; ++i) {
        if ((strcmp(argv[i], "--version") == 0) ||
            (strcmp(argv[i], "-version") == 0)) {
            print_version();
            return 0;
        }
    }

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
        if (zsp_session_set_top_level_package(&session, top_level_pkg)
                != ZSERIO_OK) {
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
                (void)fprintf(stderr, "error: emit failed on %s\n",
                              session.files[i].path);
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
