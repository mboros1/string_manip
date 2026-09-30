#include "core/faf_string.h"
#include "core/faf_string_arr.h"
#include "mem/faf_string_mem.h"
#include "text/faf_string_strsplit.h"
#include "faf_test.h"

#include <stdio.h>
#include <string.h>

// Slots faf_string_split reserves for `count` tokens: the token array.
static size_t split_slots(int count) {
  return ((size_t)count * sizeof(faf_string) + FAF_SLOT_BYTES - 1) /
         FAF_SLOT_BYTES;
}

// The most slots any one split_cases entry needs, optionally with a copy of
// its source in the same region.
static size_t split_cases_max_slots(bool copy_source);

static const char *str1 = "hello,world";
static void test_str1(void) {
  printf("Testing string 1...\n");
  faf_region r = faf_region_acquire();
  faf_string str = faf_string_init(str1);

  faf_string_arr arr = faf_string_split(r, str, ',');

  int len = arr.end - arr.start;
  int expected = 2;
  ASSERT_INT_EQ(expected, len, "Array length incorrect");
  faf_region_release(r);
}

static const char *str2 = "hello,world,today";
static void test_str2(void) {
  printf("Testing string 2...\n");
  faf_region r = faf_region_acquire();
  faf_string str = faf_string_init(str2);

  faf_string_arr arr = faf_string_split(r, str, ',');

  int len = arr.end - arr.start;
  int expected = 3;
  ASSERT_INT_EQ(expected, len, "Array length incorrect");
  faf_region_release(r);
}

static const char *str3 = "";
static void test_str3(void) {
  printf("Testing string 3...\n");
  faf_region r = faf_region_acquire();
  faf_string str = faf_string_init(str3);

  faf_string_arr arr = faf_string_split(r, str, ',');

  int len = arr.end - arr.start;
  int expected = 1;
  ASSERT_INT_EQ(expected, len, "Array length incorrect");
  faf_region_release(r);
}

static const char *str4 = ",,,,,,,,,,,";
static void test_str4(void) {
  printf("Testing string 4...\n");
  faf_region r = faf_region_acquire();
  faf_string str = faf_string_init(str4);

  faf_string_arr arr = faf_string_split(r, str, ',');

  int len = arr.end - arr.start;
  int expected = 12;
  ASSERT_INT_EQ(expected, len, "Array length incorrect");
  faf_region_release(r);
}

static const char *str5 =
    "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";
static void test_str5(void) {
  printf("Testing string 5...\n");
  TEST_REQUIRE(split_slots(36) <= FAF_POOL_SLOTS,
               "36 tokens need a bigger region (FAF_POOL_SLOTS)");
  faf_region r = faf_region_acquire();
  faf_string str = faf_string_init(str5);

  faf_string_arr arr = faf_string_split(r, str, 'h');

  int len = arr.end - arr.start;
  int expected = 36;
  ASSERT_INT_EQ(expected, len, "Array length incorrect");
  faf_region_release(r);
}

static const char *str6 = ",,asdf,asfasdfg,,,";
static void test_str6(void) {
  printf("Testing string 6...\n");
  faf_region r = faf_region_acquire();
  faf_string str = faf_string_init(str6);

  faf_string_arr arr = faf_string_split(r, str, ',');

  int len = arr.end - arr.start;
  int expected = 7;
  ASSERT_INT_EQ(expected, len, "Array length incorrect");
  faf_region_release(r);
}

static const char *str7 = "1234567890123456,123";
static void test_str7(void) {
  printf("Testing string 7...\n");
  faf_region r = faf_region_acquire();
  faf_string str = faf_string_init(str7);

  faf_string_arr arr = faf_string_split(r, str, ',');

  int len = arr.end - arr.start;
  int expected = 2;
  ASSERT_INT_EQ(expected, len, "Array length incorrect");
  faf_region_release(r);

  // faf_foreach(str, arr) {
  //   printf("%.*s\n", (int)(str->end - str->start), str->start);
  // }
}

