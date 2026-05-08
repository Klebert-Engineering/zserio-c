/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Fabian Klebert
 */

package zserio.extension.c;

import zserio.ast.BooleanType;
import zserio.ast.Field;
import zserio.ast.FloatType;
import zserio.ast.StdIntegerType;
import zserio.ast.StringType;
import zserio.ast.Subtype;
import zserio.ast.TypeReference;
import zserio.ast.ZserioType;
import zserio.extension.common.ZserioExtensionException;

/**
 * Lowers one zserio struct {@link Field} to its C99 representation:
 * the C member type plus the per-field read / write / free statements
 * that go into the generated {@code _read} / {@code _write} / {@code _free}
 * function bodies.
 *
 * Phase 2 batch 3 scope: primitive integers, {@code bool}, {@code float*},
 * and {@code string}. Compound categories — {@code bit:N}, {@code varint*},
 * {@code bytes}, {@code extern}, enums / bitmasks / subtypes / nested
 * structs as fields, arrays, optionals, parameterised types — land in
 * subsequent batches and currently raise {@link ZserioExtensionException}.
 */
public final class FieldFormatter
{
    private FieldFormatter()
    {
        // utility class
    }

    /** Lower one field. */
    public static FieldData format(Field field) throws ZserioExtensionException
    {
        final String name = CIdentifierFormatter.toLowerSnake(field.getName());
        final TypeReference ref = field.getTypeInstantiation().getTypeReference();
        final ZserioType base = resolveSubtype(ref);

        if (base instanceof BooleanType)
        {
            return scalar(name, "bool", "bool");
        }
        if (base instanceof StdIntegerType)
        {
            final StdIntegerType std = (StdIntegerType)base;
            final String suffix = (std.isSigned() ? "int" : "uint") + std.getBitSize();
            return scalar(name, suffix + "_t", suffix);
        }
        if (base instanceof FloatType)
        {
            final FloatType ft = (FloatType)base;
            final String nativeType = ft.getBitSize() == 64 ? "double" : "float";
            return scalar(name, nativeType, "float" + ft.getBitSize());
        }
        if (base instanceof StringType)
        {
            return string(name);
        }
        throw new ZserioExtensionException(
                "zserio-c: struct field '" + field.getName() + "': "
                        + base.getClass().getSimpleName() + " is not yet supported as a field type");
    }

    private static ZserioType resolveSubtype(TypeReference ref)
    {
        ZserioType resolved = ref.getBaseTypeReference().getType();
        while (resolved instanceof Subtype)
        {
            resolved = ((Subtype)resolved).getTypeReference().getBaseTypeReference().getType();
        }
        return resolved;
    }

    /** Build a `FieldData` for a fixed-width primitive read/write pair. */
    private static FieldData scalar(String name, String nativeType, String runtimeSuffix)
    {
        final String read  = "ZSERIO_TRY(zserio_read_"  + runtimeSuffix + "(bs, &self->" + name + "));";
        final String write = "ZSERIO_TRY(zserio_write_" + runtimeSuffix + "(bs, self->"  + name + "));";
        return new FieldData(name, nativeType, read, write, "", false);
    }

    private static FieldData string(String name)
    {
        // The runtime owns the buffer, so write needs a view; we
        // construct one inline rather than via a runtime helper to
        // keep the generated code dependency-light.
        final String read = "ZSERIO_TRY(zserio_read_string(bs, alloc, &self->" + name + "));";
        final String write =
                "{\n"
                + "        const zserio_string_view_t " + name + "_view = "
                + "{ self->" + name + ".bytes, self->" + name + ".length };\n"
                + "        ZSERIO_TRY(zserio_write_string(bs, " + name + "_view));\n"
                + "    }";
        final String free = "zserio_string_free(&self->" + name + ", alloc);";
        return new FieldData(name, "zserio_string_t", read, write, free, true);
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

        FieldData(String name, String nativeType, String readStmt, String writeStmt,
                String freeStmt, boolean owned)
        {
            this.name = name;
            this.nativeType = nativeType;
            this.readStmt = readStmt;
            this.writeStmt = writeStmt;
            this.freeStmt = freeStmt;
            this.owned = owned;
        }

        public String  getName()      { return name; }
        public String  getNativeType()     { return nativeType; }
        public String  getReadStmt()  { return readStmt; }
        public String  getWriteStmt() { return writeStmt; }
        public String  getFreeStmt()  { return freeStmt; }
        public boolean getOwned()     { return owned; }
    }
}
