<#include "FileHeader.inc.ftl">
<@file_header generatorDescription/>

#ifndef ${includeGuard}
#define ${includeGuard}

#include <stdbool.h>
#include <stdint.h>

typedef ${nativeType} ${typeName};

<#list items as item>
static const ${typeName} ${item.name} = (${typeName})${item.value};
</#list>

#endif /* ${includeGuard} */
