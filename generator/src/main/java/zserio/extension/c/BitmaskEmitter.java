/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Fabian Klebert
 */

package zserio.extension.c;

import zserio.ast.BitmaskType;
import zserio.extension.common.OutputFileManager;
import zserio.extension.common.ZserioExtensionException;

/**
 * Emits one {@code .h} file per zserio {@code bitmask} declaration.
 *
 * The generated form mirrors {@link EnumerationEmitter}: a typedef of
 * the underlying integer plus a {@code static const} per named flag.
 * Bitmask values are bit positions (or explicit), and the runtime
 * stores OR-of-flags; no closed-set check is emitted, matching
 * upstream's permissive treatment.
 */
public final class BitmaskEmitter extends CDefaultEmitter
{
    private static final String TEMPLATE = "Bitmask.h.ftl";

    public BitmaskEmitter(OutputFileManager outputFileManager,
            CExtensionParameters cParameters, TemplateDataContext context)
    {
        super(outputFileManager, cParameters, context);
    }

    @Override
    public void beginBitmask(BitmaskType bitmaskType) throws ZserioExtensionException
    {
        final BitmaskEmitterTemplateData data =
                new BitmaskEmitterTemplateData(context, bitmaskType);
        processHeaderTemplate(TEMPLATE, data, bitmaskType.getPackage().getPackageName(),
                bitmaskType.getName());
    }
}
