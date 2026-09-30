#include "core/faf_string.h"
#include "text/faf_string_case.h"
#include "mem/faf_string_mem.h"
#include "faf_test.h"

#include <stdio.h>
#include <string.h>

static const char *long_str =
      "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";

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

/* ---- Arenas over caller memory ---- */

// Room for small arenas, with a canary past the end to catch overruns.
#define ARENA_BUF 4096
#define CANARY 64
static _Alignas(64) unsigned char arena_buf[ARENA_BUF + CANARY];
static _Alignas(64) unsigned char arena_buf2[ARENA_BUF];

static void fill_canary(size_t nbytes) {
  memset(arena_buf + nbytes, 0xA5, CANARY);
}

static bool canary_intact(size_t nbytes) {
  for (size_t i = 0; i < CANARY; ++i)
    if (arena_buf[nbytes + i] != 0xA5)
      return false;
  return true;
}

static void test_arena_init(void) {
  // faf_arena_bytes is enough at every alignment of the buffer
  const size_t npools = 3, slots = 20;
  size_t need = faf_arena_bytes(npools, slots);
  ASSERT_TRUE(need > 0 && need + 64 <= ARENA_BUF, "unexpected arena size");
  for (size_t shift = 0; shift < 64; ++shift) {
    faf_arena a;
    ASSERT_TRUE(faf_arena_init(&a, arena_buf + shift, need, npools),
                "init failed with faf_arena_bytes of memory");
    faf_region r = faf_arena_acquire(&a);
    ASSERT_TRUE(faf_region_valid(r), "no region from a new arena");
    ASSERT_TRUE(faf_region_capacity(r) >= slots, "pools smaller than asked");
    faf_span sp = faf_reserve(r, 1);
    ASSERT_TRUE((uintptr_t)sp.ptr % FAF_SLOT_BYTES == 0,
                "arena slots not aligned to FAF_SLOT_BYTES");
    faf_region_release(r);
  }
}

static void test_arena_init_rejects(void) {
  faf_arena a;
  ASSERT_TRUE(!faf_arena_init(&a, NULL, ARENA_BUF, 2), "NULL buffer accepted");
  ASSERT_TRUE(!faf_arena_init(&a, arena_buf, ARENA_BUF, 0), "0 pools accepted");
  ASSERT_TRUE(!faf_arena_init(&a, arena_buf, ARENA_BUF, UINT16_MAX),
              "UINT16_MAX pools accepted");
  ASSERT_TRUE(!faf_arena_init(&a, arena_buf, 8, 2), "8 bytes accepted");
  // the smallest buffer that works gives each pool exactly one slot, and one
  // byte less is rejected
  size_t min = 1;
  while (min <= ARENA_BUF && !faf_arena_init(&a, arena_buf, min, 4))
    ++min;
  ASSERT_TRUE(min <= faf_arena_bytes(4, 1), "faf_arena_bytes(4, 1) too small");
  ASSERT_INT_EQ(1, (int)faf_region_capacity(faf_arena_acquire(&a)),
                "smallest arena has more than a slot per pool");
  ASSERT_TRUE(!faf_arena_init(&a, arena_buf, min - 1, 4),
              "too small for a slot per pool accepted");
  ASSERT_TRUE(faf_region_valid(faf_arena_acquire(&a)) == false,
              "failed arena handed out a region");
  ASSERT_INT_EQ(0, (int)faf_arena_bytes(0, 10), "0 pools has a size");
  ASSERT_INT_EQ(0, (int)faf_arena_bytes(2, SIZE_MAX / 2), "overflow has a size");
}

