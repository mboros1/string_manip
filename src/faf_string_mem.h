#ifndef FAF_STRING_MEM_H
#define FAF_STRING_MEM_H

#include "faf_backend.h"
#include "faf_string.h"

#include <stdbool.h>
#include <stdint.h>

// Region allocator for the FAF string library.
//
// Memory is an arena: a fixed set of pools. A region is exclusive ownership
// of one pool: acquire it, allocate from it with a bump pointer, then release
// it, which frees everything allocated from it at once in O(1).
//
// There is a default arena in static storage (faf_region_acquire), sized at
// build time. Other arenas can be laid over memory the caller provides
// (faf_arena_init), sized at run time. Arenas and region handles are opaque
// and the same in every build, so they can be used across a foreign function
// interface as they are.
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

// Up to FAF_MAX_ARENAS arenas at once, the default one included (arenas are
// registered in a table, and a region handle names its arena's entry).
#ifndef FAF_MAX_ARENAS
#define FAF_MAX_ARENAS 16
#endif
_Static_assert(FAF_MAX_ARENAS >= 1 && FAF_MAX_ARENAS <= 256,
               "FAF_MAX_ARENAS must be in [1, 256]");

// A set of pools over one block of memory. Opaque: the caller provides
// faf_arena_size() bytes for it (aligned for a pointer) and sets it up with
// faf_arena_init.
typedef struct faf_arena faf_arena;

// A region: exclusive use of one pool of an arena. An opaque integer that
// names the arena, the pool and the pool's generation, so a handle used after
// release (or after its arena is gone) is detected instead of silently
// aliasing. 0 is never a region.
typedef uint64_t faf_region;

#define FAF_REGION_NONE ((faf_region)0)

// A contiguous run of reserved slots.
typedef struct {
  faf_slot *ptr;
  size_t slots;
} faf_span;

#define FAF_SPAN_NONE ((faf_span){.ptr = NULL, .slots = 0})

// Bytes the caller provides for an arena itself (its bookkeeping lives in
// the buffer given to faf_arena_init).
size_t faf_arena_size(void);

// Bytes of buffer an arena of `npools` pools of at least `pool_bytes` each
// needs, bookkeeping and alignment included; 0 if that overflows.
size_t faf_arena_bytes(size_t npools, size_t pool_bytes);

// Lay an arena of `npools` pools over buf[0, nbytes), in `a`: the bookkeeping
// goes at the start of buf, and the rest is split evenly into pools. False
// (and `a` usable for nothing) if a or buf is NULL or misaligned, npools is 0
// or at least UINT16_MAX, nbytes doesn't leave a slot per pool, or
// FAF_MAX_ARENAS arenas are already in use.
bool faf_arena_init(faf_arena *a, void *buf, size_t nbytes, size_t npools);

// Retire `a`: its handles become invalid (even if a later arena takes its
// place in the table). Release its regions first; calling it twice is a no-op.
void faf_arena_fini(faf_arena *a);

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

// The start of `r`'s memory: where its first reservation went. NULL for an
// invalid handle. (A region's owner can keep a header there.)
void *faf_region_base(faf_region r);

#endif // FAF_STRING_MEM_H
