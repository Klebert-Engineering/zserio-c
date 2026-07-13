/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Klebert Engineering
 */

package zserio.extension.c;

import zserio.ast.BitmaskType;
import zserio.ast.CompoundType;
import zserio.ast.EnumType;
import zserio.ast.PackageName;
import zserio.ast.Subtype;
import zserio.ast.ZserioType;
import zserio.extension.common.ZserioExtensionException;

/** FreeMarker data for {@code Subtype.h.ftl}. */
public final class SubtypeEmitterTemplateData
{
    private final String generatorDescription;
    private final String typeName;
    private final String nativeType;
    private final String includeGuard;
    private final String extraInclude; // null for built-in subtypes

    public SubtypeEmitterTemplateData(TemplateDataContext context, Subtype subType)
            throws ZserioExtensionException
    {
        this.generatorDescription = context.getGeneratorDescription();
        final PackageName pkg = subType.getPackage().getPackageName();
        final String zserioName = subType.getName();
        this.typeName = CIdentifierFormatter.typeName(pkg, zserioName);
        this.includeGuard = CIdentifierFormatter.includeGuard(pkg, zserioName);
        this.nativeType = CNativeTypeMapper.nativeType(subType.getTypeReference());

        // If the subtype's resolved underlying is a compound / enum /
        // bitmask, our typedef references that type's typedef name and
        // therefore needs that type's header included. Built-in
        // underlyings (int, bool, float, ...) need no extra include.
        final ZserioType resolved = resolveUnderlying(subType);
        this.extraInclude = headerIncludeFor(resolved);
    }

    private static ZserioType resolveUnderlying(Subtype subType)
    {
        ZserioType t = subType.getTypeReference().getBaseTypeReference().getType();
        while (t instanceof Subtype)
        {
            t = ((Subtype)t).getTypeReference().getBaseTypeReference().getType();
        }
        return t;
    }

    private static String headerIncludeFor(ZserioType type)
    {
        if (type instanceof CompoundType)
        {
            final CompoundType ct = (CompoundType)type;
            return CIdentifierFormatter.headerIncludePath(
                    ct.getPackage().getPackageName(), ct.getName());
        }
        if (type instanceof EnumType)
        {
            final EnumType et = (EnumType)type;
            return CIdentifierFormatter.headerIncludePath(
                    et.getPackage().getPackageName(), et.getName());
        }
        if (type instanceof BitmaskType)
        {
            final BitmaskType bt = (BitmaskType)type;
            return CIdentifierFormatter.headerIncludePath(
                    bt.getPackage().getPackageName(), bt.getName());
        }
        return null;
    }

    public String getGeneratorDescription() { return generatorDescription; }
    public String getTypeName()             { return typeName; }
    public String getNativeType()           { return nativeType; }
    public String getIncludeGuard()         { return includeGuard; }
    public String getExtraInclude()         { return extraInclude; }
    public boolean getHasExtraInclude()     { return extraInclude != null; }
}
