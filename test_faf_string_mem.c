#include "faf_string.h"
#include "faf_string_mem.h"
#include <stdio.h>
#include <string.h>

#define FAF_STRING_ASSERT_IMPL
#include "faf_string_assert.h"

#define NDEBUG

int tests_run = 0;

void test_next_pool() {
  tests_run++;
  printf("Testing pool initialization...\n");
  pool_t pool = next_pool();
  pool_off_t curr_idx = get_pool_offset(pool);
  pool_off_t remaining_space = get_pool_remaining(pool);

  char error_str[100];
  sprintf(error_str, "Current index incorrect: actual: %d, expected: %d\n", 0,
          curr_idx);
  assert(curr_idx == 0, error_str);

  int expected = 1024;
  sprintf(error_str, "Remaining space is incorrect: actual: %d, expected: %d\n",
           remaining_space, expected);
  assert(remaining_space == expected, error_str);
}


void test_allocations() {
  tests_run++;
  printf("Testing allocation...\n");
  pool_t pool = next_pool();

  faf_string* str1 = faf_string_alloc(pool);
  pool_off_t curr_idx = get_pool_offset(pool);
  pool_off_t remaining_space = get_pool_remaining(pool);

  char error_str[100];
  sprintf(error_str, "Current index incorrect: actual: %d, expected: %d\n", 0,
          curr_idx);
  assert(curr_idx == 1, error_str);

  int expected = 1023;
  sprintf(error_str, "Remaining space is incorrect: actual: %d, expected: %d\n",
            remaining_space, expected);
  assert(remaining_space == expected, error_str);

  const char* test_str = "Hello World!\n";
  *str1 = faf_string_init(test_str);

  printf("Test printing allocated string: \'%s\'", str1->start);

  int actual = strcmp(test_str, str1->start);
  expected = 0;

  sprintf(error_str, "String compare between allocated string and test string failed: actual: %d, expected: %d\n",
           expected, remaining_space);
  assert(actual == expected, error_str);
}

// Main function to run all tests
int main() {
  test_next_pool();
  test_allocations();

  if (tests_failed) {
    printf("%d of %d tests failed!\n", tests_failed, tests_run);
  } else {
    printf("%d tests passed\n", tests_run);
  }
  return 0;
}
