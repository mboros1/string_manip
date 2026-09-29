#include "faf.h"
#include "faf_test.h"

#include <stdio.h>
#include <string.h>

#define S(lit) faf_string_init(lit)

static uint32_t rng_state = 12345;
static uint32_t rng(void) {
  rng_state ^= rng_state << 13;
  rng_state ^= rng_state >> 17;
  rng_state ^= rng_state << 5;
  return rng_state;
}

static void test_init(void) {
  char buf[16];
  faf_ring r;
  ASSERT_TRUE(faf_ring_init(&r, buf, sizeof buf), "init");
  ASSERT_FALSE(faf_ring_init(&r, NULL, 16), "NULL buffer");
  ASSERT_TRUE(faf_ring_ref_is_none(faf_ring_push(&r, S("a"))),
              "push after a failed init");
  ASSERT_FALSE(faf_ring_init(&r, buf, 0), "empty buffer");
  ASSERT_TRUE(faf_ring_ref_is_none(faf_ring_push(&r, S(""))),
              "push to an empty buffer");
}

static void test_push_get(void) {
  char buf[32];
  faf_ring r;
  faf_ring_init(&r, buf, sizeof buf);
  faf_ring_ref a = faf_ring_push(&r, S("hello"));
  faf_ring_ref b = faf_ring_push(&r, S(""));
  faf_ring_ref c = faf_ring_push(&r, S("world"));
  faf_string sa = faf_ring_get(&r, a), sb = faf_ring_get(&r, b),
             sc = faf_ring_get(&r, c);
  ASSERT_TRUE(faf_string_eq(sa, S("hello")), "first");
  ASSERT_TRUE(faf_string_eq(sb, S("")) && !faf_string_is_none(sb), "empty");
  ASSERT_TRUE(faf_string_eq(sc, S("world")), "third");
  ASSERT_TRUE(*sa.end == '\0' && *sb.end == '\0' && *sc.end == '\0',
              "NUL terminated");
  ASSERT_TRUE(faf_ring_ref_is_none(faf_ring_push(&r, FAF_STRING_NONE)),
              "none is not pushed");
  ASSERT_TRUE(faf_string_is_none(faf_ring_get(&r, FAF_RING_REF_NONE)),
              "get of the none handle");
}

static void test_too_long(void) {
  char buf[8];
  faf_ring r;
  faf_ring_init(&r, buf, sizeof buf);
  faf_ring_ref a = faf_ring_push(&r, S("abc"));
  ASSERT_TRUE(faf_ring_ref_is_none(faf_ring_push(&r, S("12345678"))),
              "longer than the buffer");
  ASSERT_TRUE(faf_string_eq(faf_ring_get(&r, a), S("abc")),
              "a failed push changes nothing");
  // exactly the buffer (7 + NUL) fits, and replaces everything
  faf_ring_ref b = faf_ring_push(&r, S("1234567"));
  ASSERT_TRUE(faf_string_eq(faf_ring_get(&r, b), S("1234567")), "whole buffer");
  ASSERT_FALSE(faf_ring_valid(&r, a), "lapped");
}

// Random pushes against a model that records which push last wrote each
// byte (a wrap marks the skipped tail as written). A handle must be valid
// exactly when every byte of its string (and its NUL) is still its own, and
// then read back what was pushed.
enum { MAXCAP = 97, NPUSH = 3000, KEEP = 256 };

static bool model_run(size_t cap, uint64_t start_head) {
  static char buf[MAXCAP], text[MAXCAP];
  static int owner[MAXCAP];
  static faf_ring_ref refs[KEEP];
  static int ids[KEEP];
  faf_ring r;
  faf_ring_init(&r, buf, cap);
  r.head = start_head; // keeps head == off (mod cap): start_head % cap == 0
  for (size_t i = 0; i < cap; ++i)
    owner[i] = -1;
  for (int i = 0; i < KEEP; ++i)
    ids[i] = -1;

  for (int id = 0; id < NPUSH; ++id) {
    size_t len = rng() % cap; // 0 .. cap - 1: always fits
    for (size_t k = 0; k < len; ++k)
      text[k] = (char)('a' + (id + k) % 26);
    size_t off = r.off;
    faf_ring_ref ref = faf_ring_push(&r, (faf_string){text, text + len});
    if (faf_ring_ref_is_none(ref))
      return false;
    if (off + len + 1 > cap) { // wrapped: the tail counts as written
      for (size_t k = off; k < cap; ++k)
        owner[k] = -1;
      off = 0;
    }
    for (size_t k = off; k <= off + len; ++k)
      owner[k] = id;
    refs[id % KEEP] = ref;
    ids[id % KEEP] = id;

    for (int j = 0; j < KEEP; ++j) {
      if (ids[j] < 0)
        continue;
      faf_ring_ref q = refs[j];
      bool intact = true;
      for (size_t k = q.off; k <= (size_t)q.off + q.len; ++k)
        intact &= owner[k] == ids[j];
      if (faf_ring_valid(&r, q) != intact)
        return false;
      faf_string s = faf_ring_get(&r, q);
      if (intact) {
        if (faf_string_len(s) != q.len || s.start[q.len] != '\0')
          return false;
        for (size_t k = 0; k < q.len; ++k)
          if (s.start[k] != (char)('a' + (ids[j] + k) % 26))
            return false;
      } else if (!faf_string_is_none(s)) {
        return false;
      }
    }
  }
  return true;
}

static void test_model(void) {
  ASSERT_TRUE(model_run(64, 0), "cap 64");
  ASSERT_TRUE(model_run(97, 0), "cap 97 (not a power of two)");
  ASSERT_TRUE(model_run(1, 0), "cap 1 (only empty strings)");
  ASSERT_TRUE(model_run(2, 0), "cap 2");
}

// head is 64 bits: nothing changes where a 32-bit count would wrap
static void test_head_past_32_bits(void) {
  ASSERT_TRUE(model_run(64, 0xFFFFF000u), "head crossing 2^32");
  ASSERT_TRUE(model_run(97, (uint64_t)97 << 40), "head near 2^47");
}

static test_case_t ring_tests[] = {
    {"init", test_init},
    {"push_get", test_push_get},
    {"too_long", test_too_long},
    {"model", test_model},
    {"head_past_32_bits", test_head_past_32_bits},
};

static void ring_setup(void) {}

static void ring_teardown(void) {}

int main(int argc, char **argv) {
  test_suite_t suite = TEST_SUITE("Ring", ring_tests,
                                  sizeof(ring_tests) / sizeof(ring_tests[0]),
                                  ring_setup, ring_teardown);
  register_test_suite(suite);
  return test_main(argc, argv);
}
