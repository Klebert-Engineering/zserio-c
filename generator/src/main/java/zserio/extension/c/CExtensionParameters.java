/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Fabian Klebert
 */

package zserio.extension.c;

import java.io.File;

import org.apache.commons.cli.Option;
import org.apache.commons.cli.Options;

import zserio.extension.common.ZserioExtensionException;
import zserio.tools.ExtensionParameters;

/**
 * Command-line parameters for the zserio-c extension.
 *
 * Phase 2 skeleton: only the {@code -c <output_dir>} option is
 * registered, since its presence is what {@link CExtension#isEnabled}
 * keys off. Future options (per
 * {@code docs/upstream-analysis/03-plugin-spi.md} §8.2) will land
 * here as the generator implementation matures:
 *
 * <ul>
 *   <li>{@code -cWithCodeComments} — preserve doc comments in
 *       generated headers (default on)</li>
 *   <li>{@code -cWithSourcesAmalgamation} — emit one C file per
 *       package instead of per-type (default off)</li>
 *   <li>{@code -cWithRangeChecks} — emit runtime range checks for
 *       bit fields (default on; required by REQ-FS-006)</li>
 * </ul>
 *
 * Notably {@bf NOT} planned: {@code -cWithReflection},
 * {@code -cWithJsonCode}, {@code -cWithTypeInfo}. Those are out of
 * v1 scope per {@code 01-schema-language.md}.
 */
public final class CExtensionParameters
{
    private static final String OPTION_C = "c";

    private final String outputDir;

    public CExtensionParameters(ExtensionParameters parameters)
            throws ZserioExtensionException
    {
        check(parameters);
        this.outputDir = parameters.getCommandLineArg(OPTION_C);
    }

    public String getOutputDir()
    {
        return outputDir;
    }

    /**
     * Make sure {@link #outputDir} exists on disk; create it if not.
     * Used by the Phase 2 skeleton's no-op {@code process} to leave a
     * trace that the extension was invoked. Will be replaced by the
     * generator's {@code OutputFileManager} once real emission lands.
     */
    public void ensureOutputDirectoryExists() throws ZserioExtensionException
    {
        if (outputDir == null)
        {
            throw new ZserioExtensionException(
                    "zserio-c: missing required option -" + OPTION_C);
        }
        final File dir = new File(outputDir);
        if (!dir.exists() && !dir.mkdirs())
        {
            throw new ZserioExtensionException(
                    "zserio-c: failed to create output directory " + outputDir);
        }
    }

    static void registerOptions(Options options)
    {
        final Option option = new Option(OPTION_C, true,
                "generate C99 sources into the given output directory");
        option.setArgName("outputDir");
        option.setRequired(false);
        options.addOption(option);
    }

    static boolean hasOptionC(ExtensionParameters parameters)
    {
        return parameters.argumentExists(OPTION_C);
    }

    private static void check(ExtensionParameters parameters)
            throws ZserioExtensionException
    {
        if (!hasOptionC(parameters))
        {
            throw new ZserioExtensionException(
                    "zserio-c: option -" + OPTION_C + " is required");
        }
    }
}
