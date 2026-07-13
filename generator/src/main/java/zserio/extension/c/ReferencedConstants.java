/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Klebert Engineering
 */

package zserio.extension.c;

import java.util.LinkedHashSet;
import java.util.Set;

import zserio.ast.ArrayInstantiation;
import zserio.ast.BitmaskType;
import zserio.ast.ChoiceCase;
import zserio.ast.ChoiceCaseExpression;
import zserio.ast.ChoiceDefault;
import zserio.ast.ChoiceType;
import zserio.ast.CompoundType;
import zserio.ast.Constant;
import zserio.ast.DynamicBitFieldInstantiation;
import zserio.ast.EnumType;
import zserio.ast.Expression;
import zserio.ast.Field;
import zserio.ast.Function;
import zserio.ast.ParameterizedTypeInstantiation;
import zserio.ast.TypeInstantiation;

/**
 * Discovers the header paths of every {@code const} that the
 * compound's lowered code will reference at the C level.
 *
 * Constants that appear in expressions (constraints, optional
 * clauses, choice selectors, parameterised-type arguments, array
 * lengths, dynamic-bit-field widths, function bodies) get lowered to
 * a {@code <pkg>_<NAME>} macro defined in a per-const header. The
 * compound's translation unit must {@code #include} that header so
 * the macro is in scope; without it the C compiler reports
 * {@code <name> undeclared}.
 *
 * The upstream {@link Expression#getReferencedSymbolObjects} method
 * recursively collects symbols referenced by an expression; this
 * helper just orchestrates which expressions a compound exposes.
 */
final class ReferencedConstants
{
    private ReferencedConstants()
    {
        // utility class
    }

    /** All include paths needed for constants referenced by {@code compound}. */
    static Set<String> headerIncludePathsFor(CompoundType compound)
    {
        final Set<String> out = new LinkedHashSet<>();
        for (final Field f : compound.getFields())
        {
            collectFromField(f, out);
        }
        for (final Function fn : compound.getFunctions())
        {
            collectFromExpression(fn.getResultExpression(), out);
        }
        if (compound instanceof ChoiceType)
        {
            final ChoiceType ct = (ChoiceType)compound;
            collectFromExpression(ct.getSelectorExpression(), out);
            for (final ChoiceCase cc : ct.getChoiceCases())
            {
                for (final ChoiceCaseExpression cce : cc.getExpressions())
                {
                    collectFromExpression(cce.getExpression(), out);
                }
                if (cc.getField() != null)
                {
                    collectFromField(cc.getField(), out);
                }
            }
            final ChoiceDefault def = ct.getChoiceDefault();
            if (def != null && def.getField() != null)
            {
                collectFromField(def.getField(), out);
            }
        }
        return out;
    }

    private static void collectFromField(Field f, Set<String> out)
    {
        collectFromExpression(f.getConstraintExpr(), out);
        collectFromExpression(f.getOptionalClauseExpr(), out);
        collectFromExpression(f.getInitializerExpr(), out);
        collectFromExpression(f.getAlignmentExpr(), out);
        collectFromExpression(f.getOffsetExpr(), out);
        collectFromInstantiation(f.getTypeInstantiation(), out);
    }

    private static void collectFromInstantiation(TypeInstantiation typeInst, Set<String> out)
    {
        if (typeInst instanceof ArrayInstantiation)
        {
            final ArrayInstantiation ai = (ArrayInstantiation)typeInst;
            collectFromExpression(ai.getLengthExpression(), out);
            collectFromInstantiation(ai.getElementTypeInstantiation(), out);
            return;
        }
        if (typeInst instanceof DynamicBitFieldInstantiation)
        {
            final DynamicBitFieldInstantiation dyn = (DynamicBitFieldInstantiation)typeInst;
            collectFromExpression(dyn.getLengthExpression(), out);
            return;
        }
        if (typeInst instanceof ParameterizedTypeInstantiation)
        {
            for (final Expression arg :
                    ((ParameterizedTypeInstantiation)typeInst).getTypeArguments())
            {
                collectFromExpression(arg, out);
            }
        }
    }

    private static void collectFromExpression(Expression expr, Set<String> out)
    {
        if (expr == null)
        {
            return;
        }
        for (final Constant c : expr.getReferencedSymbolObjects(Constant.class))
        {
            out.add(CIdentifierFormatter.headerIncludePath(
                    c.getPackage().getPackageName(), c.getName()));
        }
        // Enum literals like `E.BLACK` lower to a `<pkg>_<ENUM>_<ITEM>`
        // static const in the enum's own header. The compound's
        // translation unit needs the header in scope for any expression
        // that mentions the enum value. After template substitution the
        // referenced EnumType may live in any package, so collect every
        // referenced enum/bitmask explicitly.
        for (final EnumType e : expr.getReferencedSymbolObjects(EnumType.class))
        {
            out.add(CIdentifierFormatter.headerIncludePath(
                    e.getPackage().getPackageName(), e.getName()));
        }
        for (final BitmaskType b : expr.getReferencedSymbolObjects(BitmaskType.class))
        {
            out.add(CIdentifierFormatter.headerIncludePath(
                    b.getPackage().getPackageName(), b.getName()));
        }
    }
}
