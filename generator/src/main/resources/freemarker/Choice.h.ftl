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
<#list cases as case>
<#if case.hasField && case.field.extraMember?has_content>
    ${case.field.extraMember}
</#if>
</#list>
<#if hasDefaultField && defaultField.extraMember?has_content>
    ${defaultField.extraMember}
</#if>
    union {
<#list cases as case>
<#if case.hasField>
        ${case.field.declaration};
</#if>
</#list>
<#if hasDefaultField>
        ${defaultField.declaration};
</#if>
    } u;
} ${typeName};

void            ${funcPrefix}_init(${typeName} *self);
void            ${funcPrefix}_set_parameters(${typeName} *self<#list parameters as p>,
        ${p.nativeType} ${p.name}</#list>);
zserio_status_t ${funcPrefix}_read(${typeName} *self,
        zserio_bitstream_reader_t *bs, const zserio_allocator_t *alloc);
zserio_status_t ${funcPrefix}_write(${typeName} *self,
        zserio_bitstream_writer_t *bs);
void            ${funcPrefix}_free(${typeName} *self, const zserio_allocator_t *alloc);

<#list functions as fn>
static inline ${fn.returnType} ${fn.fullName}(const ${typeName} *self)
{
    (void)self;
    return (${fn.returnType})(${fn.body});
}

</#list>
#endif /* ${includeGuard} */
