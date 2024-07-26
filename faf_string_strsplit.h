#ifndef FAF_STRING_STRSPLIT_H
#define FAF_STRING_STRSPLIT_H

#include "faf_string.h"
#include "faf_string_arr.h"
#include "faf_string_mem.h"

// Split a string into an array of strings on the provided seperator.
// `pool` is the memory pool in which to allocate the return array.
// `str` is the string to be split.
// `tok` is the seperator character.
// Runtime Complexity: O(n) where n = strlen(str).
// Memory Complexity: O(m) where m = count(tok) in str.
faf_string_arr faf_string_split(pool_t pool, faf_string str, char tok);


#endif //  FAF_STRING_STRSPLIT_H
