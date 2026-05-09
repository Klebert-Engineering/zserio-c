/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Fabian Klebert
 */

package zserio.extension.c;

import java.util.ArrayList;
import java.util.LinkedHashSet;
import java.util.List;

import zserio.ast.ChoiceCase;
import zserio.ast.ChoiceCaseExpression;
import zserio.ast.ChoiceDefault;
import zserio.ast.ChoiceType;
import zserio.ast.Field;
import zserio.ast.PackageName;
import zserio.extension.common.ZserioExtensionException;

/** FreeMarker data for {@code Choice.h.ftl} and {@code Choice.c.ftl}. */
public final class ChoiceEmitterTemplateData
{
    private final String generatorDescription;
    private final String typeName;
    private final String funcPrefix;
    private final String includeGuard;
    private final String headerIncludePath;
    private final List<ParameterData> parameters;
    private final String selectorExpr;
    private final List<CaseData> cases;
    private final FieldFormatter.FieldData defaultField; // null if no default
    private final boolean hasDefault;
    private final List<FunctionData> functions;
    private final List<String> extraIncludes;
    private final boolean hasOwnedCases;

    public ChoiceEmitterTemplateData(TemplateDataContext context, ChoiceType choiceType)
            throws ZserioExtensionException
    {
        this.generatorDescription = context.getGeneratorDescription();
        final PackageName pkg = choiceType.getPackage().getPackageName();
        final String zserioName = choiceType.getName();
        this.typeName = CIdentifierFormatter.typeName(pkg, zserioName);
        this.funcPrefix = CIdentifierFormatter.funcPrefix(pkg, zserioName);
        this.includeGuard = CIdentifierFormatter.includeGuard(pkg, zserioName);
        this.headerIncludePath = CIdentifierFormatter.headerIncludePath(pkg, zserioName);

        if (choiceType.getTypeParameters().isEmpty())
        {
            // Upstream's grammar requires a selector and selectors
            // resolve through parameters in practice, so an empty
            // parameter list would mean the choice is unreachable. Be
            // strict rather than silently skipping.
            throw new ZserioExtensionException(
                    "zserio-c: choice '" + zserioName + "' has no parameters; selector cannot resolve");
        }
        this.parameters = ParameterData.from(choiceType.getTypeParameters());

        // The selector resolves against the choice's own state —
        // typically a parameter ref but the formatter accepts any
        // expression now. The owner is the choice itself so any
        // function calls in the selector resolve to this compound.
        this.selectorExpr =
                CExpressionFormatter.format(
                        choiceType.getSelectorExpression(), "self->", choiceType);

        final List<CaseData> built = new ArrayList<>();
        final LinkedHashSet<String> includes = new LinkedHashSet<>();
        boolean owned = false;
        for (final ChoiceCase c : choiceType.getChoiceCases())
        {
            final List<String> labels = new ArrayList<>();
            for (final ChoiceCaseExpression e : c.getExpressions())
            {
                labels.add(CExpressionFormatter.formatCaseLabel(e.getExpression()));
            }
            FieldFormatter.FieldData fd = null;
            if (c.getField() != null)
            {
                final Field caseField = c.getField();
                if (caseField.isOptional())
                {
                    throw new ZserioExtensionException(
                            "zserio-c: choice '" + zserioName + "' has optional case field '"
                                    + caseField.getName() + "'; not supported in v1");
                }
                fd = FieldFormatter.format(caseField, "self->u.", choiceType);
                if (fd.getExtraInclude() != null)
                {
                    includes.add(fd.getExtraInclude());
                }
                owned = owned || fd.getOwned();
            }
            built.add(new CaseData(labels, fd));
        }
        this.cases = built;

        // Default case: optional. Empty default (no field) just means
        // "any unmatched selector value is legal but contributes
        // nothing to the wire" — the read/write/free dispatch turns
        // the unmatched arm into a no-op instead of returning
        // ZSERIO_ERR_INVALID_TAG.
        final ChoiceDefault def = choiceType.getChoiceDefault();
        if (def != null)
        {
            this.hasDefault = true;
            if (def.getField() != null)
            {
                if (def.getField().isOptional())
                {
                    throw new ZserioExtensionException(
                            "zserio-c: choice '" + zserioName + "' has optional default field '"
                                    + def.getField().getName() + "'; not supported in v1");
                }
                this.defaultField = FieldFormatter.format(def.getField(), "self->u.", choiceType);
                if (this.defaultField.getExtraInclude() != null)
                {
                    includes.add(this.defaultField.getExtraInclude());
                }
                owned = owned || this.defaultField.getOwned();
            }
            else
            {
                this.defaultField = null;
            }
        }
        else
        {
            this.hasDefault = false;
            this.defaultField = null;
        }

        this.functions = FunctionData.from(choiceType);
        this.extraIncludes = new ArrayList<>(includes);
        this.hasOwnedCases = owned;
    }

    public String  getGeneratorDescription() { return generatorDescription; }
    public String  getTypeName()             { return typeName; }
    public String  getFuncPrefix()           { return funcPrefix; }
    public String  getIncludeGuard()         { return includeGuard; }
    public String  getHeaderIncludePath()    { return headerIncludePath; }
    public List<ParameterData> getParameters() { return parameters; }
    public String  getSelectorExpr()         { return selectorExpr; }
    public List<CaseData> getCases()         { return cases; }
    public FieldFormatter.FieldData getDefaultField() { return defaultField; }
    public boolean getHasDefault()           { return hasDefault; }
    public boolean getHasDefaultField()      { return defaultField != null; }
    public List<FunctionData> getFunctions() { return functions; }
    public boolean getHasFunctions()         { return !functions.isEmpty(); }
    public List<String>   getExtraIncludes() { return extraIncludes; }
    public boolean getHasOwnedCases()        { return hasOwnedCases; }

    /** One choice case: case labels + the lowered field (null for empty case). */
    public static final class CaseData
    {
        private final List<String> labels;
        private final FieldFormatter.FieldData field;

        CaseData(List<String> labels, FieldFormatter.FieldData field)
        {
            this.labels = labels;
            this.field = field;
        }

        public List<String> getLabels() { return labels; }
        public FieldFormatter.FieldData getField() { return field; }
        public boolean getHasField() { return field != null; }
    }
}
