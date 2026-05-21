/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Klebert Engineering
 */

#ifndef ZSERIO_ZSERIO_H_
#define ZSERIO_ZSERIO_H_

/**
 * @file
 * Umbrella include for the zserio-c runtime. Generated code emits
 * `#include <zserio/zserio.h>` and gets the full public API in one
 * pass; hand-written code can include the more specific headers
 * directly.
 *
 * The public API surface is fixed by
 * `docs/upstream-analysis/02-wire-format.md` §8 and
 * `04-cpp-runtime-internals.md` §4.
 */

#include "zserio/allocator.h"
#include "zserio/bitstream.h"
#include "zserio/bytes.h"
#include "zserio/delta.h"
#include "zserio/expressions.h"
#include "zserio/extern.h"
#include "zserio/float.h"
#include "zserio/primitives.h"
#include "zserio/status.h"
#include "zserio/string.h"
#include "zserio/varint.h"

#endif /* ZSERIO_ZSERIO_H_ */
