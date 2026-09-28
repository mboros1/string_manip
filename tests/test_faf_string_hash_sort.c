#include "faf.h"
#include "faf_test.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define S(lit) faf_string_init(lit)

static uint64_t rng_state = 0x9E3779B97F4A7C15ull;
static uint32_t rng(void) {
  rng_state ^= rng_state << 13;
  rng_state ^= rng_state >> 7;
  rng_state ^= rng_state << 17;
  return (uint32_t)rng_state;
}

void test_hash_basics(void) {
  ASSERT_TRUE(faf_string_hash(S("hello")) == faf_string_hash(S("hello")), "deterministic");
  ASSERT_TRUE(faf_string_hash(S("hello")) != faf_string_hash(S("hellp")), "differs");
  ASSERT_TRUE(faf_string_hash(S("")) != faf_string_hash(faf_string_init_n("\0", 1)),
              "length matters");
  ASSERT_TRUE(faf_string_hash_seed(S("hello"), 1) != faf_string_hash_seed(S("hello"), 2),
              "seed matters");
  // pinned: the hash must not change between backends, hosts or releases
  printf("    hash(\"hello\") = 0x%016llx\n", (unsigned long long)faf_string_hash(S("hello")));
  ASSERT_TRUE(faf_string_hash(S("hello")) == 0xa0b13b03c8b90939ull, "pinned hash value");
}

void test_hash_avalanche(void) {
  // flipping any one input bit should flip about half the output bits
  for (size_t len = 1; len <= 24; len += 7) {
    unsigned char buf[24];
    double total = 0;
    int samples = 0;
    for (int trial = 0; trial < 50; ++trial) {
      for (size_t i = 0; i < len; ++i)
        buf[i] = (unsigned char)rng();
      uint64_t h0 = faf_string_hash(faf_string_init_n((char *)buf, len));
      for (size_t bit = 0; bit < len * 8; ++bit) {
        buf[bit / 8] ^= (unsigned char)(1 << (bit % 8));
        uint64_t h1 = faf_string_hash(faf_string_init_n((char *)buf, len));
        buf[bit / 8] ^= (unsigned char)(1 << (bit % 8));
        total += __builtin_popcountll(h0 ^ h1);
        ++samples;
      }
    }
    double mean = total / samples;
    ASSERT_TRUE(mean > 30.0 && mean < 34.0, "poor avalanche");
  }
}

void test_sort_chars(void) {
  faf_region r = faf_region_acquire();
  ASSERT_STR_EQ("aaabnn", faf_string_sort_chars(r, S("banana")).start, "sort_chars");
  ASSERT_STR_EQ("", faf_string_sort_chars(r, S("")).start, "sort_chars empty");
  ASSERT_STR_EQ("Zaz\xA9\xC3", faf_string_sort_chars(r, S("z\xC3\xA9Za")).start,
                "sort_chars unsigned order");
  faf_region_release(r);
}

static int by_cmp(const void *a, const void *b) {
  return faf_string_cmp(*(const faf_string *)a, *(const faf_string *)b);
}

void test_arr_sort(void) {
  static char pool[4096 * 8];
  static faf_string items[4096], want[4096];
  const size_t sizes[] = {0, 1, 2, 3, 16, 17, 100, 1000, 4096};
  for (size_t si = 0; si < sizeof(sizes) / sizeof(sizes[0]); ++si) {
    size_t n = sizes[si];
    for (int pattern = 0; pattern < 4; ++pattern) {
      for (size_t i = 0; i < n; ++i) {
        char *s = pool + i * 8;
        size_t len = rng() % 7;
        for (size_t k = 0; k < len; ++k)
          s[k] = (char)('a' + rng() % (pattern == 1 ? 2 : 26)); // 1: many duplicates
        items[i] = faf_string_init_n(s, len);
      }
      memcpy(want, items, n * sizeof(faf_string));
      qsort(want, n, sizeof(faf_string), by_cmp);
      if (pattern == 2) // already sorted
        memcpy(items, want, n * sizeof(faf_string));
      if (pattern == 3) // reverse sorted
        for (size_t i = 0; i < n; ++i)
          items[i] = want[n - 1 - i];

      faf_string_arr arr = {.start = items, .end = items + n};
      faf_string_arr_sort(arr);
      int ok = 1;
      for (size_t i = 0; i < n; ++i)
        ok &= faf_string_eq(items[i], want[i]);
      ASSERT_TRUE(ok, "arr_sort order differs from qsort");
    }
  }
}

// Test case definitions
test_case_t hash_sort_tests[] = {
    {"hash_basics", test_hash_basics},
    {"hash_avalanche", test_hash_avalanche},
    {"sort_chars", test_sort_chars},
    {"arr_sort", test_arr_sort},
};

void hash_sort_setup(void) {}

void hash_sort_teardown(void) {}

int main(int argc, char **argv) {
  test_suite_t suite = TEST_SUITE("HashSort", hash_sort_tests,
                                  sizeof(hash_sort_tests) / sizeof(hash_sort_tests[0]),
                                  hash_sort_setup, hash_sort_teardown);
  register_test_suite(suite);
  return test_main(argc, argv);
}
