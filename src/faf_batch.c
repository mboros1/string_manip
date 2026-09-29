#include "faf_batch.h"
#include "faf_string.h"
#include "faf_string_case.h"
#include "faf_string_hash.h"
#include "faf_string_mem.h"
#include "faf_string_search.h"
#include "kernels/faf_kernels.h"

/* 2026-09-29
 * Loops over a batch, for foreign function interfaces: each string becomes a
 * faf_string view and goes to the same function a C caller would use, so a
 * batch result is always the per-string result. What a batch saves is the
 * crossing: one call from another language instead of one per string.
 */

static inline faf_string view(const char *data, const int64_t *starts,
                              const int64_t *ends, size_t i) {
  return (faf_string){.start = data + starts[i], .end = data + ends[i]};
}

/* ---- Making batches ---- */

size_t faf_batch_split_count(const char *data, size_t len, char sep) {
  return faf_k_count_byte(data, len, sep) + 1;
}

#define SPLIT_BATCH 64

size_t faf_batch_split(const char *data, size_t len, char sep,
                       int64_t *starts, int64_t *ends, size_t cap) {
  size_t pos[SPLIT_BATCH];
  size_t pieces = 0, from = 0;
  for (;;) {
    size_t got = faf_k_find_bytes(data + from, len - from, sep, pos, SPLIT_BATCH);
    size_t base = from;
    for (size_t k = 0; k < got; ++k, ++pieces) {
      if (pieces < cap) {
        starts[pieces] = (int64_t)from;
        ends[pieces] = (int64_t)(base + pos[k]);
      }
      from = base + pos[k] + 1;
    }
    if (got < SPLIT_BATCH)
      break;
  }
  if (pieces < cap) {
    starts[pieces] = (int64_t)from;
    ends[pieces] = (int64_t)len;
  }
  return pieces + 1;
}

size_t faf_batch_select(const int64_t *starts, const int64_t *ends, size_t n,
                        const uint8_t *mask, int64_t *out_starts,
                        int64_t *out_ends) {
  size_t m = 0;
  for (size_t i = 0; i < n; ++i) {
    // written unconditionally, kept only when selected: no branch per string
    out_starts[m] = starts[i];
    out_ends[m] = ends[i];
    m += mask[i] != 0;
  }
  return m;
}

void faf_batch_take(const int64_t *starts, const int64_t *ends,
                    const int64_t *idx, size_t m, int64_t *out_starts,
                    int64_t *out_ends) {
  for (size_t i = 0; i < m; ++i) {
    out_starts[i] = starts[idx[i]];
    out_ends[i] = ends[idx[i]];
  }
}

/* ---- Per string results ---- */

void faf_batch_lengths(const int64_t *starts, const int64_t *ends, size_t n,
                       int64_t *out) {
  for (size_t i = 0; i < n; ++i)
    out[i] = ends[i] - starts[i];
}

int64_t faf_batch_total(const int64_t *starts, const int64_t *ends, size_t n) {
  int64_t total = 0;
  for (size_t i = 0; i < n; ++i)
    total += ends[i] - starts[i];
  return total;
}

void faf_batch_find(const char *data, const int64_t *starts,
                    const int64_t *ends, size_t n, const char *needle,
                    size_t needle_len, int64_t *out) {
  faf_string sub = faf_string_init_n(needle, needle_len);
  for (size_t i = 0; i < n; ++i) {
    size_t at = faf_string_find(view(data, starts, ends, i), sub);
    out[i] = at == FAF_NPOS ? -1 : (int64_t)at;
  }
}

void faf_batch_count(const char *data, const int64_t *starts,
                     const int64_t *ends, size_t n, const char *needle,
                     size_t needle_len, int64_t *out) {
  faf_string sub = faf_string_init_n(needle, needle_len);
  for (size_t i = 0; i < n; ++i)
    out[i] = (int64_t)faf_string_count(view(data, starts, ends, i), sub);
}

// Stamps out a 1 / 0 per string function over a predicate f(str, needle).
#define FAF_BATCH_TEST(name, f)                                                \
  size_t name(const char *data, const int64_t *starts, const int64_t *ends,    \
              size_t n, const char *needle, size_t needle_len, uint8_t *out) { \
    faf_string sub = faf_string_init_n(needle, needle_len);                    \
    size_t yes = 0;                                                            \
    for (size_t i = 0; i < n; ++i) {                                           \
      out[i] = f(view(data, starts, ends, i), sub);                            \
      yes += out[i];                                                           \
    }                                                                          \
    return yes;                                                                \
  }

FAF_BATCH_TEST(faf_batch_contains, faf_string_contains)
FAF_BATCH_TEST(faf_batch_starts_with, faf_string_starts_with)
FAF_BATCH_TEST(faf_batch_ends_with, faf_string_ends_with)
FAF_BATCH_TEST(faf_batch_eq, faf_string_eq)
FAF_BATCH_TEST(faf_batch_eq_icase, faf_string_eq_icase)

void faf_batch_hash(const char *data, const int64_t *starts,
                    const int64_t *ends, size_t n, uint64_t seed,
                    uint64_t *out) {
  for (size_t i = 0; i < n; ++i)
    out[i] = faf_string_hash_seed(view(data, starts, ends, i), seed);
}

