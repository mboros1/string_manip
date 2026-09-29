#include "faf_batch.h"
#include "faf_string.h"
#include "faf_string_case.h"
#include "faf_string_hash.h"
#include "faf_string_search.h"
#include "kernels/faf_kernels.h"

/* 2026-09-29
 * Many strings per call, for foreign function interfaces. Each string becomes
 * a faf_string view and goes to the function a C caller would use, so a batch
 * result is always the per-string result. A batch and its arrays (and the
 * bytes of results that make new strings) are one reservation in the region
 * the caller passes.
 */

enum {
  ORDERED = 1, // views ascending and not overlapping
  DENSE = 2,   // ORDERED, and at most one byte between neighbours
};

struct faf_batch {
  const char *data;
  const int64_t *starts;
  const int64_t *ends;
  size_t n;
  uint32_t flags;
};

// Where a caller passes NULL for something of length 0: even NULL + 0 is
// undefined in C, so an empty buffer stands in for it.
static const char nothing[1];
static inline const char *or_empty(const char *p) { return p ? p : nothing; }

static inline faf_string view(const faf_batch *h, size_t i) {
  return (faf_string){.start = h->data + h->starts[i],
                      .end = h->data + h->ends[i]};
}

/* ---- Layout of a new batch ---- */

// A new batch being built: the batch, then its arrays and bytes, carved from
// one reservation in the caller's region.
typedef struct {
  faf_batch *b;
  char *cur;
} builder;

// Reserve room for a batch plus `bytes` of arrays and data (with slack for
// aligning each piece). False, and the region unchanged, if it doesn't fit.
static bool start(builder *bd, faf_region r, size_t bytes) {
  size_t need = sizeof(faf_batch) + bytes + 4 * sizeof(int64_t);
  faf_span sp = faf_reserve(r, need / FAF_SLOT_BYTES + 1);
  if (!sp.ptr)
    return false;
  bd->b = (faf_batch *)(void *)sp.ptr;
  bd->cur = (char *)sp.ptr + sizeof(faf_batch);
  return true;
}

// The next `bytes`, aligned for int64_t.
static void *carve(builder *bd, size_t bytes) {
  uintptr_t p = ((uintptr_t)bd->cur + 7) & ~(uintptr_t)7;
  bd->cur = (char *)p + bytes;
  return (void *)p;
}

static inline faf_batch *done(builder *bd, const char *data,
                              const int64_t *starts, const int64_t *ends,
                              size_t n, uint32_t flags) {
  *bd->b = (faf_batch){.data = or_empty(data), .starts = starts, .ends = ends,
                       .n = n, .flags = flags};
  return bd->b;
}

/* ---- Making batches ---- */

#define SPLIT_BATCH 64

