#include "faf_string_concat.h"
#include "faf_string_mem.h"

faf_string* faf_string_concat(pool_t pool, faf_string str1, faf_string str2) {
  faf_string *alloc = faf_string_alloc(pool);

  const char *data = str1.start;
  alloc->start = (const char *)&mempools[pool * BLOCK_LEN + pool_idxs[pool]];
  for (; data + 16 <= str1.end; data += 16) {
    simde__m128i chars = simde_mm_loadu_si128((const simde__m128 *)data);
    int idx = pool_idxs[pool]++;
    mempools[pool * BLOCK_LEN + idx].data = chars;
  }
  int remaining = str1.end - data;
  alloc->end =
      (const char
           *)(((uintptr_t)&mempools[pool * BLOCK_LEN + pool_idxs[pool]]) +
              remaining);
  if (remaining > 0) {
    int idx = pool_idxs[pool]++;
    char buffer[16] = {0};
    for (int i = 0; i < remaining; ++i) {
      buffer[i] = data[i];
    }
    // TODO: fill out the rest of the buffer with the beginning of the next string
    simde__m128i chars = simde_mm_loadu_si128((const simde__m128 *)buffer);
    mempools[pool * BLOCK_LEN + idx].data = chars;
  }
  // TODO: start the next string where we left off filling out the remaining buffer.

  return alloc;
}
