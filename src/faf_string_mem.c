#include "faf_string_mem.h"
#include "kernels/faf_kernels.h"

/* 2024-07-23
 * This defines the global dynamic allocator for the FAF string library.
 *
 * 2026-09-27
 * Reworked into a region allocator. Three layers:
 *   1. Backing memory: static slot storage, private to this file.
 *   2. Region lifetime: explicit acquire/release. Ownership (`in_use`) is kept
 *      separate from the cursor, and a generation per pool catches handles
 *      used after release.
 *   3. Allocation: contiguous span reservation with a bounds check. The bump
 *      policy is a strategy function so a concurrent variant can be added by
 *      writing one bump function and one FAF_DEFINE_RESERVE line.
 */

/* ---- Layer 1: backing memory ---- */

#define STORAGE_SLOTS (FAF_NPOOLS * FAF_POOL_SLOTS)

static FAF_POOL_ATTR faf_slot storage[STORAGE_SLOTS];

static inline faf_slot *pool_base(uint16_t pool) {
  return &storage[(size_t)pool * FAF_POOL_SLOTS];
}

bool faf_mem_contains(const void *p, size_t n) {
  uintptr_t lo = (uintptr_t)storage;
  uintptr_t hi = lo + sizeof(storage);
  uintptr_t q = (uintptr_t)p;
  return q >= lo && q <= hi && n <= hi - q;
}

/* ---- Layer 2: region lifetime ---- */

typedef struct {
  size_t cursor; // slots used
  uint16_t gen;  // bumped on every release
  bool in_use;
} pool_state;

static pool_state pools[FAF_NPOOLS];

faf_region faf_region_acquire(void) {
  for (uint16_t i = 0; i < FAF_NPOOLS; ++i) {
    if (!pools[i].in_use) {
      pools[i].in_use = true;
      pools[i].cursor = 0;
      return (faf_region){.pool = i, .gen = pools[i].gen};
    }
  }
  return FAF_REGION_NONE;
}

bool faf_region_valid(faf_region r) {
  return r.pool < FAF_NPOOLS && pools[r.pool].in_use &&
         pools[r.pool].gen == r.gen;
}

void faf_region_release(faf_region r) {
  if (!faf_region_valid(r))
    return;
#ifdef FAF_DEBUG
  // poison, so reads through stale strings stand out
  faf_memset(pool_base(r.pool), 0xDD, pools[r.pool].cursor * FAF_SLOT_BYTES);
#endif
  pools[r.pool].gen++;
  pools[r.pool].in_use = false;
}

size_t faf_region_used(faf_region r) {
  return faf_region_valid(r) ? pools[r.pool].cursor : 0;
}

size_t faf_region_remaining(faf_region r) {
  return faf_region_valid(r) ? FAF_POOL_SLOTS - pools[r.pool].cursor : 0;
}

/* ---- Layer 3: allocation ---- */

// Strategy: returns the old cursor, or SIZE_MAX if `n` more slots would
// exceed `cap`. Checking inside the strategy means a failed reserve never
// moves the cursor.
typedef size_t (*bump_fn)(size_t *cursor, size_t n, size_t cap);

static inline size_t bump_local(size_t *cursor, size_t n, size_t cap) {
  size_t old = *cursor;
  if (n > cap - old)
    return SIZE_MAX;
  *cursor = old + n;
  return old;
}

// Core: shared logic and invariants. `bump` is a constant at every call site,
// so with optimizations on this inlines down to a compare and an add.
static inline faf_span reserve_core(faf_region r, size_t slots, bump_fn bump) {
  if (!faf_region_valid(r))
    return FAF_SPAN_NONE;
  size_t off = bump(&pools[r.pool].cursor, slots, FAF_POOL_SLOTS);
  if (off == SIZE_MAX)
    return FAF_SPAN_NONE;
  return (faf_span){.ptr = pool_base(r.pool) + off, .slots = slots};
}

// Stamps out a named reserve function for a bump strategy.
#define FAF_DEFINE_RESERVE(name, bump)                                         \
  faf_span name(faf_region r, size_t slots) {                                  \
    return reserve_core(r, slots, bump);                                       \
  }

FAF_DEFINE_RESERVE(faf_reserve, bump_local)

bool faf_reserve_extend(faf_region r, faf_span *sp, size_t more) {
  if (!faf_region_valid(r) || !sp->ptr)
    return false;
  pool_state *ps = &pools[r.pool];
  // only the most recent reservation can grow, and only into free space
  if (sp->ptr + sp->slots != pool_base(r.pool) + ps->cursor)
    return false;
  if (bump_local(&ps->cursor, more, FAF_POOL_SLOTS) == SIZE_MAX)
    return false;
  sp->slots += more;
  return true;
}

void faf_reserve_shrink(faf_region r, faf_span *sp, size_t slots) {
  if (!faf_region_valid(r) || !sp->ptr || slots >= sp->slots)
    return;
  pool_state *ps = &pools[r.pool];
  if (sp->ptr + sp->slots == pool_base(r.pool) + ps->cursor)
    ps->cursor -= sp->slots - slots; // most recent: give the tail back
  sp->slots = slots;
}

/* ---- Copy ---- */

faf_string faf_string_copy(faf_region r, faf_string str) {
  size_t len = faf_string_len(str);
  faf_span sp = faf_reserve(r, faf_slots_for(len));
  if (!sp.ptr)
    return FAF_STRING_NONE;

  char *dst = (char *)sp.ptr;
  char *dst_end = dst + sp.slots * FAF_SLOT_BYTES;
  faf_memcpy(dst, str.start, len);
  // NUL terminate and zero pad the rest of the last slot
  faf_memset(dst + len, 0, (size_t)(dst_end - (dst + len)));

  return (faf_string){.start = dst, .end = dst + len};
}
