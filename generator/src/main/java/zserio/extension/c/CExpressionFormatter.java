/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Fabian Klebert
 */

package zserio.extension.c;

import java.math.BigInteger;

import zserio.ast.AstNode;
import zserio.ast.Expression;
import zserio.ast.Field;
import zserio.ast.Parameter;
import zserio.extension.common.ZserioExtensionException;

/**
 * Minimal expression-to-C formatter for the limited expression
 * categories that Phase 2 batch 6 actually needs:
 *
 * <ul>
 *   <li>{@link #formatRefOrLiteral} — a single identifier referring
 *       to a {@link Parameter} or a {@link Field} (rendered as
 *       {@code <holder><name>}), or a compile-time integer / boolean
 *       literal.</li>
 *   <li>{@link #formatCaseLabel} — a compile-time integer expression
 *       used as a {@code case} label (rendered as {@code <value>u}).</li>
 * </ul>
 *
 * Anything else — arithmetic, function calls, struct-of-struct access
 * paths — raises {@link ZserioExtensionException}. A fuller
 * {@code CExpressionFormattingPolicy} (mirroring upstream's
 * CppExpressionFormattingPolicy with operator-precedence-aware output)
 * will replace this once we need it for constraints / array lengths /
 * dynamic-bit fields.
 */
public final class CExpressionFormatter
{
    private CExpressionFormatter()
    {
        // utility class
    }

    /**
     * Format an identifier reference (parameter / field) or a literal.
     * For an identifier, the result is {@code <holderExpr><name>}.
     */
    public static String formatRefOrLiteral(Expression expr, String holderExpr)
            throws ZserioExtensionException
    {
        final AstNode sym = expr.getExprSymbolObject();
        if (sym instanceof Parameter)
        {
            return holderExpr + CIdentifierFormatter.toLowerSnake(((Parameter)sym).getName());
        }
        if (sym instanceof Field)
        {
            return holderExpr + CIdentifierFormatter.toLowerSnake(((Field)sym).getName());
        }
        final BigInteger iv = expr.getIntegerValue();
        if (iv != null)
        {
            return iv.toString();
        }
        if (expr.getExprType() == Expression.ExpressionType.BOOLEAN)
        {
            // Boolean literals come through as "true" / "false" raw text.
            final String text = expr.getText();
            if ("true".equals(text) || "false".equals(text))
            {
                return text;
            }
        }
        throw new ZserioExtensionException(
                "zserio-c: expression '" + expr.getText() + "' is too complex for the v1 formatter; "
                        + "only single identifier refs and integer/boolean literals are supported here");
    }

    /**
     * Format a Choice case-label expression. Must evaluate to a
     * compile-time integer.
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
