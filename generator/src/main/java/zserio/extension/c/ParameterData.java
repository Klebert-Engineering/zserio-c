/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Fabian Klebert
 */

package zserio.extension.c;

import java.util.ArrayList;
import java.util.List;

import zserio.ast.BitmaskType;
import zserio.ast.BytesType;
import zserio.ast.CompoundType;
import zserio.ast.EnumType;
import zserio.ast.ExternType;
import zserio.ast.InstantiateType;
import zserio.ast.PackageName;
import zserio.ast.Parameter;
import zserio.ast.StringType;
import zserio.ast.Subtype;
import zserio.ast.TypeReference;
import zserio.ast.ZserioType;
import zserio.extension.common.ZserioExtensionException;

/**
 * Lowering of one zserio compound-type {@link Parameter} to C.
 *
 * Each parameter becomes a struct member (so reads / writes / free can
 * see the value the caller pinned via {@code _set_parameters}) and a
 * parameter on the generated {@code _set_parameters} helper.
 *
 * Phase 2 batch 6 scope: parameters whose type maps via the existing
 * built-in / subtype / enum / bitmask paths in
 * {@link CNativeTypeMapper}. Compound parameter types (a parameter
 * whose type is itself a struct / choice) are out of scope.
 */
public final class ParameterData
{
    private final String name;
    private final String nativeType;
    private final String extraInclude;

    public ParameterData(String name, String nativeType, String extraInclude)
    {
        this.name = name;
        this.nativeType = nativeType;
        this.extraInclude = extraInclude;
    }

    public String getName()       { return name; }
    public String getNativeType() { return nativeType; }

    /**
     * Header that defines {@link #getNativeType()}, or {@code null}
     * if the type is a built-in / stdint name. The owning compound's
     * template data folds this into its include set so the generated
     * header forward-declares all parameter types.
     */
    public String getExtraInclude() { return extraInclude; }

    /** Build a {@code List<ParameterData>} from one compound's parameter list. */
    public static List<ParameterData> from(List<Parameter> params) throws ZserioExtensionException
    {
        final List<ParameterData> out = new ArrayList<>();
        for (final Parameter p : params)
        {
            final TypeReference ref = p.getTypeReference();
            final String cType = CNativeTypeMapper.nativeType(ref);
            out.add(new ParameterData(
                    CIdentifierFormatter.toLowerSnake(p.getName()),
                    cType,
                    extraIncludeFor(ref)));
        }
        return out;
    }

    /**
     * Header to {@code #include} when a parameter of the given type
     * is declared, or {@code null} for stdint / runtime built-ins.
     *
     * Walks {@code subtype} / {@code instantiate} layers to find the
     * underlying user-defined type. The C parameter is declared with
     * the underlying type's typedef name (see
     * {@link CNativeTypeMapper#nativeType(TypeReference)}), so the
     * include must point at the underlying type's header.
     */
    private static String extraIncludeFor(TypeReference ref)
    {
        ZserioType resolved = ref.getBaseTypeReference().getType();
        while (resolved instanceof Subtype || resolved instanceof InstantiateType)
        {
            if (resolved instanceof Subtype)
            {
                resolved = ((Subtype)resolved).getTypeReference()
                        .getBaseTypeReference().getType();
            }
            else
            {
                resolved = ((InstantiateType)resolved).getTypeReference()
                        .getBaseTypeReference().getType();
            }
        }
        final PackageName pkg;
        final String name;
        if (resolved instanceof CompoundType)
        {
            final CompoundType ct = (CompoundType)resolved;
            pkg = ct.getPackage().getPackageName();
            name = ct.getName();
        }
        else if (resolved instanceof EnumType)
        {
            final EnumType et = (EnumType)resolved;
            pkg = et.getPackage().getPackageName();
            name = et.getName();
        }
        else if (resolved instanceof BitmaskType)
        {
            final BitmaskType bt = (BitmaskType)resolved;
            pkg = bt.getPackage().getPackageName();
            name = bt.getName();
        }
        else if (resolved instanceof StringType)
        {
            return "zserio/string.h";
        }
        else if (resolved instanceof BytesType)
        {
            return "zserio/bytes.h";
        }
        else if (resolved instanceof ExternType)
        {
            return "zserio/extern.h";
        }
        else
        {
            // Built-in scalar / float: the C type name comes from the
            // runtime headers already pulled in by every generated
            // unit; no extra include needed.
            return null;
        }
        return CIdentifierFormatter.headerIncludePath(pkg, name);
    }
}
