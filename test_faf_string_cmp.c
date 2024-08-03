
#include "faf_string.h"
#include "faf_string_arr.h"
#include "faf_string_mem.h"
#include "faf_string_cmp.h"

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
  sprintf(error_str, "Array compare incorrect: actual: %d, expected: %d\n", cmp,
          expected);
  assert(cmp == expected, error_str);
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
  sprintf(error_str, "Array compare incorrect: actual: %d, expected: %d\n", cmp,
          expected);
  assert(cmp == expected, error_str);
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
  sprintf(error_str, "Array compare incorrect: actual: %d, expected: %d\n", cmp,
          expected);
  assert(cmp == expected, error_str);
}

int main(void) {
  test_str1();
  test_str2();
  test_str3();

  if (tests_failed) {
    printf("%d of %d tests failed!\n", tests_failed, tests_run);
  } else {
    printf("%d tests passed\n", tests_run);
  }
  return 0;
}