// Every test string, with its separator and expected token count.
static struct split_case {
  const char *str;
  char tok;
  int count;
} split_cases[] = {
    {"hello,world", ',', 2},  {"hello,world,today", ',', 3},
    {"", ',', 1},             {",,,,,,,,,,,", ',', 12},
    {NULL, 'h', 36},          {",,asdf,asfasdfg,,,", ',', 7},
    {"1234567890123456,123", ',', 2},
};

static void check_tokens(faf_string src, faf_string_arr arr, char tok,
                         int expected_count) {
  ASSERT_INT_EQ(expected_count, (int)(arr.end - arr.start),
                "Array length incorrect");
  // tokens tile the source exactly: each ends on a separator, and the next
  // starts right after it
  const char *expect_start = src.start;
  faf_foreach(t, arr) {
    ASSERT_TRUE(t->start == expect_start, "Token start incorrect");
    for (const char *c = t->start; c < t->end; ++c) {
      ASSERT_TRUE(*c != tok, "Token contains the separator");
    }
    expect_start = t->end + 1;
  }
  if (arr.end > arr.start) // an empty (failed) result has no last token
    ASSERT_TRUE((arr.end - 1)->end == src.end, "Last token end incorrect");
}

static size_t split_cases_max_slots(bool copy_source) {
  split_cases[4].str = str5;
  size_t most = 0;
  for (size_t i = 0; i < sizeof(split_cases) / sizeof(split_cases[0]); ++i) {
    size_t need = split_slots(split_cases[i].count);
    if (copy_source)
      need += faf_slots_for(strlen(split_cases[i].str));
    if (need > most)
      most = need;
  }
  return most;
}

static void test_token_contents(void) {
  TEST_REQUIRE(split_cases_max_slots(false) <= FAF_POOL_SLOTS,
               "the largest case needs a bigger region (FAF_POOL_SLOTS)");
  split_cases[4].str = str5;
  for (size_t i = 0; i < sizeof(split_cases) / sizeof(split_cases[0]); ++i) {
    faf_region r = faf_region_acquire(); // a region per case
    faf_string src = faf_string_init(split_cases[i].str);
    faf_string_arr arr = faf_string_split(r, src, split_cases[i].tok);
    check_tokens(src, arr, split_cases[i].tok, split_cases[i].count);
    faf_region_release(r);
  }
}

static void test_split_region_source(void) {
  // a source already in pool storage takes the direct tail load path; it has
  // to give the same answer as the bounce buffer path
  TEST_REQUIRE(split_cases_max_slots(true) <= FAF_POOL_SLOTS,
               "the largest case and its copy need a bigger region");
  split_cases[4].str = str5;
  for (size_t i = 0; i < sizeof(split_cases) / sizeof(split_cases[0]); ++i) {
    faf_region r = faf_region_acquire(); // a region per case
    faf_string src = faf_string_copy(r, faf_string_init(split_cases[i].str));
    faf_string_arr arr = faf_string_split(r, src, split_cases[i].tok);
    check_tokens(src, arr, split_cases[i].tok, split_cases[i].count);
    faf_region_release(r);
  }
}

static void test_split_nul_separator(void) {
  // zero padding past the end must never count as a separator
  faf_region r = faf_region_acquire();
  faf_string caller = faf_string_init("abc");
  faf_string owned = faf_string_copy(r, caller);

  faf_string_arr a = faf_string_split(r, caller, '\0');
  faf_string_arr b = faf_string_split(r, owned, '\0');
  ASSERT_INT_EQ(1, (int)(a.end - a.start), "Padding matched in caller memory");
  ASSERT_INT_EQ(1, (int)(b.end - b.start), "Padding matched in pool storage");

  // a real embedded NUL still splits
  faf_string embedded = faf_string_init_n("ab\0cd", 5);
  faf_string_arr c = faf_string_split(r, embedded, '\0');
  ASSERT_INT_EQ(2, (int)(c.end - c.start), "Embedded NUL did not split");
  faf_region_release(r);
}

