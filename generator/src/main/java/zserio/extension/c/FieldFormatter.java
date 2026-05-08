/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Fabian Klebert
 */

package zserio.extension.c;

import zserio.ast.BitmaskType;
import zserio.ast.BooleanType;
import zserio.ast.BytesType;
import zserio.ast.CompoundType;
import zserio.ast.EnumType;
import zserio.ast.ExternType;
import zserio.ast.Field;
import zserio.ast.FixedBitFieldType;
import zserio.ast.FloatType;
import zserio.ast.PackageName;
import zserio.ast.ParameterizedTypeInstantiation;
import zserio.ast.StdIntegerType;
import zserio.ast.StringType;
import zserio.ast.Subtype;
import zserio.ast.TypeInstantiation;
import zserio.ast.TypeReference;
import zserio.ast.VarIntegerType;
import zserio.ast.ZserioType;
import zserio.extension.common.ZserioExtensionException;

/**
 * Lowers one zserio struct {@link Field} to its C99 representation.
 *
 * Produces {@link FieldData}: the C member type plus the per-field
 * {@code read} / {@code write} / {@code free} statements that go into
 * the generated function bodies, and an optional include path for the
 * struct header.
 *
 * Phase 2 batch 4 scope: integer primitives, {@code bool}, {@code float*},
 * {@code string}, {@code bytes}, {@code extern}, fixed-width bit fields
 * ({@code bit:N} / {@code int:N}), all variable-length integer flavours
 * ({@code varuintN} / {@code varintN} / {@code varuint} / {@code varint}
 * / {@code varsize}), enums, bitmasks, subtypes, and nested compound
 * types (struct / choice / union — they wire to the C functions emitted
 * by their respective emitters).
 *
 * Out of scope until later batches: dynamic-width bit fields
 * ({@code bit<n>}), arrays, optionals, parameterised types.
 */
public final class FieldFormatter
{
    private FieldFormatter()
    {
        // utility class
    }

    /**
     * Lower one field. {@code holderExpr} is the C expression that
     * yields the struct holding the field — typically {@code "self->"}
     * for top-level structs or {@code "self->u."} for the cases of
     * a union (where each case lives inside an anonymous member union).
     */
    public static FieldData format(Field field, String holderExpr) throws ZserioExtensionException
    {
        final String name = CIdentifierFormatter.toLowerSnake(field.getName());
        final TypeInstantiation typeInst = field.getTypeInstantiation();
        final TypeReference ref = typeInst.getTypeReference();
        final ZserioType direct = ref.getType();

        // C member type: prefer the user-facing typedef (subtype /
        // enum / bitmask / nested compound) over the raw integer width,
        // and remember any header that needs to be #included for it.
        final TypeBinding b = bindType(direct);

        // Compounds wire through the child's emitted *_read/_write/_free
        // helpers. They don't touch the runtime read/write tables, so
        // they short-circuit before the underlying-type dispatch.
        if (direct instanceof CompoundType)
        {
            return compound(name, (CompoundType)direct, typeInst, b, holderExpr);
        }

        final ZserioType under = underlyingType(direct);

        if (under instanceof BooleanType)
        {
            return scalar(name, b, "bool", holderExpr);
        }
        if (under instanceof StdIntegerType)
        {
            final StdIntegerType std = (StdIntegerType)under;
            return scalar(name, b, intSuffix(std.getBitSize(), std.isSigned()), holderExpr);
        }
        if (under instanceof FloatType)
        {
            return scalar(name, b, "float" + ((FloatType)under).getBitSize(), holderExpr);
        }
        if (under instanceof VarIntegerType)
        {
            return scalar(name, b, varintSuffix((VarIntegerType)under), holderExpr);
        }
        if (under instanceof FixedBitFieldType)
        {
            final FixedBitFieldType bf = (FixedBitFieldType)under;
            return bitField(name, b, bf.getBitSize(), bf.isSigned(), holderExpr);
        }
        if (under instanceof StringType)
        {
            return string(name, holderExpr);
        }
        if (under instanceof BytesType)
        {
            return bytes(name, holderExpr);
        }
        if (under instanceof ExternType)
        {
            return externField(name, holderExpr);
        }
        throw new ZserioExtensionException(
                "zserio-c: struct field '" + field.getName() + "': "
                        + under.getClass().getSimpleName()
                        + " is not yet supported as a field type");
    }

    /* -------- read/write/free shapes per category -------- */

    /** Fixed-width primitive / bool / float / varint via a typed read fn. */
    private static FieldData scalar(String name, TypeBinding b, String runtimeSuffix, String h)
    {
        // The runtime function has a stdint-typed pointer parameter,
        // and a C typedef is identical to its underlying type, so we
        // can pass &<holder>name directly even when the C type is a
        // user-facing typedef (subtype / enum / bitmask).
        final String read  = "ZSERIO_TRY(zserio_read_"  + runtimeSuffix
                + "(bs, &" + h + name + "));";
        final String write = "ZSERIO_TRY(zserio_write_" + runtimeSuffix
                + "(bs, " + h + name + "));";
        return new FieldData(name, b.cType, read, write, "", false, b.extraInclude);
    }

