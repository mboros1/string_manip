// Benchmarks for the region allocator and the string operations built on it.
// Build with `make bench` (compiled at -O2, independent of CFLAGS).
//
// Each benchmark reports the best of RUNS runs, in ns per operation.

#include "faf.h"
#include "faf_kernels.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define RUNS 5
#define NLINES 20000

static volatile size_t sink;
// escape malloc results so the compiler can't elide malloc/free pairs
static char *volatile sink_ptr;

static double now_ns(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec * 1e9 + ts.tv_nsec;
}

#define BENCH(label, ops, ...)                                                  \
  do {                                                                         \
    double best = 1e30;                                                        \
    for (int run_ = 0; run_ < RUNS; ++run_) {                                  \
      double t0_ = now_ns();                                                   \
      __VA_ARGS__;                                                             \
      double t_ = now_ns() - t0_;                                              \
      if (t_ < best)                                                           \
        best = t_;                                                             \
    }                                                                          \
    printf("  %-44s %9.1f ns/op\n", label, best / (ops));                      \
  } while (0)

/* ---- Test data ---- */

static char *lines[NLINES];
static size_t line_lens[NLINES];

// CSV-like records: 8 fields of 2..24 mixed-case alphanumerics.
static void make_lines(void) {
  const char alnum[] =
      "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
  srand(42);
  for (int i = 0; i < NLINES; ++i) {
    char buf[256];
    size_t n = 0;
    for (int f = 0; f < 8; ++f) {
      int flen = 2 + rand() % 23;
      for (int c = 0; c < flen; ++c)
        buf[n++] = alnum[rand() % (sizeof(alnum) - 1)];
      if (f < 7)
        buf[n++] = ',';
    }
    buf[n] = '\0';
    lines[i] = strdup(buf);
    line_lens[i] = n;
  }
}

/* ---- 1. Per-record processing: region vs malloc ---- */

// split the line into owned fields, lowercase field 2, concat fields 0 and 1,
// then free everything for the record
static void record_region(void) {
  size_t acc = 0;
  for (int i = 0; i < NLINES; ++i) {
    faf_region r = faf_region_acquire();
    faf_string line = faf_string_init_n(lines[i], line_lens[i]);
    faf_string_arr f = faf_string_split_owned(r, line, ',');
    faf_string lower = faf_string_to_lower(r, f.start[2]);
    faf_string key = faf_string_concat(r, f.start[0], f.start[1]);
    acc += faf_string_len(lower) + faf_string_len(key) + (size_t)key.start[0];
    faf_region_release(r);
  }
  sink = acc;
}

static void record_malloc(void) {
  size_t acc = 0;
  for (int i = 0; i < NLINES; ++i) {
    size_t len = line_lens[i];
    char *own = malloc(len + 1);
    memcpy(own, lines[i], len + 1);

    size_t count = 1;
    for (size_t j = 0; j < len; ++j)
      count += own[j] == ',';
    char **fields = malloc(count * sizeof(char *));
    char *p = own;
    for (size_t k = 0; k < count; ++k)
      fields[k] = strsep(&p, ",");

    size_t l2 = strlen(fields[2]);
    char *lower = malloc(l2 + 1);
    for (size_t j = 0; j <= l2; ++j)
      lower[j] = (char)tolower((unsigned char)fields[2][j]);

    size_t l0 = strlen(fields[0]), l1 = strlen(fields[1]);
    char *key = malloc(l0 + l1 + 1);
    memcpy(key, fields[0], l0);
    memcpy(key + l0, fields[1], l1 + 1);

    acc += l2 + l0 + l1 + (size_t)key[0];
    free(key);
    free(lower);
    free(fields);
    free(own);
  }
  sink = acc;
}

/* ---- 2. Many small allocations, freed together ---- */

#define SMALL_BATCH 64
#define SMALL_OPS (NLINES / SMALL_BATCH * SMALL_BATCH)

static void small_region(void) {
  size_t acc = 0;
  for (int i = 0; i + SMALL_BATCH <= NLINES; i += SMALL_BATCH) {
    faf_region r = faf_region_acquire();
    for (int k = 0; k < SMALL_BATCH; ++k) {
      faf_string s = faf_string_init_n(lines[i + k], 20);
      acc += (size_t)faf_string_copy(r, s).start[3];
    }
    faf_region_release(r);
  }
  sink = acc;
}

static void small_malloc(void) {
  size_t acc = 0;
  char *ptrs[SMALL_BATCH];
  for (int i = 0; i + SMALL_BATCH <= NLINES; i += SMALL_BATCH) {
    for (int k = 0; k < SMALL_BATCH; ++k) {
      ptrs[k] = malloc(21);
      sink_ptr = ptrs[k];
      memcpy(ptrs[k], lines[i + k], 20);
      ptrs[k][20] = '\0';
      acc += (size_t)ptrs[k][3];
    }
    for (int k = 0; k < SMALL_BATCH; ++k)
      free(ptrs[k]);
  }
  sink = acc;
}

