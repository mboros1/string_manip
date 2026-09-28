#include "faf_string.h"
#include "faf_string_arr.h"
#include "faf_string_concat.h"
#include "faf_string_mem.h"
#include "faf_test.h"

#include <stdio.h>
#include <string.h>

static const char *str1 = "hello,world";
static void test_str1(void) {
  printf("Testing string 1...\n");
  faf_region r = faf_region_acquire();
  faf_string str = faf_string_init(str1);

  faf_string str_concat = faf_string_concat(r, str, str);
  printf("%.*s\n", (int)faf_string_len(str_concat), str_concat.start);

  ASSERT_INT_EQ(22, (int)faf_string_len(str_concat), "Concat length incorrect");
  ASSERT_STR_EQ("hello,worldhello,world", str_concat.start,
                "Concat contents incorrect");
  faf_region_release(r);
}

static void test_lengths(void) {
  // every split of lengths around the 16 byte boundaries, from both caller
  // memory and pool storage
  const char *src = "0123456789abcdefghijklmnopqrstuvwxyzABCDEF";
  char expected[128];
  for (size_t la = 0; la <= 33; ++la) {
    for (size_t lb = 0; lb <= 33; ++lb) {
      // a region per case, so this fits pools of any size
      faf_region r = faf_region_acquire();
      faf_string a = faf_string_init_n(src, la);
      faf_string b = faf_string_init_n(src + 5, lb);
      memcpy(expected, a.start, la);
      memcpy(expected + la, b.start, lb);
      expected[la + lb] = '\0';

      faf_string c1 = faf_string_concat(r, a, b);
      ASSERT_STR_EQ(expected, c1.start, "Concat from caller memory incorrect");

      faf_string c2 = faf_string_concat(r, faf_string_copy(r, a),
                                        faf_string_copy(r, b));
      ASSERT_STR_EQ(expected, c2.start, "Concat from pool storage incorrect");
      ASSERT_INT_EQ((int)(la + lb), (int)faf_string_len(c2),
                    "Concat length incorrect");
      faf_region_release(r);
    }
  }
}

static void test_out_of_space(void) {
  faf_region r = faf_region_acquire();
  faf_reserve(r, FAF_POOL_SLOTS - 1);
  faf_string a = faf_string_init("0123456789");
  ASSERT_TRUE(faf_string_is_none(faf_string_concat(r, a, a)),
              "Concat into a full region succeeded");
  faf_region_release(r);
}

// Test case definitions
static test_case_t string_concat_tests[] = {
    {"basic_concat", test_str1},
    {"lengths", test_lengths},
    {"out_of_space", test_out_of_space},
};

// Setup and teardown functions
static void string_concat_setup(void) {
    // Any setup code needed before each test
}

static void string_concat_teardown(void) {
    // Any cleanup code needed after each test
}

// Main function
int main(int argc, char** argv) {
    // Register test suite
    test_suite_t suite = TEST_SUITE(
        "StringConcat", 
        string_concat_tests,
        sizeof(string_concat_tests) / sizeof(string_concat_tests[0]),
        string_concat_setup,
        string_concat_teardown
    );
    register_test_suite(suite);
    
    // Normal execution
    return test_main(argc, argv);
}
