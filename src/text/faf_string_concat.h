#ifndef FAF_STRING_CONCAT_H
#define FAF_STRING_CONCAT_H

#include "../core/faf_string.h"
#include "../mem/faf_string_mem.h"


// Concatonate `str2` onto the end of `str1`.
// The result is allocated in `r`, NUL terminated, and owned by `r`.
// Returns FAF_STRING_NONE if `r` is out of space.
//
// Runtime Complexity; O(n + m) where n=strlen(str1) and m=strlen(str2)
// Memory Complexity: O(n + m)
faf_string faf_string_concat(faf_region r, faf_string str1, faf_string str2);

#endif // FAF_STRING_CONCAT_H
