/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Fabian Klebert
 */

package zserio.extension.c;

import zserio.ast.BitmaskType;
import zserio.ast.BooleanType;
import zserio.ast.CompoundType;
import zserio.ast.DynamicBitFieldInstantiation;
import zserio.ast.DynamicBitFieldType;
import zserio.ast.EnumType;
import zserio.ast.Expression;
import zserio.ast.Field;
import zserio.ast.FixedBitFieldType;
import zserio.ast.StdIntegerType;
import zserio.ast.Subtype;
import zserio.ast.TypeInstantiation;
import zserio.ast.TypeReference;
import zserio.ast.VarIntegerType;
import zserio.ast.ZserioType;
import zserio.extension.common.ZserioExtensionException;

/**
 * Lowers a struct {@link Field} to its packed-array companion form.
 *
 * Packed arrays of compound elements feed each element through a
 * {@code _read_packed} / {@code _write_packed} helper that uses a
 * shared {@link zserio.ast.ZserioType packing context} to delta-
 * encode the compound's packable scalar fields across all array
 * iterations. This formatter produces, per Field:
 *
 * <ul>
 *   <li>{@code ctxMember} — the C declaration that goes into the
 *       compound's packing-context struct (a
 *       {@code zserio_delta_context_t} for an integer / bit-field /
 *       enum / bitmask, or empty for non-packable fields);</li>
 *   <li>{@code initStmt} — initialiser line that lives in the
 *       compound's {@code _init_packing_context} body
 *       ({@code zserio_delta_init} with the field's bit width and
 *       signedness);</li>
 *   <li>{@code observeStmt} — observe line for the
 *       {@code _observe_packing_context} body;</li>
 *   <li>{@code readStmt} — replaces the regular read for use in
 *       {@code _read_packed} (delta-encoded) or stays identical for
 *       non-packable fields;</li>
 *   <li>{@code writeStmt} — same idea for {@code _write_packed}.</li>
 * </ul>
 *
 * Phase 3 batch 26 scope: scalar packable fields only. Non-scalar
 * "packable" categories (nested compound packable, varint packable)
 * are currently routed through the plain read/write — wire-format
 * matches require a follow-up batch.
 */
public final class PackingFieldFormatter
{
    private PackingFieldFormatter()
    {
        // utility class
    }

    /** Container for the per-field packed-array lowering. */
    public static final class PackedField
    {
        private final boolean usesContext;
        private final String name;
        private final String ctxMember;
        private final String initStmt;
        private final String observeStmt;
        private final String readStmt;
        private final String writeStmt;

        PackedField(boolean usesContext, String name, String ctxMember,
                String initStmt, String observeStmt,
                String readStmt, String writeStmt)
        {
            this.usesContext = usesContext;
            this.name = name;
            this.ctxMember = ctxMember;
            this.initStmt = initStmt;
            this.observeStmt = observeStmt;
            this.readStmt = readStmt;
            this.writeStmt = writeStmt;
        }

        public boolean getUsesContext() { return usesContext; }
        public String  getName()        { return name; }
        public String  getCtxMember()   { return ctxMember; }
        public String  getInitStmt()    { return initStmt; }
        public String  getObserveStmt() { return observeStmt; }
        public String  getReadStmt()    { return readStmt; }
        public String  getWriteStmt()   { return writeStmt; }
    }

