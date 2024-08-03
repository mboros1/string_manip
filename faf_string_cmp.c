
#include "faf_string_cmp.h"

int faf_string_cmp(faf_string *str1, faf_string *str2) {
  int len1 = str1->end - str1->start;
  int len2 = str2->end - str2->start;
  int len = len1 > len2 ? len2 : len1;

  int idx;
  for (idx = 0; idx + 16 <= len; idx += 16) {
    simde__m128i chunk1 =
        simde_mm_loadu_si128((simde__m128i *)&str1->start[idx]);
    simde__m128i chunk2 =
        simde_mm_loadu_si128((simde__m128i *)&str2->start[idx]);

    simde__m128i cmp = simde_mm_cmpeq_epi8(chunk1, chunk2);
    int mask = simde_mm_movemask_epi8(cmp);
    if (mask != 0xFFFF) {
      int pos = __builtin_ffs(~mask) - 1;
      char c1 = str1->start[idx + pos];
      char c2 = str2->start[idx + pos];
      if (c1 < c2)
        return -1;
      if (c1 > c2)
        return 1;
    }
  }
  int remaining = str1->end - (str1->start + idx);
  if (remaining > 0) {
    char buffer1[16] = {0};
    char buffer2[16] = {0};
    for (int i = 0; i < remaining; ++i) {
      buffer1[i] = str1->start[i + idx];
      buffer2[i] = str2->start[i + idx];
    }
    simde__m128i chunk1 = simde_mm_loadu_si128((simde__m128i *)buffer1);
    simde__m128i chunk2 = simde_mm_loadu_si128((simde__m128i *)buffer2);
    simde__m128i cmp = simde_mm_cmpeq_epi8(chunk1, chunk2);
    int mask = simde_mm_movemask_epi8(cmp);

    if (mask != 0xFFFF) {
      int pos = __builtin_ffs(~mask) - 1;
      char c1 = str1->start[idx + pos];
      char c2 = str2->start[idx + pos];
      if (c1 < c2)
        return -1;
      if (c1 > c2)
        return 1;
    }
  }

  int diff = len1 - len2;
  if (diff < 0)
    return -1;
  if (diff > 0)
    return 1;
  return 0;
}
