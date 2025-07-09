#ifndef FAF_STRING_CONTAINS_H
#define FAF_STRING_CONTAINS_H

#include "faf_string.h"
#include <stdbool.h>

// Check if haystack contains needle substring
// Uses Two-Way algorithm with SIMD optimizations for first character matching
// Runtime Complexity: O(n + m) where n = haystack length, m = needle length
// Memory Complexity: O(1)
bool faf_string_contains(faf_string haystack, faf_string needle);

#endif // FAF_STRING_CONTAINS_H