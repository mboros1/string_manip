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
static int64_t *starts, *ends, *out64, *os, *oe, *offs;
static uint8_t *mask;
static uint64_t *hashes;
static char *dst;

// False if there isn't memory for the input and the outputs (small boards).
static bool setup(void) {
  buf_len = 0;
  for (int i = 0; i < NLINES; ++i)
    buf_len += line_lens[i] + 1;
  size_t n = NLINES;
  buf = malloc(buf_len);
  starts = malloc(n * sizeof *starts);
  ends = malloc(n * sizeof *ends);
  out64 = malloc(n * sizeof *out64);
  os = malloc(n * sizeof *os);
  oe = malloc(n * sizeof *oe);
  offs = malloc((n + 1) * sizeof *offs);
  mask = malloc(n);
  hashes = malloc(n * sizeof *hashes);
  dst = malloc(buf_len + 1);
  if (!buf || !starts || !ends || !out64 || !os || !oe || !offs || !mask ||
      !hashes || !dst)
    return false;
  size_t at = 0;
  for (int i = 0; i < NLINES; ++i) {
    memcpy(buf + at, lines[i], line_lens[i]);
    at += line_lens[i];
    buf[at++] = '\n';
  }
  buf_len = at - 1; // no trailing newline: exactly NLINES pieces
  faf_batch_split(buf, buf_len, '\n', starts, ends, n);
  return true;
}

static void teardown(void) {
  free(buf), free(starts), free(ends), free(out64), free(os), free(oe);
  free(offs), free(mask), free(hashes), free(dst);
}

static faf_string line_at(size_t i) {
  return faf_string_init_n(buf + starts[i], (size_t)(ends[i] - starts[i]));
}

/* ---- per string, in C ---- */

static void loop_split(void) {
  faf_string rest = faf_string_init_n(buf, buf_len), piece;
  size_t k = 0;
  while (faf_string_next_token(&rest, '\n', &piece)) {
    os[k] = piece.start - buf;
    oe[k++] = piece.end - buf;
  }
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

void bench_batch(void) {
  section("Batch calls", "%d CSV-like lines in one buffer; ns per line", NLINES);
  if (!setup()) {
    printf("  not enough memory for %zu bytes of input and outputs\n",
           2 * buf_len);
    teardown();
    return;
  }
  const size_t n = NLINES;

  group_begin("split into lines", NS_PER_OP);
  BENCH("faf_batch_split", n,
        sink += faf_batch_split(buf, buf_len, '\n', os, oe, n));
  BENCH("next_token loop", n, loop_split());

  group_begin("contains \"ab\"", NS_PER_OP);
  BENCH("faf_batch_contains", n,
        sink += faf_batch_contains(buf, starts, ends, n, "ab", 2, mask));
  group_note("in %.0f%% of lines",
             100.0 * faf_batch_contains(buf, starts, ends, n, "ab", 2, mask) / n);
  BENCH("faf_string_contains loop", n, loop_contains("ab"));

  group_begin("contains \"zzz\" (no line)", NS_PER_OP);
  BENCH("faf_batch_contains", n,
        sink += faf_batch_contains(buf, starts, ends, n, "zzz", 3, mask));
  BENCH("faf_string_contains loop", n, loop_contains("zzz"));

  group_begin("hash", NS_PER_OP);
  BENCH("faf_batch_hash", n,
        faf_batch_hash(buf, starts, ends, n, 0, hashes);
        sink += (size_t)hashes[n - 1]);
  BENCH("faf_string_hash loop", n, loop_hash());

  group_begin("lower case copy", NS_PER_OP);
  BENCH("faf_batch_ascii_case", n,
        faf_batch_ascii_case(buf, starts, ends, n, 0, dst, offs);
        sink += (size_t)offs[n]);
  BENCH("faf_k_ascii_case loop", n, loop_lower());

  // one operation each, with nothing to compare against
  group_begin("count ',' per line", NS_PER_OP);
  BENCH("faf_batch_count", n,
        faf_batch_count(buf, starts, ends, n, ",", 1, out64);
        sink += (size_t)out64[0]);
  group_begin("find \"ab\" per line", NS_PER_OP);
  BENCH("faf_batch_find", n,
        faf_batch_find(buf, starts, ends, n, "ab", 2, out64);
        sink += (size_t)out64[0]);
  group_begin("select by mask (views only)", NS_PER_OP);
  BENCH("faf_batch_select", n,
        sink += faf_batch_select(starts, ends, n, mask, os, oe));
  group_begin("copy into a new buffer", NS_PER_OP);
  BENCH("faf_batch_compact", n,
        faf_batch_compact(buf, starts, ends, n, dst, offs);
        sink += (size_t)offs[n]);
  BENCH("faf_batch_join with \\n", n,
        sink += (size_t)faf_batch_join(buf, starts, ends, n, "\n", 1, dst));
  group_end();

  teardown();
}
