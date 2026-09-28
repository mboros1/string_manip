#include "faf_string.h"
#include "faf_string_mem.h"
#include "faf_test.h"

#include <stdio.h>
#include <string.h>

static const char *long_str =
      "LoJxoUH9kIUGg8EhZvChVx5tKlagaCBX6cGDKR7aDNWW2XMl4mhOxxM2QG4Mex0fXjfHuTFf"
      "1gdP5v6CjbUHgePZgwtZISwIrmEjwhgBGYwSQuffc2uEgW5tP4eWZURC19apUQaUNdfeVPBB"
      "Lx1yAnu4i0P4G7lnWwaoKk3hGoaM3sM1YAv4WZM1UGjcwPybJZhWn8r8Uabal2usCCODlBDE"
      "L7VYWxVl4U2gvoI6FLDy5YDWSbhN9i15QPKCN90RwVNfH6r8hhg4AeVdpKr2ePQEHoiZ8Q2K"
      "mRqmNjaqImESE9c7lHxdJoeWJ9hpP2VpVDx9QnKhup9z3hVIhZUCoQCbY3ThfaZPs4kftoVG"
      "xuPUjMdjK61KA3YqhRT4FwMMcxohcs2b7WL3qyYuHXXtx1SnAnCUjMDwsb1e25ndxuppw4F2"
      "ti6ZwYY63k7zY8mcNzoUsGIswicW2qFlhqgqkTqm8eASquu3DhS4YBsGPWRdfHj1o1zMqm1W"
      "7DRqPXzkUXy9RUIfJ6bDeuFdIvNhi5kcpuxwmqgB1GopjpCQ9lpsDg2yI5KQhPiUB1UhFABC"
      "dzFFAJCMQm4xO8ayJg6OeQD9AOP62HinKllcZ02r2svkzZlhsE9ddg444tj6IMi2gY7y0fxx"
      "SLIZGrEg6QpGJz71nq9vJ9GtJ9NDkT4aSiAhlUFqk7Dn4raweUKQjXB10LckW8bqtKhriBeQ"
      "MpwZDIcHoaEEdjOofJ2ErgGgiEgvOKkCktnThEVjZaqKLZIqi5vB2SQSNAlAgDYC0f7lyqOr"
      "F6uYUaNhxqXkSmMKjHL9OypnkfKC5STKe2pWFhjLsufkC9I1V6CyRBJBB04QbsktM2UOBVHt"
      "uEwQzESXKkvYREXCbOKDhiIOAH1hP0Rkjd07OqUuoB9q9HMyg3PoSm5wAsqxxzFbU6Jjuspe"
      "IFDUqUwlqysszUGP619Ga0o9WOrtQtDDX3M03WQ9op3IIU8DAYTZWkekdfkf8PUiZToOMArX"
      "jfPfCumY7ufxCftqNSG2WEPALwLqyCJnsfVdBguejxrTLI1hvF6bgHC6cUkom0Z9jYzlC5IW"
      "LXmezDd00DhnadVE8C01eb2RFx42o3q9WgboI0EC4UTGCXAgK0jqebEXFyGWdE9WGZ5uK3Ly"
      "pBUKk9PKoBNzyZdnGFDt3qzj6xyGONTvMVBeZrY5NdR57ZPTLBra8XOTCGG1goy1NxcAJPr6"
      "EprDjQPSAsfpvElnbGX9NnwlbBiB8vyVT5wUd07GnHvsjBp5lUcrQa3s6AfFHQRXYLWyIXy2"
      "d34Taex5nBUmWkLk7r116dEdfLPsvs2lvneHNtZD5VQQ79jU2EVx2TjmYZRs1B61daUS9aZX"
      "ZHYgNzXe2QY7CeWpHqrSmDlDAqDpreYpNB1sdcMV3YgzHg9o8eBbNWrJE47iLVstnJwi8Xc2"
      "epIWxzs7fAa6E5t0GlrRRw3Nd5SG6jBbAMhPsI0XZsitBRgsy8fjiHaQaq92yBdJFYQe9fm4"
      "QJgD12fAPaxwXdelOqC6WnGCvZe0FkyxNQTYZuKBZNWtrE7TzD89f4VlSeQzhQa53JAZMHny"
      "pOt1783mZlFUnuxIHnwNgRFuOKO7wSH7bCJdYb3mMPIH8CclSeeBtbFxPotCLEkea2yyAM50"
      "0Fqj27IpYKa9Es6TvL6cfjOgIv97GZw5otF8WOOjEiK9PvSVEd119PpFQIbigfnxzhBQbRzF"
      "zwWIdVFpSv51PdX0O7ft0OmMfBgnmHiA1aCY58qQpDypXSVtycbOqTNqEYMtGzdGvq7r5ibK"
      "nKdxvoSApDIAJkZ2Gsw3Wsm1cwLVE6ts98OwyFHlTD7fBTWI28v5LrqZe16KSaIqIrT5y1pe"
      "sA59h1QawuvzJu9Csj96yE2OffYYbu7ybj6z2WGPB3HRJrK7gr8aN7dQoYtxo5ZQ4MBBvyMJ"
      "KI0s737pWcAPRNyCAUUTRPssag9wmvxpMUzdTCO7pQ93";

