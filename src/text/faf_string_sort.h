#ifndef FAF_STRING_SORT_H
#define FAF_STRING_SORT_H

#include "../core/faf_string.h"
#include "../core/faf_string_arr.h"
#include "../mem/faf_string_mem.h"

// The bytes of `str` in ascending (unsigned) order, allocated in `r`.
// Counting sort. Runtime Complexity: O(n) Memory Complexity: O(n)
faf_string faf_string_sort_chars(faf_region r, faf_string str);

// Sort the strings of `arr` in place by faf_string_cmp. Not stable.
// Introsort. Runtime Complexity: O(n log n) comparisons, worst case.
void faf_string_arr_sort(faf_string_arr arr);

#endif // FAF_STRING_SORT_H