    /** Fixed-width bit field; read into a wide raw, then cast. */
    private static FieldData bitField(String name, TypeBinding b, int bits, boolean signed, String h)
    {
        final String runtimeRead  = signed ? "zserio_bitstream_read_signed" : "zserio_bitstream_read_bits";
        final String runtimeWrite = signed ? "zserio_bitstream_write_signed" : "zserio_bitstream_write_bits";
        final String rawType      = signed ? "int64_t" : "uint64_t";
        final String castOnWrite  = signed ? "(int64_t)" : "(uint64_t)";

        final String read =
                "{\n"
                + "        " + rawType + " " + name + "_raw;\n"
                + "        ZSERIO_TRY(" + runtimeRead + "(bs, " + bits
                + ", &" + name + "_raw));\n"
                + "        " + h + name + " = (" + b.cType + ")" + name + "_raw;\n"
                + "    }";
        final String write = "ZSERIO_TRY(" + runtimeWrite + "(bs, " + bits
                + ", " + castOnWrite + h + name + "));";
        return new FieldData(name, b.cType, read, write, "", false, b.extraInclude);
    }

    private static FieldData string(String name, String h)
    {
        final String read = "ZSERIO_TRY(zserio_read_string(bs, alloc, &" + h + name + "));";
        final String write =
                "{\n"
                + "        const zserio_string_view_t " + name + "_view = "
                + "{ " + h + name + ".bytes, " + h + name + ".length };\n"
                + "        ZSERIO_TRY(zserio_write_string(bs, " + name + "_view));\n"
                + "    }";
        final String free = "zserio_string_free(&" + h + name + ", alloc);";
        return new FieldData(name, "zserio_string_t", read, write, free, true, "zserio/string.h");
    }

    private static FieldData bytes(String name, String h)
    {
        final String read = "ZSERIO_TRY(zserio_read_bytes(bs, alloc, &" + h + name + "));";
        final String write =
                "{\n"
                + "        const zserio_bytes_view_t " + name + "_view = "
                + "{ " + h + name + ".bytes, " + h + name + ".length };\n"
                + "        ZSERIO_TRY(zserio_write_bytes(bs, " + name + "_view));\n"
                + "    }";
        final String free = "zserio_bytes_free(&" + h + name + ", alloc);";
        return new FieldData(name, "zserio_bytes_t", read, write, free, true, "zserio/bytes.h");
    }

    private static FieldData externField(String name, String h)
    {
        final String read = "ZSERIO_TRY(zserio_read_extern(bs, alloc, &" + h + name + "));";
        final String write =
                "{\n"
                + "        const zserio_bit_view_t " + name + "_view = "
                + "{ " + h + name + ".bytes, 0u, " + h + name + ".bit_length };\n"
                + "        ZSERIO_TRY(zserio_write_extern(bs, " + name + "_view));\n"
                + "    }";
        final String free = "zserio_extern_free(&" + h + name + ", alloc);";
        return new FieldData(name, "zserio_extern_t", read, write, free, true, "zserio/extern.h");
    }

    private static FieldData compound(String name, CompoundType ct, TypeInstantiation typeInst,
            TypeBinding b, String h) throws ZserioExtensionException
    {
        final PackageName pkg = ct.getPackage().getPackageName();
        final String prefix = CIdentifierFormatter.funcPrefix(pkg, ct.getName());

        // For a parameterised child, the parent must pin the child's
        // parameter members before each read or write — the parent's
        // own state may have changed since the last read, and the
        // child's read needs its parameters in place to drive the
        // selector / array-length / constraint expressions.
        String setParamsLine = "";
        if (typeInst instanceof ParameterizedTypeInstantiation)
        {
            final ParameterizedTypeInstantiation pti = (ParameterizedTypeInstantiation)typeInst;
            final StringBuilder args = new StringBuilder();
            for (final ParameterizedTypeInstantiation.InstantiatedParameter ip
                    : pti.getInstantiatedParameters())
            {
                args.append(", ");
                args.append(CExpressionFormatter.formatRefOrLiteral(ip.getArgumentExpression(), h));
            }
            // The trailing newline + indent keeps the rendered statement
            // visually aligned with the read/write line that follows it
            // when the template substitutes the placeholder.
            setParamsLine = prefix + "_set_parameters(&" + h + name + args + ");\n        ";
        }

        final String read  = setParamsLine + "ZSERIO_TRY(" + prefix
                + "_read(&" + h + name + ", bs, alloc));";
        final String write = setParamsLine + "ZSERIO_TRY(" + prefix
                + "_write(&" + h + name + ", bs));";
        // Always emit a free call. For primitive-only children the
        // generated _free is itself a no-op apart from the NULL guard,
        // so this stays cheap; emitting unconditionally avoids needing
        // to look at the child's own field list to decide.
        final String free = prefix + "_free(&" + h + name + ", alloc);";
        return new FieldData(name, b.cType, read, write, free, true, b.extraInclude);
    }

