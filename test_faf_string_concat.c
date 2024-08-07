
#include "faf_string.h"
#include "faf_string_arr.h"
#include "faf_string_concat.h"
#include "faf_string_mem.h"

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
  int expected = 12;
  sprintf(error_str, "Array length incorrect: actual: %d, expected: %d\n", len,
          expected);
  assert(len == expected, error_str);
}

int main(void) {
  test_str1();

  if (tests_failed) {
    printf("%d of %d tests failed!\n", tests_failed, tests_run);
  } else {
    printf("%d tests passed\n", tests_run);
  }
  return 0;
}
