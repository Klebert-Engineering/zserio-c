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
    if (self == NULL || bs == NULL) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }
<#if !hasOwnedFields>
    (void)alloc;
</#if>
<#list fields as field>
    ${field.readStmt}
</#list>
    return ZSERIO_OK;
}

zserio_status_t ${funcPrefix}_write(${typeName} *self,
        zserio_bitstream_writer_t *bs)
{
    if (self == NULL || bs == NULL) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }
<#list fields as field>
    ${field.writeStmt}
</#list>
    return ZSERIO_OK;
}

void ${funcPrefix}_free(${typeName} *self, const zserio_allocator_t *alloc)
{
    if (self == NULL) {
        return;
    }
<#if !hasOwnedFields>
    (void)alloc;
</#if>
<#list fields as field>
<#if field.freeStmt?has_content>
    ${field.freeStmt}
</#if>
</#list>
}

<#if hasPackingContext>
zserio_status_t ${funcPrefix}_init_packing_context(${typeName} *self,
        ${funcPrefix}_packing_ctx_t *ctx)
{
    if (self == NULL || ctx == NULL) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }
    /* The bit-width expressions baked into init may or may not reference
     * `self->...` (parameters / dynamic-width bit fields do; plain
     * fixed-width fields don't). Silence the warning either way. */
    (void)self;
<#list packedFields as pf>
<#if pf.usesContext>
    ${pf.initStmt}
</#if>
</#list>
    return ZSERIO_OK;
}

zserio_status_t ${funcPrefix}_observe_packing_context(${typeName} *self,
        ${funcPrefix}_packing_ctx_t *ctx)
{
    if (self == NULL || ctx == NULL) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }
<#list packedFields as pf>
<#if pf.usesContext>
    ${pf.observeStmt}
</#if>
</#list>
    return ZSERIO_OK;
}

zserio_status_t ${funcPrefix}_read_packed(${typeName} *self,
        zserio_bitstream_reader_t *bs, const zserio_allocator_t *alloc,
        ${funcPrefix}_packing_ctx_t *ctx)
{
    if (self == NULL || bs == NULL || ctx == NULL) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }
<#if !hasOwnedFields>
    (void)alloc;
</#if>
<#list packedFields as pf>
    ${pf.readStmt}
</#list>
    return ZSERIO_OK;
}

zserio_status_t ${funcPrefix}_write_packed(${typeName} *self,
        zserio_bitstream_writer_t *bs,
        ${funcPrefix}_packing_ctx_t *ctx)
{
    if (self == NULL || bs == NULL || ctx == NULL) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }
<#list packedFields as pf>
    ${pf.writeStmt}
</#list>
    return ZSERIO_OK;
}
</#if>
