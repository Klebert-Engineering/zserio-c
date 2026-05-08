/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Fabian Klebert
 */

package zserio.extension.c;

import zserio.ast.Subtype;
import zserio.extension.common.OutputFileManager;
import zserio.extension.common.ZserioExtensionException;

/**
 * Emits one {@code .h} file per zserio {@code subtype} declaration.
 * The output is a {@code typedef} from the C native type to the
 * subtype's mangled name.
 */
public final class SubtypeEmitter extends CDefaultEmitter
{
    private static final String TEMPLATE = "Subtype.h.ftl";

    public SubtypeEmitter(OutputFileManager outputFileManager,
            CExtensionParameters cParameters, TemplateDataContext context)
    {
        super(outputFileManager, cParameters, context);
    }

    @Override
    public void beginSubtype(Subtype subType) throws ZserioExtensionException
    {
        final SubtypeEmitterTemplateData data =
                new SubtypeEmitterTemplateData(context, subType);
        processHeaderTemplate(TEMPLATE, data, subType.getPackage().getPackageName(),
                subType.getName());
    }
}
