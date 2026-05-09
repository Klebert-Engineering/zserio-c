/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Fabian Klebert
 */

package zserio.extension.c;

import java.math.BigInteger;

import zserio.ast.CompoundType;
import zserio.ast.Expression;
import zserio.extension.common.ExpressionFormatter;
import zserio.extension.common.ZserioExtensionException;

/**
 * Thin facade over the upstream {@link ExpressionFormatter} +
 * {@link CExpressionFormattingPolicy} pair.
 *
 * Two entry points cover what generated C99 needs today:
 *
 * <ul>
 *   <li>{@link #format} — full expression lowering against a holder
 *       expression. Used for choice selectors, parameter argument
 *       expressions, expression-length array bounds, and (in upcoming
 *       batches) constraints + {@code if}-optional clauses.</li>
 *   <li>{@link #formatCaseLabel} — Choice case labels need a C
 *       <em>integer constant expression</em>; static-const enum
 *       members don't qualify in C99, so the value is taken from the
 *       AST's compile-time integer evaluation directly.</li>
 * </ul>
 */
public final class CExpressionFormatter
{
    private CExpressionFormatter()
    {
        // utility class
    }

    /**
     * Lower {@code expr} to C99 source. Identifier references resolve
     * against the supplied holder ({@code "self->"} for top-level
     * compound bodies, occasionally something else for nested
     * scopes).
     */
    public static String format(Expression expr, String holderExpr)
            throws ZserioExtensionException
    {
        return format(expr, holderExpr, null);
    }

    /**
     * Same as {@link #format(Expression, String)} but with the
     * compound that contains the expression. Used when same-compound
     * function calls need to find their owning compound for the
     * generated C function-name prefix.
     */
    public static String format(Expression expr, String holderExpr, CompoundType currentOwner)
            throws ZserioExtensionException
    {
        final CExpressionFormattingPolicy policy =
                new CExpressionFormattingPolicy(holderExpr, currentOwner);
        return new ExpressionFormatter(policy).formatGetter(expr);
    }

    /**
     * Convenience for the existing call sites that lower a single
     * identifier reference or literal — kept stable while the
     * surrounding code migrates to {@link #format}.
     */
    public static String formatRefOrLiteral(Expression expr, String holderExpr)
            throws ZserioExtensionException
    {
        return format(expr, holderExpr);
    }

    /**
     * Format a Choice case-label expression. The expression must
     * evaluate to a compile-time scalar — typically an integer or
     * a boolean. Boolean selectors lower to {@code 0u} / {@code 1u}
     * so the case label fits the C switch shape uniformly.
     */
    public static String formatCaseLabel(Expression expr) throws ZserioExtensionException
    {
        final BigInteger iv = expr.getIntegerValue();
        if (iv != null)
        {
            return iv.toString() + "u";
        }
        // Boolean choice selectors: zserio writes `case true:` and
        // `case false:`; the C selector reads the discriminant as
        // 0 / 1 so we map the labels accordingly.
        if (expr.getExprType() == Expression.ExpressionType.BOOLEAN)
        {
            final String text = expr.getText();
            if ("true".equals(text))
            {
                return "1u";
            }
            if ("false".equals(text))
            {
                return "0u";
            }
        }
        throw new ZserioExtensionException(
                "zserio-c: choice case label '" + expr.getText()
                        + "' does not evaluate to a compile-time integer or boolean");
    }
}
