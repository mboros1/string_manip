#include "faf_string.h"
#include "faf_string_arr.h"
#include "faf_string_mem.h"
#include "faf_string_strsplit.h"

#include <stdio.h>
#include <string.h>

#define FAF_STRING_ASSERT_IMPL
#include "faf_string_assert.h"

#define NDEBUG

int tests_run = 0;

char error_str[10 * 1024];

// TODO: implement tests for various strings
const char *str1 = "hello,world";
void test_str1() {
  tests_run++;
  printf("Testing string 1...\n");
  pool_t pool = next_pool();
  faf_string str = faf_string_init(str1);

  faf_string_arr arr = faf_string_split(pool, str, ',');

  int len = arr.end - arr.start;
  int expected = 2;
  sprintf(error_str, "Array length incorrect: actual: %d, expected: %d\n", len,
          expected);
  assert(len == expected, error_str);
}

const char *str2 = "hello,world,today";
void test_str2() {
  tests_run++;
  printf("Testing string 2...\n");
  pool_t pool = next_pool();
  faf_string str = faf_string_init(str2);

  faf_string_arr arr = faf_string_split(pool, str, ',');

  int len = arr.end - arr.start;
  int expected = 3;
  sprintf(error_str, "Array length incorrect: actual: %d, expected: %d\n", len,
          expected);
  assert(len == expected, error_str);
}

const char *str3 = "";
void test_str3() {
  tests_run++;
  printf("Testing string 3...\n");
  pool_t pool = next_pool();
  faf_string str = faf_string_init(str3);

  faf_string_arr arr = faf_string_split(pool, str, ',');

  int len = arr.end - arr.start;
  int expected = 1;
  sprintf(error_str, "Array length incorrect: actual: %d, expected: %d\n", len,
          expected);
  assert(len == expected, error_str);
}

const char *str4 = ",,,,,,,,,,,";
void test_str4() {
  tests_run++;
  printf("Testing string 4...\n");
  pool_t pool = next_pool();
  faf_string str = faf_string_init(str4);

  faf_string_arr arr = faf_string_split(pool, str, ',');

  int len = arr.end - arr.start;
  int expected = 12;
  sprintf(error_str, "Array length incorrect: actual: %d, expected: %d\n", len,
          expected);
  assert(len == expected, error_str);
}

const char *str5 =
    "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";
void test_str5() {
  tests_run++;
  printf("Testing string 5...\n");
  pool_t pool = next_pool();
  faf_string str = faf_string_init(str5);

  faf_string_arr arr = faf_string_split(pool, str, 'h');

  int len = arr.end - arr.start;
  int expected = 36;
  sprintf(error_str, "Array length incorrect: actual: %d, expected: %d\n", len,
          expected);
  assert(len == expected, error_str);
  faf_foreach(pos, arr) {
    printf("%.*s\n", (int)(pos->end - pos->start), pos->start);
  }
}

const char *str6 = ",,asdf,asfasdfg,,,";
void test_str6() {
  tests_run++;
  printf("Testing string 6...\n");
  pool_t pool = next_pool();
  faf_string str = faf_string_init(str6);

  faf_string_arr arr = faf_string_split(pool, str, ',');

  int len = arr.end - arr.start;
  int expected = 7;
  sprintf(error_str, "Array length incorrect: actual: %d, expected: %d\n", len,
          expected);
  assert(len == expected, error_str);
}

int main(void) {
  test_str1();
  test_str2();
  test_str3();
  test_str4();
  test_str5();
  test_str6();

  if (tests_failed) {
    printf("%d of %d tests failed!\n", tests_failed, tests_run);
  } else {
    printf("%d tests passed\n", tests_run);
  }
  return 0;
}
