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
        // Array field whose element type has packable leaves: in a
        // packed-parent body, upstream uses packed-array encoding for
        // the field even when the schema doesn't say `packed`. Route
        // through FieldFormatter's packed-array codegen so the
        // parent's _read_packed / _write_packed call the element's
        // _read_packed / _write_packed with a fresh per-array context.
        // The array's storage layout (count + ptr or fixed-len buffer)
        // is unchanged from the unpacked path; only the read/write
        // statements differ.
        if (field.getTypeInstantiation() instanceof zserio.ast.ArrayInstantiation
                && !field.isOptional()
                && field.getConstraintExpr() == null)
        {
            final zserio.ast.ArrayInstantiation ai =
                    (zserio.ast.ArrayInstantiation)field.getTypeInstantiation();
            final ZserioType eltDirect = ai.getElementTypeInstantiation()
                    .getTypeReference().getType();
            final CompoundType eltCompound = compoundLeaf(eltDirect);
            if (eltCompound != null && hasPackableField(eltCompound))
            {
                final FieldFormatter.FieldData packedData =
                        FieldFormatter.packedCompoundArrayField(
                                plainData.getName(), ai, holderExpr, owner);
                // The plain-path read / write include any align(N):
                // wrap that FieldFormatter.format applied to the
                // FieldData. The packed substitution loses that wrap
                // — so re-format the alignment expression here and
                // prepend the same align call to the packed read /
                // write. (Without this, an `align(8): T[];` field's
                // padding bits get consumed as element data and the
                // bitstream slips.)
                String packedRead = packedData.getReadStmt();
                String packedWrite = packedData.getWriteStmt();
                if (field.getAlignmentExpr() != null)
                {
                    final String formattedAlign = CExpressionFormatter.format(
                            field.getAlignmentExpr(), holderExpr, owner);
                    packedRead = "ZSERIO_TRY(zserio_bitstream_reader_align(bs, "
                            + "(size_t)(" + formattedAlign + ")));\n    "
                            + packedRead;
                    packedWrite = "ZSERIO_TRY(zserio_bitstream_writer_align(bs, "
                            + "(size_t)(" + formattedAlign + ")));\n    "
                            + packedWrite;
                }
                // The packed-array read / write each scope a local
                // packing context — there's nothing for the parent
                // compound's packing-context struct to carry, so
                // `usesContext` stays false (no init/observe at parent
                // level for this field).
                return new PackedField(/* usesContext */ false,
                        plainData.getName(),
                        /* ctxMember */ "",
                        /* initStmt */ "",
                        /* observeStmt */ "",
                        packedRead,
                        packedWrite);
            }
        }

        // Compound subfield with its own packing context: the parent
        // must hold a nested context member and recurse into the
        // child's _init / _observe / _read_packed / _write_packed
        // helpers. This matches upstream's nested
        // ZserioPackingContext pattern.
        final CompoundType compoundChild = compoundPackableUnderlying(field);
        if (compoundChild != null)
        {
            return formatCompoundPacked(field, plainData,
                    compoundChild, holderExpr, owner, ctxHolder);
        }

        if (!isSimplePackable(field))
        {
            // Field is either not packable at all (string, bytes,
            // extern, optional, array, …) or its packing requires
            // machinery we don't generate yet. Fall through to the
            // regular read / write — the wire format may not match
            // upstream for some of these, which Phase 4 round-trip
            // tests will surface.
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
        String readStmt =
                "{\n"
                + "        uint64_t " + name + "_raw;\n"
                + "        ZSERIO_TRY(zserio_delta_read(&" + ctxField + ", bs, &"
                + name + "_raw));\n"
                + "        " + access + " = (" + cType + ")" + name + "_raw;\n"
                + "    }";
        String writeStmt = "ZSERIO_TRY(zserio_delta_write(&" + ctxField + ", bs, "
                + uintCast + access + "));";

        // Constraint enforcement on the packed path — same shape as
        // FieldFormatter.wrapConstraint produces for the plain path,
        // so a constrained field validates its value either way.
        if (field.getConstraintExpr() != null)
        {
            final String formatted = CExpressionFormatter.format(
                    field.getConstraintExpr(), holderExpr, owner);
            final String check =
                    "if (!(" + formatted + ")) {\n"
                    + "        return ZSERIO_ERR_CONSTRAINT_VIOLATED;\n"
                    + "    }";
            readStmt  = readStmt + "\n    " + check;
            writeStmt = check + "\n    " + writeStmt;
        }

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
            if (isSimplePackable(f) || compoundPackableUnderlying(f) != null)
            {
                return true;
            }
        }
        return false;
    }

    /**
     * Walk subtype / instantiate layers down to a {@link CompoundType},
     * or return {@code null} if the type doesn't resolve to one.
     * Used by the array-element classifier to decide if a packed
     * element-array path applies.
     */
    private static CompoundType compoundLeaf(ZserioType direct)
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
        return (t instanceof CompoundType) ? (CompoundType)t : null;
    }

    /**
     * If {@code field} is a non-optional, non-constraint, non-array
     * compound whose underlying type has at least one packable field
     * (transitively), return that compound. Otherwise null.
     *
     * The result drives the parent's nested-context emission: the
     * parent's packing context carries a child packing context for
     * this field, and the parent's init / observe / read / write
     * recurse into the child's matching helpers.
     */
    private static CompoundType compoundPackableUnderlying(Field field)
    {
        if (field.isOptional() || field.getConstraintExpr() != null)
        {
            return null;
        }
        final TypeInstantiation typeInst = field.getTypeInstantiation();
        if (typeInst instanceof zserio.ast.ArrayInstantiation)
        {
            return null;
        }
        ZserioType t = typeInst.getTypeReference().getType();
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
        // Only structures (not choices / unions) emit packing-context
        // helpers — their templates are the ones we extended for the
        // recursive packed-context machinery. Treating a Choice/Union
        // child as compound-packable would dangle a reference to a
        // non-existent <prefix>_packing_ctx_t typedef.
        if (!(t instanceof zserio.ast.StructureType))
        {
            return null;
        }
        final CompoundType ct = (CompoundType)t;
        return hasPackableField(ct) ? ct : null;
    }

    /**
     * Compound subfield with a packing context: emit a nested context
     * member + recursive init / observe / read / write into the
     * child's _packed helpers.
     *
     * For parameterised children (e.g. {@code Position2D(coordShift)}
     * inside an Intersection), the parameter must be propagated to
     * the child instance before any init / observe / read / write —
     * the child's init computes parameter-driven delta widths from
     * its own {@code self->shift} (etc.), so an unset parameter would
     * yield wrong widths and a stream-misalignment cascade.
     */
    private static PackedField formatCompoundPacked(Field field,
            FieldFormatter.FieldData plainData,
            CompoundType child,
            String holderExpr, CompoundType owner, String ctxHolder)
            throws ZserioExtensionException
    {
        final String name = plainData.getName();
        final String access = holderExpr + name;
        final String ctxField = ctxHolder + name;
        final String childPrefix = CIdentifierFormatter.funcPrefix(
                child.getPackage().getPackageName(), child.getName());

        final String setParams = childSetParametersLine(
                field, child, access, holderExpr, owner);

        final String ctxMember = childPrefix + "_packing_ctx_t " + name + ";";
        final String initStmt = setParams
                + "ZSERIO_TRY(" + childPrefix
                + "_init_packing_context(&" + access + ", &" + ctxField + "));";
        final String observeStmt = setParams
                + "ZSERIO_TRY(" + childPrefix
                + "_observe_packing_context(&" + access + ", &" + ctxField + "));";
        final String readStmt = setParams
                + "ZSERIO_TRY(" + childPrefix
                + "_read_packed(&" + access + ", bs, alloc, &" + ctxField + "));";
        final String writeStmt = setParams
                + "ZSERIO_TRY(" + childPrefix
                + "_write_packed(&" + access + ", bs, &" + ctxField + "));";

        return new PackedField(/* usesContext */ true,
                name, ctxMember, initStmt, observeStmt, readStmt, writeStmt);
    }

    /**
     * Build the {@code <child>_set_parameters(&slot, args...);} line
     * for a parameterised nested compound subfield, or the empty
     * string when the child has no parameters. The line ends with
     * {@code \n    } so the caller can splice it directly before the
     * recursive call.
     */
    private static String childSetParametersLine(Field field, CompoundType child,
            String access, String holderExpr, CompoundType owner)
            throws ZserioExtensionException
    {
        final TypeInstantiation typeInst = field.getTypeInstantiation();
        if (!(typeInst instanceof zserio.ast.ParameterizedTypeInstantiation))
        {
            return "";
        }
        final String childPrefix = CIdentifierFormatter.funcPrefix(
                child.getPackage().getPackageName(), child.getName());
        final zserio.ast.ParameterizedTypeInstantiation pti =
                (zserio.ast.ParameterizedTypeInstantiation)typeInst;
        final StringBuilder args = new StringBuilder();
        for (final zserio.ast.ParameterizedTypeInstantiation.InstantiatedParameter ip
                : pti.getInstantiatedParameters())
        {
            args.append(", ");
            args.append(CExpressionFormatter.format(
                    ip.getArgumentExpression(), holderExpr, owner));
        }
        return childPrefix + "_set_parameters(&" + access + args + ");\n    ";
    }

    private static boolean isSimplePackable(Field field)
    {
        if (field.isOptional())
        {
            return false;
        }
        // A constraint doesn't disqualify a field from packing —
        // upstream's encoder still delta-encodes constrained scalars
        // and validates the constraint after decode. We do the same:
        // the format() method below appends the constraint check to
        // the packed readStmt.
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
