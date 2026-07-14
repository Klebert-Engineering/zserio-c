/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Klebert Engineering
 */

package zserio.extension.c;

import zserio.ast.StructureType;
import zserio.extension.common.OutputFileManager;
import zserio.extension.common.ZserioExtensionException;

/**
 * Emits one header + one source per zserio {@code struct} declaration.
 *
 * The header carries the C struct typedef and the
 * {@code init / read / write / free} prototypes; the source carries
 * the implementations that wire up to the runtime read/write helpers.
 *
 * Phase 2 batch 3 supports fields whose type is one of: integer
 * primitive, {@code bool}, {@code float*}, {@code string}. Compound
 * field types (bit fields, varints, enums, bitmasks, nested structs,
 * arrays, optionals, parameterised types) raise a clear error in
 * {@link FieldFormatter} until their support lands.
 */
public final class StructureEmitter extends CDefaultEmitter
{
    private static final String HEADER_TEMPLATE = "Structure.h.ftl";
    private static final String SOURCE_TEMPLATE = "Structure.c.ftl";

    public StructureEmitter(OutputFileManager outputFileManager,
            CExtensionParameters cParameters, TemplateDataContext context)
    {
        super(outputFileManager, cParameters, context);
    }

    @Override
    public void beginStructure(StructureType structType) throws ZserioExtensionException
    {
        final StructureEmitterTemplateData data =
                new StructureEmitterTemplateData(context, structType);
        processHeaderTemplate(HEADER_TEMPLATE, data,
                structType.getPackage().getPackageName(), structType.getName());
        processSourceTemplate(SOURCE_TEMPLATE, data,
                structType.getPackage().getPackageName(), structType.getName());
    }
}