static void test_acquire(void) {
  faf_region r = faf_region_acquire();

  ASSERT_TRUE(faf_region_valid(r), "Acquired region is not valid");
  ASSERT_INT_EQ(0, (int)faf_region_used(r), "New region is not empty");
  ASSERT_INT_EQ(FAF_POOL_SLOTS, (int)faf_region_remaining(r),
                "New region capacity incorrect");

  faf_region_release(r);
}

static void test_acquire_distinct(void) {
  TEST_REQUIRE(FAF_NPOOLS >= 2, "needs two pools (FAF_NPOOLS)");
  // two acquires without any allocation in between must not share a pool
  faf_region a = faf_region_acquire();
  faf_region b = faf_region_acquire();

  ASSERT_TRUE(faf_region_valid(a) && faf_region_valid(b),
              "Acquired regions are not valid");
  ASSERT_TRUE(a.pool != b.pool, "Two live regions share a pool");

  faf_region_release(a);
  faf_region_release(b);
}

static void test_acquire_exhausted(void) {
  faf_region all[FAF_NPOOLS];
  for (int i = 0; i < FAF_NPOOLS; ++i) {
    all[i] = faf_region_acquire();
    ASSERT_TRUE(faf_region_valid(all[i]), "Acquire failed with pools free");
  }

  faf_region extra = faf_region_acquire();
  ASSERT_FALSE(faf_region_valid(extra), "Acquire succeeded with no free pools");

  const int freed = FAF_NPOOLS / 2; // any valid index, for any FAF_NPOOLS
  faf_region_release(all[freed]);
  extra = faf_region_acquire();
  ASSERT_TRUE(faf_region_valid(extra), "Acquire failed after a release");

  faf_region_release(extra);
  for (int i = 0; i < FAF_NPOOLS; ++i) {
    faf_region_release(all[i]); // all[freed] is stale: must be a no-op
  }
}

static void test_reserve(void) {
  faf_region r = faf_region_acquire();

  faf_span a = faf_reserve(r, 10);
  faf_span b = faf_reserve(r, 5);
  ASSERT_TRUE(a.ptr != NULL && b.ptr != NULL, "Reserve failed");
  ASSERT_TRUE(b.ptr == a.ptr + 10, "Spans are not contiguous");
  ASSERT_TRUE(((uintptr_t)a.ptr & (FAF_SLOT_BYTES - 1)) == 0,
              "Span is not slot aligned");
  ASSERT_INT_EQ(15, (int)faf_region_used(r), "Used slots incorrect");
  ASSERT_INT_EQ(FAF_POOL_SLOTS - 15, (int)faf_region_remaining(r),
                "Remaining slots incorrect");

  faf_region_release(r);
}

static void test_reserve_bounds(void) {
  faf_region r = faf_region_acquire();

  faf_span too_big = faf_reserve(r, FAF_POOL_SLOTS + 1);
  ASSERT_TRUE(too_big.ptr == NULL, "Oversized reserve succeeded");
  ASSERT_INT_EQ(0, (int)faf_region_used(r), "Failed reserve moved the cursor");

  faf_span all = faf_reserve(r, FAF_POOL_SLOTS);
  ASSERT_TRUE(all.ptr != NULL, "Reserving the whole pool failed");

  faf_span one_more = faf_reserve(r, 1);
  ASSERT_TRUE(one_more.ptr == NULL, "Reserve past capacity succeeded");
  ASSERT_INT_EQ(FAF_POOL_SLOTS, (int)faf_region_used(r),
                "Failed reserve moved the cursor");

  faf_region_release(r);
}

static void test_release_invalidates(void) {
  faf_region r = faf_region_acquire();
  faf_region_release(r);

  ASSERT_FALSE(faf_region_valid(r), "Handle still valid after release");
  ASSERT_TRUE(faf_reserve(r, 1).ptr == NULL, "Reserve on stale handle succeeded");

  // the pool comes back with a new generation, so the old handle stays stale
  faf_region again = faf_region_acquire();
  ASSERT_TRUE(again.pool == r.pool, "Expected the same pool back");
  ASSERT_FALSE(faf_region_valid(r), "Stale handle valid after reacquire");
  ASSERT_INT_EQ(0, (int)faf_region_used(again), "Reacquired region not empty");

  faf_region_release(r); // stale: must not release `again`
  ASSERT_TRUE(faf_region_valid(again), "Stale release freed the new owner");
  faf_region_release(again);
}

