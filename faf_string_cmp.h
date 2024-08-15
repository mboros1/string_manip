#ifndef FAF_STRING_CMP_H
#define  FAF_STRING_CMP_H

#include "faf_string.h"

// Compare two strings, and return a value based on their
// lexigraphical order. 
// Returns -1 if str1 < str2.
// Returns 0 if str1 == str2.
// Returns 1 if str1 > str2.
//
// Runtime Complexity; O(n)
// Memory Complexity: O(1)
int faf_string_cmp(faf_string* str1, faf_string* str2);




#endif // FAF_STRING_CMP_H
