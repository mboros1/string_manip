#ifndef FAF_STRING_MEM_H
#define FAF_STRING_MEM_H

#include "../core/faf_backend.h"
#include "../core/faf_string.h"

#include <stdbool.h>
#include <stdint.h>

// Region allocator for the FAF string library.
//
// Memory is an arena: a fixed set of pools. A region is exclusive ownership
// of one pool: acquire it, allocate from it with a bump pointer, then release
// it, which frees everything allocated from it at once in O(1).
//
// There is a default arena in static storage (faf_region_acquire), sized at
// build time. Any number of other arenas can be laid over memory the caller
// provides (faf_arena_init), sized at run time.
//
// Not thread-safe: an arena and its regions belong to one thread at a time.
// Arenas share nothing, so threads that each use their own never contend.

// Build-time sizing of the default arena: FAF_NPOOLS pools of FAF_POOL_SLOTS slots, so
// FAF_NPOOLS * FAF_POOL_SLOTS * FAF_SLOT_BYTES bytes of static storage
// (192 KB by default with SIMD kernels, 96 KB with the scalar ones). A region can never hold more than one pool, so
// FAF_POOL_SLOTS also caps the size of a single string. Small targets lower
// these, e.g. -DFAF_NPOOLS=4 -DFAF_POOL_SLOTS=512 for 32 KB.
#ifndef FAF_NPOOLS
#define FAF_NPOOLS 12
#endif
#ifndef FAF_POOL_SLOTS
#define FAF_POOL_SLOTS 1024
#endif
// Bytes per slot: the allocation granularity and alignment of region
// memory. Results are zero padded to the end of their last slot. Defaults to
// one vector register on SIMD backends (so strings start vector aligned), and
// to 8 on the scalar backend, which wastes less on small targets.
#ifndef FAF_SLOT_BYTES
#if FAF_VECTOR_BYTES
#define FAF_SLOT_BYTES FAF_VECTOR_BYTES
#else
#define FAF_SLOT_BYTES 8
#endif
#endif

// Placement of the pool storage, e.g. EXT_RAM_BSS_ATTR to put it in PSRAM on
// an ESP32, or a section attribute for a linker script. Empty by default.
#ifndef FAF_POOL_ATTR
#define FAF_POOL_ATTR
#endif

_Static_assert(FAF_NPOOLS >= 1 && FAF_NPOOLS < UINT16_MAX,
               "FAF_NPOOLS must be in [1, 65534]: pools are indexed by uint16_t");
_Static_assert(FAF_POOL_SLOTS >= 1, "FAF_POOL_SLOTS must be at least 1");
// token arrays (faf_string_split) live in slots, so a slot must be aligned
// for faf_string
_Static_assert((FAF_SLOT_BYTES & (FAF_SLOT_BYTES - 1)) == 0 &&
                   FAF_SLOT_BYTES >= _Alignof(faf_string),
               "FAF_SLOT_BYTES must be a power of two, at least the alignment "
               "of faf_string");

typedef struct {
  _Alignas(FAF_SLOT_BYTES) unsigned char bytes[FAF_SLOT_BYTES];
} faf_slot;

struct faf_pool_state; // private to faf_string_mem.c

// A set of pools over one block of memory. The fields are private: set them up
// with faf_arena_init. The struct must stay where it is while regions from it
// are in use (handles point to it); the memory it manages must outlive it.
typedef struct {
  faf_slot *slots;              // npools * pool_slots slots
  struct faf_pool_state *pools; // npools entries
  size_t pool_slots;            // slots per pool
  uint16_t npools;
} faf_arena;

// Handle to an acquired pool of `arena`. `gen` is the pool's generation at
// acquire time, so a handle used after release is detected instead of
// silently aliasing.
typedef struct {
  faf_arena *arena;
  uint16_t pool;
  uint16_t gen;
} faf_region;

#define FAF_REGION_NONE ((faf_region){.arena = NULL, .pool = UINT16_MAX, .gen = 0})

// A contiguous run of reserved slots.
typedef struct {
  faf_slot *ptr;
  size_t slots;
} faf_span;

#define FAF_SPAN_NONE ((faf_span){.ptr = NULL, .slots = 0})

// Bytes of memory an arena of `npools` pools of `pool_slots` slots needs,
// including its bookkeeping and alignment slack; 0 if that overflows.
size_t faf_arena_bytes(size_t npools, size_t pool_slots);

// Lay an arena of `npools` pools over buf[0, nbytes): the bookkeeping goes at
// the start, and the rest is split evenly into pools. The arena has no pools
// in use. Returns false (and leaves *a with no pools, so every acquire fails)
// if buf is NULL, npools is 0 or at least UINT16_MAX, or nbytes doesn't leave
// at least one slot per pool.
//
// Re-initializing an arena resets its generations: handles from before are
// not detected as stale. Release them first.
bool faf_arena_init(faf_arena *a, void *buf, size_t nbytes, size_t npools);

