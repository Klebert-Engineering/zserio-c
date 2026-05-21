/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Klebert Engineering
 */

#ifndef ZSERIO_VERSION_H_
#define ZSERIO_VERSION_H_

/**
 * @file
 * Version constants for zserio-c, exposed for both runtime and
 * build-time consumption.
 *
 * Five compatibility axes per `docs/spec/compatibility.md`
 * (REQ-DM-007):
 *
 * - Release version (zserio-c overall): MAJOR.MINOR.PATCH
 * - Wire format version: MAJOR.MINOR (independent of release)
 * - Schema language version: MAJOR.MINOR (independent of release)
 * - Runtime API version: tracks release MAJOR.MINOR
 * - Generated-code API version: tracks release MAJOR.MINOR
 *
 * The release version follows SemVer 2.0. Pre-1.0 (`0.x.y`)
 * means the runtime and generated-code APIs may change between
 * minor releases; wire format and schema language are stable
 * from day one regardless of release version.
 *
 * These constants are the canonical source for everything that
 * needs to know a zserio-c version. The CI gate
 * `tools/check_version_consistency.py` enforces agreement with
 * the CMake project version and the version lines in
 * `docs/spec/compatibility.md`.
 */

/* Release version of the zserio-c project as a whole. */
#define ZSERIO_C_VERSION_MAJOR 0
#define ZSERIO_C_VERSION_MINOR 1
#define ZSERIO_C_VERSION_PATCH 0
#define ZSERIO_C_VERSION_STRING "0.1.0"

/* Wire format version. Independent of the release version.
 * 1.0 is compatible with upstream Zserio BIN 1.0, byte-equal
 * with upstream zserio v2.18.1 (REQ-FN-001). */
#define ZSERIO_WIRE_FORMAT_VERSION_MAJOR 1
#define ZSERIO_WIRE_FORMAT_VERSION_MINOR 0
#define ZSERIO_WIRE_FORMAT_VERSION_STRING "1.0"

/* Schema language version. Independent of the release version.
 * 1.0 is the subset of upstream Zserio language v2.18.1 that
 * zserio-c accepts (no `service`, `pubsub`, `sql_table`,
 * `sql_database`). See `docs/spec/schema-language.md`. */
#define ZSERIO_SCHEMA_LANGUAGE_VERSION_MAJOR 1
#define ZSERIO_SCHEMA_LANGUAGE_VERSION_MINOR 0
#define ZSERIO_SCHEMA_LANGUAGE_VERSION_STRING "1.0"

/* Upstream pin: the upstream zserio release whose wire format
 * and schema language we currently interoperate with. */
#define ZSERIO_UPSTREAM_PIN_VERSION "v2.18.1"

#endif /* ZSERIO_VERSION_H_ */
