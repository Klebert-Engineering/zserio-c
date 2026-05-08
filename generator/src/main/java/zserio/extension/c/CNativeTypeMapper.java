/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Fabian Klebert
 */

package zserio.extension.c;

import zserio.ast.BooleanType;
import zserio.ast.DynamicBitFieldType;
import zserio.ast.FixedBitFieldType;
import zserio.ast.FloatType;
import zserio.ast.StdIntegerType;
import zserio.ast.StringType;
import zserio.ast.Subtype;
import zserio.ast.TypeInstantiation;
import zserio.ast.TypeReference;
import zserio.ast.VarIntegerType;
import zserio.ast.ZserioType;
import zserio.extension.common.ZserioExtensionException;

/**
 * Maps zserio scalar types to their C99 native type names.
 *
 * Scope for the Phase 2 batch 1: built-in scalar types only (the
 * subset used by {@code const} declarations). Compound types
 * (struct / choice / union / enum / bitmask / subtype / arrays /
 * string / bytes / extern) land here as the corresponding emitters
 * are added.
 */
public final class CNativeTypeMapper
{
    private CNativeTypeMapper()
    {
        // utility class
    }

    /**
     * Returns the C type name for a zserio {@link TypeInstantiation}.
     * For built-in scalars this is the {@code <stdint.h>} typedef
     * (e.g. {@code uint32_t}); for {@code bit:N} / {@code int:N}
     * the smallest fitting fixed-width type.
     *
     * @throws ZserioExtensionException for type categories not yet
     *         supported by Phase 2 batch 1 (compound types, arrays,
     *         strings, etc.).
     */
    public static String nativeType(TypeInstantiation typeInst) throws ZserioExtensionException
    {
        return nativeType(typeInst.getTypeReference());
    }

    /** Same as {@link #nativeType(TypeInstantiation)} but starting
     *  from a {@link TypeReference}. Used by {@code Subtype}, which
     *  carries a reference rather than a full instantiation. */
    public static String nativeType(TypeReference ref) throws ZserioExtensionException
    {
        final ZserioType base = ref.getBaseTypeReference().getType();
        // Walk through subtype chains so the C output unwraps as
        // far as the underlying built-in.
        ZserioType resolved = base;
        while (resolved instanceof Subtype)
        {
            final Subtype st = (Subtype)resolved;
            resolved = st.getTypeReference().getBaseTypeReference().getType();
        }
        return nativeTypeFor(resolved);
    }

    /** Map a resolved (subtype-stripped) {@link ZserioType} to its C
     *  native name. Visible for {@link FieldFormatter} which holds
     *  the built-in directly rather than via a {@link TypeReference}. */
    static String nativeTypeFor(ZserioType type) throws ZserioExtensionException
    {
        if (type instanceof BooleanType)
        {
            return "bool";
        }
        if (type instanceof StdIntegerType)
        {
            final StdIntegerType std = (StdIntegerType)type;
            return integerTypeFor(std.getBitSize(), std.isSigned());
        }
        if (type instanceof FixedBitFieldType)
        {
            final FixedBitFieldType bit = (FixedBitFieldType)type;
            return integerTypeFor(bit.getBitSize(), bit.isSigned());
        }
        if (type instanceof DynamicBitFieldType)
        {
            // Dynamic-width bit fields don't have a known compile-time
            // width; the encoded magnitude fits in 64 bits, so the
            // widest fixed-width type covers all cases.
            final DynamicBitFieldType dyn = (DynamicBitFieldType)type;
            return integerTypeFor(64, dyn.isSigned());
        }
        if (type instanceof VarIntegerType)
        {
            final VarIntegerType vi = (VarIntegerType)type;
            return integerTypeFor(vi.getMaxBitSize(), vi.isSigned());
        }
        if (type instanceof FloatType)
        {
            final FloatType ft = (FloatType)type;
            return ft.getBitSize() == 64 ? "double" : "float";
        }
        if (type instanceof StringType)
        {
            // string consts don't fit a `static const T name = value;`
            // pattern cleanly; reject for now and revisit when we have
            // the string-const lowering pattern designed.
            throw new ZserioExtensionException(
                    "zserio-c: string constants are not yet supported");
        }
        throw new ZserioExtensionException(
                "zserio-c: native type mapping not implemented for "
                        + type.getClass().getSimpleName());
    }

    private static String integerTypeFor(int bitSize, boolean signed) throws ZserioExtensionException
    {
        final String prefix = signed ? "int" : "uint";
        final int container;
        if (bitSize <= 8)
        {
            container = 8;
        }
        else if (bitSize <= 16)
        {
            container = 16;
        }
        else if (bitSize <= 32)
        {
            container = 32;
        }
        else if (bitSize <= 64)
        {
            container = 64;
        }
        else
        {
            throw new ZserioExtensionException(
                    "zserio-c: integer width " + bitSize + " exceeds 64 bits");
        }
        return prefix + container + "_t";
    }
}