static void test_arena_regions(void) {
  faf_arena a;
  ASSERT_TRUE(faf_arena_init(&a, arena_buf, ARENA_BUF, 4), "init failed");

  faf_region all[4];
  for (int i = 0; i < 4; ++i) {
    all[i] = faf_arena_acquire(&a);
    ASSERT_TRUE(faf_region_valid(all[i]), "arena pool not handed out");
    for (int j = 0; j < i; ++j)
      ASSERT_TRUE(all[i].pool != all[j].pool, "two live regions share a pool");
  }
  ASSERT_TRUE(!faf_region_valid(faf_arena_acquire(&a)),
              "acquire past the arena's pools succeeded");

  // strings allocated from arena regions live in the arena's memory
  faf_string lower = faf_string_to_lower(all[2], faf_string_init("HeLLo"));
  ASSERT_STR_EQ("hello", lower.start, "wrong result in arena region");
  ASSERT_TRUE(faf_arena_contains(&a, lower.start, 6), "result not in arena");
  ASSERT_TRUE(!faf_mem_contains(lower.start, 1), "result in default arena");

  faf_region_release(all[2]);
  ASSERT_TRUE(!faf_region_valid(all[2]), "released handle still valid");
  faf_region again = faf_arena_acquire(&a);
  ASSERT_TRUE(again.pool == all[2].pool, "freed pool not reused");
  ASSERT_TRUE(!faf_region_valid(all[2]), "old handle valid after reuse");

  faf_region_release(again);
  for (int i = 0; i < 4; ++i)
    faf_region_release(all[i]);
}

static void test_arena_independent(void) {
  faf_arena a, b;
  ASSERT_TRUE(faf_arena_init(&a, arena_buf, ARENA_BUF, 1), "init a failed");
  ASSERT_TRUE(faf_arena_init(&b, arena_buf2, ARENA_BUF, 1), "init b failed");

  faf_region ra = faf_arena_acquire(&a);
  // a exhausted: b and the default arena are unaffected
  ASSERT_TRUE(!faf_region_valid(faf_arena_acquire(&a)), "a has a second pool");
  faf_region rb = faf_arena_acquire(&b);
  faf_region rd = faf_region_acquire();
  ASSERT_TRUE(faf_region_valid(rb) && faf_region_valid(rd),
              "one arena's use blocked another");

  // same pool index and generation, different arena: not interchangeable
  faf_region forged = {.arena = &b, .pool = ra.pool, .gen = ra.gen};
  faf_region_release(ra);
  ASSERT_TRUE(faf_region_valid(rb), "releasing in a invalidated b");
  ASSERT_TRUE(faf_region_valid(forged), "b's pool should still be live");
  faf_string s = faf_string_copy(rb, faf_string_init("in b"));
  ASSERT_TRUE(faf_arena_contains(&b, s.start, 5) &&
                  !faf_arena_contains(&a, s.start, 1),
              "copy landed in the wrong arena");

  faf_region_release(rb);
  faf_region_release(rd);
}

static void test_arena_bounds(void) {
  // fill every pool to capacity; nothing may be written past nbytes
  const size_t nbytes = 1000; // deliberately not a multiple of anything
  fill_canary(nbytes);
  faf_arena a;
  ASSERT_TRUE(faf_arena_init(&a, arena_buf, nbytes, 3), "init failed");
  faf_region r[3];
  for (int i = 0; i < 3; ++i) {
    r[i] = faf_arena_acquire(&a);
    size_t cap = faf_region_capacity(r[i]);
    ASSERT_TRUE(cap > 0, "pool has no slots");
    ASSERT_TRUE(faf_reserve(r[i], cap + 1).ptr == NULL, "reserved past a pool");
    faf_span sp = faf_reserve(r[i], cap);
    ASSERT_TRUE(sp.ptr != NULL, "couldn't reserve a whole pool");
    ASSERT_TRUE(!faf_reserve_extend(r[i], &sp, 1), "extended past a pool");
    memset(sp.ptr, 'X', cap * FAF_SLOT_BYTES);
    ASSERT_INT_EQ(0, (int)faf_region_remaining(r[i]), "full pool has room");
  }
  ASSERT_TRUE(canary_intact(nbytes), "arena wrote past its buffer");
  for (int i = 0; i < 3; ++i)
    faf_region_release(r[i]);
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
    {"arena_init", test_arena_init},
    {"arena_init_rejects", test_arena_init_rejects},
    {"arena_regions", test_arena_regions},
    {"arena_independent", test_arena_independent},
    {"arena_bounds", test_arena_bounds},
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
