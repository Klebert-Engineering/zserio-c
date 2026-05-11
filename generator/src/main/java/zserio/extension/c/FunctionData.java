/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Fabian Klebert
 */

package zserio.extension.c;

import java.util.ArrayList;
import java.util.List;

import zserio.ast.BitmaskType;
import zserio.ast.CompoundType;
import zserio.ast.EnumType;
import zserio.ast.Function;
import zserio.ast.InstantiateType;
import zserio.ast.PackageName;
import zserio.ast.Subtype;
import zserio.ast.TypeReference;
import zserio.ast.ZserioType;
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
    private final String extraInclude;  // header of the return type, or null

    public FunctionData(String name, String fullName, String returnType, String body,
            String extraInclude)
    {
        this.name = name;
        this.fullName = fullName;
        this.returnType = returnType;
        this.body = body;
        this.extraInclude = extraInclude;
    }

    public String getName()         { return name; }
    public String getFullName()     { return fullName; }
    public String getReturnType()   { return returnType; }
    public String getBody()         { return body; }

    /**
     * Header that defines {@link #getReturnType()}, or {@code null}
     * for built-in / runtime-supplied types. Functions that return a
     * cross-package enum / bitmask / compound need this added to the
     * owner's include set so the generated header references the
     * declared typedef.
     */
    public String getExtraInclude() { return extraInclude; }

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
            //
            // For a Choice / Union, case-field data lives inside an
            // anonymous member union at `self->u.<name>`; parameters
            // and non-case state stay at `self->`. The expression
            // policy's `parentHolder()` strips the trailing `u.` for
            // parameter / sibling-state references, so we pass
            // `"self->u."` here and let the policy peel it back where
            // needed.
            final String holderExpr =
                    (owner instanceof zserio.ast.ChoiceType
                            || owner instanceof zserio.ast.UnionType)
                            ? "self->u." : "self->";
            final String body = CExpressionFormatter.format(
                    f.getResultExpression(), holderExpr, owner);
            final String inc = returnTypeInclude(f.getReturnTypeReference());
            out.add(new FunctionData(localName, fullName, returnType, body, inc));
        }
        return out;
    }

    /**
     * Header of the underlying user-defined type referenced by
     * {@code ref}, or {@code null} for built-ins (the same logic as
     * {@code ParameterData.extraIncludeFor} — kept local to avoid
     * coupling the two value classes).
     */
    private static String returnTypeInclude(TypeReference ref)
    {
        ZserioType resolved = ref.getBaseTypeReference().getType();
        while (resolved instanceof Subtype || resolved instanceof InstantiateType)
        {
            if (resolved instanceof Subtype)
            {
                resolved = ((Subtype)resolved).getTypeReference()
                        .getBaseTypeReference().getType();
            }
            else
            {
                resolved = ((InstantiateType)resolved).getTypeReference()
                        .getBaseTypeReference().getType();
            }
        }
        final PackageName pkg;
        final String name;
        if (resolved instanceof CompoundType)
        {
            final CompoundType ct = (CompoundType)resolved;
            pkg = ct.getPackage().getPackageName();
            name = ct.getName();
        }
        else if (resolved instanceof EnumType)
        {
            final EnumType et = (EnumType)resolved;
            pkg = et.getPackage().getPackageName();
            name = et.getName();
        }
        else if (resolved instanceof BitmaskType)
        {
            final BitmaskType bt = (BitmaskType)resolved;
            pkg = bt.getPackage().getPackageName();
            name = bt.getName();
        }
        else
        {
            return null;
        }
        return CIdentifierFormatter.headerIncludePath(pkg, name);
    }
}