static void test_copy_short(void) {
  faf_region r = faf_region_acquire();

  const char *base_str = "hello world";
  faf_string copy = faf_string_copy(r, faf_string_init(base_str));

  ASSERT_TRUE(copy.start != base_str, "Copy did not allocate");
  ASSERT_TRUE(faf_mem_contains(copy.start, 1), "Copy is not in pool storage");
  ASSERT_STR_EQ(base_str, copy.start, "Copied string differs");

  faf_region_release(r);
}

static void test_copy_long(void) {
  TEST_REQUIRE(faf_slots_for(strlen(long_str)) <= FAF_POOL_SLOTS,
               "long_str doesn't fit in one region (FAF_POOL_SLOTS)");
  faf_region r = faf_region_acquire();

  faf_string copy = faf_string_copy(r, faf_string_init(long_str));

  ASSERT_INT_EQ((int)strlen(long_str), (int)faf_string_len(copy),
                "Copied length differs");
  ASSERT_STR_EQ(long_str, copy.start, "Copied string differs");
  ASSERT_INT_EQ((int)faf_slots_for(strlen(long_str)), (int)faf_region_used(r),
                "Copy used the wrong number of slots");

  faf_region_release(r);
}

static void test_copy_nul_terminated(void) {
  // dirty the pool first so a missing terminator can't be hidden by zeroed
  // static memory
  faf_region r = faf_region_acquire();
  faf_span sp = faf_reserve(r, FAF_POOL_SLOTS);
  memset(sp.ptr, 'X', (size_t)FAF_POOL_SLOTS * FAF_SLOT_BYTES);
  uint16_t pool = r.pool;
  faf_region_release(r);

  // lengths on and around the 16 byte boundary, each copied to the start of
  // the dirty pool
  const char *src = "0123456789abcdef0123456789ABCDEF!";
  for (size_t len = 0; len <= 33; ++len) {
    r = faf_region_acquire();
    ASSERT_TRUE(r.pool == pool, "Expected the dirty pool back");
    faf_string copy = faf_string_copy(r, faf_string_init_n(src, len));
    ASSERT_TRUE(!faf_string_is_none(copy), "Copy failed");
    if (!faf_string_is_none(copy)) {
      ASSERT_INT_EQ((int)len, (int)strlen(copy.start),
                    "Copy is not NUL terminated at its length");
      ASSERT_TRUE(memcmp(src, copy.start, len) == 0, "Copied bytes differ");
    }
    faf_region_release(r);
  }
}

static void test_copy_from_region(void) {
  // copying a string that already lives in pool storage takes the masked
  // tail path; the bytes after it must not leak into the copy
  faf_region r = faf_region_acquire();
  faf_string a = faf_string_copy(r, faf_string_init("abcdefghijklmnopqrs"));
  faf_string b = faf_string_copy(r, faf_string_init("ZZZZZZZZZZZZZZZZZZZZZZZZ"));
  faf_string prefix = {.start = a.start, .end = a.start + 17};

  faf_string c = faf_string_copy(r, prefix);
  ASSERT_STR_EQ("abcdefghijklmnopq", c.start, "Copy from region differs");
  ASSERT_STR_EQ("ZZZZZZZZZZZZZZZZZZZZZZZZ", b.start, "Neighbour was clobbered");

  faf_region_release(r);
}

static void test_copy_out_of_space(void) {
  faf_region r = faf_region_acquire();
  faf_reserve(r, FAF_POOL_SLOTS - faf_slots_for(5)); // room for one "short"

  faf_string fits = faf_string_copy(r, faf_string_init("short"));
  faf_string none = faf_string_copy(r, faf_string_init("short"));
  ASSERT_FALSE(faf_string_is_none(fits), "Copy into the last slots failed");
  ASSERT_TRUE(faf_string_is_none(none), "Copy into a full region succeeded");

  faf_region_release(r);
}

// Test case definitions
static test_case_t string_mem_tests[] = {
    {"acquire", test_acquire},
    {"acquire_distinct", test_acquire_distinct},
    {"acquire_exhausted", test_acquire_exhausted},
    {"reserve", test_reserve},
    {"reserve_bounds", test_reserve_bounds},
    {"release_invalidates", test_release_invalidates},
    {"copy_short", test_copy_short},
    {"copy_long", test_copy_long},
    {"copy_nul_terminated", test_copy_nul_terminated},
    {"copy_from_region", test_copy_from_region},
    {"copy_out_of_space", test_copy_out_of_space},
};

// Setup and teardown functions
static void string_mem_setup(void) {
    // Any setup code needed before each test
}

static void string_mem_teardown(void) {
    // Any cleanup code needed after each test
}

// Main function
int main(int argc, char** argv) {
    // Register test suite
    test_suite_t suite = TEST_SUITE(
        "StringMemory", 
        string_mem_tests,
        sizeof(string_mem_tests) / sizeof(string_mem_tests[0]),
        string_mem_setup,
        string_mem_teardown
    );
    register_test_suite(suite);
    
    // Normal execution
    return test_main(argc, argv);
}
