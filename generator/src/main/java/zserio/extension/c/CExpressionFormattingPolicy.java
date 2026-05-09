/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Fabian Klebert
 */

package zserio.extension.c;

import java.math.BigInteger;

import zserio.ast.AstNode;
import zserio.ast.BitmaskType;
import zserio.ast.BitmaskValue;
import zserio.ast.Constant;
import zserio.ast.EnumItem;
import zserio.ast.EnumType;
import zserio.ast.Expression;
import zserio.ast.Field;
import zserio.ast.Package;
import zserio.ast.Parameter;
import zserio.ast.ZserioType;
import zserio.extension.common.DefaultExpressionFormattingPolicy;
import zserio.extension.common.ZserioExtensionException;

/**
 * C-flavored {@link DefaultExpressionFormattingPolicy}.
 *
 * Lowers zserio expressions (constraints, optional clauses, choice
 * selectors, parameter arguments, array length expressions) to C99
 * source. Most operators inherit the default policy's text — zserio
 * binary / unary / ternary operators use C-compatible glyphs almost
 * verbatim. Only a handful of methods need C-specific overrides:
 *
 * <ul>
 *   <li>{@link #getIdentifier} — emits {@code <holderExpr><name>} for
 *       a parameter / field reference at the leftmost end of a dot
 *       chain, the bare member name when nested inside a dot chain,
 *       and the package-prefixed constant macro for top-level
 *       {@code const} references;</li>
 *   <li>{@link #getBinaryLiteral} — C99 has no {@code 0b…} syntax, so
 *       binary literals get rewritten as hexadecimal;</li>
 *   <li>the unsupported categories (function calls, isset, lengthof,
 *       valueof, numbits, array element, implicit-array {@code @index})
 *       throw a clear error so we notice when a real schema needs
 *       them.</li>
 * </ul>
 *
 * The policy carries one input — the {@code holderExpr} — so the
 * caller (a struct read body, a choice's selector dispatch, an
 * expression-length array's count line) controls how a parameter or
 * field reference resolves into a C lvalue.
 */
public final class CExpressionFormattingPolicy extends DefaultExpressionFormattingPolicy
{
    private final String holderExpr;

    /**
     * @param holderExpr C expression that prefixes a leftmost
     *                   parameter / field reference. Typically
     *                   {@code "self->"}.
     */
    public CExpressionFormattingPolicy(String holderExpr)
    {
        this.holderExpr = holderExpr;
    }

    /* -------- atoms -------- */

    @Override
    public String getDecimalLiteral(Expression expr, boolean isNegative)
    {
        return expr.getText();
    }

    @Override
    public String getHexadecimalLiteral(Expression expr, boolean isNegative)
    {
        return expr.getText();
    }

    @Override
    public String getOctalLiteral(Expression expr, boolean isNegative)
    {
        // zserio octal "0NNN" is parsed the same way by C.
        return expr.getText();
    }

    @Override
    public String getBinaryLiteral(Expression expr, boolean isNegative)
            throws ZserioExtensionException
    {
        // Zserio binary literal is "<bits>b" / "<bits>B". C99 has no
        // 0b… syntax (that's C23+), so we rewrite as hexadecimal.
        final String text = expr.getText();
        if (text.length() < 2 || (text.charAt(text.length() - 1) != 'b'
                && text.charAt(text.length() - 1) != 'B'))
        {
            throw new ZserioExtensionException(
                    "zserio-c: binary literal '" + text + "' missing 'b' suffix");
        }
        final BigInteger value = new BigInteger(text.substring(0, text.length() - 1), 2);
        return "0x" + value.toString(16);
    }

    @Override
    public String getFloatLiteral(Expression expr, boolean isNegative)
    {
        return expr.getText();
    }

    @Override
    public String getDoubleLiteral(Expression expr, boolean isNegative)
    {
        return expr.getText();
    }

    @Override
    public String getBoolLiteral(Expression expr)
    {
        // "true" / "false" — valid C99 since we already include <stdbool.h>.
        return expr.getText();
    }

    @Override
    public String getStringLiteral(Expression expr)
    {
        // Already includes the surrounding double quotes; zserio's
        // string-literal grammar is C-compatible.
        return expr.getText();
    }

