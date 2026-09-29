#include "faf_batch.h"
#include "faf_string.h"
#include "faf_string_case.h"
#include "faf_string_hash.h"
#include "faf_string_search.h"
#include "kernels/faf_kernels.h"

/* 2026-09-29
 * Many strings per call, for foreign function interfaces. Each string becomes
 * a faf_string view and goes to the function a C caller would use, so a batch
 * result is always the per-string result.
 *
 * A batch is a region whose first bytes are a header: the data pointer, the
 * views and what is known about their order. New batches (a split, a select,
 * converted bytes) are laid out in their region after the header, so freeing
 * the region frees all of it.
 */

#define MAGIC 0xFAFBA7C4u

enum {
  ORDERED = 1, // views ascending and not overlapping
  DENSE = 2,   // ORDERED, and at most one byte between neighbours
};

typedef struct {
  const char *data;
  const int64_t *starts;
  const int64_t *ends;
  size_t n;
  uint32_t flags;
  uint32_t magic;
} header;

// The header of a live batch, or NULL (stale handle, or not a batch). The
// magic number catches a plain region passed by mistake; it is a guard, not a
// proof.
static inline header *hdr(faf_batch b) {
  header *h = (header *)faf_region_base((faf_region)b);
  return h && faf_region_used((faf_region)b) * FAF_SLOT_BYTES >= sizeof(header) &&
                 h->magic == MAGIC
             ? h
             : NULL;
}

static inline faf_string view(const header *h, size_t i) {
  return (faf_string){.start = h->data + h->starts[i],
                      .end = h->data + h->ends[i]};
}

/* ---- Layout of a new batch ---- */

// A new batch being built: a region, and the reservation its header and
// arrays are carved from.
typedef struct {
  faf_region r;
  header *h;
  char *cur;
} builder;

// Reserve room for a header plus `bytes` of arrays and data (with slack for
// aligning each piece), in a new region of `arena`.
static bool start(builder *bd, faf_arena *arena, size_t bytes) {
  bd->r = arena ? faf_arena_acquire(arena) : faf_region_acquire();
  size_t need = sizeof(header) + bytes + 4 * sizeof(int64_t);
  faf_span sp = faf_reserve(bd->r, need / FAF_SLOT_BYTES + 1);
  if (!sp.ptr) {
    faf_region_release(bd->r);
    return false;
  }
  bd->h = (header *)(void *)sp.ptr;
  bd->cur = (char *)sp.ptr + sizeof(header);
  *bd->h = (header){.magic = MAGIC};
  return true;
}

// The next `bytes`, aligned for int64_t.
static void *carve(builder *bd, size_t bytes) {
  uintptr_t p = ((uintptr_t)bd->cur + 7) & ~(uintptr_t)7;
  bd->cur = (char *)p + bytes;
  return (void *)p;
}

static inline faf_batch done(builder *bd, const char *data, const int64_t *starts,
                             const int64_t *ends, size_t n, uint32_t flags) {
  bd->h->data = data;
  bd->h->starts = starts;
  bd->h->ends = ends;
  bd->h->n = n;
  bd->h->flags = flags;
  return (faf_batch)bd->r;
}

/* ---- Making batches ---- */

#define SPLIT_BATCH 64

faf_batch faf_batch_split(faf_arena *arena, const char *data, size_t len,
                          char sep) {
  // (len 0: data may be NULL, so no kernel sees it)
  size_t n = len ? faf_k_count_byte(data, len, sep) + 1 : 1;
  builder bd;
  if (n > SIZE_MAX / 16 || !start(&bd, arena, 2 * n * sizeof(int64_t)))
    return 0;
  int64_t *starts = carve(&bd, n * sizeof(int64_t));
  int64_t *ends = carve(&bd, n * sizeof(int64_t));

  size_t pos[SPLIT_BATCH];
  size_t k = 0, from = 0;
  for (; len;) {
    size_t got = faf_k_find_bytes(data + from, len - from, sep, pos, SPLIT_BATCH);
    size_t base = from;
    for (size_t j = 0; j < got; ++j, ++k) {
      starts[k] = (int64_t)from;
      ends[k] = (int64_t)(base + pos[j]);
      from = base + pos[j] + 1;
    }
    if (got < SPLIT_BATCH)
      break;
  }
  starts[k] = (int64_t)from;
  ends[k] = (int64_t)len;
  return done(&bd, data, starts, ends, n, ORDERED | DENSE);
}

faf_batch faf_batch_from_offsets(faf_arena *arena, const char *data,
                                 const int64_t *offsets, size_t n) {
  builder bd;
  if (!start(&bd, arena, 0))
    return 0;
  return done(&bd, data, offsets, offsets + 1, n, ORDERED | DENSE);
}

