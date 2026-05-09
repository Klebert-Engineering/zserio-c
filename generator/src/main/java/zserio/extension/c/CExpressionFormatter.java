/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Fabian Klebert
 */

package zserio.extension.c;

import java.math.BigInteger;

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
        final CExpressionFormattingPolicy policy = new CExpressionFormattingPolicy(holderExpr);
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
     * Format a Choice case-label expression. Must evaluate to a
     * compile-time integer; the C result is suffixed with {@code u}
     * so it matches the unsigned switch discriminant.
     */
    public static String formatCaseLabel(Expression expr) throws ZserioExtensionException
    {
        final BigInteger iv = expr.getIntegerValue();
        if (iv == null)
        {
            throw new ZserioExtensionException(
                    "zserio-c: choice case label '" + expr.getText()
                            + "' does not have a compile-time integer value");
        }
        return iv.toString() + "u";
    }
}
