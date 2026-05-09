<#include "FileHeader.inc.ftl">
<@file_header generatorDescription/>

#ifndef ${includeGuard}
#define ${includeGuard}

#include <stdbool.h>
#include <stdint.h>
<#if hasExtraInclude>
#include "${extraInclude}"
</#if>

typedef ${nativeType} ${typeName};

#endif /* ${includeGuard} */