faf_batch faf_batch_from_views(faf_arena *arena, const char *data,
                               const int64_t *starts, const int64_t *ends,
                               size_t n) {
  builder bd;
  if (!start(&bd, arena, 0))
    return 0;
  return done(&bd, data, starts, ends, n, 0);
}

void faf_batch_free(faf_batch b) {
  header *h = hdr(b);
  if (!h)
    return;
  h->magic = 0; // the pool's next owner must not look like this batch
  faf_region_release((faf_region)b);
}

/* ---- Reading a batch ---- */

size_t faf_batch_len(faf_batch b) {
  header *h = hdr(b);
  return h ? h->n : 0;
}

const char *faf_batch_data(faf_batch b) {
  header *h = hdr(b);
  return h ? h->data : NULL;
}

const int64_t *faf_batch_starts(faf_batch b) {
  header *h = hdr(b);
  return h ? h->starts : NULL;
}

const int64_t *faf_batch_ends(faf_batch b) {
  header *h = hdr(b);
  return h ? h->ends : NULL;
}

int64_t faf_batch_total(faf_batch b) {
  header *h = hdr(b);
  int64_t total = 0;
  for (size_t i = 0; h && i < h->n; ++i)
    total += h->ends[i] - h->starts[i];
  return total;
}

/* ---- Per string results ---- */

void faf_batch_lengths(faf_batch b, int64_t *out) {
  header *h = hdr(b);
  for (size_t i = 0; h && i < h->n; ++i)
    out[i] = h->ends[i] - h->starts[i];
}

void faf_batch_find(faf_batch b, const char *needle, size_t needle_len,
                    int64_t *out) {
  header *h = hdr(b);
  faf_string sub = faf_string_init_n(needle, needle_len);
  for (size_t i = 0; h && i < h->n; ++i) {
    size_t at = faf_string_find(view(h, i), sub);
    out[i] = at == FAF_NPOS ? -1 : (int64_t)at;
  }
}

void faf_batch_count(faf_batch b, const char *needle, size_t needle_len,
                     int64_t *out) {
  header *h = hdr(b);
  faf_string sub = faf_string_init_n(needle, needle_len);
  for (size_t i = 0; h && i < h->n; ++i)
    out[i] = (int64_t)faf_string_count(view(h, i), sub);
}

// Stamps out a 1 / 0 per string function over a predicate f(str, needle).
#define FAF_BATCH_TEST(name, f)                                                \
  size_t name(faf_batch b, const char *needle, size_t needle_len,              \
              uint8_t *out) {                                                  \
    header *h = hdr(b);                                                        \
    faf_string sub = faf_string_init_n(needle, needle_len);                    \
    size_t yes = 0;                                                            \
    for (size_t i = 0; h && i < h->n; ++i) {                                   \
      out[i] = f(view(h, i), sub);                                             \
      yes += out[i];                                                           \
    }                                                                          \
    return yes;                                                                \
  }

FAF_BATCH_TEST(faf_batch_contains, faf_string_contains)
FAF_BATCH_TEST(faf_batch_starts_with, faf_string_starts_with)
FAF_BATCH_TEST(faf_batch_ends_with, faf_string_ends_with)
FAF_BATCH_TEST(faf_batch_eq, faf_string_eq)
FAF_BATCH_TEST(faf_batch_eq_icase, faf_string_eq_icase)

void faf_batch_hash(faf_batch b, uint64_t seed, uint64_t *out) {
  header *h = hdr(b);
  for (size_t i = 0; h && i < h->n; ++i)
    out[i] = faf_string_hash_seed(view(h, i), seed);
}

/* ---- New batches ---- */

faf_batch faf_batch_select(faf_arena *arena, faf_batch b, const uint8_t *mask) {
  header *h = hdr(b);
  builder bd;
  if (!h || !start(&bd, arena, 2 * h->n * sizeof(int64_t)))
    return 0;
  int64_t *starts = carve(&bd, h->n * sizeof(int64_t));
  int64_t *ends = carve(&bd, h->n * sizeof(int64_t));
  size_t m = 0;
  for (size_t i = 0; i < h->n; ++i) {
    // written unconditionally, kept only when selected: no branch per string
    starts[m] = h->starts[i];
    ends[m] = h->ends[i];
    m += mask[i] != 0;
  }
  return done(&bd, h->data, starts, ends, m, h->flags & ORDERED);
}

