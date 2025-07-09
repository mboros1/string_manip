#ifndef FAF_STRING_TOUPPER_H
#define FAF_STRING_TOUPPER_H

#include "faf_string.h"
#include "faf_string_mem.h"

// Convert all lowercase ASCII characters to uppercase
// Non-ASCII characters and non-lowercase characters are unchanged
// Runtime Complexity: O(n)
// Memory Complexity: O(n) - allocates new string from pool
faf_string* faf_string_toupper(pool_t pool, faf_string str);

#endif // FAF_STRING_TOUPPER_H