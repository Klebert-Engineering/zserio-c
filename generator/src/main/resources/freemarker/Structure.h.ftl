<#include "FileHeader.inc.ftl">
<@file_header generatorDescription/>

#ifndef ${includeGuard}
#define ${includeGuard}

#include <stdbool.h>
#include <stdint.h>

#include <zserio/allocator.h>
#include <zserio/bitstream.h>
#include <zserio/delta.h>
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
<#list fields as field>
<#if field.extraMember?has_content>
    ${field.extraMember}
</#if>
    ${field.declaration};
</#list>
<#if !hasParameters && fields?size == 0>
    /* Empty zserio struct: zero data, zero wire bits. The placeholder
     * member keeps {@code -Wpedantic} happy (C99 forbids the empty
     * struct shape) while consuming nothing on the wire. */
    char _zserio_empty;
</#if>
} ${typeName};

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

<#if hasPackingContext>
typedef struct ${funcPrefix}_packing_ctx {
<#list packedFields as pf>
<#if pf.usesContext>
    ${pf.ctxMember}
</#if>
</#list>
} ${funcPrefix}_packing_ctx_t;

zserio_status_t ${funcPrefix}_init_packing_context(${typeName} *self,
        ${funcPrefix}_packing_ctx_t *ctx);
zserio_status_t ${funcPrefix}_observe_packing_context(${typeName} *self,
        ${funcPrefix}_packing_ctx_t *ctx);
zserio_status_t ${funcPrefix}_read_packed(${typeName} *self,
        zserio_bitstream_reader_t *bs, const zserio_allocator_t *alloc,
        ${funcPrefix}_packing_ctx_t *ctx);
zserio_status_t ${funcPrefix}_write_packed(${typeName} *self,
        zserio_bitstream_writer_t *bs,
        ${funcPrefix}_packing_ctx_t *ctx);

</#if>
<#list functions as fn>
static inline ${fn.returnType} ${fn.fullName}(const ${typeName} *self)
{
    /* The body may be a constant expression that doesn't touch
     * `self` (e.g. functions that return a fixed enum value); silence
     * the compiler's unused-parameter warning either way. */
    (void)self;
    return (${fn.returnType})(${fn.body});
}

</#list>
#endif /* ${includeGuard} */
