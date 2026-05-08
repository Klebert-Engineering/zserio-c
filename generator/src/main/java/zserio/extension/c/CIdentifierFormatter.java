/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Fabian Klebert
 */

package zserio.extension.c;

import zserio.ast.PackageName;

/**
 * Identifier mapping from zserio's name conventions to C99.
 *
 * Conventions (see {@code docs/upstream-analysis/03-plugin-spi.md} §8.4):
 * <ul>
 *   <li>Type names: {@code lower_snake_case} with package-derived prefix
 *       and a trailing {@code _t}. {@code pkg.foo.MyType} →
 *       {@code pkg_foo_my_type_t}.</li>
 *   <li>Field names: {@code lower_snake_case}. No package prefix
 *       (fields are scoped to their owning struct).</li>
 *   <li>Constant names: {@code UPPER_SNAKE_CASE} with package prefix.
 *       {@code pkg.MaxValue} → {@code pkg_MAX_VALUE}.</li>
 *   <li>Include guards: {@code <UPPER_SNAKE_PATH>_H_}.</li>
 * </ul>
 *
 * The CamelCase-to-snake conversion inserts an underscore before any
 * uppercase letter that follows a lowercase letter, then case-folds.
 * Existing underscores are preserved. Leading uppercase doesn't
 * generate a leading underscore.
 */
public final class CIdentifierFormatter
{
    private CIdentifierFormatter()
    {
        // utility class
    }

    /** Convert an arbitrary identifier to {@code lower_snake_case}. */
    public static String toLowerSnake(String name)
    {
        return convert(name, false);
    }

    /** Convert an arbitrary identifier to {@code UPPER_SNAKE_CASE}. */
    public static String toUpperSnake(String name)
    {
        return convert(name, true);
    }

    /**
     * C type name for a zserio type identified by its package + name.
     * {@code pkg.foo.MyType} → {@code pkg_foo_my_type_t}.
     */
    public static String typeName(PackageName pkg, String typeName)
    {
        final StringBuilder sb = new StringBuilder();
        appendPackagePrefix(sb, pkg);
        sb.append(toLowerSnake(typeName));
        sb.append("_t");
        return sb.toString();
    }

    /**
     * C constant name for a zserio const identified by its package + name.
     * {@code pkg.MaxValue} → {@code pkg_MAX_VALUE}.
     */
    public static String constantName(PackageName pkg, String constName)
    {
        final StringBuilder sb = new StringBuilder();
        appendPackagePrefix(sb, pkg);
        sb.append(toUpperSnake(constName));
        return sb.toString();
    }

    /**
     * Include-guard macro for a header generated from a zserio entity.
     * {@code (pkg.foo, MyType)} → {@code PKG_FOO_MY_TYPE_H_}.
     */
    public static String includeGuard(PackageName pkg, String entityName)
    {
        final StringBuilder sb = new StringBuilder();
        for (final String component : pkg.getIdList())
        {
            sb.append(toUpperSnake(component));
            sb.append('_');
        }
        sb.append(toUpperSnake(entityName));
        sb.append("_H_");
        return sb.toString();
    }

    /**
     * Function-name prefix for an entity that has a typedef plus a
     * family of {@code _init} / {@code _read} / {@code _write} /
     * {@code _free} helpers. {@code (pkg.foo, MyType)} →
     * {@code "pkg_foo_my_type"} (i.e. the typeName minus its
     * trailing {@code _t}).
     */
    public static String funcPrefix(PackageName pkg, String entityName)
    {
        final String tn = typeName(pkg, entityName);
        return tn.substring(0, tn.length() - 2);
    }

    /**
     * The {@code <pkg/path/file.h>} include path for an entity.
     * {@code (pkg.foo, MyType)} → {@code "pkg/foo/my_type.h"}.
     */
    public static String headerIncludePath(PackageName pkg, String entityName)
    {
        final StringBuilder sb = new StringBuilder();
        for (final String component : pkg.getIdList())
        {
            sb.append(toLowerSnake(component));
            sb.append('/');
        }
        sb.append(toLowerSnake(entityName));
        sb.append(".h");
        return sb.toString();
    }

    private static void appendPackagePrefix(StringBuilder sb, PackageName pkg)
    {
        for (final String component : pkg.getIdList())
        {
            sb.append(toLowerSnake(component));
            sb.append('_');
        }
    }

    private static String convert(String name, boolean upper)
    {
        final StringBuilder sb = new StringBuilder(name.length() + 4);
        for (int i = 0; i < name.length(); i++)
        {
            final char c = name.charAt(i);
            if (i > 0 && Character.isUpperCase(c) && Character.isLowerCase(name.charAt(i - 1)))
            {
                sb.append('_');
            }
            sb.append(upper ? Character.toUpperCase(c) : Character.toLowerCase(c));
        }
        return sb.toString();
    }
}
