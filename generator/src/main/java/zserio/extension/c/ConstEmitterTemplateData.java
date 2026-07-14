/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Klebert Engineering
 */

package zserio.extension.c;

import zserio.ast.Constant;
import zserio.ast.PackageName;
import zserio.ast.StringType;
import zserio.ast.TypeInstantiation;
import zserio.ast.ZserioType;
import zserio.extension.common.ZserioExtensionException;

/**
 * FreeMarker data model for {@link ConstEmitter}'s {@code Constant.h.ftl}.
 *
 * The {@code declaration} field carries the full {@code static const …}
 * line so the template doesn't need to branch on scalar vs. string
 * shape — the difference is captured here.
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
    private final String includeGuard;
    private final String declaration;

    public ConstEmitterTemplateData(TemplateDataContext context, Constant constant)
            throws ZserioExtensionException
    {
        this.generatorDescription = context.getGeneratorDescription();

        final PackageName pkg = constant.getPackage().getPackageName();
        final String zserioName = constant.getName();
        this.name = CIdentifierFormatter.constantName(pkg, zserioName);
        this.includeGuard = CIdentifierFormatter.includeGuard(pkg, zserioName);

        // The const's value expression is formatted against an empty
        // holder — a top-level const has no `self`. Other constant
        // refs in the RHS resolve through the policy's Constant
        // branch (package-prefixed C macro name).
        final String formattedValue =
                CExpressionFormatter.format(constant.getValueExpression(), "");

        final TypeInstantiation ti = constant.getTypeInstantiation();
        final ZserioType base = ti.getTypeReference().getBaseTypeReference().getType();
        if (base instanceof StringType)
        {
            // String const: `static const char NAME[] = "value";` so
            // the symbol has linkage (callers can take its address)
            // rather than the macro form.
            this.declaration = "static const char " + this.name + "[] = "
                    + formattedValue + ";";
        }
        else
        {
            final String nativeType = CNativeTypeMapper.nativeType(ti);
            // The cast keeps the assignment well-typed even when the
            // RHS is a wider literal (e.g. `const uint8 X = 150` —
            // 150 is `int` in C without the cast).
            this.declaration = "static const " + nativeType + " " + this.name
                    + " = (" + nativeType + ")" + formattedValue + ";";
        }
    }

    public String getGeneratorDescription() { return generatorDescription; }
    public String getName()                 { return name; }
    public String getIncludeGuard()         { return includeGuard; }
    public String getDeclaration()          { return declaration; }
}
