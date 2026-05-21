/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Klebert Engineering
 */

#include "zsparser/diag.h"

#include <string.h>

/*
 * Diagnostic collector + formatter. See zsparser/diag.h for the
 * design rationale. Output is written via a caller-supplied
 * callback; the library carries no <stdio.h> dependency.
 *
 * No varargs (MISRA Rule 17.1) — message formatting is done by
 * typed append helpers (`write_uint`, `write_lit`) into the
 * caller's callback. Numeric conversion uses a 32-byte scratch
 * buffer which is large enough for any uint32_t in decimal.
 */

#define ZSP_DIAG_INITIAL_CAP  ((size_t)8U)

/* ----- allocator plumbing ------------------------------------- */

static bool allocator_usable(const zserio_allocator_t *a)
{
    if (a == NULL) {
        return false;
    }
    if (a->vtable == NULL) {
        return false;
    }
    if (a->vtable->alloc == NULL) {
        return false;
    }
    if (a->vtable->free == NULL) {
        return false;
    }
    return true;
}

static void *call_alloc(const zserio_allocator_t *a, size_t size, size_t align)
{
    return a->vtable->alloc(a->userdata, size, align);
}

static void call_free(const zserio_allocator_t *a, void *p, size_t size, size_t align)
{
    a->vtable->free(a->userdata, p, size, align);
}

static void *call_realloc(const zserio_allocator_t *a,
                          void *p, size_t old_size, size_t new_size, size_t align)
{
    if (a->vtable->realloc != NULL) {
        return a->vtable->realloc(a->userdata, p, old_size, new_size, align);
    }
    void *const grown = call_alloc(a, new_size, align);
    if (grown == NULL) {
        return NULL;
    }
    const size_t to_copy = (old_size < new_size) ? old_size : new_size;
    if ((p != NULL) && (to_copy > 0U)) {
        (void)memcpy(grown, p, to_copy);
    }
    if (p != NULL) {
        call_free(a, p, old_size, align);
    }
    return grown;
}

/* ----- collector lifecycle ------------------------------------ */

zserio_status_t zsp_diag_init(zsp_diag_collector_t *c,
                              const zserio_allocator_t *alloc,
                              size_t max)
{
    if (c == NULL) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }
    if (!allocator_usable(alloc)) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }

    c->alloc       = alloc;
    c->diags       = NULL;
    c->count       = 0U;
    c->cap         = 0U;
    c->max         = (max == 0U) ? ZSP_DIAG_DEFAULT_MAX : max;
    c->error_count = 0U;
    c->truncated   = false;

    const size_t bytes = ZSP_DIAG_INITIAL_CAP * sizeof(zsp_diag_t);
    zsp_diag_t *const buf = (zsp_diag_t *)call_alloc(alloc, bytes,
                                                     sizeof(zsp_diag_t));
    if (buf == NULL) {
        return ZSERIO_ERR_OUT_OF_MEMORY;
    }
    (void)memset(buf, 0, bytes);
    c->diags = buf;
    c->cap   = ZSP_DIAG_INITIAL_CAP;
    return ZSERIO_OK;
}

void zsp_diag_dispose(zsp_diag_collector_t *c)
{
    if (c == NULL) {
        return;
    }
    if (c->alloc == NULL) {
        return;
    }

    for (size_t i = 0U; i < c->count; ++i) {
        zsp_diag_t *const d = &c->diags[i];
        if (d->message_owned && (d->message != NULL)) {
            call_free(c->alloc, (void *)(uintptr_t)d->message,
                      d->message_alloc_size, 1U);
        }
    }
    if (c->diags != NULL) {
        call_free(c->alloc, c->diags,
                  c->cap * sizeof(zsp_diag_t),
                  sizeof(zsp_diag_t));
    }
    c->diags       = NULL;
    c->count       = 0U;
    c->cap         = 0U;
    c->max         = 0U;
    c->error_count = 0U;
    c->truncated   = false;
    c->alloc       = NULL;
}

