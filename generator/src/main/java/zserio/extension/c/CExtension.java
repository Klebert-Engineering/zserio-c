/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Fabian Klebert
 */

package zserio.extension.c;

import org.apache.commons.cli.Options;

import zserio.ast.Root;
import zserio.extension.common.OutputFileManager;
import zserio.extension.common.ZserioExtensionException;
import zserio.tools.Extension;
import zserio.tools.ExtensionParameters;

/**
 * The zserio-c extension entry point.
 *
 * Phase 2 skeleton: registers the {@code -c <output_dir>} CLI option,
 * declares its expected upstream zserio-core version, and currently
 * no-ops in {@link #check} and {@link #process}. Subsequent phases
 * fill in the per-construct emitters under
 * {@code zserio.extension.c}.
 *
 * The extension is discovered by {@code zserio.tools.ExtensionManager}
 * via {@code java.util.ServiceLoader}; see the resource
 * {@code META-INF/services/zserio.tools.Extension}.
 */
public final class CExtension implements Extension
{
    @Override
    public String getName()
    {
        return "C99 Generator";
    }

    @Override
    public String getZserioVersion()
    {
        return CExtensionVersion.EXPECTED_ZSERIO_VERSION_STRING;
    }

    @Override
    public String getExtensionVersion()
    {
        return CExtensionVersion.C_EXTENSION_VERSION_STRING;
    }

    @Override
    public void registerOptions(Options options)
    {
        CExtensionParameters.registerOptions(options);
    }

    @Override
    public boolean isEnabled(ExtensionParameters parameters)
    {
        return CExtensionParameters.hasOptionC(parameters);
    }

    @Override
    public void check(Root rootNode, ExtensionParameters parameters)
            throws ZserioExtensionException
    {
        // Phase 2 skeleton: no checks yet.
        // Future content (per docs/upstream-analysis/03-plugin-spi.md):
        //   - ReservedKeywordsClashChecker against the C99 keyword set
        //   - CompatibilityChecker (already provided by extension/common)
        //   - any zserio-c-specific schema checks (e.g. forbid features
        //     out of v1 scope)
    }

    @Override
    public void process(Root rootNode, ExtensionParameters parameters)
            throws ZserioExtensionException
    {
        // Per docs/upstream-analysis/03-plugin-spi.md §4: build the
        // output-file manager, instantiate the per-construct emitters,
        // and walk the AST once per emitter. The remaining three
        // (Structure, Choice, Union) join this list as they land.
        final CExtensionParameters cParams = new CExtensionParameters(parameters);
        cParams.ensureOutputDirectoryExists();

        final OutputFileManager files = new OutputFileManager(parameters);
        final TemplateDataContext context = new TemplateDataContext(cParams);

        rootNode.walk(new ConstEmitter      (files, cParams, context));
        rootNode.walk(new SubtypeEmitter    (files, cParams, context));
        rootNode.walk(new EnumerationEmitter(files, cParams, context));
        rootNode.walk(new BitmaskEmitter    (files, cParams, context));

        files.printReport();
    }
}
