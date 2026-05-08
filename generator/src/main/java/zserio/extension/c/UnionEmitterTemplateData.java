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
import zserio.ast.UnionType;
import zserio.extension.common.ZserioExtensionException;

/** FreeMarker data for {@code Union.h.ftl} and {@code Union.c.ftl}. */
public final class UnionEmitterTemplateData
{
    private final String generatorDescription;
    private final String typeName;
    private final String funcPrefix;
    private final String includeGuard;
    private final String headerIncludePath;
    private final List<CaseData> cases;
    private final List<String> extraIncludes;
    private final boolean hasOwnedCases;

    public UnionEmitterTemplateData(TemplateDataContext context, UnionType unionType)
            throws ZserioExtensionException
    {
        this.generatorDescription = context.getGeneratorDescription();
        final PackageName pkg = unionType.getPackage().getPackageName();
        final String zserioName = unionType.getName();
        if (!unionType.getTypeParameters().isEmpty())
        {
            throw new ZserioExtensionException(
                    "zserio-c: parameterised unions are not yet supported (union '"
                            + zserioName + "')");
        }
        this.typeName = CIdentifierFormatter.typeName(pkg, zserioName);
        this.funcPrefix = CIdentifierFormatter.funcPrefix(pkg, zserioName);
        this.includeGuard = CIdentifierFormatter.includeGuard(pkg, zserioName);
        this.headerIncludePath = CIdentifierFormatter.headerIncludePath(pkg, zserioName);

        final String tagBase = CIdentifierFormatter.constantName(pkg, zserioName) + "_TAG";

        final List<CaseData> built = new ArrayList<>();
        final LinkedHashSet<String> includes = new LinkedHashSet<>();
        boolean owned = false;
        int index = 0;
        for (final Field f : unionType.getFields())
        {
            // Each case lives inside an anonymous member union named
            // `u`, so the field statements use `self->u.<name>`.
            final FieldFormatter.FieldData field = FieldFormatter.format(f, "self->u.");
            final String tagSuffix = CIdentifierFormatter.toUpperSnake(f.getName());
            built.add(new CaseData(tagBase + "_" + tagSuffix, index + "u", field));
            if (field.getExtraInclude() != null)
            {
                includes.add(field.getExtraInclude());
            }
            owned = owned || field.getOwned();
            ++index;
        }
        this.cases = built;
        this.extraIncludes = new ArrayList<>(includes);
        this.hasOwnedCases = owned;
    }

    public String  getGeneratorDescription() { return generatorDescription; }
    public String  getTypeName()             { return typeName; }
    public String  getFuncPrefix()           { return funcPrefix; }
    public String  getIncludeGuard()         { return includeGuard; }
    public String  getHeaderIncludePath()    { return headerIncludePath; }
    public List<CaseData> getCases()         { return cases; }
    public List<String>   getExtraIncludes() { return extraIncludes; }
    public boolean getHasOwnedCases()        { return hasOwnedCases; }

    /** One union case: tag macro / numeric value / lowered field. */
    public static final class CaseData
    {
        private final String tagMacro;
        private final String tagValue;
        private final FieldFormatter.FieldData field;

        CaseData(String tagMacro, String tagValue, FieldFormatter.FieldData field)
        {
            this.tagMacro = tagMacro;
            this.tagValue = tagValue;
            this.field = field;
        }

        public String  getTagMacro() { return tagMacro; }
        public String  getTagValue() { return tagValue; }
        public FieldFormatter.FieldData getField() { return field; }
    }
}
