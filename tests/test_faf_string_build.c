#include "faf.h"
#include "faf_test.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define S(lit) faf_string_init(lit)

// NUL terminated and zero padded to the end of its last slot
static int terminated(faf_string s) {
  if (faf_string_is_none(s))
    return 0;
  size_t len = faf_string_len(s);
  size_t end = faf_slots_for(len) * FAF_SLOT_BYTES;
  for (size_t i = len; i < end; ++i)
    if (s.start[i] != '\0')
      return 0;
  return 1;
}

static void test_builder_in_place(void) {
  char expected[2048] = {0};
  for (int i = 0; i < 100; ++i)
    snprintf(expected + strlen(expected), sizeof(expected) - strlen(expected),
             "item%d,", i);
  // growing in place doubles, so allow twice the final size
  TEST_REQUIRE(2 * faf_slots_for(strlen(expected)) <= FAF_POOL_SLOTS,
               "the result needs a bigger region (FAF_POOL_SLOTS)");
  faf_region r = faf_region_acquire();
  faf_builder b = faf_builder_init(r);
  for (int i = 0; i < 100; ++i) {
    faf_builder_append(&b, S("item"));
    faf_builder_append_i64(&b, i);
    faf_builder_append_char(&b, ',');
  }
  const faf_slot *first = b.sp.ptr;
  faf_string s = faf_builder_finish(&b);
  ASSERT_STR_EQ(expected, s.start, "builder contents");
  ASSERT_TRUE(terminated(s), "builder result not terminated");
  ASSERT_TRUE((const faf_slot *)s.start == first, "builder moved while growing in place");
  // finish gave the unused tail back
  ASSERT_INT_EQ((int)faf_slots_for(strlen(expected)), (int)faf_region_used(r),
                "builder left unused slots reserved");
  faf_region_release(r);
}

// Near the end of the region doubling doesn't fit, but growing in place by
// just enough still does.
static void test_builder_near_full(void) {
  const size_t room = faf_slots_for(150); // exactly the final string
  TEST_REQUIRE(room <= FAF_POOL_SLOTS, "150 bytes need a bigger region");
  faf_region r = faf_region_acquire();
  faf_reserve(r, FAF_POOL_SLOTS - room);
  faf_builder b = faf_builder_init(r);
  char expected[160] = {0};
  for (int i = 0; i < 9; ++i) { // 9 x 16 bytes, then 6: 150 bytes + NUL
    faf_builder_append(&b, S("0123456789abcdef"));
    strcat(expected, "0123456789abcdef");
  }
  faf_builder_append(&b, S("tail!!"));
  strcat(expected, "tail!!");
  const faf_slot *first = b.sp.ptr;
  faf_string s = faf_builder_finish(&b);
  ASSERT_STR_EQ(expected, s.start, "builder contents near a full region");
  ASSERT_TRUE(terminated(s), "builder result not terminated");
  ASSERT_TRUE((const faf_slot *)s.start == first, "builder moved near a full region");
  ASSERT_INT_EQ(FAF_POOL_SLOTS, (int)faf_region_used(r), "region not filled exactly");
  faf_region_release(r);
}

static void test_builder_moves(void) {
  faf_region r = faf_region_acquire();
  faf_builder b = faf_builder_init(r);
  faf_builder_append(&b, S("first part, "));
  faf_string other = faf_string_copy(r, S("allocated in between"));
  faf_builder_append(&b, S("second part that no longer fits in place"));
  faf_string s = faf_builder_finish(&b);
  ASSERT_STR_EQ("first part, second part that no longer fits in place", s.start,
                "builder contents after move");
  ASSERT_STR_EQ("allocated in between", other.start, "neighbour was clobbered");
  ASSERT_TRUE(terminated(s), "moved result not terminated");
  faf_region_release(r);
}

static void test_builder_numbers(void) {
  faf_region r = faf_region_acquire();
  faf_builder b = faf_builder_init(r);
  faf_builder_append_i64(&b, INT64_MIN);
  faf_builder_append_char(&b, ' ');
  faf_builder_append_i64(&b, INT64_MAX);
  faf_builder_append_char(&b, ' ');
  faf_builder_append_u64(&b, UINT64_MAX);
  faf_builder_append_char(&b, ' ');
  faf_builder_append_i64(&b, 0);
  faf_string s = faf_builder_finish(&b);
  ASSERT_STR_EQ("-9223372036854775808 9223372036854775807 18446744073709551615 0",
                s.start, "number formatting");
  faf_region_release(r);
}

static void test_builder_out_of_space(void) {
  faf_region r = faf_region_acquire();
  faf_reserve(r, FAF_POOL_SLOTS - faf_slots_for(4)); // room for "fits" only
  faf_builder b = faf_builder_init(r);
  ASSERT_TRUE(faf_builder_append(&b, S("fits")), "small append failed");
  char too_long[2 * FAF_SLOT_BYTES + 1];
  memset(too_long, 'x', sizeof(too_long) - 1);
  too_long[sizeof(too_long) - 1] = '\0';
  ASSERT_FALSE(faf_builder_append(&b, faf_string_init(too_long)),
               "oversized append succeeded");
  ASSERT_FALSE(faf_builder_append(&b, S("x")), "builder should stay failed");
  ASSERT_TRUE(faf_string_is_none(faf_builder_finish(&b)), "failed builder finished");
  faf_region_release(r);
}

