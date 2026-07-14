/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Klebert Engineering
 */

package zserio.extension.c;

/**
 * Cross-cutting context shared by every emitter and template data.
 *
 * Currently holds the generator description string used in file
 * banners. Future content (per
 * {@code docs/upstream-analysis/03-plugin-spi.md} §4) will include
 * the {@code PackedTypesCollector} result and any per-run options
 * derived from {@link CExtensionParameters}.
 */
public final class TemplateDataContext
{
    private final String generatorDescription;

    public TemplateDataContext(CExtensionParameters parameters)
    {
        this.generatorDescription =
                "zserio-c v" + CExtensionVersion.C_EXTENSION_VERSION_STRING
                        + " (zserio-core " + CExtensionVersion.EXPECTED_ZSERIO_VERSION_STRING + ")";
        // parameters reserved for future use (range checks, code
        // comments, amalgamation flag).
        // Suppressed here so javac's -Xlint doesn't warn about the
        // unused field; the reference is meaningful as a contract.
        @SuppressWarnings("unused")
        final CExtensionParameters _params = parameters;
    }

    public String getGeneratorDescription()
    {
        return generatorDescription;
    }
}
