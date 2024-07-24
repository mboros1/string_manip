#include "faf_string_mem.h"
#include <simde/x86/sse2.h>
#include <stdint.h>

/* 2024-07-23
 * This defines the global dynamic allocator for the FAF string library.
 * Allocation strategy is a pool of monotonic pools. Starting at the first pool,
 * first block, each allocation increments to the next data block until the last
 * block is claimed. When all blocks are claimed, the allocator moves to the
 * next pool in the list. When a block is freed, the only action taken is that
 * the status flag is cleared for that data block. A memory pool is only
 * reclaimed when all flags are cleared.
 *
 * The potential downside I'm seeing is if a program has a heavy fragmentation
 * pattern, memory can be quickly depleted. I'll roll with this for now and
 * develop a series of tests to get an understanding of the performance profile.
 *
 * 2024-07-24
 * Thinking about this some more; I'm just going to start with a pool of
 * monotonic allocators.
 */

#ifndef NPOOLS
#define NPOOLS 12
#endif

#define BLOCK_LEN 4096

faf_string mempools[NPOOLS * BLOCK_LEN];

pool_t pool_idxs[NPOOLS];

pool_t next_pool() {
  static int current_pool = 0;

  // look for first pool with its index at 0
  for (int i = 0; i < NPOOLS; ++i) {
    if (pool_idxs[current_pool]) {
      current_pool == NPOOLS - 1 ? 0 : ++current_pool;
    } else {
      break;
    }
  }
  return current_pool;
}

void faf_string_pool_reset(pool_t pool) { pool_idxs[pool] = 0; }

faf_string *faf_string_alloc(pool_t pool) {
  int idx = pool_idxs[pool]++;
  faf_string *alloc = &mempools[pool * BLOCK_LEN + idx];
  return alloc;
}

faf_string *faf_string_copy(pool_t pool, faf_string str) {
  faf_string *alloc = faf_string_alloc(pool);

  const char *data = str.start;
  for (; data + 16 <= str.end; data += 16) {
    simde__m128i chars = simde_mm_loadu_si128((const simde__m128 *)data);
    int idx = pool_idxs[pool]++;
    mempools[pool * BLOCK_LEN + idx].data = chars;
  }
  int remaining = str.end - data;
  if (remaining > 0) {
    int idx = pool_idxs[pool]++;
    char buffer[16] = {0};
    for (int i = 0; i < remaining; ++i) {
      buffer[i] = data[i];
    }
    simde__m128i chars = simde_mm_loadu_si128((const simde__m128 *)buffer);
    mempools[pool * BLOCK_LEN + idx].data = chars;
  }

  return alloc;
}