/* Grow the diagnostics array on demand (doubling, capped at max). */
static zserio_status_t grow_diags(zsp_diag_collector_t *c)
{
    if (c->count < c->cap) {
        return ZSERIO_OK;
    }
    size_t new_cap = c->cap * 2U;
    if (new_cap > c->max) {
        new_cap = c->max;
    }
    if (new_cap <= c->cap) {
        /* already at max; caller checks count vs. max before this */
        return ZSERIO_ERR_OUT_OF_MEMORY;
    }

    const size_t old_bytes = c->cap * sizeof(zsp_diag_t);
    const size_t new_bytes = new_cap * sizeof(zsp_diag_t);
    zsp_diag_t *const grown = (zsp_diag_t *)call_realloc(
            c->alloc, c->diags, old_bytes, new_bytes,
            sizeof(zsp_diag_t));
    if (grown == NULL) {
        return ZSERIO_ERR_OUT_OF_MEMORY;
    }
    (void)memset(&grown[c->cap], 0, new_bytes - old_bytes);
    c->diags = grown;
    c->cap   = new_cap;
    return ZSERIO_OK;
}

/* Count errors regardless of cap; record only if room. */
static void account_error(zsp_diag_collector_t *c, zsp_severity_t sev)
{
    if (sev == ZSP_SEV_ERROR) {
        c->error_count += 1U;
    }
}

zserio_status_t zsp_diag_emit_lit(zsp_diag_collector_t *c,
                                  zsp_severity_t        sev,
                                  zsp_file_id_t         file_id,
                                  uint32_t              offset,
                                  const char           *literal)
{
    if ((c == NULL) || (literal == NULL)) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }

    account_error(c, sev);

    if (c->count >= c->max) {
        c->truncated = true;
        return ZSERIO_OK;
    }

    ZSERIO_TRY(grow_diags(c));

    zsp_diag_t *const d = &c->diags[c->count];
    d->severity           = sev;
    d->file_id            = file_id;
    d->offset             = offset;
    d->message            = literal;
    d->message_len        = strlen(literal);
    d->message_owned      = false;
    d->message_alloc_size = 0U;
    c->count += 1U;
    return ZSERIO_OK;
}

zserio_status_t zsp_diag_emit_copy(zsp_diag_collector_t *c,
                                   zsp_severity_t        sev,
                                   zsp_file_id_t         file_id,
                                   uint32_t              offset,
                                   const char           *text,
                                   size_t                len)
{
    if ((c == NULL) || (text == NULL)) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }

    account_error(c, sev);

    if (c->count >= c->max) {
        c->truncated = true;
        return ZSERIO_OK;
    }

    ZSERIO_TRY(grow_diags(c));

    const size_t alloc_size = len + 1U;
    char *const copy = (char *)call_alloc(c->alloc, alloc_size, 1U);
    if (copy == NULL) {
        return ZSERIO_ERR_OUT_OF_MEMORY;
    }
    if (len > 0U) {
        (void)memcpy(copy, text, len);
    }
    copy[len] = '\0';

    zsp_diag_t *const d = &c->diags[c->count];
    d->severity           = sev;
    d->file_id            = file_id;
    d->offset             = offset;
    d->message            = copy;
    d->message_len        = len;
    d->message_owned      = true;
    d->message_alloc_size = alloc_size;
    c->count += 1U;
    return ZSERIO_OK;
}

size_t zsp_diag_error_count(const zsp_diag_collector_t *c)
{
    if (c == NULL) {
        return 0U;
    }
    return c->error_count;
}

bool zsp_diag_truncated(const zsp_diag_collector_t *c)
{
    if (c == NULL) {
        return false;
    }
    return c->truncated;
}

/* ----- formatter helpers -------------------------------------- */

static void write_bytes(zsp_diag_write_fn write, void *userdata,
                        const char *bytes, size_t len)
{
    if (len > 0U) {
        write(userdata, bytes, len);
    }
}

static void write_lit(zsp_diag_write_fn write, void *userdata,
                      const char *literal)
{
    write_bytes(write, userdata, literal, strlen(literal));
}

/* Decimal conversion of a uint32 into a caller-provided buffer.
 * Returns the number of bytes written. Buffer MUST be at least 11
 * bytes (max uint32 = 10 digits). */
