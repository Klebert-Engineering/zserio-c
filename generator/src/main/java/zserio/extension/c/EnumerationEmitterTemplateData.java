/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Fabian Klebert
 */

package zserio.extension.c;

import java.util.ArrayList;
import java.util.List;

import zserio.ast.EnumItem;
import zserio.ast.EnumType;
import zserio.ast.PackageName;
import zserio.extension.common.ZserioExtensionException;

/** FreeMarker data for {@code Enumeration.h.ftl}. */
public final class EnumerationEmitterTemplateData
{
    private final String generatorDescription;
    private final String typeName;
    private final String nativeType;
    private final String includeGuard;
    private final List<Item> items;

    public EnumerationEmitterTemplateData(TemplateDataContext context, EnumType enumType)
            throws ZserioExtensionException
    {
        this.generatorDescription = context.getGeneratorDescription();
        final PackageName pkg = enumType.getPackage().getPackageName();
        final String zserioName = enumType.getName();
        this.typeName = CIdentifierFormatter.typeName(pkg, zserioName);
        this.includeGuard = CIdentifierFormatter.includeGuard(pkg, zserioName);
        this.nativeType = CNativeTypeMapper.nativeType(enumType.getTypeInstantiation());

        final List<Item> built = new ArrayList<>();
        for (final EnumItem ei : enumType.getItems())
        {
            built.add(new Item(
                    CIdentifierFormatter.constantName(pkg, zserioName + "_" + ei.getName()),
                    ei.getValue().toString()));
        }
        this.items = built;
    }

    public String getGeneratorDescription() { return generatorDescription; }
    public String getTypeName()             { return typeName; }
    public String getNativeType()           { return nativeType; }
    public String getIncludeGuard()         { return includeGuard; }
    public List<Item> getItems()            { return items; }

    /** One enumerator. {@code name} is the C-mangled identifier;
     *  {@code value} is the integer value as a decimal string. */
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
