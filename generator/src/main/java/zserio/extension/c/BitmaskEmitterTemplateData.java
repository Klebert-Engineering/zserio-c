/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Fabian Klebert
 */

package zserio.extension.c;

import java.util.ArrayList;
import java.util.List;

import zserio.ast.BitmaskType;
import zserio.ast.BitmaskValue;
import zserio.ast.PackageName;
import zserio.extension.common.ZserioExtensionException;

/** FreeMarker data for {@code Bitmask.h.ftl}. */
public final class BitmaskEmitterTemplateData
{
    private final String generatorDescription;
    private final String typeName;
    private final String nativeType;
    private final String includeGuard;
    private final List<Item> items;

    public BitmaskEmitterTemplateData(TemplateDataContext context, BitmaskType bitmaskType)
            throws ZserioExtensionException
    {
        this.generatorDescription = context.getGeneratorDescription();
        final PackageName pkg = bitmaskType.getPackage().getPackageName();
        final String zserioName = bitmaskType.getName();
        this.typeName = CIdentifierFormatter.typeName(pkg, zserioName);
        this.includeGuard = CIdentifierFormatter.includeGuard(pkg, zserioName);
        this.nativeType = CNativeTypeMapper.nativeType(bitmaskType.getTypeInstantiation());

        final List<Item> built = new ArrayList<>();
        for (final BitmaskValue bv : bitmaskType.getValues())
        {
            built.add(new Item(
                    CIdentifierFormatter.constantName(pkg, zserioName + "_" + bv.getName()),
                    bv.getValue().toString()));
        }
        this.items = built;
    }

    public String getGeneratorDescription() { return generatorDescription; }
    public String getTypeName()             { return typeName; }
    public String getNativeType()           { return nativeType; }
    public String getIncludeGuard()         { return includeGuard; }
    public List<Item> getItems()            { return items; }

    /** One named flag. */
    public static final class Item
    {
        private final String name;
        private final String value;

        Item(String name, String value)
        {
            this.name = name;
            this.value = value;
        }

        public String getName()  { return name; }
        public String getValue() { return value; }
    }
}