    @Override
    public String getIdentifier(Expression expr, boolean isLastInDot, boolean isSetter)
            throws ZserioExtensionException
    {
        final AstNode sym = expr.getExprSymbolObject();
        final String zserioName = expr.getText();

        if (sym instanceof Parameter || sym instanceof Field)
        {
            // Leftmost in a dot chain (or a single isolated ID) gets
            // the holder prefix; subsequent ID nodes are bare member
            // names and the default DOT formatting (".") joins them.
            if (expr.isMostLeftId())
            {
                return holderExpr + CIdentifierFormatter.toLowerSnake(zserioName);
            }
            return CIdentifierFormatter.toLowerSnake(zserioName);
        }
        if (sym instanceof Constant)
        {
            final Constant c = (Constant)sym;
            return CIdentifierFormatter.constantName(c.getPackage().getPackageName(), c.getName());
        }
        if (sym instanceof EnumItem)
        {
            // C has no enum scope — every enumerator is a flat
            // `pkg_TYPE_ITEM` constant. Two paths:
            //  - right side of a `Color.RED` dot: the LEFT
            //    (EnumType) emits `pkg_COLOR_` (with trailing
            //    underscore), getDot collapses the separator, this
            //    side just contributes the upper-snake item name;
            //  - bare `RED` ref (most-left): we need the parent
            //    type to assemble the whole name. The AST's expr
            //    type / owner carry it for direct refs.
            final EnumItem item = (EnumItem)sym;
            if (!expr.isMostLeftId())
            {
                return CIdentifierFormatter.toUpperSnake(item.getName());
            }
            final EnumType et = resolveEnumType(expr);
            if (et == null)
            {
                throw new ZserioExtensionException(
                        "zserio-c: enumerator '" + item.getName()
                                + "' referenced without a resolvable enum type");
            }
            return CIdentifierFormatter.constantName(
                    et.getPackage().getPackageName(),
                    et.getName() + "_" + item.getName());
        }
        if (sym instanceof BitmaskValue)
        {
            final BitmaskValue bv = (BitmaskValue)sym;
            if (!expr.isMostLeftId())
            {
                return CIdentifierFormatter.toUpperSnake(bv.getName());
            }
            final BitmaskType bt = resolveBitmaskType(expr);
            if (bt == null)
            {
                throw new ZserioExtensionException(
                        "zserio-c: bitmask value '" + bv.getName()
                                + "' referenced without a resolvable bitmask type");
            }
            return CIdentifierFormatter.constantName(
                    bt.getPackage().getPackageName(),
                    bt.getName() + "_" + bv.getName());
        }
        if (sym instanceof EnumType)
        {
            // Mid-chain `Color` in `Color.RED`: emit `pkg_COLOR_`
            // (constant prefix + trailing underscore). `getDot`
            // returns an empty separator when op2 is EnumItem, so
            // the right side's item name attaches directly.
            if (isLastInDot)
            {
                throw new ZserioExtensionException(
                        "zserio-c: standalone enum-type reference '" + zserioName
                                + "' is not yet supported in expressions");
            }
            final EnumType et = (EnumType)sym;
            return CIdentifierFormatter.constantName(
                    et.getPackage().getPackageName(), et.getName()) + "_";
        }
        if (sym instanceof BitmaskType)
        {
            if (isLastInDot)
            {
                throw new ZserioExtensionException(
                        "zserio-c: standalone bitmask-type reference '" + zserioName
                                + "' is not yet supported in expressions");
            }
            final BitmaskType bt = (BitmaskType)sym;
            return CIdentifierFormatter.constantName(
                    bt.getPackage().getPackageName(), bt.getName()) + "_";
        }
        if (sym instanceof Package || sym instanceof ZserioType)
        {
            // The leftmost segment of a `pkg.…` chain (or a non-
            // enum/bitmask type ref). `getDot` collapses the
            // following separator, so this segment has nothing to
            // emit.
            return "";
        }
        throw new ZserioExtensionException(
                "zserio-c: identifier '" + zserioName + "' refers to "
                        + (sym == null ? "an unresolved symbol" : sym.getClass().getSimpleName())
                        + " — not yet supported in expressions");
    }

    private static EnumType resolveEnumType(Expression expr)
    {
        if (expr.getExprZserioType() instanceof EnumType)
        {
            return (EnumType)expr.getExprZserioType();
        }
        if (expr.getExprOwner() instanceof EnumType)
        {
            return (EnumType)expr.getExprOwner();
        }
        return null;
    }

    private static BitmaskType resolveBitmaskType(Expression expr)
    {
        if (expr.getExprZserioType() instanceof BitmaskType)
        {
            return (BitmaskType)expr.getExprZserioType();
        }
        if (expr.getExprOwner() instanceof BitmaskType)
        {
            return (BitmaskType)expr.getExprOwner();
        }
        return null;
    }

    @Override
    public String getIndex(Expression expr) throws ZserioExtensionException
    {
        throw new ZserioExtensionException(
                "zserio-c: implicit-array '@index' references are not yet supported");
    }

    /* -------- DOT (member access) -------- */

    @Override
    public BinaryExpressionFormatting getDot(Expression expr)
    {
        // C has no scoping operator for enum / bitmask members —
        // each enumerator is a flat `pkg_TYPE_ITEM` macro. When the
        // dot's left side is a package or zserio type reference, the
        // right side will produce that full flat name on its own, so
        // the separator collapses to empty.
        final Expression leftSide = expr.op1();
        if (leftSide != null)
        {
            final AstNode leftSym = leftSide.getExprSymbolObject();
            if (leftSym instanceof Package || leftSym instanceof ZserioType)
            {
                return new BinaryExpressionFormatting("");
            }
        }
        // Otherwise it's struct member access — `header.tag` →
        // `self->header.tag`, where the leftmost ID already carried
        // the holder prefix.
        return new BinaryExpressionFormatting(".");
    }

    /* -------- unsupported expression categories -------- */

    @Override
    public UnaryExpressionFormatting getFunctionCall(Expression expr)
            throws ZserioExtensionException
    {
        throw new ZserioExtensionException(
                "zserio-c: zserio expression-function calls are not yet supported");
    }

    @Override
    public UnaryExpressionFormatting getLengthOf(Expression expr)
            throws ZserioExtensionException
    {
        throw new ZserioExtensionException(
                "zserio-c: lengthof() is not yet supported");
    }

    @Override
    public UnaryExpressionFormatting getValueOf(Expression expr)
            throws ZserioExtensionException
    {
        throw new ZserioExtensionException(
                "zserio-c: valueof() is not yet supported");
    }

    @Override
    public UnaryExpressionFormatting getNumBits(Expression expr)
            throws ZserioExtensionException
    {
        throw new ZserioExtensionException(
                "zserio-c: numbits() is not yet supported");
    }

    @Override
    public BinaryExpressionFormatting getArrayElement(Expression expr, boolean isSetter)
            throws ZserioExtensionException
    {
        throw new ZserioExtensionException(
                "zserio-c: array-element expressions are not yet supported");
    }

    @Override
    public BinaryExpressionFormatting getIsSet(Expression expr)
            throws ZserioExtensionException
    {
        throw new ZserioExtensionException(
                "zserio-c: isset() is not yet supported");
    }
}
