#include "core/faf_string.h"
#include "mem/faf_string_mem.h"
#include "text/faf_string_case.h"
#include "text/faf_string_cmp.h"
#include "faf_test.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static void test_basic(void) {
  faf_region r = faf_region_acquire();
  const char *src = "Hello, World! ABC xyz";

  faf_string lower = faf_string_to_lower(r, faf_string_init(src));

  ASSERT_STR_EQ("hello, world! abc xyz", lower.start, "Lowercase incorrect");
  ASSERT_STR_EQ("Hello, World! ABC xyz", src, "Source was modified");
  faf_region_release(r);
}

static void test_all_bytes(void) {
  // every byte value, including the ones just outside 'A'..'Z' ('@', '[')
  // and the high bytes that are negative as signed chars
  char src[255];
  char expected[256];
  for (int i = 0; i < 255; ++i) {
    unsigned char c = (unsigned char)(i + 1); // skip 0, it would end the string
    src[i] = (char)c;
    expected[i] = (char)((c >= 'A' && c <= 'Z') ? c + ('a' - 'A') : c);
  }
  expected[255] = '\0';

  faf_region r = faf_region_acquire();
  faf_string lower = faf_string_to_lower(r, faf_string_init_n(src, 255));

  ASSERT_INT_EQ(255, (int)faf_string_len(lower), "Length changed");
  ASSERT_TRUE(memcmp(expected, lower.start, 256) == 0,
              "Byte mapping incorrect or not NUL terminated");
  faf_region_release(r);
}

static void test_lengths(void) {
  const char *src = "ABCDEFGHIJKLMNOPQRSTUVWXYZABCDEFGHIJKLMNOPQRSTUVWXYZ";
  for (size_t len = 0; len <= 40; ++len) {
    faf_region r = faf_region_acquire(); // per length: any pool size
    faf_string lower = faf_string_to_lower(r, faf_string_init_n(src, len));
    ASSERT_TRUE(lower.start != NULL, "to_lower failed");
    if (lower.start == NULL) {
      faf_region_release(r);
      break;
    }
    ASSERT_INT_EQ((int)len, (int)strlen(lower.start), "Length incorrect");
    for (size_t i = 0; i < len; ++i) {
      ASSERT_TRUE(lower.start[i] == tolower((unsigned char)src[i]),
                  "Character not lowered");
    }
    faf_region_release(r);
  }
}

static void test_out_of_space(void) {
  faf_region r = faf_region_acquire();
  faf_reserve(r, FAF_POOL_SLOTS);
  faf_string lower = faf_string_to_lower(r, faf_string_init("ABC"));
  ASSERT_TRUE(faf_string_is_none(lower), "to_lower into a full region succeeded");
  faf_region_release(r);
}

static void test_upper(void) {
  faf_region r = faf_region_acquire();
  const char *src = "Hello, World! abc XYZ {`}@[";
  faf_string upper = faf_string_to_upper(r, faf_string_init(src));
  ASSERT_STR_EQ("HELLO, WORLD! ABC XYZ {`}@[", upper.start, "Uppercase incorrect");

  // round trip on a long string, every length
  const char *mixed = "The Quick Brown Fox Jumps Over The Lazy Dog 0123456789";
  for (size_t len = 0; len <= strlen(mixed); ++len) {
    faf_string s = faf_string_init_n(mixed, len);
    faf_string u = faf_string_to_upper(r, s);
    for (size_t i = 0; i < len; ++i)
      ASSERT_TRUE(u.start[i] == toupper((unsigned char)mixed[i]),
                  "Character not raised");
    ASSERT_TRUE(u.start[len] == '\0', "Not NUL terminated");
    if (faf_region_remaining(r) < 16) {
      faf_region_release(r);
      r = faf_region_acquire();
    }
  }
  faf_region_release(r);
}

static void test_icase(void) {
  faf_string a = faf_string_init("Content-Length: 1234 abcdefghijklmnop");
  faf_string b = faf_string_init("content-length: 1234 ABCDEFGHIJKLMNOP");
  faf_string c = faf_string_init("content-length: 1234 ABCDEFGHIJKLMNOQ");
  ASSERT_TRUE(faf_string_eq_icase(a, b), "Case-insensitive equal failed");
  ASSERT_FALSE(faf_string_eq_icase(a, c), "Different strings compared equal");
  ASSERT_INT_EQ(0, faf_string_cmp_icase(a, b), "cmp_icase of equal strings");
  ASSERT_INT_EQ(-1, faf_string_cmp_icase(a, c), "cmp_icase ordering");
  ASSERT_INT_EQ(1, faf_string_cmp_icase(c, a), "cmp_icase ordering");
  // ordering is by folded (lower case) bytes: 'A' folds to 'a' (0x61), so
  // '[' (0x5B) sorts before it, although it sorts after 'A' (0x41) unfolded
  ASSERT_INT_EQ(-1, faf_string_cmp_icase(faf_string_init("["), faf_string_init("A")),
                "'[' should sort before 'A' once folded");
  ASSERT_INT_EQ(1, faf_string_cmp(faf_string_init("["), faf_string_init("A")),
                "'[' sorts after 'A' case-sensitively");
  ASSERT_INT_EQ(-1, faf_string_cmp_icase(faf_string_init("ab"), faf_string_init("ABC")),
                "Prefix should sort first");
  ASSERT_FALSE(faf_string_eq_icase(faf_string_init("@"), faf_string_init("`")),
               "'@' and '`' are not letters");
}

// Test case definitions
static test_case_t case_tests[] = {
    {"basic", test_basic},
    {"all_bytes", test_all_bytes},
    {"lengths", test_lengths},
    {"out_of_space", test_out_of_space},
    {"upper", test_upper},
    {"icase", test_icase},
};

// Setup and teardown functions
static void case_setup(void) {
    // Any setup code needed before each test
}

static void case_teardown(void) {
    // Any cleanup code needed after each test
}

// Main function
int main(int argc, char** argv) {
    // Register test suite
    test_suite_t suite = TEST_SUITE(
        "StringCase",
        case_tests,
        sizeof(case_tests) / sizeof(case_tests[0]),
        case_setup,
        case_teardown
    );
    register_test_suite(suite);

    // Normal execution
    return test_main(argc, argv);
}
