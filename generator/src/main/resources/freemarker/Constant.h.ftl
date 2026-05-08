<#include "FileHeader.inc.ftl">
<@file_header generatorDescription/>

#ifndef ${includeGuard}
#define ${includeGuard}

#include <stdbool.h>
#include <stdint.h>

static const ${nativeType} ${cName} = (${nativeType})${valueExpression};

#endif /* ${includeGuard} */
