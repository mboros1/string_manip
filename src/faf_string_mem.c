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
 *
 * 2026-09-29
 * Layer 1 is now a value, faf_arena: the static storage is the default arena,
 * and others can be laid over caller memory. Handles carry their arena, so
 * layers 2 and 3 are unchanged apart from reading sizes from it. Built with
 * FAF_ARENAS=0 there is only the default arena, and its sizes are constants.
 */

/* ---- Layer 1: backing memory ---- */

struct faf_pool_state {
  size_t cursor; // slots used
  uint16_t gen;  // bumped on every release
  bool in_use;
};
typedef struct faf_pool_state pool_state;

#define STORAGE_SLOTS (FAF_NPOOLS * FAF_POOL_SLOTS)

static FAF_POOL_ATTR faf_slot storage[STORAGE_SLOTS];
static pool_state default_pools[FAF_NPOOLS];
#define DEFAULT_ARENA_INIT                                                     \
  {.slots = storage,                                                           \
   .pools = default_pools,                                                     \
   .pool_slots = FAF_POOL_SLOTS,                                               \
   .npools = FAF_NPOOLS}

// With FAF_ARENAS off, every handle refers to the default arena, and the
// arena is const: the compiler folds its sizes to the build-time constants,
// so the code below compiles as if it were written against them.
#if FAF_ARENAS
static faf_arena default_arena = DEFAULT_ARENA_INIT;
#define ARENA(r) ((r).arena)
#define HAS_ARENA(r) ((r).arena != NULL)
#define REGION(a, i, g) ((faf_region){.arena = (a), .pool = (i), .gen = (g)})
faf_arena *faf_arena_default(void) { return &default_arena; }
#else
static const faf_arena default_arena = DEFAULT_ARENA_INIT;
#define ARENA(r) (&default_arena)
#define HAS_ARENA(r) true
#define REGION(a, i, g) ((faf_region){.pool = (i), .gen = (g)})
#endif

static inline faf_slot *pool_base(const faf_arena *a, uint16_t pool) {
  return a->slots + (size_t)pool * a->pool_slots;
}

static bool arena_contains(const faf_arena *a, const void *p, size_t n) {
  if (!a->slots)
    return false;
  uintptr_t lo = (uintptr_t)a->slots;
  uintptr_t hi = lo + (size_t)a->npools * a->pool_slots * FAF_SLOT_BYTES;
  uintptr_t q = (uintptr_t)p;
  return q >= lo && q <= hi && n <= hi - q;
}

bool faf_mem_contains(const void *p, size_t n) {
  return arena_contains(&default_arena, p, n);
}

#if FAF_ARENAS
// Offset that aligns `p` up to `align` (a power of two).
static inline size_t align_pad(uintptr_t p, size_t align) {
  return (size_t)(-p & (align - 1));
}

size_t faf_arena_bytes(size_t npools, size_t pool_slots) {
  if (npools == 0 || npools >= UINT16_MAX)
    return 0;
  if (pool_slots > SIZE_MAX / FAF_SLOT_BYTES / npools)
    return 0;
  size_t slot_bytes = npools * pool_slots * FAF_SLOT_BYTES;
  // worst case padding: before the pool states and before the slots
  size_t head = npools * sizeof(pool_state) + _Alignof(pool_state) - 1 +
                FAF_SLOT_BYTES - 1;
  if (slot_bytes > SIZE_MAX - head)
    return 0;
  return head + slot_bytes;
}

