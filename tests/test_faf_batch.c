#include "faf.h"
#include "faf_test.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// Batch results are checked against small reference implementations written
// here, not against the per-string library functions the batch calls.
//
// Buffers are allocated for each test (batch_setup), not static: the
// original ESP32 runs every test file in one image, and static data there
// comes out of the RAM FreeRTOS needs at startup. Batches come from an arena
// of NPOOLS pools over a heap buffer.

#define MAXN 128
#define DST_BYTES 2048
#define SPLIT_BYTES 1024
#define ARENA_BYTES (32 * 1024)
#define NPOOLS 2
static int64_t *starts, *ends, *out64, *ref64;
static uint8_t *out8;
static uint64_t *outh;
static char *dst, *split_buf, *arena_buf;
static faf_arena *A;
static faf_region R; // each test's region (batch_setup)
static faf_ctx C;    // results go to R

// A new, empty region: for tests that make many batches.
static void fresh(void) {
  faf_region_release(R);
  C.out = R = faf_arena_acquire(A);
}

// Deterministic xorshift.
static uint32_t rng_state = 2463534242u;
static uint32_t rng(void) {
  uint32_t x = rng_state;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  return rng_state = x;
}

static char lower(char c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
static char upper(char c) { return c >= 'a' && c <= 'z' ? c - 32 : c; }

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

// b must hold exactly the strings `data`[st[i], en[i]) for i < n.
static bool same_strings(const faf_batch *b, const char *data, const int64_t *st,
                         const int64_t *en, size_t n) {
  if (faf_batch_len(b) != n)
    return false;
  const char *bd = faf_batch_data(b);
  const int64_t *bs = faf_batch_starts(b), *be = faf_batch_ends(b);
  for (size_t i = 0; i < n; ++i)
    if (be[i] - bs[i] != en[i] - st[i] ||
        memcmp(bd + bs[i], data + st[i], (size_t)(en[i] - st[i])) != 0)
      return false;
  return true;
}

static void check_split(const char *s, size_t len, char sep, const char *what) {
  fresh();
  size_t want = ref_split(s, len, sep, out64, ref64);
  faf_batch *b = faf_batch_split(&C, s, len, sep);
  ASSERT_TRUE(b != 0, what);
  ASSERT_INT_EQ((int)want, (int)faf_batch_len(b), what);
  ASSERT_TRUE(faf_batch_data(b) == s, "split copied the data");
  const int64_t *bs = faf_batch_starts(b), *be = faf_batch_ends(b);
  for (size_t i = 0; b && i < want; ++i) {
    ASSERT_INT_EQ((int)out64[i], (int)bs[i], what);
    ASSERT_INT_EQ((int)ref64[i], (int)be[i], what);
  }
}

static void test_split_cases(void) {
  const char *cases[] = {"", "\n", "a", "a\n", "\na", "a\n\nb", "\n\n\n",
                         "one\ntwo\nthree", "no separators at all here"};
  for (size_t c = 0; c < sizeof cases / sizeof cases[0]; ++c)
    check_split(cases[c], strlen(cases[c]), '\n', cases[c]);
}

// Separators at every density and position, across the 64-per-scan batches.
static void split_rounds(int rounds) {
  char *buf = split_buf;
  for (int round = 0; round < rounds; ++round) {
    size_t len = rng() % SPLIT_BYTES;
    unsigned density = 1 + rng() % 40; // one separator per `density` bytes
    size_t pieces = 1;
    for (size_t i = 0; i < len; ++i) {
      buf[i] = rng() % density == 0 ? ',' : (char)('a' + rng() % 26);
      pieces += buf[i] == ',';
    }
    if (pieces > MAXN)
      continue;
    check_split(buf, len, ',', "random split");
  }
}

static void test_split_random(void) { split_rounds(200); }

// Scattered views of a short string: random, overlapping, unsorted.
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
  faf_batch *b = faf_batch_from_views(&C, words, starts, ends, n);
  ASSERT_TRUE(b != 0, "from_views failed");
  for (size_t k = 0; k < sizeof needles / sizeof needles[0]; ++k) {
    const char *nd = needles[k];
    size_t m = strlen(nd);
    faf_batch_find(b, nd, m, out64);
    for (size_t i = 0; i < n; ++i)
      ref64[i] = ref_find(words + starts[i], (size_t)(ends[i] - starts[i]), nd, m);
    ASSERT_TRUE(memcmp(out64, ref64, n * sizeof *out64) == 0, nd);

    faf_batch_count(b, nd, m, out64);
    for (size_t i = 0; i < n; ++i)
      ref64[i] = ref_count(words + starts[i], (size_t)(ends[i] - starts[i]), nd, m);
    ASSERT_TRUE(memcmp(out64, ref64, n * sizeof *out64) == 0, nd);

    size_t yes = faf_batch_contains(b, nd, m, out8), want_yes = 0;
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
  faf_batch *b = faf_batch_from_views(&C, words, starts, ends, n);
  const char *probes[] = {"", "E", "Error", "rror", "s", "errors",
                          "error again, ERROR x; errors"};
  for (size_t k = 0; k < sizeof probes / sizeof probes[0]; ++k) {
    const char *p = probes[k];
    size_t m = strlen(p);
    faf_batch_starts_with(b, p, m, out8);
    for (size_t i = 0; i < n; ++i) {
      size_t len = (size_t)(ends[i] - starts[i]);
      ASSERT_INT_EQ(m <= len && memcmp(words + starts[i], p, m) == 0, out8[i], "starts_with");
    }
    faf_batch_ends_with(b, p, m, out8);
    for (size_t i = 0; i < n; ++i) {
      size_t len = (size_t)(ends[i] - starts[i]);
      ASSERT_INT_EQ(m <= len && memcmp(words + ends[i] - m, p, m) == 0, out8[i], "ends_with");
    }
    faf_batch_eq(b, p, m, out8);
    for (size_t i = 0; i < n; ++i) {
      size_t len = (size_t)(ends[i] - starts[i]);
      ASSERT_INT_EQ(m == len && memcmp(words + starts[i], p, m) == 0, out8[i], "eq");
    }
    faf_batch_eq_icase(b, p, m, out8);
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
  faf_batch *b = faf_batch_from_views(&C, words, starts, ends, n);
  uint8_t mask[MAXN];
  size_t want = 0;
  for (size_t i = 0; i < n; ++i)
    want += (mask[i] = (uint8_t)(rng() % 3 == 0 ? 0 : rng() % 5 + 1)) != 0;
  faf_batch *s = faf_batch_select(&C, b, mask);
  ASSERT_INT_EQ((int)want, (int)faf_batch_len(s), "select count");
  ASSERT_TRUE(faf_batch_data(s) == words, "select copied bytes");
  const int64_t *ss = faf_batch_starts(s), *se = faf_batch_ends(s);
  for (size_t i = 0, k = 0; s && i < n; ++i)
    if (mask[i]) {
      ASSERT_TRUE(ss[k] == starts[i] && se[k] == ends[i], "select view");
      ++k;
    }

  int64_t idx[5] = {3, 0, 3, 63, 42};
  faf_batch *t = faf_batch_take(&C, b, idx, 5);
  const int64_t *ts = faf_batch_starts(t), *te = faf_batch_ends(t);
  for (size_t i = 0; t && i < 5; ++i)
    ASSERT_TRUE(ts[i] == starts[idx[i]] && te[i] == ends[idx[i]], "take view");
}

static void test_lengths_hash(void) {
  size_t n = make_views();
  faf_batch *b = faf_batch_from_views(&C, words, starts, ends, n);
  int64_t total = 0;
  faf_batch_lengths(b, out64);
  for (size_t i = 0; i < n; ++i) {
    ASSERT_INT_EQ((int)(ends[i] - starts[i]), (int)out64[i], "length");
    total += out64[i];
  }
  ASSERT_TRUE(total == faf_batch_total(b), "total");
  faf_batch_hash(b, 7, outh);
  for (size_t i = 0; i < n; ++i) {
    faf_string s = faf_string_init_n(words + starts[i], (size_t)(ends[i] - starts[i]));
    ASSERT_TRUE(outh[i] == faf_string_hash_seed(s, 7), "hash");
  }
}

// Every way lower / upper case can be taken: the results must match.
static void check_case(const faf_batch *b, const char *data, const int64_t *st,
                       const int64_t *en, size_t n, const char *what) {
  for (int up = 0; up <= 1; ++up) {
    faf_batch *r = faf_batch_ascii_case(&C, b, up);
    ASSERT_TRUE(r != 0, what);
    if (!r)
      return;
    ASSERT_INT_EQ((int)n, (int)faf_batch_len(r), what);
    const char *rd = faf_batch_data(r);
    const int64_t *rs = faf_batch_starts(r), *re = faf_batch_ends(r);
    int64_t lowest = n ? rs[0] : 0;
    for (size_t i = 0; i < n; ++i)
      lowest = rs[i] < lowest ? rs[i] : lowest;
    ASSERT_INT_EQ(0, (int)lowest, "result doesn't start its own buffer");
    for (size_t i = 0; i < n; ++i) {
      ASSERT_TRUE(re[i] - rs[i] == en[i] - st[i], what);
      for (int64_t j = 0; j < en[i] - st[i]; ++j) {
        char c = data[st[i] + j];
        ASSERT_TRUE(rd[rs[i] + j] == (up ? upper(c) : lower(c)), what);
      }
    }
  }
}

static void check_scattered(void);

static void test_case_paths(void) {
  // dense from a split: one pass over the range
  const char *text = "Abc,DEF,,gH,\xc3\x89t\xc3\xa9";
  size_t len = strlen(text);
  faf_batch *s = faf_batch_split(&C, text, len, ',');
  size_t n = ref_split(text, len, ',', starts, ends);
  check_case(s, text, starts, ends, n, "split");
  // views from 0: the result shares them; from elsewhere: its own
  faf_batch *r = faf_batch_ascii_case(&C, s, 0);
  ASSERT_TRUE(faf_batch_starts(r) == faf_batch_starts(s) &&
                  faf_batch_ends(r) == faf_batch_ends(s),
              "views from 0 were copied");
  // dense Arrow offsets that don't start at 0
  int64_t offs[4] = {4, 7, 7, 12};
  faf_batch *o = faf_batch_from_offsets(&C, text, offs, 3);
  int64_t os[3] = {4, 7, 7}, oe[3] = {7, 7, 12};
  check_case(o, text, os, oe, 3, "offsets from 4");
  r = faf_batch_ascii_case(&C, o, 0);
  ASSERT_TRUE(faf_batch_starts(r) != faf_batch_starts(o), "shifted views shared");
  check_scattered();
  faf_batch *e = faf_batch_from_views(&C, words, starts, ends, 0);
  check_case(e, words, starts, ends, 0, "empty batch");
}

// Views out of order, where lower / upper case picks a layout by
// FAF_TUNE_CASE_ONE_PASS_PERCENT.
static void check_scattered(void) {
  // scattered but covering most of the range, and sparse
  size_t n = make_views();
  faf_batch *v = faf_batch_from_views(&C, words, starts, ends, n);
  check_case(v, words, starts, ends, n, "scattered views");
  int64_t ss[2] = {0, 40}, se[2] = {2, 42};
  faf_batch *sp = faf_batch_from_views(&C, words, ss, se, 2);
  check_case(sp, words, ss, se, 2, "sparse views");
  // two short strings further apart than a region holds: only packing them
  // (not one pass over their range) fits
  size_t far = ARENA_BYTES / NPOOLS + 100;
  char *wide = malloc(far + 2);
  memset(wide, 'Q', far + 2);
  int64_t ws[2] = {0, (int64_t)far}, we[2] = {2, (int64_t)far + 2};
  faf_batch *w = faf_batch_from_views(&C, wide, ws, we, 2);
  check_case(w, wide, ws, we, 2, "views wider than a region");
  free(wide);
}

static void test_tuning(void) {
  int64_t v = -1;
  ASSERT_TRUE(faf_tuning_get(NULL, FAF_TUNE_CASE_ONE_PASS_PERCENT, &v) &&
                  v == 200,
              "default");
  ASSERT_TRUE(!faf_tuning_get(NULL, 0, &v) && !faf_tuning_get(NULL, 99, &v),
              "unknown key read");
  faf_region tr = faf_arena_acquire(A); // the tuning outlives each fresh()
  faf_tuning *t = faf_tuning_new(tr);
  TEST_REQUIRE(t != NULL, "no room for a tuning");
  ASSERT_TRUE(!faf_tuning_set(t, 0, 5) && !faf_tuning_set(t, 99, 5),
              "unknown key set");
  ASSERT_TRUE(!faf_tuning_set(t, FAF_TUNE_CASE_ONE_PASS_PERCENT, -1) &&
                  !faf_tuning_set(t, FAF_TUNE_CASE_ONE_PASS_PERCENT, 1000001),
              "out of range set");
  faf_tuning_get(t, FAF_TUNE_CASE_ONE_PASS_PERCENT, &v);
  ASSERT_INT_EQ(200, (int)v, "a refused set changed the value");
  // tuning changes speed, never results: every strategy gives the same
  // strings (split: found one at a time, or in batches after the first 16)
  int64_t values[12] = {0, 1, 2, 16, 99, 200, 1000, 1000000};
  for (int i = 8; i < 12; ++i)
    values[i] = rng() % 1000001;
  C.tuning = t;
  for (int i = 0; i < 12; ++i) {
    ASSERT_TRUE(faf_tuning_set(t, FAF_TUNE_CASE_ONE_PASS_PERCENT, values[i]) &&
                    faf_tuning_set(t, FAF_TUNE_SPLIT_BATCH_GAP, values[11 - i]),
                "set");
    fresh();
    check_scattered();
    split_rounds(40);
  }
  // and the key is read: 0 packs the strings, the most converts the range
  size_t n = make_views();
  faf_batch *vb = faf_batch_from_views(&C, words, starts, ends, n);
  faf_tuning_set(t, FAF_TUNE_CASE_ONE_PASS_PERCENT, 0);
  faf_batch *p = faf_batch_ascii_case(&C, vb, 0);
  ASSERT_TRUE(p && faf_batch_total(p) == faf_batch_total(vb) &&
                  faf_batch_ends(p)[n - 1] == faf_batch_total(vb),
              "percent 0 didn't pack");
  faf_tuning_set(t, FAF_TUNE_CASE_ONE_PASS_PERCENT, 1000000);
  faf_batch *o = faf_batch_ascii_case(&C, vb, 0);
  ASSERT_TRUE(o && faf_batch_ends(o)[n - 1] != faf_batch_total(vb),
              "the most didn't convert the range");
  C.tuning = NULL;
  faf_region_release(tr);
}

static void test_compact_join(void) {
  size_t n = make_views();
  faf_batch *b = faf_batch_from_views(&C, words, starts, ends, n);
  int64_t total = faf_batch_total(b);
  faf_batch *c = faf_batch_compact(&C, b);
  ASSERT_TRUE(same_strings(c, words, starts, ends, n), "compact strings");
  const int64_t *cs = faf_batch_starts(c), *ce = faf_batch_ends(c);
  ASSERT_TRUE(c && cs[0] == 0 && ce[n - 1] == total, "compact is end to end");
  for (size_t i = 1; c && i < n; ++i)
    ASSERT_TRUE(cs[i] == ce[i - 1], "compact is end to end");

  memset(dst, '#', DST_BYTES);
  int64_t wrote = faf_batch_join(b, "\r\n", 2, dst);
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
  faf_batch *none = faf_batch_from_views(&C, words, starts, ends, 0);
  ASSERT_INT_EQ(0, (int)faf_batch_join(none, "\n", 1, dst), "join of nothing");
}

static void test_inplace(void) {
  // overlapping and unsorted, with gaps: [0,5) [3,9) [20,31) [40,40) [44,46)
  const int64_t vs[] = {20, 0, 3, 40, 44}, ve[] = {31, 5, 9, 40, 46};
  size_t n = sizeof vs / sizeof vs[0], len = strlen(words);
  char *buf = split_buf;
  memcpy(buf, words, len);
  uint8_t covered[64] = {0};
  for (size_t i = 0; i < n; ++i)
    for (int64_t j = vs[i]; j < ve[i]; ++j)
      covered[j] = 1;
  faf_batch *b = faf_batch_from_views(&C, buf, vs, ve, n);
  faf_batch_ascii_case_inplace(b, 1);
  int outside = 0;
  for (size_t j = 0; j < len; ++j) {
    ASSERT_TRUE(buf[j] == (covered[j] ? upper(words[j]) : words[j]), "in place byte");
    outside += !covered[j];
  }
  ASSERT_TRUE(outside > 0, "test needs bytes outside the views");
}

static void test_too_big(void) {
  // views that don't fit in the region: NULL, and the region is unchanged
  size_t len = ARENA_BYTES; // one separator per byte: 16 bytes of views each
  char *big = malloc(len);
  TEST_REQUIRE(big != NULL, "no memory for the input");
  memset(big, ',', len);
  size_t used = faf_region_used(R);
  ASSERT_TRUE(faf_batch_split(&C, big, len, ',') == NULL,
              "oversized split succeeded");
  ASSERT_INT_EQ((int)used, (int)faf_region_used(R), "a failed split used the region");
  free(big);
}

static void test_high_bytes(void) {
  // bytes >= 0x80 (UTF-8) are ordinary bytes: searched, never case mapped
  const char *s = "caf\xc3\xa9\n\xc3\x89T\xc3\x89\n\xff\xfe";
  faf_batch *b = faf_batch_split(&C, s, strlen(s), '\n');
  ASSERT_INT_EQ(3, (int)faf_batch_len(b), "utf-8 split");
  faf_batch_find(b, "\xc3\xa9", 2, out64);
  ASSERT_TRUE(out64[0] == 3 && out64[1] == -1 && out64[2] == -1, "utf-8 find");
  faf_batch *l = faf_batch_ascii_case(&C, b, 0);
  ASSERT_TRUE(memcmp(faf_batch_data(l), "caf\xc3\xa9\n\xc3\x89t\xc3\x89\n\xff\xfe", 14) == 0,
              "lower changed a non-ASCII byte");
}

static void test_null_where_empty(void) {
  // the header promises NULL is fine wherever the length is 0
  faf_batch *e = faf_batch_split(&C, NULL, 0, ',');
  ASSERT_INT_EQ(1, (int)faf_batch_len(e), "empty split is one empty string");
  faf_batch *z = faf_batch_from_offsets(&C, NULL, out64, 0); // out64[0]: any
  const char *s = "abc";
  faf_batch *b = faf_batch_split(&C, s, 3, ',');
  faf_batch *all[] = {e, z, b};
  for (size_t k = 0; k < 3; ++k) {
    faf_batch *x = all[k];
    faf_batch_find(x, NULL, 0, out64);
    faf_batch_count(x, NULL, 0, out64);
    faf_batch_contains(x, NULL, 0, out8);
    faf_batch_starts_with(x, NULL, 0, out8);
    faf_batch_ends_with(x, NULL, 0, out8);
    faf_batch_eq(x, NULL, 0, out8);
    faf_batch_eq_icase(x, NULL, 0, out8);
    faf_batch *r = faf_batch_ascii_case(&C, x, 0);
    faf_batch *c = faf_batch_compact(&C, x);
    ASSERT_TRUE(r && c, "result of an empty or NULL batch");
    faf_batch_join(x, NULL, 0, dst);
  }
  faf_batch_join(z, ",", 1, NULL); // nothing to write
  faf_batch *t = faf_batch_take(&C, b, NULL, 0);
  faf_batch *sel = faf_batch_select(&C, z, NULL);
  ASSERT_TRUE(t && sel && faf_batch_len(t) == 0 && faf_batch_len(sel) == 0, "empty take / select");
}

static void test_shifted_result(void) {
  // a result whose views had to be shifted (they didn't start at 0): every
  // operation must read it as the strings it holds
  const char *text = "xxxxAbc,DEF,,gH";
  int64_t offs[5] = {4, 7, 11, 12, 15};
  faf_batch *o = faf_batch_from_offsets(&C, text, offs, 4); // Abc ,DEF , ,gH
  faf_batch *r = faf_batch_ascii_case(&C, o, 0);
  const char *want[4] = {"abc", ",def", ",", ",gh"};
  int64_t st[4], en[4], pos = 0;
  static char flat[32];
  for (int i = 0; i < 4; ++i) {
    size_t len = strlen(want[i]);
    memcpy(flat + pos, want[i], len);
    st[i] = pos, en[i] = pos + (int64_t)len, pos += (int64_t)len;
  }
  ASSERT_TRUE(same_strings(r, flat, st, en, 4), "shifted strings");
  faf_batch_find(r, "d", 1, out64);
  ASSERT_TRUE(out64[0] == -1 && out64[1] == 1 && out64[3] == -1, "find");
  faf_batch_contains(r, "gh", 2, out8);
  ASSERT_TRUE(!out8[0] && !out8[1] && !out8[2] && out8[3], "contains");
  int64_t n = faf_batch_join(r, "|", 1, dst);
  ASSERT_TRUE(n == 14 && memcmp(dst, "abc|,def|,|,gh", 14) == 0, "join");
  uint8_t mask[4] = {0, 1, 0, 1};
  faf_batch *sel = faf_batch_select(&C, r, mask);
  int64_t ss[2] = {st[1], st[3]}, se[2] = {en[1], en[3]};
  ASSERT_TRUE(same_strings(sel, flat, ss, se, 2), "select");
  faf_batch *up = faf_batch_ascii_case(&C, sel, 1); // a result of a result
  const int64_t *us = faf_batch_starts(up);
  ASSERT_TRUE(up && memcmp(faf_batch_data(up) + us[0], ",DEF", 4) == 0,
              "upper of a selection");
  faf_batch_ascii_case_inplace(r, 1); // r's bytes are its own region's
  ASSERT_TRUE(memcmp(faf_batch_data(r), "ABC,DEF,,GH", 11) == 0, "in place");
}

static test_case_t batch_tests[] = {
    {"split_cases", test_split_cases},
    {"split_random", test_split_random},
    {"search", test_search},
    {"prefix_suffix_eq", test_prefix_suffix_eq},
    {"select_take", test_select_take},
    {"lengths_hash", test_lengths_hash},
    {"case_paths", test_case_paths},
    {"tuning", test_tuning},
    {"compact_join", test_compact_join},
    {"inplace", test_inplace},
    {"too_big", test_too_big},
    {"high_bytes", test_high_bytes},
    {"null_where_empty", test_null_where_empty},
    {"shifted_result", test_shifted_result},
};

static void batch_setup(void) {
  int64_t **arrays[] = {&starts, &ends, &out64, &ref64};
  for (size_t i = 0; i < sizeof arrays / sizeof arrays[0]; ++i)
    *arrays[i] = malloc(SPLIT_BYTES * sizeof(int64_t));
  out8 = malloc(MAXN);
  outh = malloc(MAXN * sizeof(uint64_t));
  dst = malloc(DST_BYTES);
  split_buf = malloc(SPLIT_BYTES);
  arena_buf = malloc(ARENA_BYTES);
  memset(arena_buf, 0xA5, ARENA_BYTES); // results must not rely on zeroed memory
  A = malloc(faf_arena_size());
  faf_arena_init(A, arena_buf, ARENA_BYTES, NPOOLS);
  C = (faf_ctx){.out = R = faf_arena_acquire(A)};
}

static void batch_teardown(void) {
  faf_region_release(R);
  free(starts), free(ends), free(out64), free(ref64), free(out8), free(outh);
  free(dst), free(split_buf), free(arena_buf), free(A);
}

int main(int argc, char **argv) {
  test_suite_t suite =
      TEST_SUITE("Batch", batch_tests, sizeof(batch_tests) / sizeof(batch_tests[0]),
                 batch_setup, batch_teardown);
  register_test_suite(suite);
  return test_main(argc, argv);
}
