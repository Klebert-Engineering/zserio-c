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
    if (self == NULL || bs == NULL) {
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
</#if>
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
}