/* ---- New bytes ---- */

void faf_batch_compact(const char *data, const int64_t *starts,
                       const int64_t *ends, size_t n, char *dst,
                       int64_t *dst_offsets) {
  int64_t at = 0;
  dst_offsets[0] = 0;
  for (size_t i = 0; i < n; ++i) {
    size_t len = (size_t)(ends[i] - starts[i]);
    faf_memcpy(dst + at, data + starts[i], len);
    at += (int64_t)len;
    dst_offsets[i + 1] = at;
  }
}

void faf_batch_ascii_case(const char *data, const int64_t *starts,
                          const int64_t *ends, size_t n, int upper, char *dst,
                          int64_t *dst_offsets) {
  int64_t at = 0;
  dst_offsets[0] = 0;
  for (size_t i = 0; i < n; ++i) {
    size_t len = (size_t)(ends[i] - starts[i]);
    faf_k_ascii_case(dst + at, data + starts[i], len, upper != 0);
    at += (int64_t)len;
    dst_offsets[i + 1] = at;
  }
}

int64_t faf_batch_span(const int64_t *starts, const int64_t *ends, size_t n,
                       int64_t *lo) {
  if (n == 0) {
    *lo = 0;
    return 0;
  }
  int64_t min = starts[0], max = ends[0];
  for (size_t i = 1; i < n; ++i) {
    min = starts[i] < min ? starts[i] : min;
    max = ends[i] > max ? ends[i] : max;
  }
  *lo = min;
  return max - min;
}

void faf_batch_ascii_case_span(const char *data, const int64_t *starts,
                               const int64_t *ends, size_t n, int upper,
                               char *dst, int64_t *out_starts,
                               int64_t *out_ends) {
  int64_t lo;
  int64_t len = faf_batch_span(starts, ends, n, &lo);
  faf_k_ascii_case(dst, data + lo, (size_t)len, upper != 0);
  if (out_starts && out_ends)
    for (size_t i = 0; i < n; ++i) {
      out_starts[i] = starts[i] - lo;
      out_ends[i] = ends[i] - lo;
    }
}

void faf_batch_ascii_case_range(const char *data, int64_t lo, int64_t len,
                                int upper, char *dst) {
  faf_k_ascii_case(dst, data + lo, (size_t)len, upper != 0);
}

void faf_batch_ascii_case_inplace(char *data, const int64_t *starts,
                                  const int64_t *ends, size_t n, int upper) {
  for (size_t i = 0; i < n; ++i)
    faf_k_ascii_case(data + starts[i], data + starts[i],
                     (size_t)(ends[i] - starts[i]), upper != 0);
}

/* ---- Memory for results ---- */

#if FAF_ARENAS

// A region handle as an integer: pool + 1 in the low 16 bits (so 0 is none),
// the generation above.
static inline uint64_t handle_of(faf_region r) {
  return faf_region_valid(r) ? ((uint64_t)r.gen << 16) | (uint64_t)(r.pool + 1)
                             : 0;
}

static inline faf_region region_of(void *arena, uint64_t h) {
  if ((h & 0xFFFF) == 0)
    return FAF_REGION_NONE;
  return (faf_region){.arena = (faf_arena *)arena,
                      .pool = (uint16_t)((h & 0xFFFF) - 1),
                      .gen = (uint16_t)(h >> 16)};
}

size_t faf_ffi_arena_size(void) { return sizeof(faf_arena); }

size_t faf_ffi_arena_bytes(size_t npools, size_t pool_bytes) {
  return faf_arena_bytes(npools, (pool_bytes + FAF_SLOT_BYTES - 1) / FAF_SLOT_BYTES);
}

bool faf_ffi_arena_init(void *arena, void *buf, size_t nbytes, size_t npools) {
  return faf_arena_init((faf_arena *)arena, buf, nbytes, npools);
}

uint64_t faf_ffi_region_acquire(void *arena) {
  return handle_of(faf_arena_acquire((faf_arena *)arena));
}

size_t faf_ffi_region_capacity(void *arena) {
  return ((faf_arena *)arena)->pool_slots * FAF_SLOT_BYTES;
}

void *faf_ffi_reserve(void *arena, uint64_t region, size_t bytes) {
  size_t slots = (bytes + FAF_SLOT_BYTES - 1) / FAF_SLOT_BYTES;
  faf_span sp = faf_reserve(region_of(arena, region), slots ? slots : 1);
  return sp.ptr;
}

void faf_ffi_region_release(void *arena, uint64_t region) {
  faf_region_release(region_of(arena, region));
}

#endif // FAF_ARENAS

int64_t faf_batch_join(const char *data, const int64_t *starts,
                       const int64_t *ends, size_t n, const char *sep,
                       size_t sep_len, char *dst) {
  int64_t at = 0;
  for (size_t i = 0; i < n; ++i) {
    if (i > 0) {
      faf_memcpy(dst + at, sep, sep_len);
      at += (int64_t)sep_len;
    }
    size_t len = (size_t)(ends[i] - starts[i]);
    faf_memcpy(dst + at, data + starts[i], len);
    at += (int64_t)len;
  }
  return at;
}
