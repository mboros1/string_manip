#include "faf_string.h"
#include "faf_string_arr.h"
#include "faf_string_mem.h"
#include "faf_string_strlen.h"
#include "faf_string_strsplit.h"

#define FAF_STRING_ASSERT_IMPL
#include "faf_string_assert.h"

#define NDEBUG

int tests_run = 0;

char error_str[10 * 1024];

// TODO: implement tests for various strings
const char *str1 = "hello,world";
void test_str1() {
  tests_run++;
  printf("Testing string 1...");
  pool_t pool = next_pool();
  faf_string str = faf_string_init(str1);

  faf_string_arr arr = faf_string_split(pool, str, ',');

  faf_foreach(pos, arr) {
    printf("%.*s\n", (int)(pos->end - pos->start), pos->start);
  }
}

const char *str2 = "hello,world,today";
const char *str3 = "";
const char *str4 = ",,,,,,,,,,,";
const char *str5 = ""; // TODO: string longer then 128 bits
const char *str6 = ",,asdf,asfasdfg,,,";

int main(void) {
  test_str1();

  if (tests_failed) {
    printf("%d of %d tests failed!\n", tests_failed, tests_run);
  } else {
    printf("%d tests passed\n", tests_run);
  }
  return 0;
}
