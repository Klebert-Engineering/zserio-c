/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Klebert Engineering
 */

package zserio.extension.c;

import zserio.ast.ChoiceType;
import zserio.extension.common.OutputFileManager;
import zserio.extension.common.ZserioExtensionException;

/**
 * Emits one header + one source per zserio {@code choice} declaration.
 *
 * v1 scope: choice with one or more parameters, a selector expression
 * that is a single parameter / field reference, and case labels that
 * are compile-time integer literals. The choice produces:
 *
 * <ul>
 *   <li>a struct with the parameter members and an anonymous inner
 *       union of the case fields (empty cases contribute nothing);</li>
 *   <li>{@code _init(self)} — zero-init;</li>
 *   <li>{@code _set_parameters(self, p0, p1, ...)} — pin the
 *       parameters. The parent compound calls this before
 *       {@code _read} / {@code _write};</li>
 *   <li>{@code _read} / {@code _write} — switch on the selector
 *       expression and dispatch via {@link FieldFormatter} to the
 *       active case's read / write statement. Unknown selectors
 *       return {@code ZSERIO_ERR_INVALID_TAG};</li>
 *   <li>{@code _free} — same dispatch shape, for owned-data cases.</li>
 * </ul>
 *
 * Out of scope until later batches: default case, complex selector /
 * case-label expressions, parameterised case fields, packed-array
 * cases, optionals.
 */
public final class ChoiceEmitter extends CDefaultEmitter
{
    private static final String HEADER_TEMPLATE = "Choice.h.ftl";
    private static final String SOURCE_TEMPLATE = "Choice.c.ftl";

    public ChoiceEmitter(OutputFileManager outputFileManager,
            CExtensionParameters cParameters, TemplateDataContext context)
    {
        super(outputFileManager, cParameters, context);
    }

    @Override
    public void beginChoice(ChoiceType choiceType) throws ZserioExtensionException
    {
        final ChoiceEmitterTemplateData data =
                new ChoiceEmitterTemplateData(context, choiceType);
        processHeaderTemplate(HEADER_TEMPLATE, data,
                choiceType.getPackage().getPackageName(), choiceType.getName());
        processSourceTemplate(SOURCE_TEMPLATE, data,
                choiceType.getPackage().getPackageName(), choiceType.getName());
    }
}
