/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Klebert Engineering
 */

package zserio.extension.c;

/**
 * Version constants for the zserio-c extension.
 *
 * The {@code EXPECTED_ZSERIO_VERSION_STRING} MUST be kept in sync with
 * the pinned upstream submodule tag at {@code third_party/zserio}; the
 * upstream {@code ExtensionManager} silently ignores extensions whose
 * declared zserio-core version does not match the running core.
 */
public final class CExtensionVersion
{
    /** Version of the zserio-c extension itself. Phase 2 skeleton: 0.1.0. */
    public static final String C_EXTENSION_VERSION_STRING = "0.1.0";

    /** Expected upstream zserio-core version. Mirrors the submodule pin. */
    public static final String EXPECTED_ZSERIO_VERSION_STRING = "2.18.1";

    private CExtensionVersion()
    {
        // utility class
    }
}
