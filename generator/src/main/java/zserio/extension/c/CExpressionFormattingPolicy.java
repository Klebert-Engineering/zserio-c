/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Fabian Klebert
 */

package zserio.extension.c;

import java.math.BigInteger;

import zserio.ast.AstNode;
import zserio.ast.BitmaskType;
import zserio.ast.BitmaskValue;
import zserio.ast.CompoundType;
import zserio.ast.Constant;
import zserio.ast.EnumItem;
import zserio.ast.EnumType;
import zserio.ast.Expression;
import zserio.ast.Field;
import zserio.ast.Function;
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
    private final CompoundType currentOwner;
    private final String indexVar;

    /**
     * @param holderExpr C expression that prefixes a leftmost
     *                   parameter / field reference. Typically
     *                   {@code "self->"}.
     */
    public CExpressionFormattingPolicy(String holderExpr)
    {
        this(holderExpr, null, null);
    }

    /**
     * @param currentOwner the compound that contains the expression
     *                     being formatted. Used as the implicit
     *                     receiver for same-compound function calls
     *                     ({@code area()} where area is declared in
     *                     this compound), since the AST doesn't always
     *                     thread a function's owner through
     *                     {@code expr.getExprOwner()}.
     */
    public CExpressionFormattingPolicy(String holderExpr, CompoundType currentOwner)
    {
        this(holderExpr, currentOwner, null);
    }

    /**
     * @param indexVar C expression for the {@code @index} magic
     *                 identifier — typically the iteration variable
     *                 of the enclosing array's read / write loop.
     *                 {@code null} when no array iteration is
     *                 active; {@code @index} use throws in that case.
     */
    public CExpressionFormattingPolicy(String holderExpr, CompoundType currentOwner,
            String indexVar)
    {
        this.holderExpr = holderExpr;
        this.currentOwner = currentOwner;
        this.indexVar = indexVar;
    }

    /**
     * Holder for parameter / sibling-state references — the {@code
     * holderExpr} stripped of a trailing {@code "u."}. Inside a
     * choice/union case, the field's data lives at {@code self->u.X}
     * but parameters and parent state live one level up at
     * {@code self->X}.
     */
    private String parentHolder()
    {
        if (holderExpr.endsWith("u."))
        {
            return holderExpr.substring(0, holderExpr.length() - 2);
        }
        return holderExpr;
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
            //
            // In a Choice / Union the formatter is called with
            // holderExpr ending in "u." because each case's data lives
            // inside an anonymous member union. Parameters live one
            // level up at the parent compound (so a parameter ref
            // strips the "u."), but case-field self-references stay
            // inside the union — a constraint like
            // `case CONFIDENCE: AttributeConfidence x : x <= 100;`
            // must lower to `self->u.x`, not `self->x`.
            if (expr.isMostLeftId())
            {
                final String holder = (sym instanceof Parameter) ? parentHolder() : holderExpr;
                return holder + CIdentifierFormatter.toLowerSnake(zserioName);
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
        if (sym instanceof Function)
        {
            // Function name. When the function call is `func()`
            // (most-left), we emit the full C name and the call's
            // `(self)` arrives via getFunctionCall. When the call is
            // `recv.func()`, getDot has already emitted both the name
            // and the receiver, so this side has nothing to emit.
            if (!expr.isMostLeftId())
            {
                return "";
            }
            final Function func = (Function)sym;
            final CompoundType owner = resolveFunctionOwner(expr);
            if (owner == null)
            {
                throw new ZserioExtensionException(
                        "zserio-c: function '" + func.getName()
                                + "' has no resolvable owning compound type");
            }
            return cFunctionName(owner, func);
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
        if (indexVar == null)
        {
            throw new ZserioExtensionException(
                    "zserio-c: '@index' referenced outside an array iteration context");
        }
        // Cast to (uint32_t) to keep the result type stable —
        // @index is a varsize-shaped value upstream, and parameter
        // arguments expect that shape so set_parameters takes a
        // matching argument.
        return "(uint32_t)" + indexVar;
    }

    /* -------- DOT (member access) -------- */

    @Override
    public BinaryExpressionFormatting getDot(Expression expr) throws ZserioExtensionException
    {
        // Function call via `recv.func()`: the right-hand side is a
        // Function symbol. We collapse the dot into a full C call
        // expression with the receiver passed by address, so the
        // overall shape becomes `<funcName>(&LEFT)`. The
        // FunctionCallExpression wrapper around this then emits
        // empty before/after — the dot has produced the complete
        // call.
        final Expression rightSide = expr.op2();
        if (rightSide != null)
        {
            final AstNode rightSym = rightSide.getExprSymbolObject();
            if (rightSym instanceof Function)
            {
                final Function func = (Function)rightSym;
                // For `recv.func()` the receiver's type IS the owning
                // compound. Prefer that over the AST-set ExprOwner /
                // policy currentOwner — the latter would point at the
                // *enclosing* compound (where the expression lives,
                // e.g. LineRange2D) rather than the *callee's* compound
                // (Line2D), generating `lineRange2D_func` for what is
                // actually `line2D_func`.
                CompoundType owner = null;
                if (expr.op1() != null)
                {
                    ZserioType recvType = expr.op1().getExprZserioType();
                    while (recvType instanceof zserio.ast.Subtype)
                    {
                        recvType = ((zserio.ast.Subtype)recvType).getTypeReference()
                                .getBaseTypeReference().getType();
                    }
                    if (recvType instanceof CompoundType)
                    {
                        owner = (CompoundType)recvType;
                    }
                }
                if (owner == null)
                {
                    owner = resolveFunctionOwner(rightSide);
                }
                if (owner == null)
                {
                    throw new ZserioExtensionException(
                            "zserio-c: function '" + func.getName()
                                    + "' has no resolvable owning compound type");
                }
                final String funcName = cFunctionName(owner, func);
                return new BinaryExpressionFormatting(funcName + "(&", "", ")");
            }
        }

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

    private static String cFunctionName(CompoundType owner, Function func)
    {
        return CIdentifierFormatter.funcPrefix(
                owner.getPackage().getPackageName(), owner.getName())
                + "_" + CIdentifierFormatter.toLowerSnake(func.getName());
    }

    /**
     * Find the compound that owns the function referenced by
     * {@code expr}. Tries the AST's {@code expr.getExprOwner()} first
     * (set when the AST evaluator could thread it through); falls
     * back to the policy's {@code currentOwner} for same-compound
     * refs that the AST didn't tag.
     */
    private CompoundType resolveFunctionOwner(Expression expr)
    {
        final ZserioType o = expr.getExprOwner();
        if (o instanceof CompoundType)
        {
            return (CompoundType)o;
        }
        return currentOwner;
    }

    /* -------- built-in expression functions -------- */

    @Override
    public UnaryExpressionFormatting getLengthOf(Expression expr)
    {
        // `lengthof(arr)` lowers to the array's stored count member.
        // Variable-length and expression-length arrays already track
        // their count next to the data pointer as `<arr>_count`. The
        // operand emits as `<holder><arr>` (or a dot-chain ending in
        // `<arr>`), so suffixing `_count` produces the right access
        // path.
        return new UnaryExpressionFormatting("", "_count");
    }

    @Override
    public UnaryExpressionFormatting getValueOf(Expression expr)
    {
        // `valueof(enumValue)` and `valueof(bitmaskValue)` extract the
        // underlying integer. In our C output the typedef IS the
        // underlying integer (every enum/bitmask is a typedef of the
        // chosen std-int type), so this is a structural no-op.
        return new UnaryExpressionFormatting("", "");
    }

    @Override
    public UnaryExpressionFormatting getNumBits(Expression expr)
    {
        return new UnaryExpressionFormatting("zserio_num_bits((uint64_t)(", "))");
    }

    @Override
    public BinaryExpressionFormatting getIsSet(Expression expr)
    {
        return new BinaryExpressionFormatting(
                "zserio_isset((uint64_t)(", "), (uint64_t)(", "))");
    }

    /* -------- still unsupported -------- */

    @Override
    public UnaryExpressionFormatting getFunctionCall(Expression expr)
            throws ZserioExtensionException
    {
        final Expression op1 = expr.op1();
        if (op1 == null)
        {
            throw new ZserioExtensionException(
                    "zserio-c: function call without operand");
        }
        // `recv.func()` — the dot already produced `<funcName>(&recv)`.
        // We pile nothing on top.
        if (op1.getType() == zserio.antlr.ZserioParser.DOT)
        {
            return new UnaryExpressionFormatting("", "");
        }
        // `func()` — same-compound call. op1's getIdentifier produced
        // the C function name already; we close with `(self)`.
        final AstNode sym = op1.getExprSymbolObject();
        if (sym instanceof Function)
        {
            return new UnaryExpressionFormatting("", "(self)");
        }
        throw new ZserioExtensionException(
                "zserio-c: unsupported function-call shape");
    }

    @Override
    public BinaryExpressionFormatting getArrayElement(Expression expr, boolean isSetter)
    {
        // C indexing syntax matches zserio's: `<arr>[<idx>]`. For
        // variable arrays the C `<arr>` is the heap pointer, which
        // the compiler indexes the same way as a stack array — both
        // emit the same access expression.
        return new BinaryExpressionFormatting("", "[", "]");
    }
}