/* ---- 3. Splitting a line: array of views, iterator, owned copy ---- */

static void split_views(void) {
  size_t acc = 0;
  for (int i = 0; i < NLINES; ++i) {
    faf_region r = faf_region_acquire();
    faf_string_arr a =
        faf_string_split(r, faf_string_init_n(lines[i], line_lens[i]), ',');
    acc += faf_string_len(a.start[3]);
    faf_region_release(r);
  }
  sink = acc;
}

static void split_iterator(void) {
  size_t acc = 0;
  for (int i = 0; i < NLINES; ++i) {
    faf_string rest = faf_string_init_n(lines[i], line_lens[i]), field;
    while (faf_string_next_token(&rest, ',', &field))
      acc += faf_string_len(field);
  }
  sink = acc;
}

// the same iterator on libc memchr, as a baseline
static void split_iterator_memchr(void) {
  size_t acc = 0;
  for (int i = 0; i < NLINES; ++i) {
    const char *p = lines[i], *e = lines[i] + line_lens[i];
    for (;;) {
      const char *q = memchr(p, ',', (size_t)(e - p));
      acc += (size_t)((q ? q : e) - p);
      if (!q)
        break;
      p = q + 1;
    }
  }
  sink = acc;
}

static void split_owned(void) {
  size_t acc = 0;
  for (int i = 0; i < NLINES; ++i) {
    faf_region r = faf_region_acquire();
    faf_string_arr a = faf_string_split_owned(
        r, faf_string_init_n(lines[i], line_lens[i]), ',');
    acc += faf_string_len(a.start[3]);
    faf_region_release(r);
  }
  sink = acc;
}

/* ---- 4. Kernels: SIMD backend vs scalar reference vs libc ---- */

#define BIG (64 * 1024)
static char big[BIG + 64];

// Run `expr` over every line (short inputs) or over `big` (throughput).
#define LINES_BENCH(label, expr)                                               \
  BENCH(label, NLINES, {                                                       \
    size_t acc = 0;                                                            \
    for (int i = 0; i < NLINES; ++i) {                                         \
      const char *s = lines[i];                                                \
      size_t n = line_lens[i];                                                 \
      (void)s, (void)n;                                                        \
      acc += (size_t)(expr);                                                   \
    }                                                                          \
    sink = acc;                                                                \
  })

#define BIG_BENCH(label, expr)                                                 \
  do {                                                                         \
    double best = 1e30;                                                        \
    for (int run_ = 0; run_ < RUNS; ++run_) {                                  \
      double t0_ = now_ns();                                                   \
      for (int rep_ = 0; rep_ < 200; ++rep_) {                                  \
        __asm__ volatile("" ::: "memory"); /* no hoisting across reps */     \
        sink = (size_t)(expr);                                                 \
      }                                                                        \
      double t_ = (now_ns() - t0_) / 200;                                       \
      if (t_ < best)                                                           \
        best = t_;                                                             \
    }                                                                          \
    printf("  %-44s %9.2f GB/s\n", label, BIG / best);                         \
  } while (0)

static size_t libc_count(const char *s, size_t n, char c) {
  size_t count = 0;
  for (const char *p = s; (p = memchr(p, c, (size_t)(s + n - p))); ++p)
    ++count;
  return count;
}

static size_t memchr_idx(const char *s, size_t n, char c) {
  const char *p = memchr(s, c, n);
  return p ? (size_t)(p - s) : n;
}

static char lower_buf[BIG + 64];
static char *line_copies[NLINES];

