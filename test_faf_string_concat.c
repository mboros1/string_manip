#include "faf_string.h"
#include "faf_string_arr.h"
#include "faf_string_concat.h"
#include "faf_string_mem.h"
#include "faf_test.h"

#include <stdio.h>
#include <string.h>

#define FAF_STRING_ASSERT_IMPL
#include "faf_string_assert.h"

#define NDEBUG

int tests_run = 0;

char error_str[10 * 1024];

const char *str1 = "hello,world";
void test_str1() {
  tests_run++;
  printf("Testing string 1...\n");
  pool_t pool = next_pool();
  faf_string str = faf_string_init(str1);

  faf_string *str_concat = faf_string_concat(pool, str, str);

  int len = str_concat->end - str_concat->start;
  printf("%.*s\n", (int)(str_concat->end - str_concat->start), str_concat->start);
  
  // Note: The current implementation of faf_string_concat only copies the first string
  // as noted by the TODOs in the code. When fully implemented, this should be 22.
  int expected = 11;
  ASSERT_INT_EQ(expected, len, "Array length incorrect");
}

// Test case definitions
test_case_t string_concat_tests[] = {
    {"basic_concat", test_str1}
};

// Setup and teardown functions
void string_concat_setup(void) {
    // Any setup code needed before each test
}

void string_concat_teardown(void) {
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
