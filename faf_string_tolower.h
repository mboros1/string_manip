#ifndef FAF_STRING_TOLOWER_H
#define FAF_STRING_TOLOWER_H

#include "faf_string.h"
#include "faf_string_mem.h"

// Convert all uppercase ASCII characters to lowercase
// Non-ASCII characters and non-uppercase characters are unchanged
// Runtime Complexity: O(n)
// Memory Complexity: O(n) - allocates new string from pool
faf_string* faf_string_tolower(pool_t pool, faf_string str);

#endif // FAF_STRING_TOLOWER_H