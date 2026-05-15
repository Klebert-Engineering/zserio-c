<#include "FileHeader.inc.ftl">
<@file_header generatorDescription/>

#include "${headerIncludePath}"

#include <string.h>

#include <zserio/zserio.h>

void ${funcPrefix}_init(${typeName} *self)
{
    if (self == NULL) {
        return;
    }
    (void)memset(self, 0, sizeof(*self));
}

<#if hasParameters>
void ${funcPrefix}_set_parameters(${typeName} *self<#list parameters as p>,
        ${p.nativeType} ${p.name}</#list>)
{
    if (self == NULL) {
        return;
    }
<#list parameters as p>
    self->${p.name} = ${p.name};
</#list>
}

</#if>
zserio_status_t ${funcPrefix}_read(${typeName} *self,
        zserio_bitstream_reader_t *bs, const zserio_allocator_t *alloc)
{
    if ((self == NULL) || (bs == NULL)) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }
<#if !hasOwnedCases>
    (void)alloc;
</#if>
    {
        uint32_t raw_tag;
        ZSERIO_TRY(zserio_read_varsize(bs, &raw_tag));
        self->tag = raw_tag;
    }
    switch (self->tag) {
<#list cases as case>
    case ${case.tagValue}: {
        ${case.field.readStmt}
        break;
    }
</#list>
    default:
        return ZSERIO_ERR_INVALID_TAG;
    }
    return ZSERIO_OK;
}

zserio_status_t ${funcPrefix}_write(${typeName} *self,
        zserio_bitstream_writer_t *bs)
{
    if ((self == NULL) || (bs == NULL)) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }
    ZSERIO_TRY(zserio_write_varsize(bs, self->tag));
    switch (self->tag) {
<#list cases as case>
    case ${case.tagValue}: {
        ${case.field.writeStmt}
        break;
    }
</#list>
    default:
        return ZSERIO_ERR_INVALID_TAG;
    }
    return ZSERIO_OK;
}

void ${funcPrefix}_free(${typeName} *self, const zserio_allocator_t *alloc)
{
    if (self == NULL) {
        return;
    }
<#if !hasOwnedCases>
    (void)alloc;
<#else>
    switch (self->tag) {
<#list cases as case>
<#if case.field.freeStmt?has_content>
    case ${case.tagValue}: {
        ${case.field.freeStmt}
        break;
    }
</#if>
</#list>
    default:
        break;
    }
</#if>
}

<#if hasPackingContext>
zserio_status_t ${funcPrefix}_init_packing_context(${typeName} *self,
        ${funcPrefix}_packing_ctx_t *ctx)
{
    if ((self == NULL) || (ctx == NULL)) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }
    /* The varsize tag is delta-encoded across `packed Union[]` —
     * configure it in trait-driven mode (matches upstream's
     * VarSizeArrayTraits). The runtime's delta_init_traits is
     * idempotent so repeated calls per element are harmless. */
    ZSERIO_TRY(zserio_delta_init_traits(&ctx->tag, &zserio_delta_traits_varsize));
    /* Union packing context: dispatch the active arm's init by the
     * tag. Inactive arms leave their corresponding context member
     * zero — never used because read/write/observe dispatch the same
     * way. */
    switch (self->tag) {
<#list cases as case>
    case ${case.tagValue}: {
<#if case.packed.usesContext>
        ${case.packed.initStmt}
</#if>
        break;
    }
</#list>
    default:
        break;
    }
    return ZSERIO_OK;
}

zserio_status_t ${funcPrefix}_observe_packing_context(${typeName} *self,
        ${funcPrefix}_packing_ctx_t *ctx)
{
    if ((self == NULL) || (ctx == NULL)) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }
    /* Re-init: a Union's active arm depends on the per-element tag.
     * The array driver's outer init only configures the arm matching
     * element[0]'s tag; subsequent elements with a different arm need
     * their context configured at their own observe / read / write
     * time. zserio_delta_init / _init_traits are idempotent, so this
     * is a no-op for already-configured arms; the array driver zero-
     * initialises the packing context up front so uninitialised arms
     * read as "not yet configured". */
    ZSERIO_TRY(${funcPrefix}_init_packing_context(self, ctx));
    ZSERIO_TRY(zserio_delta_observe(&ctx->tag, (uint64_t)self->tag));
    switch (self->tag) {
<#list cases as case>
    case ${case.tagValue}: {
<#if case.packed.usesContext>
        ${case.packed.observeStmt}
</#if>
        break;
    }
</#list>
    default:
        break;
    }
    return ZSERIO_OK;
}

zserio_status_t ${funcPrefix}_read_packed(${typeName} *self,
        zserio_bitstream_reader_t *bs, const zserio_allocator_t *alloc,
        ${funcPrefix}_packing_ctx_t *ctx)
{
    if ((self == NULL) || (bs == NULL) || (ctx == NULL)) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }
<#if !hasOwnedCases>
    (void)alloc;
</#if>
    /* Re-init: see the comment in _observe_packing_context. The
     * tag's delta context must be configured BEFORE reading so the
     * descriptor (isPacked + maxBitNumber) is decoded into it on the
     * first element. */
    ZSERIO_TRY(${funcPrefix}_init_packing_context(self, ctx));
    {
        uint64_t raw_tag;
        ZSERIO_TRY(zserio_delta_read(&ctx->tag, bs, &raw_tag));
        self->tag = (uint32_t)raw_tag;
    }
    switch (self->tag) {
<#list cases as case>
    case ${case.tagValue}: {
<#if case.packed.usesContext>
        ${case.packed.readStmt}
<#else>
        ${case.field.readStmt}
</#if>
        break;
    }
</#list>
    default:
        return ZSERIO_ERR_INVALID_TAG;
    }
    return ZSERIO_OK;
}

zserio_status_t ${funcPrefix}_write_packed(${typeName} *self,
        zserio_bitstream_writer_t *bs,
        ${funcPrefix}_packing_ctx_t *ctx)
{
    if ((self == NULL) || (bs == NULL) || (ctx == NULL)) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }
    /* Re-init: see the comment in _observe_packing_context. */
    ZSERIO_TRY(${funcPrefix}_init_packing_context(self, ctx));
    ZSERIO_TRY(zserio_delta_write(&ctx->tag, bs, (uint64_t)self->tag));
    switch (self->tag) {
<#list cases as case>
    case ${case.tagValue}: {
<#if case.packed.usesContext>
        ${case.packed.writeStmt}
<#else>
        ${case.field.writeStmt}
</#if>
        break;
    }
</#list>
    default:
        return ZSERIO_ERR_INVALID_TAG;
    }
    return ZSERIO_OK;
}
</#if>
