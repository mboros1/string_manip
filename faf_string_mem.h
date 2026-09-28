#ifndef FAF_STRING_MEM_H
#define FAF_STRING_MEM_H

#include "faf_string.h"

#include <stdbool.h>
#include <stdint.h>

// Region allocator for the FAF string library.
//
// Memory is a fixed, static set of pools. A region is exclusive ownership of
// one pool: acquire it, allocate from it with a bump pointer, then release it,
// which frees everything allocated from it at once in O(1).
//
// Not thread-safe: acquire/release/reserve assume a single thread.

#ifndef FAF_NPOOLS
#define FAF_NPOOLS 12
#endif

// slots per pool
#define FAF_POOL_SLOTS 1024
// bytes per slot, one SIMD register
#define FAF_SLOT_BYTES 16

typedef struct {
  _Alignas(16) unsigned char bytes[FAF_SLOT_BYTES];
} faf_slot;

// Handle to an acquired pool. `gen` is the pool's generation at acquire time,
// so a handle used after release is detected instead of silently aliasing.
typedef struct {
  uint16_t pool;
  uint16_t gen;
} faf_region;

#define FAF_REGION_NONE ((faf_region){.pool = UINT16_MAX, .gen = 0})

// A contiguous run of reserved slots.
typedef struct {
  faf_slot *ptr;
  size_t slots;
} faf_span;

#define FAF_SPAN_NONE ((faf_span){.ptr = NULL, .slots = 0})

// Claim a free pool. Returns FAF_REGION_NONE when every pool is in use.
faf_region faf_region_acquire(void);

// Free everything allocated from `r` and invalidate `r` and every string
// allocated from it. Releasing an invalid handle is a no-op.
void faf_region_release(faf_region r);

// Is `r` a live handle (acquired, and not released since)?
bool faf_region_valid(faf_region r);

// Slots used / still available in `r`. 0 for an invalid handle.
size_t faf_region_used(faf_region r);
size_t faf_region_remaining(faf_region r);

// Reserve `slots` contiguous slots from `r`. Returns FAF_SPAN_NONE if `r` is
// invalid or there isn't room; the region is unchanged on failure.
faf_span faf_reserve(faf_region r, size_t slots);

// Grow `*sp` by `more` slots in place. Only possible while `*sp` is the most
// recent reservation in `r` and there is room; returns false (and leaves `*sp`
// unchanged) otherwise. This is what lets builders append without copying.
bool faf_reserve_extend(faf_region r, faf_span *sp, size_t more);

// Shrink `*sp` to `slots`. If it is the most recent reservation the freed
// slots go back to `r`; otherwise they are just unused until release.
void faf_reserve_shrink(faf_region r, faf_span *sp, size_t slots);

// Slots needed to hold `len` bytes plus a NUL terminator.
static inline size_t faf_slots_for(size_t len) {
  return len / FAF_SLOT_BYTES + 1;
}

// Copy `str` into `r`. The copy is NUL terminated and zero padded to the end
// of its last slot. Returns FAF_STRING_NONE if `r` is out of space.
faf_string faf_string_copy(faf_region r, faf_string str);

// ---- Memory primitives ----
//
// The library uses these instead of libc's memcpy/memset so it can build
// freestanding (they are kernels: see faf_kernels_simd.c). Compilers still
// emit calls to `memcpy`/`memset` on their own (struct copies, recognized
// loops); for a target with no libc, build faf_kernels_simd.c with
// -DFAF_PROVIDE_LIBC_MEM to also define memcpy, memset, memmove and memcmp,
// which GCC and Clang require even when freestanding.

// Copy `n` bytes from `src` to `dst`. The ranges must not overlap.
void *faf_memcpy(void *restrict dst, const void *restrict src, size_t n);

// Set `n` bytes at `dst` to `(unsigned char)c`.
void *faf_memset(void *dst, int c, size_t n);

// True if [p, p + n) lies inside pool storage, i.e. is owned by some region.
bool faf_mem_contains(const void *p, size_t n);

#endif // FAF_STRING_MEM_H
