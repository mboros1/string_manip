#include "faf.h"
#include "faf_test.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// Batch results are checked against small reference implementations written
// here, not against the per-string library functions the batch calls.

// Buffers are allocated for each test (batch_setup), not static: the
// original ESP32 runs every test file in one image, and static data there
// comes out of the RAM FreeRTOS needs at startup.
#define MAXN 128
#define DST_BYTES 2048
#define SPLIT_BYTES 1024
static int64_t *starts, *ends, *out64, *ref64, *os, *oe, *dst_off;
static uint8_t *out8;
static uint64_t *outh;
static char *dst, *split_buf;

// Deterministic xorshift.
static uint32_t rng_state = 2463534242u;
static uint32_t rng(void) {
  uint32_t x = rng_state;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  return rng_state = x;
}

// Reference split: pieces of s[0, len) between `sep` bytes.
static size_t ref_split(const char *s, size_t len, char sep, int64_t *st,
                        int64_t *en) {
  size_t k = 0, from = 0;
  for (size_t i = 0; i < len; ++i)
    if (s[i] == sep) {
      st[k] = (int64_t)from, en[k] = (int64_t)i, ++k;
      from = i + 1;
    }
  st[k] = (int64_t)from, en[k] = (int64_t)len;
  return k + 1;
}

static int64_t ref_find(const char *s, size_t n, const char *sub, size_t m) {
  for (size_t i = 0; i + m <= n; ++i)
    if (memcmp(s + i, sub, m) == 0)
      return (int64_t)i;
  return -1;
}

static int64_t ref_count(const char *s, size_t n, const char *sub, size_t m) {
  if (m == 0)
    return (int64_t)n + 1;
  int64_t c = 0;
  for (size_t i = 0; i + m <= n;)
    if (memcmp(s + i, sub, m) == 0)
      ++c, i += m;
    else
      ++i;
  return c;
}

