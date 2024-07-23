#include "faf_string_mem.h"
#include <simde/x86/sse2.h>

/* 2024-07-23
 * This defines the global dynamic allocator for the FAF string library.
 * Allocation strategy is a pool of monotonic pools. Starting at the first pool,
 * first block, each allocation increments to the next data block until the last block
 * is claimed. When all blocks are claimed, the allocator moves to the next pool in the
 * list. When a block is freed, the only action taken is that the status flag is cleared
 * for that data block. A memory pool is only reclaimed when all flags are cleared.
 *
 * The potential downside I'm seeing is if a program has a heavy fragmentation pattern,
 * memory can be quickly depleted. I'll roll with this for now and develop a series of
 * tests to get an understanding of the performance profile.
 */

#ifndef NPOOLS
// by default, 12 pools for 192 kB of data
#define NPOOLS 12
#endif


typedef struct {
  simde__m128i status_flags;
  simde__m128i data_blocks[128];
} faf_string_mempool;

faf_string_mempool allocator_pool[NPOOLS];
