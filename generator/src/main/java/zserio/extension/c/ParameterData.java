/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Fabian Klebert
 */

package zserio.extension.c;

import java.util.ArrayList;
import java.util.List;

import zserio.ast.Parameter;
import zserio.ast.TypeReference;
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

    public ParameterData(String name, String nativeType)
    {
        this.name = name;
        this.nativeType = nativeType;
    }

    public String getName()       { return name; }
    public String getNativeType() { return nativeType; }

    /** Build a {@code List<ParameterData>} from one compound's parameter list. */
    public static List<ParameterData> from(List<Parameter> params) throws ZserioExtensionException
    {
        final List<ParameterData> out = new ArrayList<>();
        for (final Parameter p : params)
        {
            final TypeReference ref = p.getTypeReference();
            final String cType = CNativeTypeMapper.nativeType(ref);
            out.add(new ParameterData(CIdentifierFormatter.toLowerSnake(p.getName()), cType));
        }
        return out;
    }
}
