/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Fabian Klebert
 */

package zserio.extension.c;

import java.io.File;

import zserio.ast.PackageName;
import zserio.extension.common.DefaultTreeWalker;
import zserio.extension.common.FreeMarkerUtil;
import zserio.extension.common.OutputFileManager;
import zserio.extension.common.ZserioExtensionException;

/**
 * Base class for every per-construct emitter.
 *
 * Threads the {@link OutputFileManager}, {@link CExtensionParameters},
 * and {@link TemplateDataContext} through to subclasses, and provides
 * a single {@link #processHeaderTemplate} helper that mirrors
 * upstream's {@code CppDefaultEmitter.processHeaderTemplate}: render
 * a FreeMarker template against a data POJO into a path under the
 * configured output directory, then register the file with the
 * output-file manager.
 */
public abstract class CDefaultEmitter extends DefaultTreeWalker
{
    private static final String HEADER_EXTENSION = ".h";
    private static final String SOURCE_EXTENSION = ".c";

    protected final OutputFileManager outputFileManager;
    protected final CExtensionParameters cParameters;
    protected final TemplateDataContext context;

    protected CDefaultEmitter(OutputFileManager outputFileManager,
            CExtensionParameters cParameters, TemplateDataContext context)
    {
        this.outputFileManager = outputFileManager;
        this.cParameters = cParameters;
        this.context = context;
    }

    @Override
    public boolean traverseTemplateInstantiations()
    {
        return true;
    }

    /**
     * Render {@code templateName} against {@code templateData} and
     * write the result to a {@code .h} file under
     * {@code <output>/<package>/<entity>.h}.
     */
    protected final void processHeaderTemplate(String templateName, Object templateData,
            PackageName pkg, String entityName) throws ZserioExtensionException
    {
        processTemplate(templateName, templateData, pkg, entityName, HEADER_EXTENSION);
    }

    /** Same as {@link #processHeaderTemplate} but for a {@code .c} file. */
    protected final void processSourceTemplate(String templateName, Object templateData,
            PackageName pkg, String entityName) throws ZserioExtensionException
    {
        processTemplate(templateName, templateData, pkg, entityName, SOURCE_EXTENSION);
    }

    private void processTemplate(String templateName, Object templateData,
            PackageName pkg, String entityName, String extension) throws ZserioExtensionException
    {
        final File outDir = new File(cParameters.getOutputDir(), pkg.toFilesystemPath());
        final String fileName = CIdentifierFormatter.toLowerSnake(entityName) + extension;
        final File outFile = new File(outDir, fileName);
        FreeMarkerUtil.processTemplate(templateName, templateData, outFile, getClass());
        outputFileManager.registerOutputFile(outFile);
    }
}
