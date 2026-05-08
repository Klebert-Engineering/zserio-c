/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Fabian Klebert
 */

package zserio.extension.c;

import zserio.ast.PackageName;
import zserio.ast.Subtype;
import zserio.extension.common.ZserioExtensionException;

/** FreeMarker data for {@code Subtype.h.ftl}. */
public final class SubtypeEmitterTemplateData
{
    private final String generatorDescription;
    private final String typeName;
    private final String nativeType;
    private final String includeGuard;

    public SubtypeEmitterTemplateData(TemplateDataContext context, Subtype subType)
            throws ZserioExtensionException
    {
        this.generatorDescription = context.getGeneratorDescription();
        final PackageName pkg = subType.getPackage().getPackageName();
        final String zserioName = subType.getName();
        this.typeName = CIdentifierFormatter.typeName(pkg, zserioName);
        this.includeGuard = CIdentifierFormatter.includeGuard(pkg, zserioName);
        this.nativeType = CNativeTypeMapper.nativeType(subType.getTypeReference());
    }

    public String getGeneratorDescription() { return generatorDescription; }
    public String getTypeName()             { return typeName; }
    public String getNativeType()           { return nativeType; }
    public String getIncludeGuard()         { return includeGuard; }
}
