#ifndef FAF_STRING_CONCAT_H
#define FAF_STRING_CONCAT_H

#include "faf_string.h"
#include "faf_string_mem.h"


// Concatonate `str2` onto the end of `str1`.
// Requires allocating a new string.
//
// Runtime Complexity; O(n + m) where n=strlen(str1) and m=strlen(str2)
// Memory Complexity: O(n + m)
faf_string* faf_string_concat(pool_t pool, faf_string* str1, faf_string* str2);

#endif // FAF_STRING_CONCAT_H
