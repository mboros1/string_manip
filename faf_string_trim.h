#ifndef FAF_STRING_TRIM_H
#define FAF_STRING_TRIM_H

#include "faf_string.h"
#include "faf_string_mem.h"

// Trim whitespace from both ends of string
// Runtime Complexity: O(n)
// Memory Complexity: O(n) - allocates new string from pool
faf_string* faf_string_trim(pool_t pool, faf_string str);

// Trim whitespace from left (start) of string
// Runtime Complexity: O(n)
// Memory Complexity: O(n) - allocates new string from pool
faf_string* faf_string_ltrim(pool_t pool, faf_string str);

// Trim whitespace from right (end) of string
// Runtime Complexity: O(n)
// Memory Complexity: O(n) - allocates new string from pool
faf_string* faf_string_rtrim(pool_t pool, faf_string str);

#endif // FAF_STRING_TRIM_H