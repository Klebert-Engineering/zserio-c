/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Fabian Klebert
 */

package zserio.extension.c;

import java.util.ArrayList;
import java.util.List;

import zserio.ast.Field;
import zserio.ast.PackageName;
import zserio.ast.StructureType;
import zserio.extension.common.ZserioExtensionException;

/** FreeMarker data for {@code Structure.h.ftl} and {@code Structure.c.ftl}. */
public final class StructureEmitterTemplateData
{
    private final String generatorDescription;
    private final String typeName;
    private final String funcPrefix;
    private final String includeGuard;
    private final String headerIncludePath;
    private final List<FieldFormatter.FieldData> fields;
    private final boolean hasOwnedFields;

    public StructureEmitterTemplateData(TemplateDataContext context, StructureType structType)
            throws ZserioExtensionException
    {
        this.generatorDescription = context.getGeneratorDescription();
        final PackageName pkg = structType.getPackage().getPackageName();
        final String zserioName = structType.getName();
        this.typeName = CIdentifierFormatter.typeName(pkg, zserioName);
        // Function names drop the trailing "_t" so prototypes read
        // `pkg_name_read(...)` instead of `pkg_name_t_read(...)`.
        this.funcPrefix = this.typeName.substring(0, this.typeName.length() - 2);
        this.includeGuard = CIdentifierFormatter.includeGuard(pkg, zserioName);
        this.headerIncludePath = CIdentifierFormatter.headerIncludePath(pkg, zserioName);

        final List<FieldFormatter.FieldData> built = new ArrayList<>();
        boolean owned = false;
        for (final Field f : structType.getFields())
        {
            final FieldFormatter.FieldData data = FieldFormatter.format(f);
            built.add(data);
            owned = owned || data.getOwned();
        }
        this.fields = built;
        this.hasOwnedFields = owned;
    }

    public String  getGeneratorDescription() { return generatorDescription; }
    public String  getTypeName()             { return typeName; }
    public String  getFuncPrefix()           { return funcPrefix; }
    public String  getIncludeGuard()         { return includeGuard; }
    public String  getHeaderIncludePath()    { return headerIncludePath; }
    public List<FieldFormatter.FieldData> getFields() { return fields; }
    public boolean getHasOwnedFields()       { return hasOwnedFields; }
}
