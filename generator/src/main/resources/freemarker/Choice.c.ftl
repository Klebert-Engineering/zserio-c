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
    default: {
<#if hasDefault>
<#if hasDefaultField>
        ${defaultField.readStmt}
</#if>
        break;
<#else>
        return ZSERIO_ERR_INVALID_TAG;
</#if>
    }
    }
    return ZSERIO_OK;
}

zserio_status_t ${funcPrefix}_write(${typeName} *self,
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
    default: {
<#if hasDefault>
<#if hasDefaultField>
        ${defaultField.writeStmt}
</#if>
        break;
<#else>
        return ZSERIO_ERR_INVALID_TAG;
</#if>
    }
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
    default: {
<#if hasDefaultField && defaultField.freeStmt?has_content>
        ${defaultField.freeStmt}
</#if>
        break;
    }
    }
}

<#if hasPackingContext>
zserio_status_t ${funcPrefix}_init_packing_context(${typeName} *self,
        ${funcPrefix}_packing_ctx_t *ctx)
{
    if (self == NULL || ctx == NULL) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }
    /* Choice packing context: dispatch the active arm's init by
     * the selector. Inactive arms leave their corresponding context
     * member zero — never used because read/write/observe dispatches
     * the same way. */
    switch (${selectorExpr}) {
<#list cases as case>
<#list case.labels as lbl>
    case ${lbl}:
</#list>
    {
<#if case.hasField && case.packed.usesContext>
        ${case.packed.initStmt}
</#if>
        break;
    }
</#list>
    default: {
<#if hasDefaultField && defaultPacked.usesContext>
        ${defaultPacked.initStmt}
</#if>
        break;
    }
    }
    return ZSERIO_OK;
}

zserio_status_t ${funcPrefix}_observe_packing_context(${typeName} *self,
        ${funcPrefix}_packing_ctx_t *ctx)
{
    if (self == NULL || ctx == NULL) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }
    /* Re-init: a Choice's active arm depends on the per-element
     * selector. The array driver's outer init only configures the arm
     * matching element[0]'s selector; subsequent elements with a
     * different arm need their context configured at their own
     * observe / read / write time. zserio_delta_init / _init_traits
     * are idempotent, so this is a no-op for already-configured arms;
     * the array driver zero-initialises the packing context up front
     * so uninitialised arms read as "not yet configured". */
    ZSERIO_TRY(${funcPrefix}_init_packing_context(self, ctx));
    switch (${selectorExpr}) {
<#list cases as case>
<#list case.labels as lbl>
    case ${lbl}:
</#list>
    {
<#if case.hasField && case.packed.usesContext>
        ${case.packed.observeStmt}
</#if>
        break;
    }
</#list>
    default: {
<#if hasDefaultField && defaultPacked.usesContext>
        ${defaultPacked.observeStmt}
</#if>
        break;
    }
    }
    return ZSERIO_OK;
}

zserio_status_t ${funcPrefix}_read_packed(${typeName} *self,
        zserio_bitstream_reader_t *bs, const zserio_allocator_t *alloc,
        ${funcPrefix}_packing_ctx_t *ctx)
{
    if (self == NULL || bs == NULL || ctx == NULL) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }
<#if !hasOwnedCases>
    (void)alloc;
</#if>
    /* Re-init: see the comment in _observe_packing_context. */
    ZSERIO_TRY(${funcPrefix}_init_packing_context(self, ctx));
    switch (${selectorExpr}) {
<#list cases as case>
<#list case.labels as lbl>
    case ${lbl}:
</#list>
    {
<#if case.hasField>
<#if case.packed.usesContext>
        ${case.packed.readStmt}
<#else>
        ${case.field.readStmt}
</#if>
</#if>
        break;
    }
</#list>
    default: {
<#if hasDefault>
<#if hasDefaultField>
<#if defaultPacked.usesContext>
        ${defaultPacked.readStmt}
<#else>
        ${defaultField.readStmt}
</#if>
</#if>
        break;
<#else>
        return ZSERIO_ERR_INVALID_TAG;
</#if>
    }
    }
    return ZSERIO_OK;
}

zserio_status_t ${funcPrefix}_write_packed(${typeName} *self,
        zserio_bitstream_writer_t *bs,
        ${funcPrefix}_packing_ctx_t *ctx)
{
    if (self == NULL || bs == NULL || ctx == NULL) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }
    /* Re-init: see the comment in _observe_packing_context. */
    ZSERIO_TRY(${funcPrefix}_init_packing_context(self, ctx));
    switch (${selectorExpr}) {
<#list cases as case>
<#list case.labels as lbl>
    case ${lbl}:
</#list>
    {
<#if case.hasField>
<#if case.packed.usesContext>
        ${case.packed.writeStmt}
<#else>
        ${case.field.writeStmt}
</#if>
</#if>
        break;
    }
</#list>
    default: {
<#if hasDefault>
<#if hasDefaultField>
<#if defaultPacked.usesContext>
        ${defaultPacked.writeStmt}
<#else>
        ${defaultField.writeStmt}
</#if>
</#if>
        break;
<#else>
        return ZSERIO_ERR_INVALID_TAG;
</#if>
    }
    }
    return ZSERIO_OK;
}
</#if>

