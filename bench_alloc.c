// Allocation benchmarks: where regions win, where they don't, and what they
// cost in memory.
//
//   size sweep       region vs a plain bump arena vs malloc, 8 B .. 4 KB
//   region overhead  acquire/release with other pools already held
//   churn            a long-lived table of strings, updated in place
//   growth           building a string by appending

#include "bench.h"
#include "faf.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define POOL_BYTES ((size_t)FAF_POOL_SLOTS * FAF_SLOT_BYTES)

// Deterministic xorshift, so every run and every variant sees the same data.
static uint32_t rng_state;
static uint32_t rng(void) {
  uint32_t x = rng_state;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  return rng_state = x;
}

static _Alignas(16) char src[4096 + 64];

/* ---- 1. Size sweep ---- */

#define SWEEP_OPS 20000

// The simplest possible arena, as a baseline for what the region adds
// (handles, generation checks, zero padding).
static _Alignas(16) char arena[POOL_BYTES];
static size_t arena_off;

static inline char *arena_copy(const char *p, size_t len) {
  size_t need = (len + 16) & ~(size_t)15; // len + NUL, rounded to 16
  if (need > POOL_BYTES - arena_off)
    return NULL;
  char *d = arena + arena_off;
  arena_off += need;
  memcpy(d, p, len);
  d[len] = '\0';
  return d;
}

static void sweep_region(size_t len, int batch) {
  size_t acc = 0;
  faf_string s = faf_string_init_n(src, len);
  for (int done = 0; done + batch <= SWEEP_OPS; done += batch) {
    faf_region r = faf_region_acquire();
    for (int k = 0; k < batch; ++k) {
      faf_string c = faf_string_copy(r, s);
      sink_ptr = (char *)c.start;
      acc += (size_t)c.start[k & 3];
    }
    faf_region_release(r);
  }
  sink = acc;
}

static void sweep_arena(size_t len, int batch) {
  size_t acc = 0;
  for (int done = 0; done + batch <= SWEEP_OPS; done += batch) {
    arena_off = 0;
    for (int k = 0; k < batch; ++k) {
      char *c = arena_copy(src, len);
      sink_ptr = c;
      acc += (size_t)c[k & 3];
    }
  }
  sink = acc;
}

static void sweep_malloc(size_t len, int batch) {
  size_t acc = 0;
  char *ptrs[64];
  for (int done = 0; done + batch <= SWEEP_OPS; done += batch) {
    for (int k = 0; k < batch; ++k) {
      char *c = malloc(len + 1);
      memcpy(c, src, len);
      c[len] = '\0';
      sink_ptr = ptrs[k] = c;
      acc += (size_t)c[k & 3];
    }
    for (int k = 0; k < batch; ++k)
      free(ptrs[k]);
  }
  sink = acc;
}

static void size_sweep(void) {
  static const size_t sizes[] = {8, 16, 32, 64, 256, 1024, 4096};
  static char names[7][48];
  section("Size sweep",
          "copy a string of N bytes (NUL included) in batches, then free the "
          "batch; ns per string");
  for (size_t i = 0; i < 7; ++i) {
    size_t len = sizes[i] - 1;
    // as many as fit in one region, at most 64
    size_t per_region = POOL_BYTES / (faf_slots_for(len) * FAF_SLOT_BYTES);
    int batch = per_region < 64 ? (int)per_region : 64;
    int ops = SWEEP_OPS / batch * batch;
    snprintf(names[i], sizeof names[i], "%zu bytes, batches of %d",
             sizes[i], batch);
    group_begin(names[i], NS_PER_OP);
    BENCH("region: faf_string_copy", ops, sweep_region(len, batch));
    BENCH("bump arena: memcpy", ops, sweep_arena(len, batch));
    BENCH("malloc + memcpy + free", ops, sweep_malloc(len, batch));
  }
}

/* ---- 2. Region acquire/release overhead ---- */

#define ACQUIRE_OPS 200000

static void acquire_release_loop(void) {
  size_t acc = 0;
  for (int i = 0; i < ACQUIRE_OPS; ++i) {
    faf_region r = faf_region_acquire();
    acc += r.pool;
    faf_region_release(r);
  }
  sink = acc;
}

static void malloc_free_loop(void) {
  for (int i = 0; i < ACQUIRE_OPS; ++i) {
    char *p = malloc(16);
    sink_ptr = p;
    free(p);
  }
}

static void region_overhead(void) {
  static char names[3][48];
  const int held_counts[3] = {0, FAF_NPOOLS / 2, FAF_NPOOLS - 1};
  section("Region overhead",
          "acquire + release of one region while others are held (acquire "
          "scans for a free pool)");
  group_begin(NULL, NS_PER_OP);
  for (int c = 0; c < 3; ++c) {
    faf_region held[FAF_NPOOLS];
    for (int h = 0; h < held_counts[c]; ++h)
      held[h] = faf_region_acquire();
    snprintf(names[c], sizeof names[c], "acquire + release, %d of %d held",
             held_counts[c], FAF_NPOOLS);
    BENCH(names[c], ACQUIRE_OPS, acquire_release_loop());
    for (int h = 0; h < held_counts[c]; ++h)
      faf_region_release(held[h]);
  }
  BENCH("for scale: malloc(16) + free", ACQUIRE_OPS, malloc_free_loop());
}

