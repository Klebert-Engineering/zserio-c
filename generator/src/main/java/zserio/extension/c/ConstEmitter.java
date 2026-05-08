/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Fabian Klebert
 */

package zserio.extension.c;

import zserio.ast.Constant;
import zserio.extension.common.OutputFileManager;
import zserio.extension.common.ZserioExtensionException;

/**
 * Emits one {@code .h} file per zserio {@code const} declaration.
 *
 * The output is a {@code static const T name = value;} declaration
 * inside an include-guarded header. This is the simplest emitter and
 * exists primarily to validate the FreeMarker / OutputFileManager
 * pipeline before the more substantial compound-type emitters land.
 */
public final class ConstEmitter extends CDefaultEmitter
{
    private static final String TEMPLATE = "Constant.h.ftl";

    public ConstEmitter(OutputFileManager outputFileManager,
            CExtensionParameters cParameters, TemplateDataContext context)
    {
        super(outputFileManager, cParameters, context);
    }

    @Override
    public void beginConst(Constant constant) throws ZserioExtensionException
    {
        final ConstEmitterTemplateData data =
                new ConstEmitterTemplateData(context, constant);
        processHeaderTemplate(TEMPLATE, data, constant.getPackage().getPackageName(),
                constant.getName());
    }
}
