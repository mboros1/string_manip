#ifndef FAF_STRING_HASH_H
#define FAF_STRING_HASH_H

#include "faf_string.h"
#include <stdint.h>

// Compute hash of string using SIMD-optimized FNV-1a algorithm
// Runtime Complexity: O(n)
// Memory Complexity: O(1)
uint64_t faf_string_hash(faf_string str);

#endif // FAF_STRING_HASH_H