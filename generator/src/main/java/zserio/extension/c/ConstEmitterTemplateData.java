/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Fabian Klebert
 */

package zserio.extension.c;

import zserio.ast.Constant;
import zserio.ast.PackageName;
import zserio.extension.common.ZserioExtensionException;

/**
 * FreeMarker data model for {@link ConstEmitter}'s {@code Constant.h.ftl}.
 *
 * All fields are exposed via getters so the FreeMarker template can
 * reference them as {@code ${cName}}, {@code ${nativeType}}, etc.
 */
public final class ConstEmitterTemplateData
{
    // Field names start with a lowercase letter followed by a lowercase
    // letter so that JavaBean decapitalisation produces the expected
    // FreeMarker property name. (`getCName()` would expose as `CName`
    // because both first chars are uppercase — JavaBean spec preserves
    // the case in that pattern. Use `name` to avoid the trap.)
    private final String generatorDescription;
    private final String name;
    private final String nativeType;
    private final String includeGuard;
    private final String valueExpression;

    public ConstEmitterTemplateData(TemplateDataContext context, Constant constant)
            throws ZserioExtensionException
    {
        this.generatorDescription = context.getGeneratorDescription();

        final PackageName pkg = constant.getPackage().getPackageName();
        final String zserioName = constant.getName();
        this.name = CIdentifierFormatter.constantName(pkg, zserioName);
        this.includeGuard = CIdentifierFormatter.includeGuard(pkg, zserioName);
        this.nativeType = CNativeTypeMapper.nativeType(constant.getTypeInstantiation());

        // Phase 2 batch 1: use the literal text from the AST. A future
        // expression formatter will rewrite identifier references and
        // typed-suffix integer literals; for now we wrap the value in
        // an explicit cast to the native type, which makes it
        // type-correct regardless of the literal's original form.
        this.valueExpression = constant.getValueExpression().getText();
    }

    public String getGeneratorDescription() { return generatorDescription; }
    public String getName()                 { return name; }
    public String getNativeType()           { return nativeType; }
    public String getIncludeGuard()         { return includeGuard; }
    public String getValueExpression()      { return valueExpression; }
}
