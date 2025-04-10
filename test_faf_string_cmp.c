#include "faf_string.h"
#include "faf_string_arr.h"
#include "faf_string_mem.h"
#include "faf_string_cmp.h"
#include "faf_test.h"

#include <stdio.h>
#include <string.h>

#define FAF_STRING_ASSERT_IMPL
#include "faf_string_assert.h"

#define NDEBUG

int tests_run = 0;

char error_str[10 * 1024];

const char *str1_1 = "hello";
const char *str1_2 = "hello";
void test_str1() {
  tests_run++;
  printf("Testing string 1:1 to 1:2...\n");
  faf_string str1 = faf_string_init(str1_1);
  faf_string str2 = faf_string_init(str1_2);

  int cmp = faf_string_cmp(&str1, &str2);
  int expected = 0;
  ASSERT_INT_EQ(expected, cmp, "Array compare incorrect");
}

const char *str2_1 = "hello";
const char *str2_2 = "hello1";
void test_str2() {
  tests_run++;
  printf("Testing string 2:1 to 2:2...\n");
  faf_string str1 = faf_string_init(str2_1);
  faf_string str2 = faf_string_init(str2_2);

  int cmp = faf_string_cmp(&str1, &str2);
  int expected = -1;
  ASSERT_INT_EQ(expected, cmp, "Array compare incorrect");
}

const char *str3_1 = "b";
const char *str3_2 = "a";
void test_str3() {
  tests_run++;
  printf("Testing string 3:1 to 3:2...\n");
  faf_string str1 = faf_string_init(str3_1);
  faf_string str2 = faf_string_init(str3_2);

  int cmp = faf_string_cmp(&str1, &str2);
  int expected = 1;
  ASSERT_INT_EQ(expected, cmp, "Array compare incorrect");
}

// Test case definitions
test_case_t string_cmp_tests[] = {
    {"equal_strings", test_str1},
    {"first_shorter", test_str2},
    {"second_smaller", test_str3}
};

// Setup and teardown functions
void string_cmp_setup(void) {
    // Any setup code needed before each test
}

void string_cmp_teardown(void) {
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
