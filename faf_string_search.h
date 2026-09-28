#ifndef FAF_STRING_SEARCH_H
#define FAF_STRING_SEARCH_H

#include "faf_string.h"

// Searching. Nothing here allocates; indices are byte offsets into `str`, and
// FAF_NPOS means not found. An empty `sub` matches at every position, as in
// most string libraries: find returns 0, rfind returns len, count len + 1.

// Byte-for-byte equality. Faster than faf_string_cmp == 0: unequal lengths
// return immediately. Runtime Complexity: O(n)
bool faf_string_eq(faf_string a, faf_string b);

bool faf_string_starts_with(faf_string str, faf_string prefix);
bool faf_string_ends_with(faf_string str, faf_string suffix);

// First / last index of the byte `c`.
size_t faf_string_find_char(faf_string str, char c);
size_t faf_string_rfind_char(faf_string str, char c);

// First / last index of `sub`. Runtime Complexity: O(n * m) worst case, but
// candidates are filtered 16 bytes at a time on sub's first byte.
size_t faf_string_find(faf_string str, faf_string sub);
size_t faf_string_rfind(faf_string str, faf_string sub);

bool faf_string_contains(faf_string str, faf_string sub);

// Number of non-overlapping occurrences of `sub`.
size_t faf_string_count(faf_string str, faf_string sub);

// First index of any byte in `chars` (like strpbrk / strcspn).
size_t faf_string_find_any(faf_string str, faf_string chars);

#endif // FAF_STRING_SEARCH_H