/* ---- 3. Churn: a long-lived table of strings ---- */

// Each step lowercases a record (a temporary), builds a key from its first
// two fields and stores it over a random entry of the table. malloc frees the
// old entry; a region can't, so when it fills up the live entries are copied
// into a fresh region and the old one is released (compaction).

#define CHURN_STEPS 20000
#define CHURN_MAX 512

static struct {
  size_t compactions, copied, failures, peak_bytes;
} churn;

static void churn_region(int live) {
  faf_string table[CHURN_MAX];
  memset(&churn, 0, sizeof churn);
  rng_state = 12345;

  faf_region cur = faf_region_acquire();
  for (int i = 0; i < live; ++i)
    table[i] = faf_string_copy(cur, faf_string_init_n(lines[i], 16));

  size_t acc = 0;
  for (int step = 0; step < CHURN_STEPS; ++step) {
    int i = step % NLINES;
    faf_region tmp = faf_region_acquire();
    faf_string lower =
        faf_string_to_lower(tmp, faf_string_init_n(lines[i], line_lens[i]));
    faf_string a, b, rest;
    faf_string_split_once(lower, ',', &a, &rest);
    faf_string_split_once(rest, ',', &b, &rest);

    faf_string key = faf_string_concat(cur, a, b);
    if (faf_string_is_none(key)) {
      faf_region next = faf_region_acquire();
      for (int k = 0; k < live; ++k)
        table[k] = faf_string_copy(next, table[k]);
      size_t both = (faf_region_used(cur) + faf_region_used(next)) *
                    FAF_SLOT_BYTES;
      if (both > churn.peak_bytes)
        churn.peak_bytes = both;
      faf_region_release(cur);
      cur = next;
      churn.compactions++;
      churn.copied += (size_t)live;
      key = faf_string_concat(cur, a, b);
    }
    if (faf_string_is_none(key)) {
      churn.failures++;
    } else {
      table[rng() % (uint32_t)live] = key;
      acc += faf_string_len(key);
    }
    size_t used = faf_region_used(cur) * FAF_SLOT_BYTES;
    if (used > churn.peak_bytes)
      churn.peak_bytes = used;
    faf_region_release(tmp);
  }
  faf_region_release(cur);
  sink = acc;
}

static void churn_malloc(int live) {
  char *table[CHURN_MAX];
  size_t table_len[CHURN_MAX];
  memset(&churn, 0, sizeof churn);
  rng_state = 12345;

  size_t live_bytes = 0;
  for (int i = 0; i < live; ++i) {
    table[i] = strndup(lines[i], 16);
    table_len[i] = 16;
    live_bytes += 17;
  }
  churn.peak_bytes = live_bytes;

  size_t acc = 0;
  for (int step = 0; step < CHURN_STEPS; ++step) {
    int i = step % NLINES;
    size_t n = line_lens[i];
    char *lower = malloc(n + 1);
    for (size_t j = 0; j <= n; ++j)
      lower[j] = (char)tolower((unsigned char)lines[i][j]);
    char *c1 = memchr(lower, ',', n);
    char *c2 = memchr(c1 + 1, ',', (size_t)(lower + n - c1 - 1));
    size_t la = (size_t)(c1 - lower), lb = (size_t)(c2 - c1 - 1);

    char *key = malloc(la + lb + 1);
    memcpy(key, lower, la);
    memcpy(key + la, c1 + 1, lb);
    key[la + lb] = '\0';

    uint32_t slot = rng() % (uint32_t)live;
    live_bytes += la + lb + 1;
    if (live_bytes > churn.peak_bytes)
      churn.peak_bytes = live_bytes;
    live_bytes -= table_len[slot] + 1;
    free(table[slot]);
    table[slot] = key;
    table_len[slot] = la + lb;
    acc += la + lb;
    free(lower);
  }
  for (int i = 0; i < live; ++i)
    free(table[i]);
  sink = acc;
}

static void churn_benches(void) {
  static const int lives[] = {64, 256, 512};
  static char names[3][48];
  section("Churn",
          "a table of N live strings; each step builds a key from a temporary "
          "and replaces a random entry; ns per step");
  for (int c = 0; c < 3; ++c) {
    int live = lives[c];
    snprintf(names[c], sizeof names[c], "%d live strings", live);
    group_begin(names[c], NS_PER_OP);

    BENCH("region, compact when full", CHURN_STEPS, churn_region(live));
    if (churn.failures) {
      group_failed();
      group_note("%zu of %d stores: live set > one region", churn.failures,
                 CHURN_STEPS);
    } else
      group_note("peak %.1f KB, %zu compactions (%zu copies)",
                 churn.peak_bytes / 1024.0, churn.compactions, churn.copied);

    BENCH("malloc + free", CHURN_STEPS, churn_malloc(live));
    group_note("peak %.1f KB requested", churn.peak_bytes / 1024.0);
  }
}