static char lower(char c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

static void check_split(const char *s, size_t len, char sep, const char *what) {
  int64_t *rs = out64, *re = ref64; // scratch for the reference
  size_t want = ref_split(s, len, sep, rs, re);
  ASSERT_INT_EQ((int)want, (int)faf_batch_split_count(s, len, sep), what);
  size_t got = faf_batch_split(s, len, sep, starts, ends, MAXN);
  ASSERT_INT_EQ((int)want, (int)got, what);
  for (size_t i = 0; i < want; ++i) {
    ASSERT_INT_EQ((int)rs[i], (int)starts[i], what);
    ASSERT_INT_EQ((int)re[i], (int)ends[i], what);
  }
}

static void test_split_cases(void) {
  const char *cases[] = {"", "\n", "a", "a\n", "\na", "a\n\nb", "\n\n\n",
                         "one\ntwo\nthree", "no separators at all here"};
  for (size_t c = 0; c < sizeof cases / sizeof cases[0]; ++c)
    check_split(cases[c], strlen(cases[c]), '\n', cases[c]);
}

static void test_split_random(void) {
  // separators at every density and position, across the 64-per-scan batches
  char *buf = split_buf;
  for (int round = 0; round < 200; ++round) {
    size_t len = rng() % SPLIT_BYTES;
    unsigned density = 1 + rng() % 40; // one separator per `density` bytes
    for (size_t i = 0; i < len; ++i)
      buf[i] = rng() % density == 0 ? ',' : (char)('a' + rng() % 26);
    size_t pieces = faf_batch_split_count(buf, len, ',');
    if (pieces > MAXN)
      continue;
    check_split(buf, len, ',', "random split");
  }
}

static void test_split_cap(void) {
  const char *s = "a,b,c,d,e";
  int64_t st[3] = {-7, -7, -7}, en[3] = {-7, -7, -7};
  ASSERT_INT_EQ(5, (int)faf_batch_split(s, strlen(s), ',', st, en, 2),
                "split returns the total when capped");
  ASSERT_TRUE(st[1] == 2 && en[1] == 3, "second piece wrong when capped");
  ASSERT_TRUE(st[2] == -7 && en[2] == -7, "split wrote past cap");
  ASSERT_INT_EQ(5, (int)faf_batch_split(s, strlen(s), ',', NULL, NULL, 0),
                "split with cap 0 counts");
}

// A batch of random words with gaps between them, and some overlapping views.
static const char *words = "Error: disk FULL; error again, ERROR x; errors";
static size_t make_views(void) {
  size_t len = strlen(words), n = 0;
  for (; n < 64; ++n) {
    size_t a = rng() % (len + 1), b = rng() % (len + 1);
    starts[n] = (int64_t)(a < b ? a : b);
    ends[n] = (int64_t)(a < b ? b : a);
  }
  return n;
}

static void test_search(void) {
  const char *needles[] = {"", "e", "rr", "error", "ERROR", "errors", "zz",
                           "Error: disk FULL; error again, ERROR x; errors!"};
  size_t n = make_views();
  for (size_t k = 0; k < sizeof needles / sizeof needles[0]; ++k) {
    const char *nd = needles[k];
    size_t m = strlen(nd);
    faf_batch_find(words, starts, ends, n, nd, m, out64);
    for (size_t i = 0; i < n; ++i)
      ref64[i] = ref_find(words + starts[i], (size_t)(ends[i] - starts[i]), nd, m);
    ASSERT_TRUE(memcmp(out64, ref64, n * sizeof *out64) == 0, nd);

    faf_batch_count(words, starts, ends, n, nd, m, out64);
    for (size_t i = 0; i < n; ++i)
      ref64[i] = ref_count(words + starts[i], (size_t)(ends[i] - starts[i]), nd, m);
    ASSERT_TRUE(memcmp(out64, ref64, n * sizeof *out64) == 0, nd);

    size_t yes = faf_batch_contains(words, starts, ends, n, nd, m, out8);
    size_t want_yes = 0;
    for (size_t i = 0; i < n; ++i) {
      int want = ref_find(words + starts[i], (size_t)(ends[i] - starts[i]), nd, m) >= 0;
      ASSERT_INT_EQ(want, out8[i], nd);
      want_yes += (size_t)want;
    }
    ASSERT_INT_EQ((int)want_yes, (int)yes, "contains count");
  }
}

static void test_prefix_suffix_eq(void) {
  size_t n = make_views();
  const char *probes[] = {"", "E", "Error", "rror", "s", "errors",
                          "error again, ERROR x; errors"};
  for (size_t k = 0; k < sizeof probes / sizeof probes[0]; ++k) {
    const char *p = probes[k];
    size_t m = strlen(p);
    faf_batch_starts_with(words, starts, ends, n, p, m, out8);
    for (size_t i = 0; i < n; ++i) {
      size_t len = (size_t)(ends[i] - starts[i]);
      int want = m <= len && memcmp(words + starts[i], p, m) == 0;
      ASSERT_INT_EQ(want, out8[i], "starts_with");
    }
    faf_batch_ends_with(words, starts, ends, n, p, m, out8);
    for (size_t i = 0; i < n; ++i) {
      size_t len = (size_t)(ends[i] - starts[i]);
      int want = m <= len && memcmp(words + ends[i] - m, p, m) == 0;
      ASSERT_INT_EQ(want, out8[i], "ends_with");
    }
    faf_batch_eq(words, starts, ends, n, p, m, out8);
    for (size_t i = 0; i < n; ++i) {
      size_t len = (size_t)(ends[i] - starts[i]);
      int want = m == len && memcmp(words + starts[i], p, m) == 0;
      ASSERT_INT_EQ(want, out8[i], "eq");
    }
    faf_batch_eq_icase(words, starts, ends, n, p, m, out8);
    for (size_t i = 0; i < n; ++i) {
      size_t len = (size_t)(ends[i] - starts[i]);
      int want = m == len;
      for (size_t j = 0; want && j < m; ++j)
        want = lower(words[starts[i] + j]) == lower(p[j]);
      ASSERT_INT_EQ(want, out8[i], "eq_icase");
    }
  }
}

static void test_select_take(void) {
  size_t n = make_views();
  uint8_t mask[MAXN];
  size_t want = 0;
  for (size_t i = 0; i < n; ++i)
    want += (mask[i] = (uint8_t)(rng() % 3 == 0 ? 0 : rng() % 5 + 1)) != 0;
  size_t m = faf_batch_select(starts, ends, n, mask, os, oe);
  ASSERT_INT_EQ((int)want, (int)m, "select count");
  for (size_t i = 0, k = 0; i < n; ++i)
    if (mask[i]) {
      ASSERT_TRUE(os[k] == starts[i] && oe[k] == ends[i], "select view");
      ++k;
    }

  int64_t idx[5] = {3, 0, 3, 63, 42};
  faf_batch_take(starts, ends, idx, 5, os, oe);
  for (size_t i = 0; i < 5; ++i)
    ASSERT_TRUE(os[i] == starts[idx[i]] && oe[i] == ends[idx[i]], "take view");
}

static void test_lengths_hash(void) {
  size_t n = make_views();
  int64_t total = 0;
  faf_batch_lengths(starts, ends, n, out64);
  for (size_t i = 0; i < n; ++i) {
    ASSERT_INT_EQ((int)(ends[i] - starts[i]), (int)out64[i], "length");
    total += out64[i];
  }
  ASSERT_TRUE(total == faf_batch_total(starts, ends, n), "total");

  faf_batch_hash(words, starts, ends, n, 7, outh);
  for (size_t i = 0; i < n; ++i) {
    faf_string s = faf_string_init_n(words + starts[i],
                                     (size_t)(ends[i] - starts[i]));
    ASSERT_TRUE(outh[i] == faf_string_hash_seed(s, 7), "hash");
  }
}

static void test_new_bytes(void) {
  size_t n = make_views();
  int64_t total = faf_batch_total(starts, ends, n);
  ASSERT_TRUE(total < (int64_t)DST_BYTES, "test buffer too small");

  memset(dst, '#', DST_BYTES);
  faf_batch_compact(words, starts, ends, n, dst, dst_off);
  ASSERT_TRUE(dst_off[0] == 0 && dst_off[n] == total, "compact offsets");
  for (size_t i = 0; i < n; ++i) {
    size_t len = (size_t)(ends[i] - starts[i]);
    ASSERT_TRUE(dst_off[i + 1] - dst_off[i] == (int64_t)len, "compact length");
    ASSERT_TRUE(memcmp(dst + dst_off[i], words + starts[i], len) == 0,
                "compact bytes");
  }
  ASSERT_TRUE(dst[total] == '#', "compact wrote past its total");

  for (int upper = 0; upper <= 1; ++upper) {
    memset(dst, '#', DST_BYTES);
    faf_batch_ascii_case(words, starts, ends, n, upper, dst, dst_off);
    for (size_t i = 0; i < n; ++i)
      for (int64_t j = 0; j < ends[i] - starts[i]; ++j) {
        char c = words[starts[i] + j];
        char want = upper ? (c >= 'a' && c <= 'z' ? c - 32 : c) : lower(c);
        ASSERT_TRUE(dst[dst_off[i] + j] == want, "ascii_case byte");
      }
    ASSERT_TRUE(dst[total] == '#', "ascii_case wrote past its total");
  }

  memset(dst, '#', DST_BYTES);
  int64_t wrote = faf_batch_join(words, starts, ends, n, "\r\n", 2, dst);
  ASSERT_TRUE(wrote == total + 2 * (int64_t)(n - 1), "join size");
  ASSERT_TRUE(dst[wrote] == '#', "join wrote past its size");
  int64_t at = 0;
  for (size_t i = 0; i < n; ++i) {
    if (i > 0) {
      ASSERT_TRUE(memcmp(dst + at, "\r\n", 2) == 0, "join separator");
      at += 2;
    }
    size_t len = (size_t)(ends[i] - starts[i]);
    ASSERT_TRUE(memcmp(dst + at, words + starts[i], len) == 0, "join bytes");
    at += (int64_t)len;
  }
  ASSERT_INT_EQ(0, (int)faf_batch_join(words, starts, ends, 0, "\n", 1, dst),
                "join of nothing");
}

static void test_high_bytes(void) {
  // bytes >= 0x80 (UTF-8) are ordinary bytes: searched, never case mapped
  const char *s = "caf\xc3\xa9\n\xc3\x89T\xc3\x89\n\xff\xfe";
  size_t n = faf_batch_split(s, strlen(s), '\n', starts, ends, MAXN);
  ASSERT_INT_EQ(3, (int)n, "utf-8 split");
  faf_batch_find(s, starts, ends, n, "\xc3\xa9", 2, out64);
  ASSERT_TRUE(out64[0] == 3 && out64[1] == -1 && out64[2] == -1, "utf-8 find");
  faf_batch_ascii_case(s, starts, ends, n, 0, dst, dst_off);
  ASSERT_TRUE(memcmp(dst, "caf\xc3\xa9\xc3\x89t\xc3\x89\xff\xfe", 12) == 0,
              "lower changed a non-ASCII byte");
}

static void test_span_modes(void) {
  int64_t lo = -1;
  ASSERT_INT_EQ(0, (int)faf_batch_span(starts, ends, 0, &lo), "empty span");
  ASSERT_INT_EQ(0, (int)lo, "empty span start");

  size_t n = make_views(); // random, overlapping, unsorted
  for (size_t i = 0; i < n; ++i) { // keep the span off 0, so lo matters
    starts[i] = starts[i] < 5 ? 5 : starts[i];
    ends[i] = ends[i] < starts[i] ? starts[i] : ends[i];
  }
  int64_t min = starts[0], max = ends[0];
  for (size_t i = 1; i < n; ++i) {
    min = starts[i] < min ? starts[i] : min;
    max = ends[i] > max ? ends[i] : max;
  }
  int64_t len = faf_batch_span(starts, ends, n, &lo);
  ASSERT_TRUE(lo == min && len == max - min, "span bounds");
  ASSERT_TRUE(lo >= 5, "span should start after 0");

  for (int upper = 0; upper <= 1; ++upper) {
    memset(dst, '#', DST_BYTES);
    faf_batch_ascii_case_span(words, starts, ends, n, upper, dst, os, oe);
    ASSERT_TRUE(dst[len] == '#', "span wrote past its length");
    for (int64_t j = 0; j < len; ++j) {
      char c = words[lo + j];
      char want = upper ? (c >= 'a' && c <= 'z' ? c - 32 : c) : lower(c);
      ASSERT_TRUE(dst[j] == want, "span byte");
    }
    for (size_t i = 0; i < n; ++i)
      ASSERT_TRUE(os[i] == starts[i] - lo && oe[i] == ends[i] - lo, "span views");
  }

  // lo == 0: views may be reused, NULL outputs are not written
  size_t m = faf_batch_split(words, strlen(words), ' ', starts, ends, MAXN);
  ASSERT_INT_EQ(0, (int)starts[0], "split starts at 0");
  memset(dst, '#', DST_BYTES);
  faf_batch_ascii_case_span(words, starts, ends, m, 0, dst, NULL, NULL);
  for (size_t i = 0; i < m; ++i)
    for (int64_t j = starts[i]; j < ends[i]; ++j)
      ASSERT_TRUE(dst[j] == lower(words[j]), "span with reused views");
}

static void test_inplace(void) {
  // overlapping and unsorted, with gaps: [0,5) [3,9) [20,31) [40,40) [44,46)
  const int64_t vs[] = {20, 0, 3, 40, 44}, ve[] = {31, 5, 9, 40, 46};
  size_t n = sizeof vs / sizeof vs[0];
  memcpy(starts, vs, sizeof vs);
  memcpy(ends, ve, sizeof ve);
  size_t len = strlen(words);
  char *buf = split_buf;
  memcpy(buf, words, len);
  uint8_t covered[64] = {0};
  for (size_t i = 0; i < n; ++i)
    for (int64_t j = starts[i]; j < ends[i]; ++j)
      covered[j] = 1;
  faf_batch_ascii_case_inplace(buf, starts, ends, n, 1);
  int outside = 0;
  for (size_t j = 0; j < len; ++j) {
    char c = words[j];
    char want = covered[j] ? (c >= 'a' && c <= 'z' ? c - 32 : c) : c;
    ASSERT_TRUE(buf[j] == want, "in place byte");
    outside += !covered[j];
  }
  ASSERT_TRUE(outside > 0, "test needs bytes outside the views");
}

#if FAF_ARENAS
static void test_ffi_arena(void) {
  void *arena = malloc(faf_ffi_arena_size());
  size_t need = faf_ffi_arena_bytes(3, 100);
  ASSERT_TRUE(need > 300 && need <= SPLIT_BYTES, "arena bytes");
  ASSERT_TRUE(faf_ffi_arena_init(arena, split_buf + 1, need, 3), "init at need");
  ASSERT_TRUE(faf_ffi_region_capacity(arena) >= 100, "pools hold pool_bytes");
  const size_t nbytes = 1024;
  char *buf = split_buf; // SPLIT_BYTES of heap
  ASSERT_TRUE(faf_ffi_arena_init(arena, buf, nbytes, 2), "arena init");
  ASSERT_TRUE(!faf_ffi_arena_init(arena, buf, 4, 2), "tiny arena accepted");
  ASSERT_TRUE(faf_ffi_arena_init(arena, buf, nbytes, 2), "arena re-init");

  uint64_t a = faf_ffi_region_acquire(arena), b = faf_ffi_region_acquire(arena);
  ASSERT_TRUE(a && b && a != b, "two regions");
  ASSERT_TRUE(faf_ffi_region_acquire(arena) == 0, "third region from two pools");
  size_t cap = faf_ffi_region_capacity(arena);
  ASSERT_TRUE(cap > 0 && cap <= nbytes / 2, "capacity");

  ASSERT_TRUE(faf_ffi_reserve(arena, a, cap + 1) == NULL, "reserved past capacity");
  char *p = faf_ffi_reserve(arena, a, cap);
  ASSERT_TRUE(p >= buf && p + cap <= buf + nbytes, "region memory in the buffer");
  ASSERT_TRUE((uintptr_t)p % FAF_SLOT_BYTES == 0, "slot aligned");
  memset(p, 'x', cap);
  ASSERT_TRUE(faf_ffi_reserve(arena, a, 1) == NULL, "full region reserved more");
  char *q = faf_ffi_reserve(arena, b, 10);
  ASSERT_TRUE(q && (q + 10 <= p || q >= p + cap), "regions overlap");

  faf_ffi_region_release(arena, a);
  ASSERT_TRUE(faf_ffi_reserve(arena, a, 1) == NULL, "stale handle reserved");
  uint64_t c = faf_ffi_region_acquire(arena);
  ASSERT_TRUE(c && c != a, "reacquired region has a new handle");
  faf_ffi_region_release(arena, a); // stale: must not free c
  ASSERT_TRUE(faf_ffi_reserve(arena, c, cap) != NULL, "stale release freed the owner");
  faf_ffi_region_release(arena, 0); // ignored
  faf_ffi_region_release(arena, b);
  faf_ffi_region_release(arena, c);
  ASSERT_TRUE(faf_ffi_region_acquire(arena) != 0, "released regions reusable");
  free(arena);
}
#endif

static test_case_t batch_tests[] = {
    {"split_cases", test_split_cases},
    {"split_random", test_split_random},
    {"split_cap", test_split_cap},
    {"search", test_search},
    {"prefix_suffix_eq", test_prefix_suffix_eq},
    {"select_take", test_select_take},
    {"lengths_hash", test_lengths_hash},
    {"new_bytes", test_new_bytes},
    {"high_bytes", test_high_bytes},
    {"span_modes", test_span_modes},
    {"inplace", test_inplace},
#if FAF_ARENAS
    {"ffi_arena", test_ffi_arena},
#endif
};

static void batch_setup(void) {
  int64_t **arrays[] = {&starts, &ends, &out64, &ref64, &os, &oe};
  for (size_t i = 0; i < sizeof arrays / sizeof arrays[0]; ++i)
    *arrays[i] = malloc(MAXN * sizeof(int64_t));
  dst_off = malloc((MAXN + 1) * sizeof(int64_t));
  out8 = malloc(MAXN);
  outh = malloc(MAXN * sizeof(uint64_t));
  dst = malloc(DST_BYTES);
  split_buf = malloc(SPLIT_BYTES);
}

static void batch_teardown(void) {
  free(starts), free(ends), free(out64), free(ref64), free(os), free(oe);
  free(dst_off), free(out8), free(outh), free(dst), free(split_buf);
}

int main(int argc, char **argv) {
  test_suite_t suite =
      TEST_SUITE("Batch", batch_tests, sizeof(batch_tests) / sizeof(batch_tests[0]),
                 batch_setup, batch_teardown);
  register_test_suite(suite);
  return test_main(argc, argv);
}
