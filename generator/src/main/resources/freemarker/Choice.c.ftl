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

zserio_status_t ${funcPrefix}_read(${typeName} *self,
        zserio_bitstream_reader_t *bs, const zserio_allocator_t *alloc)
{
    if (self == NULL || bs == NULL) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }
<#if !hasOwnedCases>
    (void)alloc;
</#if>
    switch (${selectorExpr}) {
<#list cases as case>
<#list case.labels as lbl>
    case ${lbl}:
</#list>
    {
<#if case.hasField>
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

zserio_status_t ${funcPrefix}_write(const ${typeName} *self,
        zserio_bitstream_writer_t *bs)
{
    if (self == NULL || bs == NULL) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }
    switch (${selectorExpr}) {
<#list cases as case>
<#list case.labels as lbl>
    case ${lbl}:
</#list>
    {
<#if case.hasField>
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

void ${funcPrefix}_free(${typeName} *self, const zserio_allocator_t *alloc)
{
    if (self == NULL) {
        return;
    }
<#if !hasOwnedCases>
    (void)alloc;
</#if>
    switch (${selectorExpr}) {
<#list cases as case>
<#if case.hasField && case.field.freeStmt?has_content>
<#list case.labels as lbl>
    case ${lbl}:
</#list>
    {
        ${case.field.freeStmt}
        break;
    }
</#if>
</#list>
    default:
        break;
    }
}
