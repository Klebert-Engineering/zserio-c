/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Fabian Klebert
 */

package zserio.extension.c;

import zserio.ast.EnumType;
import zserio.extension.common.OutputFileManager;
import zserio.extension.common.ZserioExtensionException;

/**
 * Emits one {@code .h} file per zserio {@code enum} declaration.
 *
 * Generated form:
 * <pre>
 *   typedef &lt;underlying_native_type&gt; pkg_color_t;
 *   static const pkg_color_t PKG_COLOR_RED   = (pkg_color_t)0x2;
 *   ...
 * </pre>
 *
 * We deliberately use a typedef of the underlying integer plus
 * {@code static const} enumerators, not C's {@code enum}, because
 * (a) C enum's underlying width is implementation-defined and we
 * want explicit width control, and (b) MISRA C:2012 prefers
 * {@code static const} over {@code #define} macros.
 */
public final class EnumerationEmitter extends CDefaultEmitter
{
    private static final String TEMPLATE = "Enumeration.h.ftl";

    public EnumerationEmitter(OutputFileManager outputFileManager,
            CExtensionParameters cParameters, TemplateDataContext context)
    {
        super(outputFileManager, cParameters, context);
    }

    @Override
    public void beginEnumeration(EnumType enumType) throws ZserioExtensionException
    {
        final EnumerationEmitterTemplateData data =
                new EnumerationEmitterTemplateData(context, enumType);
        processHeaderTemplate(TEMPLATE, data, enumType.getPackage().getPackageName(),
                enumType.getName());
    }
}
