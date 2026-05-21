/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Klebert Engineering
 */

#ifndef ZSPARSER_DIAG_H_
#define ZSPARSER_DIAG_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "zserio/allocator.h"
#include "zserio/status.h"

#include "zsparser/source.h"

/**
 * @file
 * Diagnostic collection and formatting for the zserio-c parser.
 *
 * Diagnostics carry `(severity, file_id, byte_offset, message)`.
 * The source manager (`zsparser/source.h`) recovers line and
 * column lazily when the formatter renders the diagnostic.
 *
 * The collector caps the diagnostic count (Q6 decision: 20 errors
 * per compile) so cascading nonsense from a single root cause
 * doesn't drown out the useful signal. Errors past the cap are
 * silently dropped; the `truncated` flag is set so callers
 * (typically the formatter) can render a "... and N more errors"
 * trailer.
 *
 * Output is written through a caller-supplied callback rather
 * than `FILE *` directly so the parser library carries no I/O
 * dependency. Drivers / CLIs translate the callback into
 * `fwrite(...)` or any other sink as appropriate.
 */

typedef enum zsp_severity {
    ZSP_SEV_NOTE  = 0,
    ZSP_SEV_WARN  = 1,
    ZSP_SEV_ERROR = 2
} zsp_severity_t;

/** Hard cap from Q6. */
#define ZSP_DIAG_DEFAULT_MAX  ((size_t)20U)

/**
 * Single diagnostic record. Lifetime of `message` is the
 * collector's allocator; either a static literal supplied via
 * `zsp_diag_emit_lit` or a copied buffer allocated by
 * `zsp_diag_emit_copy`.
 */
typedef struct zsp_diag {
    zsp_severity_t severity;
    zsp_file_id_t  file_id;
    uint32_t       offset;       /**< Byte offset into the file. */
    const char    *message;
    size_t         message_len;
    bool           message_owned; /**< True if dispose must free message. */
    size_t         message_alloc_size;
} zsp_diag_t;

/**
 * Diagnostic collector. Holds up to `max` records; further emits
 * past the cap set `truncated` and otherwise no-op.
 *
 * `error_count` counts emitted ZSP_SEV_ERROR records (including
 * any that were dropped after the cap). Callers determine
 * "compilation failed" from `error_count > 0`, not from the
 * collector's length, so capping doesn't cause silent acceptance
 * of broken sources.
 */
typedef struct zsp_diag_collector {
    const zserio_allocator_t *alloc;
    zsp_diag_t               *diags;
    size_t                    count;
    size_t                    cap;
    size_t                    max;          /**< Hard cap from Q6. */
    size_t                    error_count;  /**< Counts emitted errors. */
    bool                      truncated;
} zsp_diag_collector_t;

/**
 * Initialise an empty collector. `max` of zero defaults to
 * ZSP_DIAG_DEFAULT_MAX (20). `alloc` MUST outlive the collector.
 */
zserio_status_t zsp_diag_init(zsp_diag_collector_t *c,
                              const zserio_allocator_t *alloc,
                              size_t max);

/** Release every owned record + the diagnostics array. */
void zsp_diag_dispose(zsp_diag_collector_t *c);

/**
 * Emit a diagnostic whose message is a static string literal with
 * static lifetime. The collector stores the pointer directly; no
 * copy is made. Use this for fixed messages like
 * `"expected ';' after field declaration"`.
 *
 * Returns ZSERIO_OK regardless of cap state — over-cap emits set
 * `truncated` and increment `error_count` (if applicable) without
 * recording the diagnostic. Returns ZSERIO_ERR_OUT_OF_MEMORY only
 * on initial allocator failure under the cap.
 */
zserio_status_t zsp_diag_emit_lit(zsp_diag_collector_t *c,
                                  zsp_severity_t        sev,
                                  zsp_file_id_t         file_id,
                                  uint32_t              offset,
                                  const char           *literal);

/**
 * Emit a diagnostic whose message is copied from `text`. The
 * collector allocates a buffer from its allocator and copies
 * `len` bytes (no NUL is required in the input; the copy is
 * NUL-terminated for callers that expect a C string). Returns
 * ZSERIO_ERR_OUT_OF_MEMORY if the copy allocation fails.
 *
 * Used by the future builder API and by callers that need to
 * splice in identifier or token-name fragments.
 */
zserio_status_t zsp_diag_emit_copy(zsp_diag_collector_t *c,
                                   zsp_severity_t        sev,
                                   zsp_file_id_t         file_id,
                                   uint32_t              offset,
                                   const char           *text,
                                   size_t                len);

/** Returns the count of emitted error-severity diagnostics (including any over-cap). */
size_t zsp_diag_error_count(const zsp_diag_collector_t *c);

/** Returns true if the cap was hit and one or more records were dropped. */
bool zsp_diag_truncated(const zsp_diag_collector_t *c);

/**
 * Output sink. Called by the formatter with chunks of UTF-8
 * encoded text. Implementations write into a `FILE *`, a buffer,
 * a log sink, etc. The callback MUST consume all bytes (no
 * partial writes); writers that may fail should set an
 * out-of-band error flag accessible via their `userdata` pointer.
 *
 * `bytes` is NOT NUL-terminated; use `len`.
 */
typedef void (*zsp_diag_write_fn)(void *userdata,
                                  const char *bytes,
                                  size_t len);

/**
 * Render every diagnostic in `c` in gcc-style format. Each
 * diagnostic produces:
 *
 *   file.zs:LINE:COL: error: <message>
 *      LINE | <source line>
 *           |        ^
 *
 * After all diagnostics, if `c->truncated` is set, a trailing
 * "... N additional errors suppressed (cap=M)" line is rendered.
 *
 * `src` is used to look up the file name and recover line/col.
 * Returns ZSERIO_OK on success; ZSERIO_ERR_INVALID_ARGUMENT for
 * NULL inputs; ZSERIO_ERR_OUT_OF_MEMORY if the line-start table
 * build fails inside `zsp_source_line_col`.
 */
zserio_status_t zsp_diag_format_gcc(const zsp_diag_collector_t *c,
                                    zsp_source_t              *src,
                                    zsp_diag_write_fn          write,
                                    void                      *userdata);

/**
 * Render every diagnostic as one JSON object per line (NDJSON).
 * Shape: {"file":"...", "line":N, "col":N, "severity":"error",
 *         "message":"..."}.
 *
 * Intended for IDE / tooling consumers; not pretty-printed.
 */
zserio_status_t zsp_diag_format_json(const zsp_diag_collector_t *c,
                                     zsp_source_t              *src,
                                     zsp_diag_write_fn          write,
                                     void                      *userdata);

#endif /* ZSPARSER_DIAG_H_ */
