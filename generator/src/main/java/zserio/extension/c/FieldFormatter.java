/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Fabian Klebert
 */

package zserio.extension.c;

import java.math.BigInteger;

import zserio.ast.ArrayInstantiation;
import zserio.ast.BitmaskType;
import zserio.ast.BooleanType;
import zserio.ast.BytesType;
import zserio.ast.CompoundType;
import zserio.ast.EnumType;
import zserio.ast.Expression;
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
        if (field.isOptional() && field.getOptionalClauseExpr() != null)
        {
            throw new ZserioExtensionException(
                    "zserio-c: field '" + field.getName()
                            + "' has an `optional ... if` clause; only auto-optionals are supported in v1");
        }

        final FieldData base = formatBase(field, holderExpr);
        if (field.isOptional())
        {
            return wrapAutoOptional(base, holderExpr);
        }
        return base;
    }

    /** The non-optional core lowering. */
    private static FieldData formatBase(Field field, String holderExpr) throws ZserioExtensionException
    {
        final String name = CIdentifierFormatter.toLowerSnake(field.getName());
        final TypeInstantiation typeInst = field.getTypeInstantiation();

        if (typeInst instanceof ArrayInstantiation)
        {
            return arrayField(name, (ArrayInstantiation)typeInst, holderExpr, field.getName());
        }

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

    /**
     * Wrap a non-optional {@link FieldData} so that read / write are
     * gated by a 1-bit presence flag and free runs only when present.
     * Adds a {@code bool <name>_present;} sibling member so callers
     * can query whether the field is set.
     */
    private static FieldData wrapAutoOptional(FieldData inner, String h)
    {
        final String present = h + inner.name + "_present";
        final String read =
                "{\n"
                + "        uint64_t " + inner.name + "_presence = 0u;\n"
                + "        ZSERIO_TRY(zserio_bitstream_read_bits(bs, 1, &" + inner.name + "_presence));\n"
                + "        " + present + " = (" + inner.name + "_presence != 0u);\n"
                + "        if (" + present + ") {\n"
                + "            " + inner.readStmt + "\n"
                + "        }\n"
                + "    }";
        final String write =
                "if (" + present + ") {\n"
                + "        ZSERIO_TRY(zserio_bitstream_write_bits(bs, 1, 1u));\n"
                + "        " + inner.writeStmt + "\n"
                + "    } else {\n"
                + "        ZSERIO_TRY(zserio_bitstream_write_bits(bs, 1, 0u));\n"
                + "    }";
        // Free: only worth gating the inner free if it actually frees
        // anything. For primitive optional fields the inner free is
        // empty, so the outer free degrades to no-op too.
        final String free = inner.freeStmt.isEmpty() ? ""
                : "if (" + present + ") {\n"
                + "        " + inner.freeStmt + "\n"
                + "    }";
        // Stack the presence flag in front of any extraMember the inner
        // had (e.g. an array's count member): the outer presence flag
        // gates the whole storage, count and value alike.
        final String combinedExtra = "bool " + inner.name + "_present;"
                + (inner.extraMember.isEmpty() ? "" : "\n    " + inner.extraMember);
        return new FieldData(
                inner.name, inner.nativeType, inner.declaration,
                read, write, free,
                inner.owned, inner.extraInclude,
                combinedExtra);
    }

    /* -------- arrays -------- */

    /**
     * Lower an array-typed field.
     *
     * <ul>
     *   <li>variable-length ({@code T[]}): a varsize length prefix on
     *       the wire; storage is {@code size_t <name>_count;
     *       <eltType> *<name>;} with allocator ownership of the buffer.
     *   <li>fixed-length ({@code T[N]} where N is a compile-time
     *       integer): no length prefix; storage is a C array
     *       {@code <eltType> <name>[N];}.
     * </ul>
     *
     * Element types: integer primitives, {@code bool}, {@code float*}
     * (passed through the matching {@code zserio_read_*} helper);
     * {@code string} / {@code bytes} / {@code extern} (allocator-owned,
     * per-element free); non-parameterised compound types (call the
     * child's {@code _read} / {@code _write} / {@code _free}).
     *
     * Out of scope: implicit arrays ({@code implicit T[]}), packed
     * arrays ({@code packed T[]}), parameterised compound elements,
     * non-literal length expressions, alignment / offset annotations.
     */
    private static FieldData arrayField(String name, ArrayInstantiation ai, String h, String origName)
            throws ZserioExtensionException
    {
        if (ai.isImplicit())
        {
            throw new ZserioExtensionException(
                    "zserio-c: implicit arrays are not yet supported (field '" + origName + "')");
        }
        if (ai.isPacked())
        {
            throw new ZserioExtensionException(
                    "zserio-c: packed arrays are not yet supported (field '" + origName + "')");
        }

        final TypeInstantiation eltInst = ai.getElementTypeInstantiation();
        final ZserioType eltDirect = eltInst.getTypeReference().getType();
        final TypeBinding eltBinding = bindType(eltDirect);
        final ElementOps ops = elementOps(eltDirect, eltInst, eltBinding, origName);

        final Expression lenExpr = ai.getLengthExpression();
        if (lenExpr == null)
        {
            return variableArray(name, ops, h);
        }
        final BigInteger fixedLen = lenExpr.getIntegerValue();
        if (fixedLen == null)
        {
            throw new ZserioExtensionException(
                    "zserio-c: fixed-length array bound for '" + origName
                            + "' must be a compile-time integer in v1");
        }
        return fixedArray(name, ops, fixedLen.toString(), h);
    }

    /**
     * Per-element runtime ops for an array. Each line is a template
     * with {@code $SLOT$} substituted with the actual slot expression
     * ({@code <ptr>[<i>]} for variable, {@code <name>[<i>]} for fixed)
     * by the array-shape helpers below.
     */
    private static ElementOps elementOps(ZserioType direct, TypeInstantiation typeInst,
            TypeBinding b, String origName) throws ZserioExtensionException
    {
        // Non-parameterised compound element: call the child's
        // _read / _write / _free directly. Parameterised elements need
        // a per-slot set_parameters call we don't generate yet.
        if (direct instanceof CompoundType)
        {
            final CompoundType ct = (CompoundType)direct;
            if (!ct.getTypeParameters().isEmpty()
                    || typeInst instanceof ParameterizedTypeInstantiation)
            {
                throw new ZserioExtensionException(
                        "zserio-c: parameterised compound array elements are not yet supported "
                                + "(field '" + origName + "')");
            }
            final String prefix = CIdentifierFormatter.funcPrefix(
                    ct.getPackage().getPackageName(), ct.getName());
            return new ElementOps(b.cType, b.extraInclude, /* owned */ true,
                    "ZSERIO_TRY(" + prefix + "_read(&$SLOT$, bs, alloc));",
                    "ZSERIO_TRY(" + prefix + "_write(&$SLOT$, bs));",
                    prefix + "_free(&$SLOT$, alloc);");
        }

        final ZserioType under = underlyingType(direct);

        if (under instanceof StringType)
        {
            final String write =
                    "{\n"
                    + "            const zserio_string_view_t slot_view = "
                    + "{ $SLOT$.bytes, $SLOT$.length };\n"
                    + "            ZSERIO_TRY(zserio_write_string(bs, slot_view));\n"
                    + "        }";
            return new ElementOps("zserio_string_t", "zserio/string.h", /* owned */ true,
                    "ZSERIO_TRY(zserio_read_string(bs, alloc, &$SLOT$));",
                    write,
                    "zserio_string_free(&$SLOT$, alloc);");
        }
        if (under instanceof BytesType)
        {
            final String write =
                    "{\n"
                    + "            const zserio_bytes_view_t slot_view = "
                    + "{ $SLOT$.bytes, $SLOT$.length };\n"
                    + "            ZSERIO_TRY(zserio_write_bytes(bs, slot_view));\n"
                    + "        }";
            return new ElementOps("zserio_bytes_t", "zserio/bytes.h", /* owned */ true,
                    "ZSERIO_TRY(zserio_read_bytes(bs, alloc, &$SLOT$));",
                    write,
                    "zserio_bytes_free(&$SLOT$, alloc);");
        }
        if (under instanceof ExternType)
        {
            final String write =
                    "{\n"
                    + "            const zserio_bit_view_t slot_view = "
                    + "{ $SLOT$.bytes, 0u, $SLOT$.bit_length };\n"
                    + "            ZSERIO_TRY(zserio_write_extern(bs, slot_view));\n"
                    + "        }";
            return new ElementOps("zserio_extern_t", "zserio/extern.h", /* owned */ true,
                    "ZSERIO_TRY(zserio_read_extern(bs, alloc, &$SLOT$));",
                    write,
                    "zserio_extern_free(&$SLOT$, alloc);");
        }

        // Primitive (or subtype/enum/bitmask layered over a primitive)
        final String suffix;
        if (under instanceof BooleanType)
        {
            suffix = "bool";
        }
        else if (under instanceof StdIntegerType)
        {
            final StdIntegerType std = (StdIntegerType)under;
            suffix = intSuffix(std.getBitSize(), std.isSigned());
        }
        else if (under instanceof FloatType)
        {
            suffix = "float" + ((FloatType)under).getBitSize();
        }
        else
        {
            throw new ZserioExtensionException(
                    "zserio-c: array element type "
                            + under.getClass().getSimpleName()
                            + " is not yet supported (field '" + origName + "')");
        }
        return new ElementOps(b.cType, b.extraInclude, /* owned */ false,
                "ZSERIO_TRY(zserio_read_" + suffix + "(bs, &$SLOT$));",
                "ZSERIO_TRY(zserio_write_" + suffix + "(bs, $SLOT$));",
                /* freeLine */ null);
    }

    private static FieldData variableArray(String name, ElementOps ops, String h)
    {
        // Storage: count + heap pointer. The count member sits before
        // the value member so structs read in the order schemas read.
        final String countExpr = h + name + "_count";
        final String ptrExpr   = h + name;
        final String iVar      = name + "_i";
        final String slotExpr  = ptrExpr + "[" + iVar + "]";
        final String declaration = ops.cType + " *" + name;
        final String extraMember = "size_t " + name + "_count;";

        final String elementRead  = ops.readLine.replace("$SLOT$", slotExpr);
        final String elementWrite = ops.writeLine.replace("$SLOT$", slotExpr);

        // Owned-element variable arrays must zero the freshly-allocated
        // buffer so a partial-fill failure leaves the unread slots in
        // a state where the runtime _free is a no-op (NULL bytes / 0
        // length / zero-init compound members). Primitive elements
        // don't need the memset — the read fully overwrites each slot
        // and there's no per-element free that could trip on garbage.
        final String memsetLine = ops.owned
                ? "            (void)memset(" + ptrExpr + ", 0, "
                        + countExpr + " * sizeof(*(" + ptrExpr + ")));\n"
                : "";

        final String read =
                "{\n"
                + "        uint32_t " + name + "_size = 0u;\n"
                + "        ZSERIO_TRY(zserio_read_varsize(bs, &" + name + "_size));\n"
                + "        " + countExpr + " = (size_t)" + name + "_size;\n"
                + "        " + ptrExpr   + " = NULL;\n"
                + "        if (" + countExpr + " > 0u) {\n"
                + "            " + ptrExpr + " = (" + ops.cType + " *)alloc->vtable->alloc(\n"
                + "                    alloc->userdata,\n"
                + "                    " + countExpr + " * sizeof(*(" + ptrExpr + ")),\n"
                + "                    sizeof(*(" + ptrExpr + ")));\n"
                + "            if (" + ptrExpr + " == NULL) {\n"
                + "                return ZSERIO_ERR_OUT_OF_MEMORY;\n"
                + "            }\n"
                + memsetLine
                + "            for (size_t " + iVar + " = 0u; " + iVar + " < "
                + countExpr + "; ++" + iVar + ") {\n"
                + "                " + elementRead + "\n"
                + "            }\n"
                + "        }\n"
                + "    }";
        final String write =
                "{\n"
                + "        ZSERIO_TRY(zserio_write_varsize(bs, (uint32_t)" + countExpr + "));\n"
                + "        for (size_t " + iVar + " = 0u; " + iVar + " < "
                + countExpr + "; ++" + iVar + ") {\n"
                + "            " + elementWrite + "\n"
                + "        }\n"
                + "    }";

        // Free: per-element free first (if needed), then the buffer.
        // The element-free loop runs unconditionally on every slot;
        // the runtime _free helpers are no-ops on zero-init storage.
        final String elementFreeLoop;
        if (ops.freeLine != null)
        {
            final String elementFree = ops.freeLine.replace("$SLOT$", slotExpr);
            elementFreeLoop =
                    "        for (size_t " + iVar + " = 0u; " + iVar + " < "
                    + countExpr + "; ++" + iVar + ") {\n"
                    + "            " + elementFree + "\n"
                    + "        }\n";
        }
        else
        {
            elementFreeLoop = "";
        }
        final String free =
                "if (" + ptrExpr + " != NULL) {\n"
                + elementFreeLoop
                + "        alloc->vtable->free(alloc->userdata, " + ptrExpr + ",\n"
                + "                " + countExpr + " * sizeof(*(" + ptrExpr + ")),\n"
                + "                sizeof(*(" + ptrExpr + ")));\n"
                + "        " + ptrExpr + " = NULL;\n"
                + "        " + countExpr + " = 0u;\n"
                + "    }";

        return new FieldData(
                name, ops.cType + " *", declaration,
                read, write, free,
                /* owned */ true, ops.extraInclude, extraMember);
    }

    private static FieldData fixedArray(String name, ElementOps ops,
            String lengthLiteral, String h)
    {
        final String declaration = ops.cType + " " + name + "[" + lengthLiteral + "]";
        final String iVar     = name + "_i";
        final String slotExpr = h + name + "[" + iVar + "]";

        final String elementRead  = ops.readLine.replace("$SLOT$", slotExpr);
        final String elementWrite = ops.writeLine.replace("$SLOT$", slotExpr);

        final String read =
                "{\n"
                + "        for (size_t " + iVar + " = 0u; " + iVar + " < "
                + lengthLiteral + "u; ++" + iVar + ") {\n"
                + "            " + elementRead + "\n"
                + "        }\n"
                + "    }";
        final String write =
                "{\n"
                + "        for (size_t " + iVar + " = 0u; " + iVar + " < "
                + lengthLiteral + "u; ++" + iVar + ") {\n"
                + "            " + elementWrite + "\n"
                + "        }\n"
                + "    }";

        // Fixed-array storage lives inline in the parent struct, so
        // there's no buffer to free — only per-element free for owned
        // element types. Element 0..N-1 are all initialized (parent
        // _init zeroed them), so the runtime _free helper is safe even
        // on partially-read state.
        final String free;
        if (ops.freeLine != null)
        {
            final String elementFree = ops.freeLine.replace("$SLOT$", slotExpr);
            free =
                    "for (size_t " + iVar + " = 0u; " + iVar + " < "
                    + lengthLiteral + "u; ++" + iVar + ") {\n"
                    + "        " + elementFree + "\n"
                    + "    }";
        }
        else
        {
            free = "";
        }

        return new FieldData(
                name, ops.cType, declaration,
                read, write, free,
                /* owned */ ops.freeLine != null, ops.extraInclude, /* extraMember */ "");
    }

    /** Per-element ops descriptor used by both variable and fixed arrays. */
    private static final class ElementOps
    {
        final String cType;
        final String extraInclude;
        final boolean owned;     // true → memset alloc + per-element free loop
        final String readLine;   // template with $SLOT$
        final String writeLine;
        final String freeLine;   // null = no per-element free needed

        ElementOps(String cType, String extraInclude, boolean owned,
                String readLine, String writeLine, String freeLine)
        {
            this.cType = cType;
            this.extraInclude = extraInclude;
            this.owned = owned;
            this.readLine = readLine;
            this.writeLine = writeLine;
            this.freeLine = freeLine;
        }
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
        private final String declaration;
        private final String readStmt;
        private final String writeStmt;
        private final String freeStmt;
        private final boolean owned;
        private final String extraInclude;
        private final String extraMember;

        /** For non-array fields where the C declaration is just
         *  {@code <nativeType> <name>}. */
        FieldData(String name, String nativeType, String readStmt, String writeStmt,
                String freeStmt, boolean owned, String extraInclude)
        {
            this(name, nativeType, nativeType + " " + name,
                    readStmt, writeStmt, freeStmt, owned, extraInclude, "");
        }

        /** Same as the simple constructor, plus an {@code extraMember}. */
        FieldData(String name, String nativeType, String readStmt, String writeStmt,
                String freeStmt, boolean owned, String extraInclude, String extraMember)
        {
            this(name, nativeType, nativeType + " " + name,
                    readStmt, writeStmt, freeStmt, owned, extraInclude, extraMember);
        }

        /** Full constructor — used for arrays where the declaration is
         *  {@code <eltType> <name>[<N>]} or {@code <eltType> *<name>}. */
        FieldData(String name, String nativeType, String declaration,
                String readStmt, String writeStmt, String freeStmt,
                boolean owned, String extraInclude, String extraMember)
        {
            this.name = name;
            this.nativeType = nativeType;
            this.declaration = declaration;
            this.readStmt = readStmt;
            this.writeStmt = writeStmt;
            this.freeStmt = freeStmt;
            this.owned = owned;
            this.extraInclude = extraInclude;
            this.extraMember = extraMember;
        }

        public String  getName()           { return name; }
        public String  getNativeType()     { return nativeType; }
        public String  getDeclaration()    { return declaration; }
        public String  getReadStmt()       { return readStmt; }
        public String  getWriteStmt()      { return writeStmt; }
        public String  getFreeStmt()       { return freeStmt; }
        public boolean getOwned()          { return owned; }
        public String  getExtraInclude()   { return extraInclude; }
        public String  getExtraMember()    { return extraMember; }
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
