// Differential tests: every faf_k_* kernel against its faf_ref_* reference,
// over all start alignments, lengths around the 16 byte block size, and
// ranges placed flush against unreadable guard pages on both sides (any read
// outside the pages a range touches crashes the test).

#include "kernels/faf_kernels.h"
#include "faf_string_mem.h"
#include "faf_test.h"

#include <stdio.h>
#include <string.h>
#if FAF_TEST_HAVE_GUARD_PAGES
#include <sys/mman.h>
#include <unistd.h>
#endif

#define MAX_LEN 80

static char *page_lo; // readable page, preceded by a guard page
static char *page_hi; // readable page, followed by a guard page
static long page;

static int failures;

#define CHECK_EQ(expected, actual, what, off, len)                             \
  do {                                                                         \
    size_t e_ = (size_t)(expected), a_ = (size_t)(actual);                     \
    if (e_ != a_ && failures++ < 10)                                           \
      printf("    %s: expected %zu got %zu (offset %zu, len %zu)\n", what, e_, \
             a_, (size_t)(off), (size_t)(len));                                \
  } while (0)

static uint64_t rng_state = 0x243F6A8885A308D3ull;
static uint32_t rng(void) {
  rng_state ^= rng_state << 13;
  rng_state ^= rng_state >> 7;
  rng_state ^= rng_state << 17;
  return (uint32_t)rng_state;
}

// Mostly the interesting bytes, so matches and case changes are frequent.
static const unsigned char alphabet[] = {'a', 'b', 'z', 'A', 'Z', ',', ' ',
                                         '\t', '\0', '@', '[', '`', '{',
                                         0x7F, 0x80, 0xC3, 0xFF};
static void fill_random(char *p, size_t n) {
  for (size_t i = 0; i < n; ++i)
    p[i] = (char)alphabet[rng() % sizeof(alphabet)];
}

// Every placement we test: offsets from the start of page_lo (reads before
// the range hit the guard page), and ranges ending exactly at the end of
// page_hi (reads after the range hit the guard page).
typedef struct {
  char *p;
  size_t off;
} placement;

static int placements(size_t len, placement out[64]) {
  int k = 0;
  for (size_t off = 0; off < 32; ++off)
    out[k++] = (placement){page_lo + off, off};
  for (size_t back = 0; back < 32; ++back)
    out[k++] = (placement){page_hi + page - len - back, back};
  return k;
}

static void setup_pages(void) {
  if (page_lo)
    return;
#if !FAF_TEST_HAVE_GUARD_PAGES
  // No memory protection: the same layout in a plain buffer. The kernels are
  // still compared with the reference at every placement; reads past the end
  // just can't be caught.
  static char mem[5 * 256];
  page = 256;
  page_lo = mem + page;
  page_hi = mem + 3 * page;
#else
  page = sysconf(_SC_PAGESIZE);
  char *mem = mmap(NULL, 5 * page, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANON, -1, 0);
  mprotect(mem, page, PROT_NONE);
  mprotect(mem + 2 * page, page, PROT_NONE);
  mprotect(mem + 4 * page, page, PROT_NONE);
  page_lo = mem + page;
  page_hi = mem + 3 * page;
#endif
}

static void test_find_count(void) {
  setup_pages();
  failures = 0;
  const char targets[] = {'a', ',', '\0', (char)0xFF, 'q'};
  placement pl[64];
  for (size_t len = 0; len <= MAX_LEN; ++len) {
    int np = placements(len, pl);
    for (int k = 0; k < np; ++k) {
      char *s = pl[k].p;
      fill_random(s, len);
      for (size_t t = 0; t < sizeof(targets); ++t) {
        char c = targets[t];
        CHECK_EQ(faf_ref_find_byte(s, len, c), faf_k_find_byte(s, len, c),
                 "find_byte", pl[k].off, len);
        CHECK_EQ(faf_ref_rfind_byte(s, len, c), faf_k_rfind_byte(s, len, c),
                 "rfind_byte", pl[k].off, len);
        CHECK_EQ(faf_ref_count_byte(s, len, c), faf_k_count_byte(s, len, c),
                 "count_byte", pl[k].off, len);
      }
      CHECK_EQ(faf_ref_ascii_prefix(s, len), faf_k_ascii_prefix(s, len),
               "ascii_prefix", pl[k].off, len);
      const size_t maxes[] = {0, 1, 3, 64};
      for (size_t mi = 0; mi < 4; ++mi) {
        size_t want[64], got[64];
        size_t nw = faf_ref_find_bytes(s, len, ',', want, maxes[mi]);
        size_t ng = faf_k_find_bytes(s, len, ',', got, maxes[mi]);
        CHECK_EQ(nw, ng, "find_bytes count", pl[k].off, len);
        for (size_t j = 0; j < nw && j < ng; ++j)
          CHECK_EQ(want[j], got[j], "find_bytes position", pl[k].off, len);
      }
    }
  }
  ASSERT_INT_EQ(0, failures, "find/count kernels differ from reference");
}

