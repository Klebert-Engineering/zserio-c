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
    private final List<ParameterData> parameters;
    private final List<CaseData> cases;
    private final boolean hasPackingContext;
    private final List<FunctionData> functions;
    private final List<String> extraIncludes;
    private final boolean hasOwnedCases;

    public UnionEmitterTemplateData(TemplateDataContext context, UnionType unionType)
            throws ZserioExtensionException
    {
        this.generatorDescription = context.getGeneratorDescription();
        final PackageName pkg = unionType.getPackage().getPackageName();
        final String zserioName = unionType.getName();
        this.typeName = CIdentifierFormatter.typeName(pkg, zserioName);
        this.funcPrefix = CIdentifierFormatter.funcPrefix(pkg, zserioName);
        this.includeGuard = CIdentifierFormatter.includeGuard(pkg, zserioName);
        this.headerIncludePath = CIdentifierFormatter.headerIncludePath(pkg, zserioName);
        this.parameters = ParameterData.from(unionType.getTypeParameters());

        final String tagBase = CIdentifierFormatter.constantName(pkg, zserioName) + "_TAG";

        final List<CaseData> built = new ArrayList<>();
        final LinkedHashSet<String> includes = new LinkedHashSet<>();
        // Pick up parameter-type headers (parameter members + the
        // _set_parameters helper signature reference these).
        for (final ParameterData p : this.parameters)
        {
            if (p.getExtraInclude() != null)
            {
                includes.add(p.getExtraInclude());
            }
        }
        boolean owned = false;
        // Unions ALWAYS emit packing helpers: even when no arm is
        // packable, the varsize tag itself is delta-encoded by
        // upstream's `packed Union[]` driver. The tag context is
        // pre-allocated as the first context member.
        boolean usesContext = true;
        int index = 0;
        for (final Field f : unionType.getFields())
        {
            if (f.isOptional())
            {
                throw new ZserioExtensionException(
                        "zserio-c: union '" + zserioName + "' has optional case '"
                                + f.getName() + "'; not supported in v1");
            }
            // Each case lives inside an anonymous member union named
            // `u`, so the field statements use `self->u.<name>`.
            final FieldFormatter.FieldData field = FieldFormatter.format(f, "self->u.", unionType);
            // Per-arm packed lowering mirrors Choice: each tag's
            // case feeds its field through the packing machinery so a
            // `packed Union[]` parent can route through this union's
            // _init_packing_context / _read_packed / _write_packed
            // helpers, dispatching by tag at every step.
            final PackingFieldFormatter.PackedField packed =
                    PackingFieldFormatter.format(f, field, "self->u.", unionType, "ctx->");
            usesContext = usesContext || packed.getUsesContext();
            final String tagSuffix = CIdentifierFormatter.toUpperSnake(f.getName());
            built.add(new CaseData(tagBase + "_" + tagSuffix, index + "u", field, packed));
            if (field.getExtraInclude() != null)
            {
                includes.add(field.getExtraInclude());
            }
            owned = owned || field.getOwned();
            ++index;
        }
        this.cases = built;
        this.hasPackingContext = usesContext;
        this.functions = FunctionData.from(unionType);
        for (final FunctionData fn : this.functions)
        {
            if (fn.getExtraInclude() != null)
            {
                includes.add(fn.getExtraInclude());
            }
        }
        includes.addAll(ReferencedConstants.headerIncludePathsFor(unionType));
        this.extraIncludes = new ArrayList<>(includes);
        this.hasOwnedCases = owned;
    }

    public String  getGeneratorDescription() { return generatorDescription; }
    public String  getTypeName()             { return typeName; }
    public String  getFuncPrefix()           { return funcPrefix; }
    public String  getIncludeGuard()         { return includeGuard; }
    public String  getHeaderIncludePath()    { return headerIncludePath; }
    public List<ParameterData> getParameters() { return parameters; }
    public boolean getHasParameters()        { return !parameters.isEmpty(); }
    public List<CaseData> getCases()         { return cases; }
    public boolean getHasPackingContext()    { return hasPackingContext; }
    public List<FunctionData> getFunctions() { return functions; }
    public boolean getHasFunctions()         { return !functions.isEmpty(); }
    public List<String>   getExtraIncludes() { return extraIncludes; }
    public boolean getHasOwnedCases()        { return hasOwnedCases; }

    /** One union case: tag macro / numeric value / lowered field / packed lowering. */
    public static final class CaseData
    {
        private final String tagMacro;
        private final String tagValue;
        private final FieldFormatter.FieldData field;
        private final PackingFieldFormatter.PackedField packed;

        CaseData(String tagMacro, String tagValue, FieldFormatter.FieldData field,
                PackingFieldFormatter.PackedField packed)
        {
            this.tagMacro = tagMacro;
            this.tagValue = tagValue;
            this.field = field;
            this.packed = packed;
        }

        public String  getTagMacro() { return tagMacro; }
        public String  getTagValue() { return tagValue; }
        public FieldFormatter.FieldData getField() { return field; }
        public PackingFieldFormatter.PackedField getPacked() { return packed; }
    }
}