static void kernel_benches(void) {
  // big: random text with a comma every ~40 bytes, no NUL until the end
  for (size_t i = 0; i < BIG; ++i)
    big[i] = (i % 40 == 39) ? ',' : (char)('A' + rand() % 58);
  big[BIG] = '\0';
  faf_byteset ws;
  faf_byteset_init(&ws, " \t\n\r", 4);
  const char *other = strdup(big);
  for (int i = 0; i < NLINES; ++i)
    line_copies[i] = strdup(lines[i]);

  printf("Kernels on short lines (~%zu bytes)\n", line_lens[0]);
  LINES_BENCH("find_byte ','          simd", faf_k_find_byte(s, n, '#'));
  LINES_BENCH("find_byte ','          ref", faf_ref_find_byte(s, n, '#'));
  LINES_BENCH("memchr                 libc", memchr_idx(s, n, '#'));
  LINES_BENCH("strlen                 simd", faf_k_strlen(s + (i & 7)));
  LINES_BENCH("strlen                 ref", faf_ref_strlen(s + (i & 7)));
  LINES_BENCH("strlen                 libc", strlen(s + (i & 7)));
  LINES_BENCH("mismatch (equal)       simd", faf_k_mismatch(s, line_copies[i], n));
  LINES_BENCH("mismatch (equal)       ref", faf_ref_mismatch(s, line_copies[i], n));
  LINES_BENCH("memcmp (equal)         libc", memcmp(s, line_copies[i], n) != 0);

  printf("Kernel throughput (64 KB buffer)\n");
  BIG_BENCH("find_byte (absent)     simd", faf_k_find_byte(big, BIG, '#'));
  BIG_BENCH("find_byte (absent)     ref", faf_ref_find_byte(big, BIG, '#'));
  BIG_BENCH("memchr (absent)        libc", memchr_idx(big, BIG, '#'));
  BIG_BENCH("count_byte             simd", faf_k_count_byte(big, BIG, ','));
  BIG_BENCH("count_byte             ref", faf_ref_count_byte(big, BIG, ','));
  BIG_BENCH("count via memchr       libc", libc_count(big, BIG, ','));
  BIG_BENCH("strlen                 simd", faf_k_strlen(big));
  BIG_BENCH("strlen                 libc", strlen(big));
  BIG_BENCH("mismatch (equal)       simd", faf_k_mismatch(big, other, BIG));
  BIG_BENCH("mismatch (equal)       ref", faf_ref_mismatch(big, other, BIG));
  BIG_BENCH("memcmp (equal)         libc", memcmp(big, other, BIG));
  BIG_BENCH("find_set whitespace    simd", faf_k_find_set(big, BIG, &ws, true));
  BIG_BENCH("find_set whitespace    ref", faf_ref_find_set(big, BIG, &ws, true));
  BIG_BENCH("strcspn whitespace     libc", strcspn(big, " \t\n\r"));
  BIG_BENCH("ascii_case lower       simd",
            (faf_k_ascii_case(lower_buf, big, BIG, false), lower_buf[7]));
  BIG_BENCH("ascii_case lower       ref",
            (faf_ref_ascii_case(lower_buf, big, BIG, false), lower_buf[7]));
  BIG_BENCH("utf8_valid (ascii)     simd",
            faf_string_utf8_valid(faf_string_init_n(big, BIG)));
  BIG_BENCH("hash                   scalar",
            faf_string_hash(faf_string_init_n(big, BIG)));
  BIG_BENCH("faf_memcpy             simd",
            (faf_memcpy(lower_buf, big, BIG), lower_buf[9]));
  BIG_BENCH("memcpy                 libc", (memcpy(lower_buf, big, BIG), lower_buf[9]));
  free((void *)other);
}

/* ---- 5. Per-line scratch buffer (generate_random_strings) ---- */

static void scratch_region(void) {
  size_t acc = 0;
  for (int i = 0; i < NLINES; ++i) {
    size_t len = 1000 + (size_t)(i * 7919) % 10000;
    faf_region r = faf_region_acquire();
    char *buf = (char *)faf_reserve(r, faf_slots_for(len)).ptr;
    buf[0] = (char)i;
    buf[len] = '\0';
    acc += (size_t)buf[0];
    faf_region_release(r);
  }
  sink = acc;
}

static void scratch_malloc(void) {
  size_t acc = 0;
  for (int i = 0; i < NLINES; ++i) {
    size_t len = 1000 + (size_t)(i * 7919) % 10000;
    char *buf = malloc(len + 1);
    sink_ptr = buf;
    buf[0] = (char)i;
    buf[len] = '\0';
    acc += (size_t)buf[0];
    free(buf);
  }
  sink = acc;
}

int main(void) {
  make_lines();
  printf("backend: %s\n", FAF_BACKEND_NAME);

  printf("Record processing (split_owned + to_lower + concat, per line)\n");
  BENCH("region: acquire ... release", NLINES, record_region());
  BENCH("malloc: strdup/strsep/malloc ... free", NLINES, record_malloc());

  printf("Small copies (20 bytes, batches of %d freed together)\n",
         SMALL_BATCH);
  BENCH("region: faf_string_copy + release", SMALL_OPS, small_region());
  BENCH("malloc: malloc + memcpy + free", SMALL_OPS, small_malloc());

  printf("Splitting a line into its 8 fields\n");
  BENCH("split (array of views)", NLINES, split_views());
  BENCH("next_token iterator (no allocation)", NLINES, split_iterator());
  BENCH("same iterator on libc memchr", NLINES, split_iterator_memchr());
  BENCH("split_owned (copy + NUL terminated)", NLINES, split_owned());

  printf("Per-line scratch buffer (1-11 KB, generate_random_strings)\n");
  BENCH("region: acquire + reserve + release", NLINES, scratch_region());
  BENCH("malloc + free", NLINES, scratch_malloc());

  kernel_benches();
  return 0;
}