static void test_sets(void) {
  setup_pages();
  failures = 0;
  const char *sets[] = {"", ",", "a,", " \t\n\v\f\r", "aAzZ",
                        "abcdefghijklmnopqrst" /* > 16: bitmap path */,
                        "\x80\xFF\xC3"};
  placement pl[64];
  for (size_t si = 0; si < sizeof(sets) / sizeof(sets[0]); ++si) {
    faf_byteset set;
    faf_byteset_init(&set, sets[si], strlen(sets[si]));
    for (size_t len = 0; len <= MAX_LEN; ++len) {
      int np = placements(len, pl);
      for (int k = 0; k < np; ++k) {
        char *s = pl[k].p;
        fill_random(s, len);
        for (int in = 0; in <= 1; ++in) {
          CHECK_EQ(faf_ref_find_set(s, len, &set, in),
                   faf_k_find_set(s, len, &set, in), "find_set", pl[k].off,
                   len);
          CHECK_EQ(faf_ref_rfind_set(s, len, &set, in),
                   faf_k_rfind_set(s, len, &set, in), "rfind_set", pl[k].off,
                   len);
        }
      }
    }
  }
  ASSERT_INT_EQ(0, failures, "set kernels differ from reference");
}

static void test_byteset(void) {
  faf_byteset set;
  faf_byteset_init(&set, "abca", 4);
  ASSERT_INT_EQ(3, set.nchars, "Duplicates should be ignored");
  ASSERT_TRUE(faf_byteset_has(&set, 'c') && !faf_byteset_has(&set, 'd'),
              "Membership incorrect");
  char many[40];
  for (int i = 0; i < 40; ++i)
    many[i] = (char)('0' + i);
  faf_byteset_init(&set, many, 40);
  ASSERT_INT_EQ(0xFF, set.nchars, "Large set should be marked as such");
  ASSERT_TRUE(faf_byteset_has(&set, '0' + 39), "Large set membership");
}

static void test_mismatch(void) {
  setup_pages();
  failures = 0;
  placement pa[64];
  for (size_t len = 0; len <= MAX_LEN; ++len) {
    int np = placements(len, pa);
    for (int k = 0; k < np; ++k) {
      // a at every placement, b at a different one on the other page
      char *a = pa[k].p;
      char *b = pa[(k + 32) % np].p;
      fill_random(a, len);
      memcpy(b, a, len);
      CHECK_EQ(faf_ref_mismatch(a, b, len), faf_k_mismatch(a, b, len),
               "mismatch equal", pa[k].off, len);
      if (len) {
        size_t at = rng() % len;
        b[at] ^= (char)(1 + rng() % 255);
        CHECK_EQ(faf_ref_mismatch(a, b, len), faf_k_mismatch(a, b, len),
                 "mismatch", pa[k].off, len);
        CHECK_EQ(faf_ref_mismatch_icase(a, b, len),
                 faf_k_mismatch_icase(a, b, len), "mismatch_icase", pa[k].off,
                 len);
        // flip case of a letter: equal ignoring case
        memcpy(b, a, len);
        for (size_t i = 0; i < len; ++i)
          if ((b[i] | 0x20) >= 'a' && (b[i] | 0x20) <= 'z')
            b[i] ^= 0x20;
        CHECK_EQ(faf_ref_mismatch_icase(a, b, len),
                 faf_k_mismatch_icase(a, b, len), "mismatch_icase flipped",
                 pa[k].off, len);
      }
    }
  }
  ASSERT_INT_EQ(0, failures, "mismatch kernels differ from reference");
}

static void test_strlen(void) {
  setup_pages();
  failures = 0;
  for (size_t len = 0; len <= MAX_LEN; ++len) {
    for (size_t back = 0; back < 32; ++back) {
      // NUL is the last byte before the guard page
      char *s = page_hi + page - 1 - len - back;
      memset(s, 'x', len);
      memset(s + len, 0, back + 1);
      CHECK_EQ(len, faf_k_strlen(s), "strlen", back, len);
    }
    for (size_t off = 0; off < 32; ++off) {
      char *s = page_lo + off;
      memset(s, 'x', len);
      s[len] = '\0';
      CHECK_EQ(len, faf_k_strlen(s), "strlen", off, len);
    }
  }
  ASSERT_INT_EQ(0, failures, "strlen differs");
}

