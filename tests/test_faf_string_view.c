#include "faf.h"
#include "faf_test.h"

#include <stdio.h>
#include <string.h>

#define S(lit) faf_string_init(lit)

static int view_is(faf_string v, const char *expected) {
  return faf_string_eq(v, S(expected));
}

static void test_slice(void) {
  faf_string s = S("hello world");
  ASSERT_TRUE(view_is(faf_string_slice(s, 0, 5), "hello"), "slice");
  ASSERT_TRUE(view_is(faf_string_slice(s, 6, 100), "world"), "slice clamps end");
  ASSERT_TRUE(view_is(faf_string_slice(s, 8, 3), ""), "slice from > to");
  ASSERT_TRUE(view_is(faf_string_slice(s, 50, 60), ""), "slice past end");
  ASSERT_TRUE(faf_string_slice(s, 2, 4).start == s.start + 2, "slice is a view");
}

static void test_trim(void) {
  ASSERT_TRUE(view_is(faf_string_trim(S("  \t hello world \r\n")), "hello world"), "trim");
  ASSERT_TRUE(view_is(faf_string_ltrim(S("  hi  ")), "hi  "), "ltrim");
  ASSERT_TRUE(view_is(faf_string_rtrim(S("  hi  ")), "  hi"), "rtrim");
  ASSERT_TRUE(view_is(faf_string_trim(S(" \t\n\v\f\r ")), ""), "trim all space");
  ASSERT_TRUE(view_is(faf_string_trim(S("")), ""), "trim empty");
  ASSERT_TRUE(view_is(faf_string_trim(S("x")), "x"), "trim none");
  // long runs so the SIMD path is exercised
  ASSERT_TRUE(view_is(faf_string_trim(S("                                 "
                                        "centered                         ")),
                      "centered"),
              "trim long");
}

static void test_next_token(void) {
  const char *expected[] = {"a", "", "bb", "ccc", ""};
  faf_string rest = S("a,,bb,ccc,"), tok;
  int i = 0;
  while (faf_string_next_token(&rest, ',', &tok)) {
    ASSERT_TRUE(i < 5 && view_is(tok, expected[i]), "token incorrect");
    ++i;
  }
  ASSERT_INT_EQ(5, i, "token count");
  ASSERT_FALSE(faf_string_next_token(&rest, ',', &tok), "iterator should stay done");

  // agrees with faf_string_split on every test input
  const char *inputs[] = {"", ",", "abc", ",,,", "1234567890123456,123", ",x,"};
  faf_region r = faf_region_acquire();
  for (size_t k = 0; k < sizeof(inputs) / sizeof(inputs[0]); ++k) {
    faf_string_arr arr = faf_string_split(r, S(inputs[k]), ',');
    faf_string it = S(inputs[k]);
    faf_string *want = arr.start;
    while (faf_string_next_token(&it, ',', &tok)) {
      ASSERT_TRUE(want < arr.end && want->start == tok.start && want->end == tok.end,
                  "iterator disagrees with split");
      ++want;
    }
    ASSERT_TRUE(want == arr.end, "iterator token count disagrees with split");
  }
  faf_region_release(r);
}

// faf_tokens gives exactly the tokens (same pointers) of faf_string_next_token
static int tokens_agree(faf_string str, char tok) {
  faf_string rest = str, a, b;
  faf_tokens t = faf_tokens_init(str, tok);
  for (;;) {
    bool more_a = faf_string_next_token(&rest, tok, &a);
    bool more_b = faf_tokens_next(&t, &b);
    if (more_a != more_b)
      return 0;
    if (!more_a)
      return !faf_tokens_next(&t, &b); // and stays done
    if (a.start != b.start || a.end != b.end)
      return 0;
  }
}

static void test_tokens(void) {
  // edge cases, and separator counts around the batch size
  const char *inputs[] = {"", ",", "abc", ",,,", ",x,", "a,,bb,ccc,",
                          "1234567890123456,123", ",,,,,,,,,,,,,,,",
                          ",,,,,,,,,,,,,,,,", ",,,,,,,,,,,,,,,,,",
                          "a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r,s,t"};
  for (size_t k = 0; k < sizeof(inputs) / sizeof(inputs[0]); ++k)
    ASSERT_TRUE(tokens_agree(S(inputs[k]), ','), inputs[k]);
  ASSERT_TRUE(tokens_agree(FAF_STRING_NONE, ','), "NONE input");

  // random lines: short tokens, many separators
  char line[200];
  unsigned seed = 7;
  for (int round = 0; round < 500; ++round) {
    size_t n = (size_t)(round % 180);
    for (size_t i = 0; i < n; ++i) {
      seed = seed * 1103515245u + 12345u;
      line[i] = (seed >> 16) % 4 == 0 ? ';' : (char)('a' + (seed >> 20) % 26);
    }
    ASSERT_TRUE(tokens_agree(faf_string_init_n(line, n), ';'), "random line");
  }
}

static void test_split_once(void) {
  faf_string left, right;
  ASSERT_TRUE(faf_string_split_once(S("key=value=more"), '=', &left, &right), "found");
  ASSERT_TRUE(view_is(left, "key") && view_is(right, "value=more"), "split_once parts");
  ASSERT_FALSE(faf_string_split_once(S("novalue"), '=', &left, &right), "not found");
  ASSERT_TRUE(faf_string_split_once(S("=x"), '=', &left, &right) && view_is(left, "") &&
                  view_is(right, "x"),
              "empty left");
}

// Test case definitions
static test_case_t view_tests[] = {
    {"slice", test_slice},
    {"trim", test_trim},
    {"next_token", test_next_token},
    {"tokens", test_tokens},
    {"split_once", test_split_once},
};

static void view_setup(void) {}

static void view_teardown(void) {}

int main(int argc, char **argv) {
  test_suite_t suite = TEST_SUITE("View", view_tests,
                                  sizeof(view_tests) / sizeof(view_tests[0]),
                                  view_setup, view_teardown);
  register_test_suite(suite);
  return test_main(argc, argv);
}
