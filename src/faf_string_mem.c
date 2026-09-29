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
 * and others can be laid over caller memory. Arenas sit in a small table, and
 * a region handle is an integer naming its table entry, so handles and arenas
 * are the same in every build and can cross a foreign function interface.
 */

/* ---- Layer 1: backing memory ---- */

typedef struct {
  size_t cursor; // slots used
  uint16_t gen;  // bumped on every release
  bool in_use;
} pool_state;

struct faf_arena {
  faf_slot *slots;    // npools * pool_slots slots
  pool_state *pools;  // npools entries
  size_t pool_slots;  // slots per pool
  uint16_t npools;    // 0: not (or no longer) an arena
  uint8_t index;      // entry in the arena table
  uint8_t epoch;      // arenas that entry held before this one
};

#define STORAGE_SLOTS (FAF_NPOOLS * FAF_POOL_SLOTS)

static FAF_POOL_ATTR faf_slot storage[STORAGE_SLOTS];
static pool_state default_pools[FAF_NPOOLS];
static faf_arena default_arena = {.slots = storage,
                                  .pools = default_pools,
                                  .pool_slots = FAF_POOL_SLOTS,
                                  .npools = FAF_NPOOLS,
                                  .index = 0,
                                  .epoch = 0};

// The arena table: entry 0 is the default arena. `epochs` counts the arenas
// an entry has held; each arena keeps its own copy, so a handle from a
// retired arena never matches the next one to take its entry.
static faf_arena *arenas[FAF_MAX_ARENAS] = {&default_arena};
static uint8_t epochs[FAF_MAX_ARENAS];

// Handle layout, low to high: pool + 1 (32 bits, so 0 is never a handle),
// generation (16), epoch (8), table entry (8).
static inline faf_region handle(const faf_arena *a, uint16_t pool, uint16_t gen) {
  return (uint64_t)(pool + 1u) | (uint64_t)gen << 32 |
         (uint64_t)a->epoch << 48 | (uint64_t)a->index << 56;
}

static inline uint32_t h_pool(faf_region r) { return (uint32_t)r - 1u; }
static inline uint16_t h_gen(faf_region r) { return (uint16_t)(r >> 32); }

// The arena `r` belongs to, or NULL if the handle can't be one of its.
static inline faf_arena *h_arena(faf_region r) {
  size_t i = (size_t)(r >> 56);
  faf_arena *a = i < FAF_MAX_ARENAS ? arenas[i] : NULL;
  return a && a->epoch == (uint8_t)(r >> 48) ? a : NULL;
}

