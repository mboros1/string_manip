#include "faf_string.h"
#include "faf_string_arr.h"
#include "faf_string_mem.h"
#include "faf_string_cmp.h"
#include "faf_test.h"

#include <stdio.h>
#include <string.h>
#if FAF_TEST_HAVE_GUARD_PAGES
#include <sys/mman.h>
#include <unistd.h>
#endif

static const char *str1_1 = "hello";
static const char *str1_2 = "hello";
static void test_str1(void) {
  printf("Testing string 1:1 to 1:2...\n");
  faf_string str1 = faf_string_init(str1_1);
  faf_string str2 = faf_string_init(str1_2);

  int cmp = faf_string_cmp(str1, str2);
  int expected = 0;
  ASSERT_INT_EQ(expected, cmp, "Array compare incorrect");
}

static const char *str2_1 = "hello";
static const char *str2_2 = "hello1";
static void test_str2(void) {
  printf("Testing string 2:1 to 2:2...\n");
  faf_string str1 = faf_string_init(str2_1);
  faf_string str2 = faf_string_init(str2_2);

  int cmp = faf_string_cmp(str1, str2);
  int expected = -1;
  ASSERT_INT_EQ(expected, cmp, "Array compare incorrect");
}

static const char *str3_1 = "b";
static const char *str3_2 = "a";
static void test_str3(void) {
  printf("Testing string 3:1 to 3:2...\n");
  faf_string str1 = faf_string_init(str3_1);
  faf_string str2 = faf_string_init(str3_2);

  int cmp = faf_string_cmp(str1, str2);
  int expected = 1;
  ASSERT_INT_EQ(expected, cmp, "Array compare incorrect");
}

// Test case definitions
static void test_no_overread(void) {
  // put the shorter string flush against an unreadable page: reading past its
  // end would crash
  TEST_REQUIRE(FAF_TEST_HAVE_GUARD_PAGES, "needs guard pages (mmap/mprotect)");
#if FAF_TEST_HAVE_GUARD_PAGES
  long page = sysconf(_SC_PAGESIZE);
  char *mem = mmap(NULL, 2 * page, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANON, -1, 0);
  ASSERT_TRUE(mem != MAP_FAILED, "mmap failed");
  if (mem == MAP_FAILED)
    return;
  ASSERT_INT_EQ(0, mprotect(mem + page, page, PROT_NONE), "mprotect failed");

  const char *longer = "abcdefghijklmnopqrstuvwxyz0123456789";
  for (int n = 0; n <= 35; ++n) {
    char *end = mem + page;
    memcpy(end - n, longer, n);
    faf_string a = faf_string_init(longer);
    faf_string b = faf_string_init_n(end - n, n);
    ASSERT_INT_EQ(1, faf_string_cmp(a, b), "Longer string should compare greater");
    ASSERT_INT_EQ(-1, faf_string_cmp(b, a), "Shorter string should compare less");
  }
  munmap(mem, 2 * page);
#endif
}

static void test_tail_mismatch(void) {
  // a difference in the final partial chunk
  faf_string a = faf_string_init("0123456789abcdefXYZ1");
  faf_string b = faf_string_init("0123456789abcdefXYZ2");
  ASSERT_INT_EQ(-1, faf_string_cmp(a, b), "Tail mismatch not found");
  ASSERT_INT_EQ(1, faf_string_cmp(b, a), "Tail mismatch not found");
  ASSERT_INT_EQ(0, faf_string_cmp(a, a), "Equal strings differ");
}

static void test_unsigned_bytes(void) {
  // bytes compare unsigned, like memcmp: 0xC3 sorts after 'z'
  faf_string hi = faf_string_init("\xC3\xA9");
  faf_string lo = faf_string_init("z");
  ASSERT_INT_EQ(1, faf_string_cmp(hi, lo), "High byte should sort after ASCII");
  ASSERT_INT_EQ(-1, faf_string_cmp(lo, hi), "ASCII should sort before high byte");
}

static test_case_t string_cmp_tests[] = {
    {"equal_strings", test_str1},
    {"first_shorter", test_str2},
    {"second_smaller", test_str3},
    {"no_overread", test_no_overread},
    {"tail_mismatch", test_tail_mismatch},
    {"unsigned_bytes", test_unsigned_bytes},
};

// Setup and teardown functions
static void string_cmp_setup(void) {
    // Any setup code needed before each test
}

static void string_cmp_teardown(void) {
    // Any cleanup code needed after each test
}

// Main function
int main(int argc, char** argv) {
    // Register test suite
    test_suite_t suite = TEST_SUITE(
        "StringCompare", 
        string_cmp_tests,
        sizeof(string_cmp_tests) / sizeof(string_cmp_tests[0]),
        string_cmp_setup,
        string_cmp_teardown
    );
    register_test_suite(suite);
    
    // Normal execution
    return test_main(argc, argv);
}
