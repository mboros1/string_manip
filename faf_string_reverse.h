#ifndef FAF_STRING_REVERSE_H
#define FAF_STRING_REVERSE_H

#include "faf_string.h"
#include "faf_string_mem.h"

// Reverse the order of characters in a string
// Runtime Complexity: O(n)
// Memory Complexity: O(n) - allocates new string from pool
faf_string* faf_string_reverse(pool_t pool, faf_string str);

#endif // FAF_STRING_REVERSE_H