static inline faf_slot *pool_base(const faf_arena *a, uint32_t pool) {
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

bool faf_arena_contains(const faf_arena *a, const void *p, size_t n) {
  return a && arena_contains(a, p, n);
}

bool faf_mem_contains(const void *p, size_t n) {
  return arena_contains(&default_arena, p, n);
}

// Offset that aligns `p` up to `align` (a power of two).
static inline size_t align_pad(uintptr_t p, size_t align) {
  return (size_t)(-p & (align - 1));
}

size_t faf_arena_size(void) { return sizeof(faf_arena); }

size_t faf_arena_bytes(size_t npools, size_t pool_bytes) {
  if (npools == 0 || npools >= UINT16_MAX)
    return 0;
  size_t pool_slots = pool_bytes / FAF_SLOT_BYTES + (pool_bytes % FAF_SLOT_BYTES != 0);
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
  if (!a || (uintptr_t)a % _Alignof(faf_arena))
    return false;
  *a = (faf_arena){.slots = NULL, .pools = NULL, .pool_slots = 0, .npools = 0,
                   .index = 0, .epoch = 0};
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

  size_t entry = 1; // 0 is the default arena
  while (entry < FAF_MAX_ARENAS && arenas[entry])
    ++entry;
  if (entry == FAF_MAX_ARENAS)
    return false;

  pool_state *pools = (pool_state *)(void *)((char *)buf + states_off);
  for (size_t i = 0; i < npools; ++i)
    pools[i] = (pool_state){.cursor = 0, .gen = 0, .in_use = false};
  *a = (faf_arena){.slots = (faf_slot *)(void *)((char *)buf + off),
                   .pools = pools,
                   .pool_slots = pool_slots,
                   .npools = (uint16_t)npools,
                   .index = (uint8_t)entry,
                   .epoch = epochs[entry]};
  arenas[entry] = a;
  return true;
}

void faf_arena_fini(faf_arena *a) {
  if (!a || a->npools == 0 || a == &default_arena || arenas[a->index] != a)
    return;
  arenas[a->index] = NULL;
  epochs[a->index]++;
  a->npools = 0;
}

/* ---- Layer 2: region lifetime ---- */

// npools is 0 for an arena that failed init or was retired, so the loop
// below finds nothing.
static inline faf_region acquire_in(faf_arena *a) {
  pool_state *pools = a->pools;
  uint16_t n = a->npools;
  for (uint16_t i = 0; i < n; ++i) {
    if (!pools[i].in_use) {
      pools[i].in_use = true;
      pools[i].cursor = 0;
      return handle(a, i, pools[i].gen);
    }
  }
  return FAF_REGION_NONE;
}

faf_region faf_arena_acquire(faf_arena *a) {
  return a ? acquire_in(a) : FAF_REGION_NONE;
}

// The default arena with its constants: entry 0 and epoch 0 add nothing to
// the handle, and the pool array and count are known at build time.
faf_region faf_region_acquire(void) {
  for (uint16_t i = 0; i < FAF_NPOOLS; ++i) {
    if (!default_pools[i].in_use) {
      default_pools[i].in_use = true;
      default_pools[i].cursor = 0;
      return (uint64_t)(i + 1u) | (uint64_t)default_pools[i].gen << 32;
    }
  }
  return FAF_REGION_NONE;
}

// The pool `r` owns, or NULL if `r` isn't live.
//
// Handles of the default arena (entry 0, which is never retired, so its
// epoch stays 0: the top 16 bits are zero) go straight to its static pool
// array, whose address and size are constants. That keeps the common case
// free of the table lookup, which otherwise costs a dependent load: with it
// on every handle, acquire + release took 11 ns on the M1 instead of 5.6.
static inline pool_state *live_pool(faf_region r, faf_arena **arena) {
  uint32_t pool = h_pool(r);
  pool_state *ps;
  faf_arena *a;
  if ((r >> 48) == 0) {
    if (pool >= FAF_NPOOLS)
      return NULL;
    a = &default_arena;
    ps = &default_pools[pool];
  } else {
    a = h_arena(r);
    if (!a || pool >= a->npools)
      return NULL;
    ps = &a->pools[pool];
  }
  if (!ps->in_use || ps->gen != h_gen(r))
    return NULL;
  *arena = a;
  return ps;
}

bool faf_region_valid(faf_region r) {
  faf_arena *a;
  return live_pool(r, &a) != NULL;
}

void faf_region_release(faf_region r) {
  faf_arena *a;
  pool_state *ps = live_pool(r, &a);
  if (!ps)
    return;
#ifdef FAF_DEBUG
  // poison, so reads through stale strings stand out
  faf_memset(pool_base(a, h_pool(r)), 0xDD, ps->cursor * FAF_SLOT_BYTES);
#endif
  ps->gen++;
  ps->in_use = false;
}

size_t faf_region_capacity(faf_region r) {
  faf_arena *a;
  return live_pool(r, &a) ? a->pool_slots : 0;
}

size_t faf_region_used(faf_region r) {
  faf_arena *a;
  pool_state *ps = live_pool(r, &a);
  return ps ? ps->cursor : 0;
}

size_t faf_region_remaining(faf_region r) {
  faf_arena *a;
  pool_state *ps = live_pool(r, &a);
  return ps ? a->pool_slots - ps->cursor : 0;
}

void *faf_region_base(faf_region r) {
  faf_arena *a;
  return live_pool(r, &a) ? (void *)pool_base(a, h_pool(r)) : NULL;
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
  faf_arena *a;
  pool_state *ps = live_pool(r, &a);
  if (!ps)
    return FAF_SPAN_NONE;
  size_t off = bump(&ps->cursor, slots, a->pool_slots);
  if (off == SIZE_MAX)
    return FAF_SPAN_NONE;
  return (faf_span){.ptr = pool_base(a, h_pool(r)) + off, .slots = slots};
}

// Stamps out a named reserve function for a bump strategy.
#define FAF_DEFINE_RESERVE(name, bump)                                         \
  faf_span name(faf_region r, size_t slots) {                                  \
    return reserve_core(r, slots, bump);                                       \
  }

FAF_DEFINE_RESERVE(faf_reserve, bump_local)

bool faf_reserve_extend(faf_region r, faf_span *sp, size_t more) {
  faf_arena *a;
  pool_state *ps = live_pool(r, &a);
  if (!ps || !sp->ptr)
    return false;
  // only the most recent reservation can grow, and only into free space
  if (sp->ptr + sp->slots != pool_base(a, h_pool(r)) + ps->cursor)
    return false;
  if (bump_local(&ps->cursor, more, a->pool_slots) == SIZE_MAX)
    return false;
  sp->slots += more;
  return true;
}

void faf_reserve_shrink(faf_region r, faf_span *sp, size_t slots) {
  faf_arena *a;
  pool_state *ps = live_pool(r, &a);
  if (!ps || !sp->ptr || slots >= sp->slots)
    return;
  if (sp->ptr + sp->slots == pool_base(a, h_pool(r)) + ps->cursor)
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
