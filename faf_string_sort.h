#ifndef FAF_STRING_SORT_H
#define FAF_STRING_SORT_H

#include "faf_string.h"
#include <stddef.h>

// Sort an array of faf_string using PDQSort algorithm
// PDQSort is a hybrid sorting algorithm that combines quicksort, heapsort, and insertion sort
// Runtime Complexity: O(n log n) average, O(n log n) worst case
// Memory Complexity: O(log n) for recursion stack
void faf_string_sort(faf_string* strings, size_t count);

#endif // FAF_STRING_SORT_H