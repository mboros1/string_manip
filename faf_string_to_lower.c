#include "faf_string_to_lower.h"
#include <simde/x86/sse2.h>

// TODO: going to have to have the ability to make copies obviously
// TODO: maybe some sort of flywheel pattern to store `const char*` data?
faf_string to_lower(faf_string str) {
  size_t len = str.end - str.start;
  size_t i = 0;
  simde__m128i A = _mm_set1_epi8('A');
  simde__m128i Z = _mm_set1_epi8('Z');
  simde__m128i diff = _mm_set1_epi8('a' - 'A');

  // Process 16 bytes at a time
  for (; i + 15 < len; i += 16) {
    // Load 16 characters from the string
    simde__m128i chars = simde_mm_loadu_si128((simde__m128i *)(str.start + i));

    // Create a mask for characters in the range 'A' to 'Z'
    simde__m128i mask =
        _mm_and_si128(_mm_cmpgt_epi8(chars, A), simde_mm_cmplt_epi8(chars, Z));

    // Apply the mask and convert to lowercase
    simde__m128i lower = simde_mm_add_epi8(chars, simde_mm_and_si128(mask, diff));

    // Store the result back to the string
    _mm_storeu_si128((simde__m128i *)(str.start + i), lower);
  }

  // Process the remaining characters
  for (; i < len; ++i) {
    if ((str.start + i) >= 'A' && (str.start + i) <= 'Z') {
      *(str.start + i) = (str.start + i) + ('a' - 'A');
    }
  }
}