static size_t fmt_uint32(uint32_t v, char *out)
{
    char tmp[10];
    size_t n = 0U;
    if (v == 0U) {
        out[0] = '0';
        return 1U;
    }
    while ((v > 0U) && (n < sizeof(tmp))) {
        tmp[n] = (char)('0' + (char)(v % 10U));
        v /= 10U;
        n += 1U;
    }
    for (size_t i = 0U; i < n; ++i) {
        out[i] = tmp[n - 1U - i];
    }
    return n;
}

static size_t fmt_size_t(size_t v, char *out)
{
    /* size_t fits in uint64 on all our targets; for diagnostics
     * we cap at uint32 range — error counts past 4B are not a
     * realistic scenario. */
    return fmt_uint32((uint32_t)v, out);
}

static void write_uint32(zsp_diag_write_fn write, void *userdata, uint32_t v)
{
    char buf[16];
    const size_t n = fmt_uint32(v, buf);
    write_bytes(write, userdata, buf, n);
}

static const char *severity_name(zsp_severity_t s)
{
    switch (s) {
    case ZSP_SEV_NOTE:  return "note";
    case ZSP_SEV_WARN:  return "warning";
    case ZSP_SEV_ERROR: return "error";
    default:            return "error";
    }
}

/* Render one diagnostic in gcc-style. */
static zserio_status_t format_one_gcc(const zsp_diag_t *d,
                                      zsp_source_t      *src,
                                      zsp_diag_write_fn  write,
                                      void              *userdata)
{
    const zsp_file_t *file = NULL;
    if (!zsp_source_get(src, d->file_id, &file)) {
        /* Fall back to a positionless render rather than dropping
         * the diagnostic — the user benefits from seeing the
         * message even when source resolution fails. */
        write_lit(write, userdata, "<unknown>: ");
        write_lit(write, userdata, severity_name(d->severity));
        write_lit(write, userdata, ": ");
        write_bytes(write, userdata, d->message, d->message_len);
        write_lit(write, userdata, "\n");
        return ZSERIO_OK;
    }

    uint32_t line = 0U;
    uint32_t col  = 0U;
    ZSERIO_TRY(zsp_source_line_col(src, d->file_id, d->offset, &line, &col));

    write_bytes(write, userdata, file->name, file->name_len);
    write_lit(write, userdata, ":");
    write_uint32(write, userdata, line);
    write_lit(write, userdata, ":");
    write_uint32(write, userdata, col);
    write_lit(write, userdata, ": ");
    write_lit(write, userdata, severity_name(d->severity));
    write_lit(write, userdata, ": ");
    write_bytes(write, userdata, d->message, d->message_len);
    write_lit(write, userdata, "\n");

    /* Source-line snippet + carat. Skip when the line is empty
     * or when retrieval fails — diagnostic is still useful
     * without the snippet. */
    const uint8_t *line_bytes = NULL;
    size_t         line_len   = 0U;
    if (zsp_source_line_text(src, d->file_id, line,
                             &line_bytes, &line_len) == ZSERIO_OK) {
        char line_buf[16];
        const size_t line_digits = fmt_uint32(line, line_buf);

        /* "   N | <line>" */
        write_lit(write, userdata, "   ");
        write_bytes(write, userdata, line_buf, line_digits);
        write_lit(write, userdata, " | ");
        write_bytes(write, userdata, (const char *)line_bytes, line_len);
        write_lit(write, userdata, "\n");

        /* "     | <spaces>^" — pad with spaces equal to line_digits
         * plus the literal "   " prefix and the " | " separator,
         * then advance to col-1 spaces in the source line. */
        write_lit(write, userdata, "   ");
        for (size_t i = 0U; i < line_digits; ++i) {
            write_lit(write, userdata, " ");
        }
        write_lit(write, userdata, " | ");
        if (col > 0U) {
            for (uint32_t i = 0U; i < (col - 1U); ++i) {
                /* Preserve tabs from the source so the carat lines
                 * up visually. Anything else collapses to a space. */
                if ((i < line_len) && (line_bytes[i] == (uint8_t)'\t')) {
                    write_lit(write, userdata, "\t");
                }
                else {
                    write_lit(write, userdata, " ");
                }
            }
        }
        write_lit(write, userdata, "^\n");
    }

    return ZSERIO_OK;
}

