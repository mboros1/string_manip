#include "faf_string_strlen.h"
#include <simde/x86/sse2.h>

// 2024-07-22:
// Using SIMD instructions, searches 16 bytes at a time.
// Reference implementation from testing in `str_len_test.c` using
// the `strlen_sse2` implementation. My thought process here is that
// the SSE2 instructions are the most widely used, and while it the
// performance might be slightly lower then the glibc implementation,
// the difference was very small and disappeared for longer strings
// while also being only a few lines of code compared to a very complicated
// implementation that had different edge cases for each platform.


size_t faf_string_strlen(const char *str) {
  const char *start = str;
  simde__m128i zeros = simde_mm_setzero_si128();

  while (1) {
    simde__m128i chars = simde_mm_loadu_si128((simde__m128i *)str);
    simde__m128i cmp = simde_mm_cmpeq_epi8(chars, zeros);
    int mask = simde_mm_movemask_epi8(cmp);
    if (mask != 0) {
      return (str - start) + __builtin_ctz(mask);
    }
    str += 16;
  }
}
