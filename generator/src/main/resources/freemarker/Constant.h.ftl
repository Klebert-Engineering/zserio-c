<#include "FileHeader.inc.ftl">
<@file_header generatorDescription/>

#ifndef ${includeGuard}
#define ${includeGuard}

#include <stdbool.h>
#include <stdint.h>

static const ${nativeType} ${name} = (${nativeType})${valueExpression};

#endif /* ${includeGuard} */
