/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Klebert Engineering
 */

package zserio.extension.c;

import zserio.ast.UnionType;
import zserio.extension.common.OutputFileManager;
import zserio.extension.common.ZserioExtensionException;

/**
 * Emits one header + one source per zserio {@code union} declaration.
 *
 * Wire format: a {@code varsize} tag (case index in declaration order)
 * followed by the active case's payload. The C representation is a
 * struct with an explicit {@code uint32_t tag} field and an anonymous
 * inner union {@code u} of the per-case fields. The header also emits
 * one {@code #define <PREFIX>_TAG_<CASE>} per case so callers can tag
 * by name rather than ordinal.
 *
 * Field types are reused via {@link FieldFormatter}, which gets a
 * holder expression of {@code "self->u."} so the generated read /
 * write / free statements address the right member of the inner union.
 *
 * Out of scope until later batches: parameterised unions and array /
 * optional cases — they raise a {@link ZserioExtensionException} via
 * {@code FieldFormatter} when encountered.
 */
public final class UnionEmitter extends CDefaultEmitter
{
    private static final String HEADER_TEMPLATE = "Union.h.ftl";
    private static final String SOURCE_TEMPLATE = "Union.c.ftl";

    public UnionEmitter(OutputFileManager outputFileManager,
            CExtensionParameters cParameters, TemplateDataContext context)
    {
        super(outputFileManager, cParameters, context);
    }

    @Override
    public void beginUnion(UnionType unionType) throws ZserioExtensionException
    {
        final UnionEmitterTemplateData data =
                new UnionEmitterTemplateData(context, unionType);
        processHeaderTemplate(HEADER_TEMPLATE, data,
                unionType.getPackage().getPackageName(), unionType.getName());
        processSourceTemplate(SOURCE_TEMPLATE, data,
                unionType.getPackage().getPackageName(), unionType.getName());
    }
}