// Transforms: compare output buffers, and check nothing outside the
// destination range is written.
static void test_transforms(void) {
  failures = 0;
  char src[MAX_LEN + 32];
  char want[MAX_LEN + 64], got[MAX_LEN + 64];
  for (size_t len = 0; len <= MAX_LEN; ++len) {
    for (size_t off = 0; off < 16; ++off) {
      fill_random(src + off, len);
      const char *s = src + off;

      for (int upper = 0; upper <= 1; ++upper) {
        memset(want, '#', sizeof(want));
        memset(got, '#', sizeof(got));
        faf_ref_ascii_case(want + 16, s, len, upper);
        faf_k_ascii_case(got + 16, s, len, upper);
        CHECK_EQ(0, memcmp(want, got, sizeof(want)), "ascii_case", off, len);
        // in place
        memcpy(got + 16, s, len);
        faf_k_ascii_case(got + 16, got + 16, len, upper);
        CHECK_EQ(0, memcmp(want, got, sizeof(want)), "ascii_case in place",
                 off, len);
      }

      memset(want, '#', sizeof(want));
      memset(got, '#', sizeof(got));
      faf_ref_reverse(want + 16, s, len);
      faf_k_reverse(got + 16, s, len);
      CHECK_EQ(0, memcmp(want, got, sizeof(want)), "reverse", off, len);

      // faf_memcpy/faf_memset handle short lengths inline
      memset(want, '#', sizeof(want));
      memset(got, '#', sizeof(got));
      memcpy(want + 16 + off, s, len);
      faf_memcpy(got + 16 + off, s, len);
      CHECK_EQ(0, memcmp(want, got, sizeof(want)), "faf_memcpy", off, len);

      memset(want, '#', sizeof(want));
      memset(got, '#', sizeof(got));
      memset(want + 16 + off, 0xA5, len);
      faf_memset(got + 16 + off, 0xA5, len);
      CHECK_EQ(0, memcmp(want, got, sizeof(want)), "faf_memset", off, len);
    }
  }
  ASSERT_INT_EQ(0, failures, "transform kernels differ from reference");
}

// Test case definitions
// Long inputs, which the short tests above don't reach: the PIE kernels
// (ESP32-S3) scan 64-byte chunks after the first ~64-128 bytes, and
// count_byte folds its lane counters every 31 chunks (1984 bytes). Every
// backend runs this against the reference.
#define LONG_LEN 2300
static char long_a[LONG_LEN + 64], long_b[LONG_LEN + 64];

static void test_long(void) {
  failures = 0;
  static const size_t lens[] = {63,   64,   65,   95,   96,   97,
                                127,  128,  200,  511,  1000, 2015,
                                2016, 2017, 2047, 2048, 2100, LONG_LEN};
  for (size_t li = 0; li < sizeof(lens) / sizeof(lens[0]); ++li) {
    size_t len = lens[li];
    for (size_t off = 0; off < 32; ++off) {
      char *s = long_a + off;

      // dense: random bytes, matches everywhere ('q' is never in them)
      fill_random(s, len);
      CHECK_EQ(faf_ref_count_byte(s, len, ','), faf_k_count_byte(s, len, ','),
               "long count dense", off, len);
      CHECK_EQ(faf_ref_find_byte(s, len, ','), faf_k_find_byte(s, len, ','),
               "long find dense", off, len);
      CHECK_EQ(len, faf_k_find_byte(s, len, 'q'), "long find absent", off, len);
      CHECK_EQ(0, faf_k_count_byte(s, len, 'q'), "long count absent", off, len);

      // sparse: one target, anywhere
      size_t at = rng() % len;
      memset(s, 'x', len);
      s[at] = ',';
      CHECK_EQ(at, faf_k_find_byte(s, len, ','), "long find one", off, len);
      CHECK_EQ(1, faf_k_count_byte(s, len, ','), "long count one", off, len);
      s[at] = (char)0x80;
      CHECK_EQ(at, faf_k_ascii_prefix(s, len), "long ascii_prefix", off, len);
      CHECK_EQ(at, faf_k_ascii_prefix(s, at), "long ascii_prefix all", off, at);

      // strlen: the NUL at the end
      memset(s, 'y', len);
      s[len] = '\0';
      CHECK_EQ(len, faf_k_strlen(s), "long strlen", off, len);

      // mismatch against a copy at every relative alignment
      fill_random(s, len);
      for (size_t boff = 0; boff < 16; ++boff) {
        char *t = long_b + boff;
        memcpy(t, s, len);
        CHECK_EQ(len, faf_k_mismatch(s, t, len), "long mismatch equal", boff, len);
        t[at] ^= 1;
        CHECK_EQ(at, faf_k_mismatch(s, t, len), "long mismatch", boff, len);
      }
    }
  }
  ASSERT_INT_EQ(0, failures, "long inputs differ from reference");
}

static test_case_t kernel_tests[] = {
    {"find_count", test_find_count}, {"sets", test_sets},
    {"byteset", test_byteset},       {"mismatch", test_mismatch},
    {"strlen", test_strlen},         {"transforms", test_transforms},
    {"long", test_long},
};

// Setup and teardown functions
static void kernel_setup(void) {}

static void kernel_teardown(void) {}

// Main function
int main(int argc, char **argv) {
  printf("Kernel backend: %s\n", FAF_BACKEND_NAME);
  test_suite_t suite =
      TEST_SUITE("Kernels", kernel_tests,
                 sizeof(kernel_tests) / sizeof(kernel_tests[0]), kernel_setup,
                 kernel_teardown);
  register_test_suite(suite);
  return test_main(argc, argv);
}
