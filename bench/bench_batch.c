// Batch benchmarks: the calls a binding makes (faf_batch.h), per line of the
// input joined into one buffer, next to the same work done a string at a
// time in C. The two should match: a batch saves crossings from another
// language, not work.

#include "bench.h"
#include "faf.h"
#include "kernels/faf_kernels.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *buf;
static size_t buf_len;
static char *arena_buf;
static faf_arena *arena;
static faf_region in_r;    // holds the input, split once
static faf_batch *lines_b;
static const int64_t *starts, *ends;
static int64_t *out64;
static uint8_t *mask;
static uint64_t *hashes;
static char *dst;

// Two pools: one holds the split input, the other each result in turn (a
// region acquired and released per call). False if there isn't memory.
static bool setup(void) {
  buf_len = 0;
  for (int i = 0; i < NLINES; ++i)
    buf_len += line_lens[i] + 1;
  size_t n = NLINES;
  size_t pool = buf_len + 2 * (n + 1) * sizeof(int64_t) + 1024; // bytes
  size_t arena_bytes = faf_arena_bytes(2, pool / FAF_SLOT_BYTES + 1);
  buf = malloc(buf_len);
  arena_buf = malloc(arena_bytes);
  arena = malloc(faf_arena_size());
  out64 = malloc(n * sizeof *out64);
  mask = malloc(n);
  hashes = malloc(n * sizeof *hashes);
  dst = malloc(buf_len + 1);
  if (!buf || !arena_buf || !arena || !out64 || !mask || !hashes || !dst ||
      !faf_arena_init(arena, arena_buf, arena_bytes, 2))
    return false;
  size_t at = 0;
  for (int i = 0; i < NLINES; ++i) {
    memcpy(buf + at, lines[i], line_lens[i]);
    at += line_lens[i];
    buf[at++] = '\n';
  }
  buf_len = at - 1; // no trailing newline: exactly NLINES pieces
  in_r = faf_arena_acquire(arena);
  lines_b = faf_batch_split(in_r, buf, buf_len, '\n');
  starts = faf_batch_starts(lines_b);
  ends = faf_batch_ends(lines_b);
  return lines_b != NULL;
}

static void teardown(void) {
  faf_region_release(in_r);
  free(buf), free(arena_buf), free(arena), free(out64), free(mask);
  free(hashes), free(dst);
}

static faf_string line_at(size_t i) {
  return faf_string_init_n(buf + starts[i], (size_t)(ends[i] - starts[i]));
}

/* ---- per string, in C ---- */

static void loop_split(void) {
  faf_string rest = faf_string_init_n(buf, buf_len), piece;
  size_t k = 0;
  while (faf_string_next_token(&rest, '\n', &piece))
    k += (size_t)(piece.end - piece.start);
  sink += k;
}

static void loop_contains(const char *needle) {
  faf_string sub = faf_string_init(needle);
  size_t yes = 0;
  for (size_t i = 0; i < NLINES; ++i)
    yes += mask[i] = faf_string_contains(line_at(i), sub);
  sink += yes;
}

static void loop_hash(void) {
  for (size_t i = 0; i < NLINES; ++i)
    hashes[i] = faf_string_hash(line_at(i));
  sink += (size_t)hashes[NLINES - 1];
}

static void loop_lower(void) {
  int64_t at = 0;
  for (size_t i = 0; i < NLINES; ++i) {
    size_t len = (size_t)(ends[i] - starts[i]);
    faf_k_ascii_case(dst + at, buf + starts[i], len, false);
    at += (int64_t)len;
  }
  sink += (size_t)at;
}

// A batch call that makes a new one, in a region taken and released around
// it, as a caller doing one step of work would.
#define MADE(call)                                                             \
  do {                                                                         \
    faf_region r = faf_arena_acquire(arena);                                   \
    sink += faf_batch_len(call);                                               \
    faf_region_release(r);                                                     \
  } while (0)

void bench_batch(void) {
  section("Batch calls", "%d CSV-like lines in one buffer; ns per line", NLINES);
  if (!setup()) {
    printf("  not enough memory for %zu bytes of input and results\n",
           3 * buf_len);
    teardown();
    return;
  }
  const size_t n = NLINES;
  const faf_batch *b = lines_b;

  group_begin("split into lines", NS_PER_OP);
  BENCH("faf_batch_split (+ region)", n,
        MADE(faf_batch_split(r, buf, buf_len, '\n')));
  BENCH("next_token loop", n, loop_split());

  group_begin("contains \"ab\"", NS_PER_OP);
  BENCH("faf_batch_contains", n, sink += faf_batch_contains(b, "ab", 2, mask));
  group_note("in %.0f%% of lines", 100.0 * faf_batch_contains(b, "ab", 2, mask) / n);
  BENCH("faf_string_contains loop", n, loop_contains("ab"));

  group_begin("contains \"zzz\" (no line)", NS_PER_OP);
  BENCH("faf_batch_contains", n, sink += faf_batch_contains(b, "zzz", 3, mask));
  BENCH("faf_string_contains loop", n, loop_contains("zzz"));

  group_begin("hash", NS_PER_OP);
  BENCH("faf_batch_hash", n,
        faf_batch_hash(b, 0, hashes);
        sink += (size_t)hashes[n - 1]);
  BENCH("faf_string_hash loop", n, loop_hash());

  group_begin("lower case copy", NS_PER_OP);
  BENCH("faf_batch_ascii_case (one pass, + region)", n,
        MADE(faf_batch_ascii_case(r, b, 0)));
  BENCH("faf_k_ascii_case loop", n, loop_lower());

  // one operation each, with nothing to compare against
  group_begin("count ',' per line", NS_PER_OP);
  BENCH("faf_batch_count", n,
        faf_batch_count(b, ",", 1, out64);
        sink += (size_t)out64[0]);
  group_begin("find \"ab\" per line", NS_PER_OP);
  BENCH("faf_batch_find", n,
        faf_batch_find(b, "ab", 2, out64);
        sink += (size_t)out64[0]);
  group_begin("select by mask (views only, + region)", NS_PER_OP);
  BENCH("faf_batch_select", n, MADE(faf_batch_select(r, b, mask)));
  group_begin("copy into a new buffer", NS_PER_OP);
  BENCH("faf_batch_compact (+ region)", n, MADE(faf_batch_compact(r, b)));
  BENCH("faf_batch_join with \\n", n,
        sink += (size_t)faf_batch_join(b, "\n", 1, dst));
  group_end();

  teardown();
}