/* ---- 4. Growth: building a string by appending ---- */

#define GROW_LEN 4000
#define GROW_REPS 500
// with interleaving: one small allocation per this many bytes appended
#define INTERLEAVE_EVERY 256

static size_t grow_region_bytes;
static size_t grow_failures;

static void grow_builder(size_t chunk, bool interleave) {
  size_t acc = 0;
  grow_failures = 0;
  for (int rep = 0; rep < GROW_REPS; ++rep) {
    faf_region r = faf_region_acquire();
    faf_builder b = faf_builder_init(r);
    size_t since = 0;
    for (size_t len = 0; len < GROW_LEN;) {
      size_t n = GROW_LEN - len < chunk ? GROW_LEN - len : chunk;
      if (n == 1)
        faf_builder_append_char(&b, src[len & 63]);
      else
        faf_builder_append(&b, faf_string_init_n(src, n));
      len += n;
      since += n;
      if (interleave && since >= INTERLEAVE_EVERY) {
        faf_reserve(r, 1); // something else allocated from the same region
        since = 0;
      }
    }
    faf_string s = faf_builder_finish(&b);
    if (faf_string_is_none(s))
      grow_failures++;
    else
      acc += (size_t)s.start[GROW_LEN / 2];
    grow_region_bytes = faf_region_used(r) * FAF_SLOT_BYTES;
    faf_region_release(r);
  }
  sink = acc;
}

static void grow_realloc(size_t chunk) {
  size_t acc = 0;
  for (int rep = 0; rep < GROW_REPS; ++rep) {
    size_t cap = 16, len = 0;
    char *buf = malloc(cap);
    while (len < GROW_LEN) {
      size_t n = GROW_LEN - len < chunk ? GROW_LEN - len : chunk;
      if (len + n + 1 > cap) {
        while (len + n + 1 > cap)
          cap *= 2;
        buf = realloc(buf, cap);
      }
      if (n == 1)
        buf[len] = src[len & 63];
      else
        memcpy(buf + len, src, n);
      len += n;
    }
    buf[len] = '\0';
    sink_ptr = buf;
    acc += (size_t)buf[GROW_LEN / 2];
    free(buf);
  }
  sink = acc;
}

static void grow_memstream(size_t chunk) {
  size_t acc = 0;
  for (int rep = 0; rep < GROW_REPS; ++rep) {
    char *buf;
    size_t size;
    FILE *f = open_memstream(&buf, &size);
    for (size_t len = 0; len < GROW_LEN;) {
      size_t n = GROW_LEN - len < chunk ? GROW_LEN - len : chunk;
      if (n == 1)
        fputc(src[len & 63], f);
      else
        fwrite(src, 1, n, f);
      len += n;
    }
    fclose(f);
    sink_ptr = buf;
    acc += (size_t)buf[GROW_LEN / 2];
    free(buf);
  }
  sink = acc;
}

// lower bound: the final size is known up front
static void grow_prealloc(size_t chunk) {
  size_t acc = 0;
  for (int rep = 0; rep < GROW_REPS; ++rep) {
    char *buf = malloc(GROW_LEN + 1);
    for (size_t len = 0; len < GROW_LEN;) {
      size_t n = GROW_LEN - len < chunk ? GROW_LEN - len : chunk;
      if (n == 1)
        buf[len] = src[len & 63];
      else
        memcpy(buf + len, src, n);
      len += n;
    }
    buf[GROW_LEN] = '\0';
    sink_ptr = buf;
    acc += (size_t)buf[GROW_LEN / 2];
    free(buf);
  }
  sink = acc;
}

static void growth(void) {
  static const size_t chunks[] = {1, 16, 1024};
  static const char *names[] = {"1-byte appends", "16-byte appends",
                                "1 KB appends"};
  section("Growth",
          "build a %d-byte string by appending; ns per string. \"interleaved\" "
          "allocates from the same region every %d bytes, so the builder can't "
          "grow in place",
          GROW_LEN, INTERLEAVE_EVERY);
  for (int c = 0; c < 3; ++c) {
    size_t chunk = chunks[c];
    group_begin(names[c], NS_PER_OP);
    BENCH("faf_builder", GROW_REPS, grow_builder(chunk, false));
    group_note("region used: %.1f KB", grow_region_bytes / 1024.0);
    BENCH("faf_builder, interleaved", GROW_REPS, grow_builder(chunk, true));
    if (grow_failures) {
      group_failed();
      group_note("%zu of %d builds", grow_failures, GROW_REPS);
    } else
      group_note("region used: %.1f KB", grow_region_bytes / 1024.0);
    BENCH("realloc, doubling", GROW_REPS, grow_realloc(chunk));
    BENCH("open_memstream", GROW_REPS, grow_memstream(chunk));
    BENCH("malloc final size up front", GROW_REPS, grow_prealloc(chunk));
  }
}

void bench_alloc(void) {
  for (size_t i = 0; i < sizeof src; ++i)
    src[i] = (char)('a' + i % 26);
  size_sweep();
  region_overhead();
  churn_benches();
  growth();
  group_end();
}
