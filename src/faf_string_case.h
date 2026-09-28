#ifndef FAF_STRING_CASE_H
#define FAF_STRING_CASE_H

#include "faf_string.h"
#include "faf_string_mem.h"

// ASCII case operations. Bytes outside 'A'-'Z' / 'a'-'z' are left unchanged;
// the library works on bytes, not Unicode.

// Lower / upper case copy of `str` allocated in `r`, NUL terminated.
// Returns FAF_STRING_NONE if `r` is out of space.
// Runtime Complexity: O(n) Memory Complexity: O(n)
faf_string faf_string_to_lower(faf_region r, faf_string str);
faf_string faf_string_to_upper(faf_region r, faf_string str);

// Case-insensitive equality / ordering (ordering by the lower case bytes,
// unsigned). Runtime Complexity: O(n) Memory Complexity: O(1)
bool faf_string_eq_icase(faf_string a, faf_string b);
int faf_string_cmp_icase(faf_string a, faf_string b);

#endif // FAF_STRING_CASE_H
