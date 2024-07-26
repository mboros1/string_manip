#include "faf_string.h"
#include "faf_string_mem.h"
#include <stdio.h>
#include <string.h>

#define FAF_STRING_ASSERT_IMPL
#include "faf_string_assert.h"

#define NDEBUG

int tests_run = 0;

char error_str[10 * 1024];

void test_next_pool() {
  tests_run++;
  printf("Testing pool initialization...\n");
  pool_t pool = next_pool();
  pool_off_t curr_idx = get_pool_offset(pool);
  pool_off_t remaining_space = get_pool_remaining(pool);

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

  faf_string *str1 = faf_string_alloc(pool);
  pool_off_t curr_idx = get_pool_offset(pool);
  pool_off_t remaining_space = get_pool_remaining(pool);

  sprintf(error_str, "Current index incorrect: actual: %d, expected: %d\n", 0,
          curr_idx);
  assert(curr_idx == 1, error_str);

  int expected = 1023;
  sprintf(error_str, "Remaining space is incorrect: actual: %d, expected: %d\n",
          remaining_space, expected);
  assert(remaining_space == expected, error_str);

  const char *test_str = "Hello World!\n";
  *str1 = faf_string_init(test_str);

  printf("Test printing allocated string: \'%s\'", str1->start);

  int actual = strcmp(test_str, str1->start);
  expected = 0;

  sprintf(error_str,
          "String compare between allocated string and test string failed: "
          "actual: %d, expected: %d\n",
          expected, remaining_space);
  assert(actual == expected, error_str);
}

void test_string_copy_short() {
  tests_run++;
  printf("Testing string copy short string (under 16 bytes)...\n");
  pool_t pool = next_pool();

  const char *base_str = "hello world";

  faf_string str1 = faf_string_init(base_str);

  faf_string *str1_copy = faf_string_copy(pool, str1);

  int actual = strcmp(base_str, str1_copy->start);
  int expected = 0;

  sprintf(error_str,
          "String compare between allocated string and test string failed: "
          "actual: %s, expected: %s\n",
          str1_copy->start, base_str);
  assert(actual == expected, error_str);
}

void test_string_copy_long() {
  tests_run++;
  printf(
      "Testing string copy long string (several multiples of 16 bytes)...\n");
  pool_t pool = next_pool();
  const char *base_str =
      "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";

  faf_string str1 = faf_string_init(base_str);

  faf_string *str1_copy = faf_string_copy(pool, str1);

  int actual = strcmp(base_str, str1_copy->start);
  int expected = 0;

  sprintf(error_str,
          "String compare between allocated string and test string failed: "
          "actual: %s, expected: %s\n",
          str1_copy->start, base_str);
  assert(actual == expected, error_str);
}

void test_alloc_pool_size_change() {
  tests_run++;
  printf("Testing allocations properly changes the size of the pool\n");

  pool_t pool = next_pool();

  sprintf(error_str, "Initial pool index incorrect: actual %d, expected%d\n",
          get_pool_offset(pool), 0);
  sprintf(error_str, "Initial pool capacity incorrect: actual %d, expected%d\n",
          get_pool_remaining(pool), 1024);

  for (int i = 0; i < 10; ++i) {
    (void)faf_string_alloc(pool);
  }
  sprintf(error_str, "After allocation pool index incorrect: actual %d, expected%d\n",
          get_pool_offset(pool), 10);
  sprintf(error_str, "After allocation pool capacity incorrect: actual %d, expected%d\n",
          get_pool_remaining(pool), 1014);

  faf_string_pool_reset(pool);

  sprintf(error_str, "After allocation pool index incorrect: actual %d, expected%d\n",
          get_pool_offset(pool), 0);
  sprintf(error_str, "After allocation pool capacity incorrect: actual %d, expected%d\n",
          get_pool_remaining(pool), 1024);

}

// Main function to run all tests
int main() {
  test_next_pool();
  test_allocations();
  test_string_copy_short();
  test_string_copy_long();
  test_alloc_pool_size_change();

  if (tests_failed) {
    printf("%d of %d tests failed!\n", tests_failed, tests_run);
  } else {
    printf("%d tests passed\n", tests_run);
  }
  return 0;
}
