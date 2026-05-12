<#include "FileHeader.inc.ftl">
<@file_header generatorDescription/>

#ifndef ${includeGuard}
#define ${includeGuard}

#include <stdbool.h>
#include <stdint.h>

#include <zserio/allocator.h>
#include <zserio/bitstream.h>
#include <zserio/delta.h>
#include <zserio/expressions.h>
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
<#list cases as case>
<#if case.field.extraMember?has_content>
    ${case.field.extraMember}
</#if>
</#list>
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

<#if hasPackingContext>
typedef struct ${funcPrefix}_packing_ctx {
    /* The varsize tag is delta-encoded across `packed Union[]`
     * elements (upstream cpp's behavior — read via
     * VarSizeArrayTraits, written through the same delta context).
     * Mirrors cpp's `ZserioPackingContext::choiceTag()` accessor. */
    zserio_delta_context_t tag;
<#list cases as case>
<#if case.packed.usesContext>
    ${case.packed.ctxMember}
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
<#-- Forward declarations so a schema function in this compound can
     call a sibling declared later. Same rationale as Structure.h.ftl. -->
<#list functions as fn>
static inline ${fn.returnType} ${fn.fullName}(const ${typeName} *self);
</#list>
<#list functions as fn>
static inline ${fn.returnType} ${fn.fullName}(const ${typeName} *self)
{
    (void)self;
    /* No cast: implicit return-statement conversion handles
     * integer widening, and a struct return type cannot be cast to
     * itself without tripping `-Wpedantic`. */
    return (${fn.body});
}

</#list>
#endif /* ${includeGuard} */