    /**
     * Lower one field for the packed-array element path.
     *
     * @param field      the field as it appears in the compound
     * @param plainData  the field's regular FieldData (used as
     *                   fall-through for non-packable fields)
     * @param holderExpr struct-access prefix for the value
     *                   (typically {@code "self->"} for top-level
     *                   compounds)
     * @param owner      the compound that contains the field, used
     *                   for same-compound function-call resolution
     * @param ctxHolder  C expression that yields the packing context
     *                   (typically {@code "ctx->"}). Members of the
     *                   context struct live under this prefix.
     */
    public static PackedField format(Field field,
            FieldFormatter.FieldData plainData,
            String holderExpr, CompoundType owner, String ctxHolder)
            throws ZserioExtensionException
    {
        if (!isSimplePackable(field))
        {
            // Field is either not packable at all (string, bytes,
            // extern, varint, optional, array, nested compound, …)
            // or its packing requires machinery we don't generate
            // yet. Fall through to the regular read / write — the
            // wire format may not match upstream for some of these,
            // which Phase 4 round-trip tests will surface.
            return new PackedField(/* usesContext */ false,
                    plainData.getName(),
                    /* ctxMember */ "",
                    /* initStmt */ "",
                    /* observeStmt */ "",
                    plainData.getReadStmt(),
                    plainData.getWriteStmt());
        }

        final String name = plainData.getName();
        final String access = holderExpr + name;
        final String ctxField = ctxHolder + name;

        final TypeInstantiation typeInst = field.getTypeInstantiation();
        final ZserioType direct = typeInst.getTypeReference().getType();
        final ZserioType under = scalarUnderlying(direct);

        final String ctxMember = "zserio_delta_context_t " + name + ";";

        final String initStmt;
        final boolean signed;
        final String uintCast;
        if (under instanceof VarIntegerType)
        {
            // Varint elements use the trait-driven delta mode: the
            // runtime's traits struct supplies the typed varint codec
            // and per-value bit-size. The delta machinery — packed
            // descriptor, max-bit-number tracking, signed-bit-bag
            // delta — runs identically to the fixed-width path.
            final String traitsSym = traitsSymbol((VarIntegerType)under);
            initStmt = "ZSERIO_TRY(zserio_delta_init_traits(&" + ctxField
                    + ", &" + traitsSym + "));";
            signed = ((VarIntegerType)under).isSigned();
            uintCast = uintCastFor(signed);
        }
        else
        {
            // Fixed-width integer / bit-field / enum / bitmask.
            final PackedScalar scalar = packedScalar(direct, typeInst, holderExpr, owner);
            initStmt = "ZSERIO_TRY(zserio_delta_init(&" + ctxField + ", "
                    + scalar.bitsExpr + ", "
                    + (scalar.signed ? "true" : "false") + "));";
            signed = scalar.signed;
            uintCast = scalar.uintCast;
        }

        final String observeStmt = "ZSERIO_TRY(zserio_delta_observe(&" + ctxField + ", "
                + uintCast + access + "));";

        final String cType = plainData.getNativeType();
        final String readStmt =
                "{\n"
                + "        uint64_t " + name + "_raw;\n"
                + "        ZSERIO_TRY(zserio_delta_read(&" + ctxField + ", bs, &"
                + name + "_raw));\n"
                + "        " + access + " = (" + cType + ")" + name + "_raw;\n"
                + "    }";

        final String writeStmt = "ZSERIO_TRY(zserio_delta_write(&" + ctxField + ", bs, "
                + uintCast + access + "));";

        return new PackedField(/* usesContext */ true,
                name, ctxMember, initStmt, observeStmt, readStmt, writeStmt);
    }

    /**
     * Map a {@link VarIntegerType} to the corresponding global
     * {@code zserio_delta_traits_t} symbol exported by the runtime.
     * The traits struct holds the typed varint codec callbacks plus
     * the bit-size cost function — the delta context plugs straight
     * into them via {@code zserio_delta_init_traits}.
     */
    private static String traitsSymbol(VarIntegerType vi) throws ZserioExtensionException
    {
        final int max = vi.getMaxBitSize();
        final boolean signed = vi.isSigned();
        // Match the table in CNativeTypeMapper.nativeTypeFor:
        //   varuint16 → 16, varuint32 → 32, varuint64 → 64, varuint → 72
        //   varint16  → 16, varint32  → 32, varint64  → 64, varint  → 72
        //   varsize   → 40
        if (!signed && max == 40)  return "zserio_delta_traits_varsize";
        if (!signed && max == 16)  return "zserio_delta_traits_varuint16";
        if (!signed && max == 32)  return "zserio_delta_traits_varuint32";
        if (!signed && max == 64)  return "zserio_delta_traits_varuint64";
        if (!signed && max == 72)  return "zserio_delta_traits_varuint";
        if (signed  && max == 16)  return "zserio_delta_traits_varint16";
        if (signed  && max == 32)  return "zserio_delta_traits_varint32";
        if (signed  && max == 64)  return "zserio_delta_traits_varint64";
        if (signed  && max == 72)  return "zserio_delta_traits_varint";
        throw new ZserioExtensionException(
                "zserio-c: no delta traits for varint with maxBitSize="
                        + max + ", signed=" + signed);
    }

    /* -------- packability classification -------- */

    /**
     * A subset of zserio's "packable" notion that this generator can
     * actually delta-encode. We accept simple scalar fields:
     * StdIntegerType, FixedBitFieldType, DynamicBitFieldType, plus
     * the same shapes wrapped in subtype / instantiate / enum /
     * bitmask layers. We deliberately exclude — pending follow-up
     * batches — VarIntegerType (delta-of-varint), nested compound
     * packable, optional packable, and array fields.
     */
    /**
     * Whether {@code ct} has at least one field that this generator
     * delta-encodes — used to decide whether to emit packing-context
     * helpers (and to decide whether a packed array of the compound
     * dispatches through the packed or the regular compound path).
     */
    public static boolean hasPackableField(CompoundType ct)
    {
        for (final Field f : ct.getFields())
        {
            if (isSimplePackable(f))
            {
                return true;
            }
        }
        return false;
    }

    private static boolean isSimplePackable(Field field)
    {
        if (field.isOptional())
        {
            return false;
        }
        if (field.getConstraintExpr() != null)
        {
            return false;
        }
        final TypeInstantiation typeInst = field.getTypeInstantiation();
        if (typeInst instanceof zserio.ast.ArrayInstantiation)
        {
            return false;
        }
        final ZserioType direct = typeInst.getTypeReference().getType();
        final ZserioType under = scalarUnderlying(direct);
        if (under instanceof StdIntegerType
                || under instanceof FixedBitFieldType
                || under instanceof DynamicBitFieldType
                || under instanceof VarIntegerType)
        {
            return true;
        }
        // BoolType is NOT in upstream's packable set; matching that
        // keeps our wire format compatible with the original
        // generators.
        if (under instanceof BooleanType)
        {
            return false;
        }
        return false;
    }