faf_batch *faf_batch_split(faf_region r, const char *data, size_t len,
                          char sep) {
  // (len 0: data may be NULL, so no kernel sees it)
  data = or_empty(data);
  size_t n = len ? faf_k_count_byte(data, len, sep) + 1 : 1;
  builder bd;
  if (n > SIZE_MAX / 16 || !start(&bd, r, 2 * n * sizeof(int64_t)))
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

faf_batch *faf_batch_from_offsets(faf_region r, const char *data,
                                 const int64_t *offsets, size_t n) {
  builder bd;
  if (!start(&bd, r, 0))
    return 0;
  return done(&bd, data, offsets, offsets + 1, n, ORDERED | DENSE);
}

faf_batch *faf_batch_from_views(faf_region r, const char *data,
                               const int64_t *starts, const int64_t *ends,
                               size_t n) {
  builder bd;
  if (!start(&bd, r, 0))
    return 0;
  return done(&bd, data, starts, ends, n, 0);
}


/* ---- Reading a batch ---- */

size_t faf_batch_len(const faf_batch *b) {
  const faf_batch *h = b;
  return h ? h->n : 0;
}

const char *faf_batch_data(const faf_batch *b) {
  const faf_batch *h = b;
  return h ? h->data : NULL;
}

const int64_t *faf_batch_starts(const faf_batch *b) {
  const faf_batch *h = b;
  return h ? h->starts : NULL;
}

const int64_t *faf_batch_ends(const faf_batch *b) {
  const faf_batch *h = b;
  return h ? h->ends : NULL;
}

int64_t faf_batch_total(const faf_batch *b) {
  const faf_batch *h = b;
  int64_t total = 0;
  for (size_t i = 0; h && i < h->n; ++i)
    total += h->ends[i] - h->starts[i];
  return total;
}

/* ---- Per string results ---- */

void faf_batch_lengths(const faf_batch *b, int64_t *out) {
  const faf_batch *h = b;
  for (size_t i = 0; h && i < h->n; ++i)
    out[i] = h->ends[i] - h->starts[i];
}

void faf_batch_find(const faf_batch *b, const char *needle, size_t needle_len,
                    int64_t *out) {
  const faf_batch *h = b;
  faf_string sub = faf_string_init_n(or_empty(needle), needle_len);
  for (size_t i = 0; h && i < h->n; ++i) {
    size_t at = faf_string_find(view(h, i), sub);
    out[i] = at == FAF_NPOS ? -1 : (int64_t)at;
  }
}

void faf_batch_count(const faf_batch *b, const char *needle, size_t needle_len,
                     int64_t *out) {
  const faf_batch *h = b;
  faf_string sub = faf_string_init_n(or_empty(needle), needle_len);
  for (size_t i = 0; h && i < h->n; ++i)
    out[i] = (int64_t)faf_string_count(view(h, i), sub);
}

// Stamps out a 1 / 0 per string function over a predicate f(str, needle).
#define FAF_BATCH_TEST(name, f)                                                \
  size_t name(const faf_batch *b, const char *needle, size_t needle_len,              \
              uint8_t *out) {                                                  \
    const faf_batch *h = b;                                                        \
    faf_string sub = faf_string_init_n(or_empty(needle), needle_len);                    \
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

void faf_batch_hash(const faf_batch *b, uint64_t seed, uint64_t *out) {
  const faf_batch *h = b;
  for (size_t i = 0; h && i < h->n; ++i)
    out[i] = faf_string_hash_seed(view(h, i), seed);
}

/* ---- New batches ---- */

faf_batch *faf_batch_select(faf_region r, const faf_batch *b, const uint8_t *mask) {
  const faf_batch *h = b;
  builder bd;
  if (!h || !start(&bd, r, 2 * h->n * sizeof(int64_t)))
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

faf_batch *faf_batch_take(faf_region r, const faf_batch *b, const int64_t *idx,
                         size_t m) {
  const faf_batch *h = b;
  builder bd;
  if (!h || !start(&bd, r, 2 * m * sizeof(int64_t)))
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
static faf_batch *packed(faf_region r, const faf_batch *h, int64_t total,
                        bool copy, bool upper) {
  builder bd;
  if (!start(&bd, r, (h->n + 1) * sizeof(int64_t) + (size_t)total))
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

faf_batch *faf_batch_compact(faf_region r, const faf_batch *b) {
  const faf_batch *h = b;
  return h ? packed(r, h, faf_batch_total(b), true, false) : NULL;
}

// One pass over data[lo, lo + span), and the same views shifted by -lo. When
// lo is 0 they need no shift, and the result shares the input's views instead
// of copying them (16 bytes a string, more than short strings themselves).
static faf_batch *one_pass(faf_region r, const faf_batch *h, int64_t lo,
                          int64_t span, bool upper) {
  size_t views = lo ? 2 * h->n * sizeof(int64_t) : 0;
  builder bd;
  if (!start(&bd, r, views + (size_t)span))
    return 0;
  if (lo == 0) {
    char *dst = carve(&bd, (size_t)span);
    faf_k_ascii_case(dst, h->data, (size_t)span, upper);
    return done(&bd, dst, h->starts, h->ends, h->n, h->flags);
  }
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

faf_batch *faf_batch_ascii_case(faf_region r, const faf_batch *b, int upper) {
  const faf_batch *h = b;
  if (!h)
    return 0;
  if (h->n == 0)
    return packed(r, h, 0, false, upper != 0);
  if (h->flags & DENSE) // in order with at most a byte between: the range
    return one_pass(r, h, h->starts[0], h->ends[h->n - 1] - h->starts[0],
                    upper != 0);
  // scattered: one pass only if the views cover at least half their range
  int64_t lo = h->starts[0], hi = h->ends[0], total = 0;
  for (size_t i = 0; i < h->n; ++i) {
    lo = h->starts[i] < lo ? h->starts[i] : lo;
    hi = h->ends[i] > hi ? h->ends[i] : hi;
    total += h->ends[i] - h->starts[i];
  }
  if (hi - lo <= 2 * total)
    return one_pass(r, h, lo, hi - lo, upper != 0);
  return packed(r, h, total, false, upper != 0);
}

/* ---- Writing ---- */

void faf_batch_ascii_case_inplace(const faf_batch *b, int upper) {
  const faf_batch *h = b;
  char *data = h ? (char *)(uintptr_t)h->data : NULL;
  for (size_t i = 0; h && i < h->n; ++i)
    faf_k_ascii_case(data + h->starts[i], data + h->starts[i],
                     (size_t)(h->ends[i] - h->starts[i]), upper != 0);
}

int64_t faf_batch_join(const faf_batch *b, const char *sep, size_t sep_len, char *dst) {
  const faf_batch *h = b;
  int64_t at = 0;
  char none[1];
  dst = dst ? dst : none; // only when there is nothing to write
  sep = or_empty(sep);
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
