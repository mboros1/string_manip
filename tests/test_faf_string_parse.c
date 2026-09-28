#include "faf.h"
#include "faf_test.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define S(lit) faf_string_init(lit)

static uint64_t rng_state = 0x9E3779B97F4A7C15ull;
static uint32_t rng(void) {
  rng_state ^= rng_state << 13;
  rng_state ^= rng_state >> 7;
  rng_state ^= rng_state << 17;
  return (uint32_t)rng_state;
}

static void test_parse_int(void) {
  int64_t i;
  uint64_t u;
  ASSERT_TRUE(faf_string_parse_i64(S("-9223372036854775808"), &i) && i == INT64_MIN, "i64 min");
  ASSERT_TRUE(faf_string_parse_i64(S("9223372036854775807"), &i) && i == INT64_MAX, "i64 max");
  ASSERT_TRUE(faf_string_parse_i64(S("+17"), &i) && i == 17, "i64 plus");
  ASSERT_TRUE(faf_string_parse_i64(S("-0"), &i) && i == 0, "i64 -0");
  ASSERT_FALSE(faf_string_parse_i64(S("9223372036854775808"), &i), "i64 overflow");
  ASSERT_FALSE(faf_string_parse_i64(S("-9223372036854775809"), &i), "i64 underflow");
  ASSERT_TRUE(faf_string_parse_u64(S("18446744073709551615"), &u) && u == UINT64_MAX, "u64 max");
  ASSERT_FALSE(faf_string_parse_u64(S("18446744073709551616"), &u), "u64 overflow");
  ASSERT_FALSE(faf_string_parse_u64(S("-1"), &u), "u64 negative");
  const char *bad[] = {"", "-", "+", " 1", "1 ", "12a", "0x10", "1.0", "--1"};
  for (size_t k = 0; k < sizeof(bad) / sizeof(bad[0]); ++k)
    ASSERT_FALSE(faf_string_parse_i64(S(bad[k]), &i), bad[k]);
  i = 5;
  faf_string_parse_i64(S("nope"), &i);
  ASSERT_TRUE(i == 5, "failed parse must not write");
}

static uint64_t bits(double d) {
  uint64_t b;
  memcpy(&b, &d, sizeof(b));
  return b;
}

static uint64_t ulp_diff(double a, double b) {
  uint64_t x = bits(a), y = bits(b);
  return x > y ? x - y : y - x;
}

static void test_parse_f64(void) {
  double d;
  // fast path: must match strtod exactly
  const char *exact[] = {"0", "1", "-1", "0.1", "3.14159", "-2.5e-3", "1e22",
                         "123456789012345", "9007199254740992", ".5", "5.",
                         "1E5", "+0.000001", "4.35", "1.7976931348623157e10"};
  for (size_t k = 0; k < sizeof(exact) / sizeof(exact[0]); ++k) {
    ASSERT_TRUE(faf_string_parse_f64(S(exact[k]), &d), exact[k]);
    ASSERT_TRUE(bits(d) == bits(strtod(exact[k], NULL)), exact[k]);
  }
  // random fast path inputs
  char buf[64];
  for (int k = 0; k < 20000; ++k) {
    uint64_t mant = ((uint64_t)rng() << 21 | rng()) % 9007199254740993ull;
    int e = (int)(rng() % 45) - 22;
    snprintf(buf, sizeof(buf), "%llue%d", (unsigned long long)mant, e);
    if (!faf_string_parse_f64(S(buf), &d) || bits(d) != bits(strtod(buf, NULL))) {
      ASSERT_TRUE(0, buf);
      break;
    }
  }
  // slow path: within a few ulp
  const char *approx[] = {"1e23", "123456789012345678901234567890", "1.7976931348623157e308",
                          "2.2250738585072014e-308", "6.02214076e23", "1e-300",
                          "0.30000000000000004441"};
  for (size_t k = 0; k < sizeof(approx) / sizeof(approx[0]); ++k) {
    ASSERT_TRUE(faf_string_parse_f64(S(approx[k]), &d), approx[k]);
    ASSERT_TRUE(ulp_diff(d, strtod(approx[k], NULL)) <= 4, approx[k]);
  }
  ASSERT_FALSE(faf_string_parse_f64(S("1e309"), &d), "overflow");
  ASSERT_TRUE(faf_string_parse_f64(S("1e-400"), &d) && d == 0.0, "underflow to zero");
  const char *bad[] = {"", ".", "-", "e5", "1e", "1e+", "+-1", "1.2.3", "1 ", "nan", "inf"};
  for (size_t k = 0; k < sizeof(bad) / sizeof(bad[0]); ++k)
    ASSERT_FALSE(faf_string_parse_f64(S(bad[k]), &d), bad[k]);
}

