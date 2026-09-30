#ifndef FAF_STRING_STRSPLIT_H
#define FAF_STRING_STRSPLIT_H

#include "../core/faf_string.h"
#include "../core/faf_string_arr.h"
#include "../mem/faf_string_mem.h"

// Split a string into an array of strings on the provided seperator.
// `r` is the region in which to allocate the return array.
// `str` is the string to be split.
// `tok` is the seperator character.
// The tokens are views into `str`: valid only while `str`'s bytes are, and
// not NUL terminated. The array itself is owned by `r`.
// Returns an empty array (start == end == NULL) if `r` is out of space.
// Runtime Complexity: O(n) where n = strlen(str).
// Memory Complexity: O(m) where m = count(tok) in str.
faf_string_arr faf_string_split(faf_region r, faf_string str, char tok);

// Like faf_string_split, but first copies `str` into `r` and splits the copy,
// replacing each separator with '\0'. Every token is owned by `r` and NUL
// terminated. `str` is not modified.
// Runtime Complexity: O(n) where n = strlen(str).
// Memory Complexity: O(n + m) where m = count(tok) in str.
faf_string_arr faf_string_split_owned(faf_region r, faf_string str, char tok);


#endif //  FAF_STRING_STRSPLIT_H