bool faf_arena_init(faf_arena *a, void *buf, size_t nbytes, size_t npools) {
  *a = (faf_arena){.slots = NULL, .pools = NULL, .pool_slots = 0, .npools = 0};
  if (!buf || npools == 0 || npools >= UINT16_MAX)
    return false;

  // [pad][pool states][pad][slots]
  uintptr_t p = (uintptr_t)buf;
  size_t off = align_pad(p, _Alignof(pool_state));
  if (off > nbytes || npools > (nbytes - off) / sizeof(pool_state))
    return false;
  size_t states_off = off;
  off += npools * sizeof(pool_state);
  off += align_pad(p + off, FAF_SLOT_BYTES);
  if (off > nbytes)
    return false;
  size_t pool_slots = (nbytes - off) / FAF_SLOT_BYTES / npools;
  if (pool_slots == 0)
    return false;

  pool_state *pools = (pool_state *)(void *)((char *)buf + states_off);
  for (size_t i = 0; i < npools; ++i)
    pools[i] = (pool_state){.cursor = 0, .gen = 0, .in_use = false};
  *a = (faf_arena){.slots = (faf_slot *)(void *)((char *)buf + off),
                   .pools = pools,
                   .pool_slots = pool_slots,
                   .npools = (uint16_t)npools};
  return true;
}

bool faf_arena_contains(const faf_arena *a, const void *p, size_t n) {
  return arena_contains(a, p, n);
}
#endif // FAF_ARENAS

/* ---- Layer 2: region lifetime ---- */

static inline faf_region acquire_in(const faf_arena *a) {
  pool_state *pools = a->pools;
  uint16_t n = a->npools;
  for (uint16_t i = 0; i < n; ++i) {
    if (!pools[i].in_use) {
      pools[i].in_use = true;
      pools[i].cursor = 0;
      return REGION((faf_arena *)a, i, pools[i].gen);
    }
  }
  return FAF_REGION_NONE;
}

#if FAF_ARENAS
faf_region faf_arena_acquire(faf_arena *a) { return acquire_in(a); }
#endif

faf_region faf_region_acquire(void) { return acquire_in(&default_arena); }

bool faf_region_valid(faf_region r) {
  if (!HAS_ARENA(r))
    return false;
  const faf_arena *a = ARENA(r);
  return r.pool < a->npools && a->pools[r.pool].in_use &&
         a->pools[r.pool].gen == r.gen;
}

void faf_region_release(faf_region r) {
  if (!faf_region_valid(r))
    return;
  pool_state *ps = &ARENA(r)->pools[r.pool];
#ifdef FAF_DEBUG
  // poison, so reads through stale strings stand out
  faf_memset(pool_base(ARENA(r), r.pool), 0xDD, ps->cursor * FAF_SLOT_BYTES);
#endif
  ps->gen++;
  ps->in_use = false;
}

size_t faf_region_capacity(faf_region r) {
  return faf_region_valid(r) ? ARENA(r)->pool_slots : 0;
}

size_t faf_region_used(faf_region r) {
  return faf_region_valid(r) ? ARENA(r)->pools[r.pool].cursor : 0;
}

size_t faf_region_remaining(faf_region r) {
  return faf_region_valid(r)
             ? ARENA(r)->pool_slots - ARENA(r)->pools[r.pool].cursor
             : 0;
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
  const faf_arena *a = ARENA(r);
  size_t off = bump(&a->pools[r.pool].cursor, slots, a->pool_slots);
  if (off == SIZE_MAX)
    return FAF_SPAN_NONE;
  return (faf_span){.ptr = pool_base(a, r.pool) + off, .slots = slots};
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
  const faf_arena *a = ARENA(r);
  pool_state *ps = &a->pools[r.pool];
  // only the most recent reservation can grow, and only into free space
  if (sp->ptr + sp->slots != pool_base(a, r.pool) + ps->cursor)
    return false;
  if (bump_local(&ps->cursor, more, a->pool_slots) == SIZE_MAX)
    return false;
  sp->slots += more;
  return true;
}

void faf_reserve_shrink(faf_region r, faf_span *sp, size_t slots) {
  if (!faf_region_valid(r) || !sp->ptr || slots >= sp->slots)
    return;
  const faf_arena *a = ARENA(r);
  pool_state *ps = &a->pools[r.pool];
  if (sp->ptr + sp->slots == pool_base(a, r.pool) + ps->cursor)
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
