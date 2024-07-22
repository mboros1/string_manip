#ifndef FAF_STRING_STRLEN_H

#include <stddef.h>


// Calculates the length of a string by search for the first null character.
// `str` a C string, MUST be null terminated!
// Runtime Complexity; O(n)
// Memory Complexity: O(1)
size_t faf_string_strlen(const char* str);


#endif // FAF_STRING_STRLEN_H
