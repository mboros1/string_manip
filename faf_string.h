#ifndef FAF_STRING_H
#define FAF_STRING_H

#include <stdbool.h>
#include <stddef.h>

// Fundamental string in FAF. `start` points to char array that is
// not necessarily null terminated.
// `end` points to one past the end of the char array
//
// A faf_string is a value (two pointers). It never owns its bytes: they live
// either in caller memory, or in a region (see faf_string_mem.h) which owns
// them until the region is released.
typedef struct {
  const char *start;
  const char *end;
} faf_string;

// Returned by allocating functions when the region is out of space or invalid.
// Distinct from an empty string, which has a non-NULL `start`.
#define FAF_STRING_NONE ((faf_string){.start = NULL, .end = NULL})

static inline int faf_string_is_none(faf_string str) {
  return str.start == NULL;
}

// "Not found" for functions that return an index.
#define FAF_NPOS ((size_t)-1)

static inline size_t faf_string_len(faf_string str) {
  return (size_t)(str.end - str.start);
}

// Initialize a faf_string from a C string.
// `str` MUST be null terminated.
// `start` initialized to `str`
// `end` is calculated using `strlen`.
// Runtime Complexity: O(n)
// Memory Complexity: O(1)
faf_string faf_string_init(const char *str);

// Initialize a faf_string from a C string.
// `str` DOES NOT need to be null terminated.
// `n` is the length of the char array.
// `start` initialized to `str`
// `end` is calculated as `start` + `n`.
// Runtime Complexity: O(1) Memory Complexity: O(1)
faf_string faf_string_init_n(const char *str, size_t n);

#endif // FAF_STRING_H