zserio_status_t zsp_diag_format_gcc(const zsp_diag_collector_t *c,
                                    zsp_source_t              *src,
                                    zsp_diag_write_fn          write,
                                    void                      *userdata)
{
    if ((c == NULL) || (src == NULL) || (write == NULL)) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }

    for (size_t i = 0U; i < c->count; ++i) {
        ZSERIO_TRY(format_one_gcc(&c->diags[i], src, write, userdata));
    }

    if (c->truncated) {
        write_lit(write, userdata, "note: additional diagnostics suppressed (cap=");
        char buf[16];
        const size_t n = fmt_size_t(c->max, buf);
        write_bytes(write, userdata, buf, n);
        write_lit(write, userdata, ")\n");
    }
    return ZSERIO_OK;
}

/* ----- JSON formatter ----------------------------------------- */

/* Write a JSON-escaped chunk. The set of characters that need
 * escaping in a JSON string is small: ", \, and control bytes
 * 0x00..0x1F. We handle the common visible ones (\n, \r, \t, \b,
 * \f) inline; others fall through to \u00XX. */
static void write_json_escaped(zsp_diag_write_fn write, void *userdata,
                               const char *text, size_t len)
{
    for (size_t i = 0U; i < len; ++i) {
        const unsigned char ch = (unsigned char)text[i];
        switch (ch) {
        case (unsigned char)'"':  write_lit(write, userdata, "\\\""); break;
        case (unsigned char)'\\': write_lit(write, userdata, "\\\\"); break;
        case (unsigned char)'\n': write_lit(write, userdata, "\\n");  break;
        case (unsigned char)'\r': write_lit(write, userdata, "\\r");  break;
        case (unsigned char)'\t': write_lit(write, userdata, "\\t");  break;
        case (unsigned char)'\b': write_lit(write, userdata, "\\b");  break;
        case (unsigned char)'\f': write_lit(write, userdata, "\\f");  break;
        default:
            if (ch < (unsigned char)0x20U) {
                static const char HEX[] = "0123456789abcdef";
                char esc[6];
                esc[0] = '\\';
                esc[1] = 'u';
                esc[2] = '0';
                esc[3] = '0';
                esc[4] = HEX[(ch >> 4) & 0x0FU];
                esc[5] = HEX[ch & 0x0FU];
                write_bytes(write, userdata, esc, sizeof(esc));
            }
            else {
                write_bytes(write, userdata, (const char *)&ch, 1U);
            }
            break;
        }
    }
}

static zserio_status_t format_one_json(const zsp_diag_t *d,
                                       zsp_source_t      *src,
                                       zsp_diag_write_fn  write,
                                       void              *userdata)
{
    const zsp_file_t *file = NULL;
    const bool have_file = zsp_source_get(src, d->file_id, &file);

    uint32_t line = 0U;
    uint32_t col  = 0U;
    if (have_file) {
        ZSERIO_TRY(zsp_source_line_col(src, d->file_id, d->offset,
                                       &line, &col));
    }

    write_lit(write, userdata, "{\"file\":\"");
    if (have_file) {
        write_json_escaped(write, userdata, file->name, file->name_len);
    }
    write_lit(write, userdata, "\",\"line\":");
    write_uint32(write, userdata, line);
    write_lit(write, userdata, ",\"col\":");
    write_uint32(write, userdata, col);
    write_lit(write, userdata, ",\"offset\":");
    write_uint32(write, userdata, d->offset);
    write_lit(write, userdata, ",\"severity\":\"");
    write_lit(write, userdata, severity_name(d->severity));
    write_lit(write, userdata, "\",\"message\":\"");
    write_json_escaped(write, userdata, d->message, d->message_len);
    write_lit(write, userdata, "\"}\n");
    return ZSERIO_OK;
}

zserio_status_t zsp_diag_format_json(const zsp_diag_collector_t *c,
                                     zsp_source_t              *src,
                                     zsp_diag_write_fn          write,
                                     void                      *userdata)
{
    if ((c == NULL) || (src == NULL) || (write == NULL)) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }
    for (size_t i = 0U; i < c->count; ++i) {
        ZSERIO_TRY(format_one_json(&c->diags[i], src, write, userdata));
    }
    return ZSERIO_OK;
}