    /* -------- type binding -------- */

    /** What C type to emit for the field, plus the include needed for it. */
    private static TypeBinding bindType(ZserioType direct) throws ZserioExtensionException
    {
        if (direct instanceof Subtype)
        {
            final Subtype st = (Subtype)direct;
            return userBinding(st.getPackage().getPackageName(), st.getName());
        }
        if (direct instanceof EnumType)
        {
            final EnumType et = (EnumType)direct;
            return userBinding(et.getPackage().getPackageName(), et.getName());
        }
        if (direct instanceof BitmaskType)
        {
            final BitmaskType bt = (BitmaskType)direct;
            return userBinding(bt.getPackage().getPackageName(), bt.getName());
        }
        if (direct instanceof CompoundType)
        {
            final CompoundType ct = (CompoundType)direct;
            return userBinding(ct.getPackage().getPackageName(), ct.getName());
        }
        // Built-in: read C type from the runtime native-type mapper for
        // anything stdint-shaped; explicit names for runtime-owned types.
        if (direct instanceof StringType)
        {
            return new TypeBinding("zserio_string_t", "zserio/string.h");
        }
        if (direct instanceof BytesType)
        {
            return new TypeBinding("zserio_bytes_t", "zserio/bytes.h");
        }
        if (direct instanceof ExternType)
        {
            return new TypeBinding("zserio_extern_t", "zserio/extern.h");
        }
        // Boolean / StdIntegerType / FixedBitFieldType / VarIntegerType /
        // FloatType — all map to a stdint name (or bool / float / double).
        return new TypeBinding(CNativeTypeMapper.nativeTypeFor(direct), null);
    }

    private static TypeBinding userBinding(PackageName pkg, String name)
    {
        return new TypeBinding(
                CIdentifierFormatter.typeName(pkg, name),
                CIdentifierFormatter.headerIncludePath(pkg, name));
    }

    /* -------- traversal helpers -------- */

    private static ZserioType underlyingType(ZserioType direct)
    {
        ZserioType t = direct;
        while (t instanceof Subtype)
        {
            t = ((Subtype)t).getTypeReference().getBaseTypeReference().getType();
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
        // The underlying of an enum/bitmask could itself be wrapped in a
        // subtype layer.
        while (t instanceof Subtype)
        {
            t = ((Subtype)t).getTypeReference().getBaseTypeReference().getType();
        }
        return t;
    }

    private static String intSuffix(int bits, boolean signed) throws ZserioExtensionException
    {
        final int container;
        if (bits <= 8) container = 8;
        else if (bits <= 16) container = 16;
        else if (bits <= 32) container = 32;
        else if (bits <= 64) container = 64;
        else throw new ZserioExtensionException("zserio-c: integer width " + bits + " > 64");
        return (signed ? "int" : "uint") + container;
    }

    /** Map a {@link VarIntegerType} to its runtime function suffix. */
    private static String varintSuffix(VarIntegerType vi) throws ZserioExtensionException
    {
        // The grammar packs varuint16/varint16/.../varuint/varint/varsize
        // into VarIntegerType differentiated by (maxBitSize, isSigned).
        // varsize is the only entry with maxBitSize == 40 — it falls
        // out of the table.
        final int max = vi.getMaxBitSize();
        final boolean signed = vi.isSigned();
        if (max == 40) return "varsize";
        if (max == 16) return signed ? "varint16" : "varuint16";
        if (max == 32) return signed ? "varint32" : "varuint32";
        if (max == 64) return signed ? "varint64" : "varuint64";
        if (max == 72) return signed ? "varint"   : "varuint";
        throw new ZserioExtensionException(
                "zserio-c: unsupported varint maxBitSize " + max);
    }

    /** Per-field FreeMarker view. */
    public static final class FieldData
    {
        private final String name;
        private final String nativeType;
        private final String readStmt;
        private final String writeStmt;
        private final String freeStmt;
        private final boolean owned;
        private final String extraInclude;

        FieldData(String name, String nativeType, String readStmt, String writeStmt,
                String freeStmt, boolean owned, String extraInclude)
        {
            this.name = name;
            this.nativeType = nativeType;
            this.readStmt = readStmt;
            this.writeStmt = writeStmt;
            this.freeStmt = freeStmt;
            this.owned = owned;
            this.extraInclude = extraInclude;
        }

        public String  getName()         { return name; }
        public String  getNativeType()   { return nativeType; }
        public String  getReadStmt()     { return readStmt; }
        public String  getWriteStmt()    { return writeStmt; }
        public String  getFreeStmt()     { return freeStmt; }
        public boolean getOwned()        { return owned; }
        public String  getExtraInclude() { return extraInclude; }
    }

    /** Pair of C type + the header providing it (or {@code null} for built-ins). */
    private static final class TypeBinding
    {
        final String cType;
        final String extraInclude;

        TypeBinding(String cType, String extraInclude)
        {
            this.cType = cType;
            this.extraInclude = extraInclude;
        }
    }
}
