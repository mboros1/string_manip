#ifndef FAF_CTX_H
#define FAF_CTX_H

#include "../mem/faf_string_mem.h"

#include <stdbool.h>
#include <stdint.h>

// Tuning: thresholds where a function picks between strategies. They change
// how fast a call is, never its result. Each build has defaults measured for
// its backend (faf_backend.h); a binding or program can override them.
//
// Opaque, so keys can be added without changing any layout a binding sees.
typedef struct faf_tuning faf_tuning;

// What a call that makes something needs besides its inputs: where results
// go, and optionally how to tune the work. Set it with named fields; a field
// left zero (or NULL) means the default.
//
//   faf_ctx ctx = {.out = r};
//   faf_batch *lines = faf_batch_split(&ctx, data, len, '\n');
typedef struct {
  faf_region out;           // results are allocated here
  const faf_tuning *tuning; // NULL: this build's defaults
} faf_ctx;

typedef enum {
  // faf_batch_ascii_case on views that aren't one dense run: convert their
  // whole range in one pass (instead of packing each string) when the range
  // is at most this percent of the strings' total length. 0..1000000.
  FAF_TUNE_CASE_ONE_PASS_PERCENT = 1,
  // faf_batch_split: after the first separators (found one at a time), find
  // the rest in batches of 64 per scan when they are on average at most this
  // many bytes apart, else keep finding them one at a time. 0..1000000.
  FAF_TUNE_SPLIT_BATCH_GAP = 2,
} faf_tune_key;

// A tuning in `r`, holding this build's defaults. NULL if `r` is out of space.
faf_tuning *faf_tuning_new(faf_region r);

// Set `key`. False, and `t` unchanged, for an unknown key or a value out of
// its range.
bool faf_tuning_set(faf_tuning *t, int key, int64_t value);

// `key`'s value in `t` (NULL: this build's default). False for an unknown key.
bool faf_tuning_get(const faf_tuning *t, int key, int64_t *value);

#endif // FAF_CTX_H