// sizeof(faf_arena), for code that can't see the struct (a binding from
// another language) and allocates arenas itself.
size_t faf_arena_size(void);

// Claim a free pool of `a`. Returns FAF_REGION_NONE when every pool is in use.
faf_region faf_arena_acquire(faf_arena *a);

// Claim a free pool of the default arena.
faf_region faf_region_acquire(void);

// Free everything allocated from `r` and invalidate `r` and every string
// allocated from it. Releasing an invalid handle is a no-op.
void faf_region_release(faf_region r);

// Is `r` a live handle (acquired, and not released since)?
bool faf_region_valid(faf_region r);

// Slots in / used by / still available in `r`. 0 for an invalid handle.
size_t faf_region_capacity(faf_region r);
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
// The environment's memcpy/memset. GCC and Clang require every environment,
// freestanding included, to provide memcpy, memset, memmove and memcmp, and
// these four are the library's only external dependencies. Going through the
// builtins lets the compiler inline small, fixed-size copies.

#if defined(__GNUC__) || defined(__clang__)
#define FAF_BUILTIN_MEMCPY __builtin_memcpy
#define FAF_BUILTIN_MEMSET __builtin_memset
#else
#include <string.h>
#define FAF_BUILTIN_MEMCPY memcpy
#define FAF_BUILTIN_MEMSET memset
#endif

// (FAF_FAST_UNALIGNED comes from faf_backend.h)

// Short lengths, which dominate string work, are handled inline with a few
// overlapping fixed-size copies (two k-byte pieces cover every length in
// [k, 2k]); fixed-size builtin copies compile to plain loads and stores, 16
// bytes at a time where the target has vectors. Only longer lengths call the
// platform's tuned memcpy/memset.

// Copy `n` bytes from `src` to `dst`. The ranges must not overlap.
static inline void *faf_memcpy(void *restrict dst, const void *restrict src,
                               size_t n) {
#if !FAF_FAST_UNALIGNED
  return FAF_BUILTIN_MEMCPY(dst, src, n);
#else
  unsigned char *d = (unsigned char *)dst;
  const unsigned char *s = (const unsigned char *)src;
  // restrict: the ranges don't overlap, so the overlapping pieces can be
  // copied directly. Shortest first: they are the most common, and each
  // check in front of them costs.
  if (n < 16) {
    if (n >= 8) {
      FAF_BUILTIN_MEMCPY(d, s, 8);
      FAF_BUILTIN_MEMCPY(d + n - 8, s + n - 8, 8);
    } else if (n >= 4) {
      FAF_BUILTIN_MEMCPY(d, s, 4);
      FAF_BUILTIN_MEMCPY(d + n - 4, s + n - 4, 4);
    } else if (n) { // 1..3: first, middle and last byte
      d[0] = s[0];
      d[n / 2] = s[n / 2];
      d[n - 1] = s[n - 1];
    }
  } else if (n <= 32) {
    FAF_BUILTIN_MEMCPY(d, s, 16);
    FAF_BUILTIN_MEMCPY(d + n - 16, s + n - 16, 16);
  } else if (n <= 64) {
    FAF_BUILTIN_MEMCPY(d, s, 16);
    FAF_BUILTIN_MEMCPY(d + 16, s + 16, 16);
    FAF_BUILTIN_MEMCPY(d + n - 32, s + n - 32, 16);
    FAF_BUILTIN_MEMCPY(d + n - 16, s + n - 16, 16);
  } else {
    return FAF_BUILTIN_MEMCPY(dst, src, n);
  }
  return dst;
#endif
}

// Set `n` bytes at `dst` to `(unsigned char)c`.
static inline void *faf_memset(void *dst, int c, size_t n) {
#if !FAF_FAST_UNALIGNED
  return FAF_BUILTIN_MEMSET(dst, c, n);
#else
  unsigned char *d = (unsigned char *)dst;
  if (n > 16)
    return FAF_BUILTIN_MEMSET(dst, c, n);
  uint64_t v = (uint64_t)(unsigned char)c * 0x0101010101010101ull;
  if (n >= 8) {
    FAF_BUILTIN_MEMCPY(d, &v, 8);
    FAF_BUILTIN_MEMCPY(d + n - 8, &v, 8);
  } else if (n >= 4) {
    uint32_t w = (uint32_t)v;
    FAF_BUILTIN_MEMCPY(d, &w, 4);
    FAF_BUILTIN_MEMCPY(d + n - 4, &w, 4);
  } else if (n) {
    d[0] = d[n / 2] = d[n - 1] = (unsigned char)c;
  }
  return dst;
#endif
}

// True if [p, p + n) lies inside the pools of `a`, i.e. is owned by some
// region of it. faf_mem_contains asks the same of the default arena.
bool faf_arena_contains(const faf_arena *a, const void *p, size_t n);
bool faf_mem_contains(const void *p, size_t n);

#endif // FAF_STRING_MEM_H
