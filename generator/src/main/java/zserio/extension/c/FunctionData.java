/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Fabian Klebert
 */

package zserio.extension.c;

import java.util.ArrayList;
import java.util.List;

import zserio.ast.CompoundType;
import zserio.ast.Function;
import zserio.ast.PackageName;
import zserio.extension.common.ZserioExtensionException;

/**
 * Lowering of one user-defined zserio {@link Function} to C.
 *
 * Each function becomes a single-statement C inline:
 *
 * <pre>
 * static inline &lt;returnType&gt; &lt;prefix&gt;_&lt;name&gt;(const &lt;prefix&gt;_t *self)
 * {
 *     return (&lt;returnType&gt;)(&lt;formatted body&gt;);
 * }
 * </pre>
 *
 * The body's expression is formatted against {@code "self->"} so
 * field / parameter references inside the function resolve to
 * accesses on the parameter.
 */
public final class FunctionData
{
    private final String name;          // C function name minus the prefix, lower_snake
    private final String fullName;      // <prefix>_<name>
    private final String returnType;
    private final String body;          // formatted result expression

    public FunctionData(String name, String fullName, String returnType, String body)
    {
        this.name = name;
        this.fullName = fullName;
        this.returnType = returnType;
        this.body = body;
    }

    public String getName()       { return name; }
    public String getFullName()   { return fullName; }
    public String getReturnType() { return returnType; }
    public String getBody()       { return body; }

    /**
     * Build {@link FunctionData} entries for every function defined
     * on {@code owner}. The owner provides the package + entity name
     * that prefixes each generated C function.
     */
    public static List<FunctionData> from(CompoundType owner) throws ZserioExtensionException
    {
        final PackageName pkg = owner.getPackage().getPackageName();
        final String prefix = CIdentifierFormatter.funcPrefix(pkg, owner.getName());
        final List<FunctionData> out = new ArrayList<>();
        for (final Function f : owner.getFunctions())
        {
            final String localName = CIdentifierFormatter.toLowerSnake(f.getName());
            final String fullName = prefix + "_" + localName;
            final String returnType = CNativeTypeMapper.nativeType(f.getReturnTypeReference());
            // The result expression sees `self` as the implicit
            // receiver; a parameter / field reference there lowers to
            // `self->...`. The owner is also passed so any same-
            // compound function call inside this function's body
            // (`area() * 2` referencing another function on the same
            // compound) resolves its owner via the policy's
            // currentOwner fallback.
            final String body = CExpressionFormatter.format(
                    f.getResultExpression(), "self->", owner);
            out.add(new FunctionData(localName, fullName, returnType, body));
        }
        return out;
    }
}