static void test_from_int(void) {
  faf_region r = faf_region_acquire();
  ASSERT_STR_EQ("-9223372036854775808", faf_string_from_i64(r, INT64_MIN).start, "from_i64 min");
  ASSERT_STR_EQ("0", faf_string_from_i64(r, 0).start, "from_i64 zero");
  ASSERT_STR_EQ("18446744073709551615", faf_string_from_u64(r, UINT64_MAX).start, "from_u64");
  // round trip
  for (int k = 0; k < 1000; ++k) {
    int64_t v = (int64_t)((uint64_t)rng() << 32 | rng());
    int64_t back;
    faf_string s = faf_string_from_i64(r, v);
    ASSERT_TRUE(faf_string_parse_i64(s, &back) && back == v, "i64 round trip");
    if (faf_region_remaining(r) < 8) {
      faf_region_release(r);
      r = faf_region_acquire();
    }
  }
  faf_region_release(r);
}

static void test_ascii_utf8(void) {
  ASSERT_TRUE(faf_string_is_ascii(S("plain ascii, long enough to use SIMD blocks")), "ascii");
  ASSERT_FALSE(faf_string_is_ascii(S("caf\xC3\xA9")), "not ascii");
  ASSERT_TRUE(faf_string_is_ascii(S("")), "empty is ascii");

  const char *valid[] = {"", "hello", "caf\xC3\xA9", "\xE2\x82\xAC 100",
                         "\xF0\x9F\x98\x80 grin", "\xEF\xBF\xBF", "\xF4\x8F\xBF\xBF",
                         "a long ascii run before the multibyte part \xC3\xA9 and after"};
  for (size_t k = 0; k < sizeof(valid) / sizeof(valid[0]); ++k)
    ASSERT_TRUE(faf_string_utf8_valid(S(valid[k])), valid[k]);

  const char *invalid[] = {"\x80", "abc\xBF", "\xC0\x80", "\xC1\xBF", "\xE0\x80\x80",
                           "\xED\xA0\x80" /* surrogate */, "\xF4\x90\x80\x80" /* > U+10FFFF */,
                           "\xF5\x80\x80\x80", "\xE2\x82" /* truncated */, "\xC3",
                           "\xC3\x28", "a long ascii run and then a truncated \xF0\x9F\x98"};
  for (size_t k = 0; k < sizeof(invalid) / sizeof(invalid[0]); ++k)
    ASSERT_FALSE(faf_string_utf8_valid(S(invalid[k])), "invalid utf-8 accepted");
}

// Test case definitions
static test_case_t parse_tests[] = {
    {"parse_int", test_parse_int},
    {"parse_f64", test_parse_f64},
    {"from_int", test_from_int},
    {"ascii_utf8", test_ascii_utf8},
};

static void parse_setup(void) {}

static void parse_teardown(void) {}

int main(int argc, char **argv) {
  test_suite_t suite = TEST_SUITE("Parse", parse_tests,
                                  sizeof(parse_tests) / sizeof(parse_tests[0]),
                                  parse_setup, parse_teardown);
  register_test_suite(suite);
  return test_main(argc, argv);
}
