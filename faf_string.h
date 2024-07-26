#ifndef FAF_STRING_H
#define FAF_STRING_H

#include <stddef.h>
#include <simde/x86/sse2.h>

// Fundamental string in FAF. `start` points to char array that is 
// not necessarily null terminated.
// `end` points to one past the end of the char array
typedef union {
  struct {
    const char *start;
    const char *end;
  };
  simde__m128i data;
} faf_string;

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
