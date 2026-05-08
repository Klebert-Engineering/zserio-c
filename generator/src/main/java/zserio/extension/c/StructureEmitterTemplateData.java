/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Fabian Klebert
 */

package zserio.extension.c;

import java.util.ArrayList;
import java.util.LinkedHashSet;
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
    private final List<String> extraIncludes;
    private final boolean hasOwnedFields;

    public StructureEmitterTemplateData(TemplateDataContext context, StructureType structType)
            throws ZserioExtensionException
    {
        this.generatorDescription = context.getGeneratorDescription();
        final PackageName pkg = structType.getPackage().getPackageName();
        final String zserioName = structType.getName();
        if (!structType.getTypeParameters().isEmpty())
        {
            throw new ZserioExtensionException(
                    "zserio-c: parameterised structs are not yet supported (struct '"
                            + zserioName + "')");
        }
        this.typeName = CIdentifierFormatter.typeName(pkg, zserioName);
        this.funcPrefix = CIdentifierFormatter.funcPrefix(pkg, zserioName);
        this.includeGuard = CIdentifierFormatter.includeGuard(pkg, zserioName);
        this.headerIncludePath = CIdentifierFormatter.headerIncludePath(pkg, zserioName);

        final List<FieldFormatter.FieldData> built = new ArrayList<>();
        // LinkedHashSet so the include order matches the schema's field
        // order — readable, and stable across re-runs.
        final LinkedHashSet<String> includes = new LinkedHashSet<>();
        boolean owned = false;
        for (final Field f : structType.getFields())
        {
            final FieldFormatter.FieldData data = FieldFormatter.format(f, "self->");
            built.add(data);
            if (data.getExtraInclude() != null)
            {
                includes.add(data.getExtraInclude());
            }
            owned = owned || data.getOwned();
        }
        this.fields = built;
        this.extraIncludes = new ArrayList<>(includes);
        this.hasOwnedFields = owned;
    }

    public String  getGeneratorDescription() { return generatorDescription; }
    public String  getTypeName()             { return typeName; }
    public String  getFuncPrefix()           { return funcPrefix; }
    public String  getIncludeGuard()         { return includeGuard; }
    public String  getHeaderIncludePath()    { return headerIncludePath; }
    public List<FieldFormatter.FieldData> getFields() { return fields; }
    public List<String> getExtraIncludes()   { return extraIncludes; }
    public boolean getHasOwnedFields()       { return hasOwnedFields; }
}