faf_batch faf_batch_take(faf_arena *arena, faf_batch b, const int64_t *idx,
                         size_t m) {
  header *h = hdr(b);
  builder bd;
  if (!h || !start(&bd, arena, 2 * m * sizeof(int64_t)))
    return 0;
  int64_t *starts = carve(&bd, m * sizeof(int64_t));
  int64_t *ends = carve(&bd, m * sizeof(int64_t));
  for (size_t i = 0; i < m; ++i) {
    starts[i] = h->starts[idx[i]];
    ends[i] = h->ends[idx[i]];
  }
  return done(&bd, h->data, starts, ends, m, 0);
}

// The strings end to end (Arrow layout): copied, or case converted.
static faf_batch packed(faf_arena *arena, const header *h, int64_t total,
                        bool copy, bool upper) {
  builder bd;
  if (!start(&bd, arena, (h->n + 1) * sizeof(int64_t) + (size_t)total))
    return 0;
  int64_t *offsets = carve(&bd, (h->n + 1) * sizeof(int64_t));
  char *dst = carve(&bd, (size_t)total);
  int64_t at = 0;
  offsets[0] = 0;
  for (size_t i = 0; i < h->n; ++i) {
    size_t len = (size_t)(h->ends[i] - h->starts[i]);
    if (copy)
      faf_memcpy(dst + at, h->data + h->starts[i], len);
    else
      faf_k_ascii_case(dst + at, h->data + h->starts[i], len, upper);
    at += (int64_t)len;
    offsets[i + 1] = at;
  }
  return done(&bd, dst, offsets, offsets + 1, h->n, ORDERED | DENSE);
}

faf_batch faf_batch_compact(faf_arena *arena, faf_batch b) {
  header *h = hdr(b);
  return h ? packed(arena, h, faf_batch_total(b), true, false) : 0;
}

// One pass over data[lo, lo + span), and the same views shifted by -lo.
static faf_batch one_pass(faf_arena *arena, const header *h, int64_t lo,
                          int64_t span, bool upper) {
  builder bd;
  if (!start(&bd, arena, 2 * h->n * sizeof(int64_t) + (size_t)span))
    return 0;
  int64_t *starts = carve(&bd, h->n * sizeof(int64_t));
  int64_t *ends = carve(&bd, h->n * sizeof(int64_t));
  char *dst = carve(&bd, (size_t)span);
  faf_k_ascii_case(dst, h->data + lo, (size_t)span, upper);
  for (size_t i = 0; i < h->n; ++i) {
    starts[i] = h->starts[i] - lo;
    ends[i] = h->ends[i] - lo;
  }
  return done(&bd, dst, starts, ends, h->n, h->flags);
}

faf_batch faf_batch_ascii_case(faf_arena *arena, faf_batch b, int upper) {
  header *h = hdr(b);
  if (!h)
    return 0;
  if (h->n == 0)
    return packed(arena, h, 0, false, upper != 0);
  if (h->flags & DENSE) // in order with at most a byte between: the range
    return one_pass(arena, h, h->starts[0], h->ends[h->n - 1] - h->starts[0],
                    upper != 0);
  // scattered: one pass only if the views cover at least half their range
  int64_t lo = h->starts[0], hi = h->ends[0], total = 0;
  for (size_t i = 0; i < h->n; ++i) {
    lo = h->starts[i] < lo ? h->starts[i] : lo;
    hi = h->ends[i] > hi ? h->ends[i] : hi;
    total += h->ends[i] - h->starts[i];
  }
  if (hi - lo <= 2 * total)
    return one_pass(arena, h, lo, hi - lo, upper != 0);
  return packed(arena, h, total, false, upper != 0);
}

/* ---- Writing ---- */

void faf_batch_ascii_case_inplace(faf_batch b, int upper) {
  header *h = hdr(b);
  char *data = h ? (char *)(uintptr_t)h->data : NULL;
  for (size_t i = 0; h && i < h->n; ++i)
    faf_k_ascii_case(data + h->starts[i], data + h->starts[i],
                     (size_t)(h->ends[i] - h->starts[i]), upper != 0);
}

int64_t faf_batch_join(faf_batch b, const char *sep, size_t sep_len, char *dst) {
  header *h = hdr(b);
  int64_t at = 0;
  for (size_t i = 0; h && i < h->n; ++i) {
    if (i > 0) {
      faf_memcpy(dst + at, sep, sep_len);
      at += (int64_t)sep_len;
    }
    size_t len = (size_t)(h->ends[i] - h->starts[i]);
    faf_memcpy(dst + at, h->data + h->starts[i], len);
    at += (int64_t)len;
  }
  return at;
}
