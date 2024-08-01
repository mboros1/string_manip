#include "faf_string_strsplit.h"
#include "faf_string_mem.h"
#include "faf_string_strlen.h"

// 2024-07-22:
// I think my general strategy will be to just find the start/end of
// each string split and return an array of the pointers, no copying.
//

void faf_string_find_char_indexes(faf_string string, char target, int *indexes,
                                  int *count) {
  simde__m128i target_vector = simde_mm_set1_epi8(target);
  *count = 0;

  const char *str = string.start;
  int len = string.end - string.start;
  for (int i = 0; i < len; i += 32) {
    simde__m128i chunk = simde_mm_loadu_si128((simde__m128i *)&str[i]);
    simde__m128i cmp = simde_mm_cmpeq_epi8(chunk, target_vector);
    int mask = simde_mm_movemask_epi8(cmp);

    while (mask) {
      int pos = __builtin_ffs(mask) - 1;
      indexes[(*count)++] = i + pos;
      mask &= mask - 1;
    }
  }
}

faf_string_arr faf_string_split(pool_t pool, faf_string str, char tok) {
  simde__m128i target_vector = simde_mm_set1_epi8(tok);

  const char *s = str.start;
  size_t len = str.end - str.start;
  faf_string *head = faf_string_alloc(pool);
  head->start = str.start;
  head->end = 0;

  faf_string *tail = head;

  int idx = 0;
  for (; idx + 16 <= len; idx += 16) {
    simde__m128i chunk = simde_mm_loadu_si128((simde__m128i *)&s[idx]);
    simde__m128i cmp = simde_mm_cmpeq_epi8(chunk, target_vector);
    int mask = simde_mm_movemask_epi8(cmp);

    while (mask) {
      int pos = __builtin_ffs(mask) - 1;
      tail->end = head->start + idx + pos;
      tail = faf_string_alloc(pool);
      tail->start = head->start + idx + pos + 1;
      mask &= mask - 1;
    }
  }

  // TODO: remainng is not calculated correctly; s does not change in the loop, maybe should be incrementing s instead of idx, or use idx for the calculation
  int remaining = str.end - s;
  if (remaining > 0) {
    char buffer[16] = {0};
    for (int i = 0; i < remaining; ++i) {
      buffer[i] = s[i];
    }
    simde__m128i chunk = simde_mm_loadu_si128((simde__m128i *)buffer);
    simde__m128i cmp = simde_mm_cmpeq_epi8(chunk, target_vector);
    int mask = simde_mm_movemask_epi8(cmp);

    while (mask) {
      int pos = __builtin_ffs(mask) - 1;
      tail->end = head->start + idx + pos;
      tail = faf_string_alloc(pool);
      tail->start = head->start + idx + pos + 1;
      mask &= mask - 1;
    }
  }
  tail->end = str.end;

  faf_string_arr arr = {
      .start = head,
      .end = tail+1,
  };
  return arr;
}
