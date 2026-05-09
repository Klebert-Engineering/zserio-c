<#include "FileHeader.inc.ftl">
<@file_header generatorDescription/>

#ifndef ${includeGuard}
#define ${includeGuard}

#include <stdbool.h>
#include <stdint.h>

#include <zserio/allocator.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
<#list extraIncludes as inc>
<#if inc?starts_with("zserio/")>
#include <${inc}>
<#else>
#include "${inc}"
</#if>
</#list>

typedef struct ${typeName} {
<#list parameters as p>
    ${p.nativeType} ${p.name};
</#list>
    uint32_t tag;
    union {
<#list cases as case>
        ${case.field.declaration};
</#list>
    } u;
} ${typeName};

<#list cases as case>
#define ${case.tagMacro} ${case.tagValue}
</#list>

void            ${funcPrefix}_init(${typeName} *self);
<#if hasParameters>
void            ${funcPrefix}_set_parameters(${typeName} *self<#list parameters as p>,
        ${p.nativeType} ${p.name}</#list>);
</#if>
zserio_status_t ${funcPrefix}_read(${typeName} *self,
        zserio_bitstream_reader_t *bs, const zserio_allocator_t *alloc);
zserio_status_t ${funcPrefix}_write(${typeName} *self,
        zserio_bitstream_writer_t *bs);
void            ${funcPrefix}_free(${typeName} *self, const zserio_allocator_t *alloc);

#endif /* ${includeGuard} */
