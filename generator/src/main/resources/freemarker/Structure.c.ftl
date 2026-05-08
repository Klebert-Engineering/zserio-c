<#include "FileHeader.inc.ftl">
<@file_header generatorDescription/>

#include "${headerIncludePath}"

#include <string.h>

#include <zserio/primitives.h>
<#if hasOwnedFields>
#include <zserio/string.h>
</#if>

void ${funcPrefix}_init(${typeName} *self)
{
    if (self == NULL) {
        return;
    }
    (void)memset(self, 0, sizeof(*self));
}

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

zserio_status_t ${funcPrefix}_write(const ${typeName} *self,
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
