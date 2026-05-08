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
    private final String generatorDescription;
    private final String cName;
    private final String nativeType;
    private final String includeGuard;
    private final String valueExpression;

    public ConstEmitterTemplateData(TemplateDataContext context, Constant constant)
            throws ZserioExtensionException
    {
        this.generatorDescription = context.getGeneratorDescription();

        final PackageName pkg = constant.getPackage().getPackageName();
        final String name = constant.getName();
        this.cName = CIdentifierFormatter.constantName(pkg, name);
        this.includeGuard = CIdentifierFormatter.includeGuard(pkg, name);
        this.nativeType = CNativeTypeMapper.nativeType(constant.getTypeInstantiation());

        // Phase 2 batch 1: use the literal text from the AST. A future
        // expression formatter will rewrite identifier references and
        // typed-suffix integer literals; for now we wrap the value in
        // an explicit cast to the native type, which makes it
        // type-correct regardless of the literal's original form.
        this.valueExpression = constant.getValueExpression().getText();
    }

    public String getGeneratorDescription() { return generatorDescription; }
    public String getCName()                { return cName; }
    public String getNativeType()           { return nativeType; }
    public String getIncludeGuard()         { return includeGuard; }
    public String getValueExpression()      { return valueExpression; }
}
