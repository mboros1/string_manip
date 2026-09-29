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