static void test_split_owned(void) {
  faf_region r = faf_region_acquire();
  char line[] = "alpha|beta||gamma-delta-epsilon-zeta|";
  faf_string src = faf_string_init(line);

  faf_string_arr arr = faf_string_split_owned(r, src, '|');

  ASSERT_INT_EQ(5, (int)(arr.end - arr.start), "Array length incorrect");
  ASSERT_STR_EQ("alpha|beta||gamma-delta-epsilon-zeta|", line,
                "Source was modified");
  const char *expected[] = {"alpha", "beta", "", "gamma-delta-epsilon-zeta", ""};
  int i = 0;
  faf_foreach(t, arr) {
    ASSERT_TRUE(faf_mem_contains(t->start, 1), "Token not owned by region");
    // NUL terminated, so plain C string functions work on each token
    ASSERT_STR_EQ(expected[i], t->start, "Owned token incorrect");
    ASSERT_INT_EQ((int)strlen(expected[i]), (int)faf_string_len(*t),
                  "Owned token length incorrect");
    ++i;
  }
  faf_region_release(r);
}

static void test_split_many_separators(void) {
  // more separators than one find_bytes batch (64), in both split variants
  char line[400];
  size_t n = 0;
  for (int i = 0; i < 150; ++i) {
    n += (size_t)snprintf(line + n, sizeof(line) - n, "%d,", i % 10);
  }
  // split_owned: the token array, a slot per 1-byte token and the source
  TEST_REQUIRE(faf_slots_for(151 * sizeof(faf_string)) + 151 + faf_slots_for(n)
                   <= FAF_POOL_SLOTS,
               "151 owned tokens need a bigger region (FAF_POOL_SLOTS)");
  faf_string src = faf_string_init_n(line, n);
  faf_region r = faf_region_acquire();
  faf_string_arr arr = faf_string_split(r, src, ',');
  check_tokens(src, arr, ',', 151);
  faf_region_release(r);

  r = faf_region_acquire();
  faf_string_arr owned = faf_string_split_owned(r, src, ',');
  ASSERT_INT_EQ(151, (int)(owned.end - owned.start), "owned token count");
  if (owned.end - owned.start == 151) { // an empty result would crash below
    for (int i = 0; i < 150; ++i) {
      char want[2] = {(char)('0' + i % 10), 0};
      ASSERT_STR_EQ(want, owned.start[i].start, "owned token contents");
    }
  }
  faf_region_release(r);
}

static void test_split_out_of_space(void) {
  // leave one slot less than two tokens need; that's 1 slot with 64-bit
  // pointers but none with 32-bit ones, where two tokens fit in one slot
  const int used = FAF_POOL_SLOTS - ((int)split_slots(2) - 1);
  faf_region r = faf_region_acquire();
  faf_reserve(r, (size_t)used);

  faf_string_arr arr = faf_string_split(r, faf_string_init("a,b"), ',');
  ASSERT_TRUE(arr.start == NULL && arr.end == NULL,
              "Split into a full region succeeded");
  ASSERT_INT_EQ(used, (int)faf_region_used(r),
                "Failed split moved the cursor");
  faf_region_release(r);
}

// Test case definitions
static test_case_t strsplit_tests[] = {
    {"basic_split", test_str1},
    {"three_part_split", test_str2},
    {"empty_string", test_str3},
    {"multiple_delimiters", test_str4},
    {"long_string", test_str5},
    {"empty_segments", test_str6},
    {"error_check", test_str7},
    {"token_contents", test_token_contents},
    {"region_source", test_split_region_source},
    {"nul_separator", test_split_nul_separator},
    {"split_owned", test_split_owned},
    {"many_separators", test_split_many_separators},
    {"out_of_space", test_split_out_of_space},
};

// Setup and teardown functions
static void strsplit_setup(void) {
    // Any setup code needed before each test
}

static void strsplit_teardown(void) {
    // Any cleanup code needed after each test
}

// Main function
int main(int argc, char** argv) {
    // Register test suite
    test_suite_t suite = TEST_SUITE(
        "StringSplit", 
        strsplit_tests,
        sizeof(strsplit_tests) / sizeof(strsplit_tests[0]),
        strsplit_setup,
        strsplit_teardown
    );
    register_test_suite(suite);
    
    // Normal execution
    return test_main(argc, argv);
}
