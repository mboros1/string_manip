#ifndef FAF_STRING_STRSPLIT_H

#include "faf_string.h"

// Split a string into an array of strings on the provided seperator.
// `str` is the string to be split.
// `tok` is the seperator character.
// Runtime Complexity: O(n) where n = strlen(str).
// Memory Complexity: O(m) where m = count(tok) in str.
faf_string* faf_string_strsplit(faf_string str, char tok);


#endif //  FAF_STRING_STRSPLIT_H