static void test_join(void) {
  faf_region r = faf_region_acquire();
  faf_string_arr parts = faf_string_split(r, S("a,bb,,ccc"), ',');
  faf_string joined = faf_string_join(r, parts, S(" | "));
  ASSERT_STR_EQ("a | bb |  | ccc", joined.start, "join");
  ASSERT_TRUE(terminated(joined), "join not terminated");
  faf_string_arr none = {.start = parts.start, .end = parts.start};
  ASSERT_STR_EQ("", faf_string_join(r, none, S(",")).start, "join nothing");
  faf_string_arr one = {.start = parts.start, .end = parts.start + 1};
  ASSERT_STR_EQ("a", faf_string_join(r, one, S(",")).start, "join one");
  faf_region_release(r);
}

static void test_repeat_pad_reverse(void) {
  faf_region r = faf_region_acquire();
  ASSERT_STR_EQ("abcabcabc", faf_string_repeat(r, S("abc"), 3).start, "repeat");
  ASSERT_STR_EQ("", faf_string_repeat(r, S("abc"), 0).start, "repeat zero");
  ASSERT_STR_EQ("", faf_string_repeat(r, S(""), 5).start, "repeat empty");
  faf_string many = faf_string_repeat(r, S("xy"), 37);
  ASSERT_INT_EQ(74, (int)faf_string_len(many), "repeat length");
  ASSERT_TRUE(terminated(many), "repeat not terminated");
  for (size_t i = 0; i < 74; ++i)
    ASSERT_TRUE(many.start[i] == (i % 2 ? 'y' : 'x'), "repeat contents");

  ASSERT_STR_EQ("00042", faf_string_pad_left(r, S("42"), 5, '0').start, "pad_left");
  ASSERT_STR_EQ("ab...", faf_string_pad_right(r, S("ab"), 5, '.').start, "pad_right");
  ASSERT_STR_EQ("toolong", faf_string_pad_left(r, S("toolong"), 3, ' ').start,
                "pad shorter width");

  faf_region_release(r);

  const char *fwd = "0123456789abcdefghijklmnopqrstuvwxyz!";
  char rev[64];
  for (size_t len = 0; len <= strlen(fwd); ++len) {
    for (size_t i = 0; i < len; ++i)
      rev[i] = fwd[len - 1 - i];
    rev[len] = '\0';
    r = faf_region_acquire(); // a region per length: fits pools of any size
    faf_string out = faf_string_reverse(r, faf_string_init_n(fwd, len));
    ASSERT_STR_EQ(rev, out.start, "reverse");
    faf_region_release(r);
  }
}

static void test_replace(void) {
  faf_region r = faf_region_acquire();
  ASSERT_STR_EQ("a-b-c", faf_string_replace(r, S("a, b, c"), S(", "), S("-")).start,
                "replace shrink");
  ASSERT_STR_EQ("x==y==z", faf_string_replace(r, S("x=y=z"), S("="), S("==")).start,
                "replace grow");
  ASSERT_STR_EQ("bb", faf_string_replace(r, S("aaaa"), S("aa"), S("b")).start,
                "replace non-overlapping");
  ASSERT_STR_EQ("same", faf_string_replace(r, S("same"), S("zz"), S("y")).start,
                "replace none");
  ASSERT_STR_EQ("abc", faf_string_replace(r, S("abc"), S(""), S("y")).start,
                "replace empty from");
  ASSERT_STR_EQ("", faf_string_replace(r, S("abab"), S("ab"), S("")).start,
                "replace to empty");
  faf_region_release(r);
}

static void test_format(void) {
  faf_region r = faf_region_acquire();
  faf_string name = S("world");
  faf_string s = faf_string_format(r, "hello %S, %s! %d%% %i %u %x %c", name, "C str",
                                   -42, 7, 3000000000u, 255, 'Z');
  ASSERT_STR_EQ("hello world, C str! -42% 7 3000000000 ff Z", s.start, "format basics");
  ASSERT_TRUE(terminated(s), "format not terminated");

  s = faf_string_format(r, "%ld %lld %llu %zu %lx %hd %hhu", -1L, (long long)INT64_MIN,
                        (unsigned long long)UINT64_MAX, (size_t)12345, 0xdeadbeefUL,
                        (short)-2, (unsigned char)200);
  ASSERT_STR_EQ("-1 -9223372036854775808 18446744073709551615 12345 deadbeef -2 200",
                s.start, "format length modifiers");

  ASSERT_STR_EQ("", faf_string_format(r, "").start, "format empty");
  ASSERT_STR_EQ("no conversions", faf_string_format(r, "no conversions").start,
                "format literal");
  ASSERT_TRUE(faf_string_is_none(faf_string_format(r, "bad %q")), "unknown conversion");
  ASSERT_TRUE(faf_string_is_none(faf_string_format(r, "trailing %")), "trailing %");
  faf_region_release(r);
}

// Test case definitions
static test_case_t build_tests[] = {
    {"builder_in_place", test_builder_in_place},
    {"builder_near_full", test_builder_near_full},
    {"builder_moves", test_builder_moves},
    {"builder_numbers", test_builder_numbers},
    {"builder_out_of_space", test_builder_out_of_space},
    {"join", test_join},
    {"repeat_pad_reverse", test_repeat_pad_reverse},
    {"replace", test_replace},
    {"format", test_format},
};

static void build_setup(void) {}

static void build_teardown(void) {}

int main(int argc, char **argv) {
  test_suite_t suite = TEST_SUITE("Build", build_tests,
                                  sizeof(build_tests) / sizeof(build_tests[0]),
                                  build_setup, build_teardown);
  register_test_suite(suite);
  return test_main(argc, argv);
}