    private static ZserioType scalarUnderlying(ZserioType direct)
    {
        ZserioType t = direct;
        while (t instanceof Subtype || t instanceof zserio.ast.InstantiateType)
        {
            if (t instanceof Subtype)
            {
                t = ((Subtype)t).getTypeReference().getBaseTypeReference().getType();
            }
            else
            {
                t = ((zserio.ast.InstantiateType)t).getTypeReference()
                        .getBaseTypeReference().getType();
            }
        }
        if (t instanceof EnumType)
        {
            t = ((EnumType)t).getTypeInstantiation().getTypeReference()
                    .getBaseTypeReference().getType();
        }
        else if (t instanceof BitmaskType)
        {
            t = ((BitmaskType)t).getTypeInstantiation().getTypeReference()
                    .getBaseTypeReference().getType();
        }
        while (t instanceof Subtype || t instanceof zserio.ast.InstantiateType)
        {
            if (t instanceof Subtype)
            {
                t = ((Subtype)t).getTypeReference().getBaseTypeReference().getType();
            }
            else
            {
                t = ((zserio.ast.InstantiateType)t).getTypeReference()
                        .getBaseTypeReference().getType();
            }
        }
        return t;
    }

    private static PackedScalar packedScalar(ZserioType direct, TypeInstantiation typeInst,
            String holderExpr, CompoundType owner) throws ZserioExtensionException
    {
        final ZserioType under = scalarUnderlying(direct);
        if (under instanceof StdIntegerType)
        {
            final StdIntegerType std = (StdIntegerType)under;
            return new PackedScalar(Integer.toString(std.getBitSize()),
                    std.isSigned(), uintCastFor(std.isSigned()));
        }
        if (under instanceof FixedBitFieldType)
        {
            final FixedBitFieldType bf = (FixedBitFieldType)under;
            return new PackedScalar(Integer.toString(bf.getBitSize()),
                    bf.isSigned(), uintCastFor(bf.isSigned()));
        }
        if (under instanceof DynamicBitFieldType)
        {
            final DynamicBitFieldType dyn = (DynamicBitFieldType)under;
            // Find the matching DynamicBitFieldInstantiation so we
            // can format its length expression — same lookup the
            // regular field path does.
            final DynamicBitFieldInstantiation inst =
                    findDynamicBitFieldInstantiation(direct, typeInst);
            if (inst == null)
            {
                throw new ZserioExtensionException(
                        "zserio-c: packed dynamic bit field has no resolvable length expression");
            }
            // num_bits is uint8_t; cast in case the formatted
            // expression evaluates to a wider type.
            final Expression lenExpr = inst.getLengthExpression();
            final String formatted = "(uint8_t)("
                    + CExpressionFormatter.format(lenExpr, holderExpr, owner) + ")";
            return new PackedScalar(formatted, dyn.isSigned(), uintCastFor(dyn.isSigned()));
        }
        throw new ZserioExtensionException(
                "zserio-c: packed scalar lowering hit unexpected underlying type "
                        + under.getClass().getSimpleName());
    }

    private static String uintCastFor(boolean signed)
    {
        // The runtime delta context takes uint64_t element bit
        // patterns. For signed fields we go through int64_t first so
        // the sign-extension is well-defined, then reinterpret as
        // uint64_t.
        return signed ? "(uint64_t)(int64_t)" : "(uint64_t)";
    }

    private static DynamicBitFieldInstantiation findDynamicBitFieldInstantiation(
            ZserioType direct, TypeInstantiation typeInst)
    {
        if (typeInst instanceof DynamicBitFieldInstantiation)
        {
            return (DynamicBitFieldInstantiation)typeInst;
        }
        ZserioType t = direct;
        while (t instanceof Subtype || t instanceof zserio.ast.InstantiateType)
        {
            if (t instanceof Subtype)
            {
                t = ((Subtype)t).getTypeReference().getBaseTypeReference().getType();
            }
            else
            {
                t = ((zserio.ast.InstantiateType)t).getTypeReference()
                        .getBaseTypeReference().getType();
            }
        }
        if (t instanceof BitmaskType)
        {
            final TypeInstantiation ti = ((BitmaskType)t).getTypeInstantiation();
            if (ti instanceof DynamicBitFieldInstantiation)
            {
                return (DynamicBitFieldInstantiation)ti;
            }
        }
        else if (t instanceof EnumType)
        {
            final TypeInstantiation ti = ((EnumType)t).getTypeInstantiation();
            if (ti instanceof DynamicBitFieldInstantiation)
            {
                return (DynamicBitFieldInstantiation)ti;
            }
        }
        return null;
    }

    private static final class PackedScalar
    {
        final String bitsExpr;     // numeric literal for fixed types, formatted expression for bit<n>
        final boolean signed;
        final String uintCast;     // cast pattern that materialises a uint64_t bit pattern

        PackedScalar(String bitsExpr, boolean signed, String uintCast)
        {
            this.bitsExpr = bitsExpr;
            this.signed = signed;
            this.uintCast = uintCast;
        }
    }
}
