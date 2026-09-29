#ifndef FAF_BATCH_H
#define FAF_BATCH_H

#include "faf_string_mem.h"

#include <stddef.h>
#include <stdint.h>

// Batches: many strings per call, for bindings from other languages (and for
// C code that works on columns of strings).
//
// A batch is views into one byte buffer: string i is data[starts[i], ends[i]).
// It lives in a region of an arena, and its handle is that region's handle:
// the views, and the bytes of results that make new strings, go away together
// with faf_batch_free. A stale handle (freed, or 0 from a call that failed) is
// rejected by every function: nothing is written, and 0 is returned.
//
// Made for foreign function interfaces: handles are integers, everything else
// is a pointer or an integer, nothing depends on build options, and nothing is
// allocated except from the arena passed in (NULL: the default arena). Input
// bytes (and the offsets of faf_batch_from_offsets) are not copied: the
// caller keeps them alive and unchanged while the batch is in use.
//
// Positions and lengths are int64_t, as in Arrow. One thread at a time per
// arena, as for regions.

typedef uint64_t faf_batch;

// ---- Making batches ----

// Split data[0, len) on `sep`, like Python's bytes.split: "a\n\nb" is "a",
// "", "b"; "" is one empty string; "a\n" ends with an empty string. 0 if the
// views don't fit in a region.
faf_batch faf_batch_split(faf_arena *arena, const char *data, size_t len,
                          char sep);

// A batch over Arrow's layout: string i is data[offsets[i], offsets[i + 1]),
// offsets has n + 1 entries. Neither is copied.
faf_batch faf_batch_from_offsets(faf_arena *arena, const char *data,
                                 const int64_t *offsets, size_t n);

// A batch over views the caller already has: string i is
// data[starts[i], ends[i]), in any order, overlapping or not. Nothing is
// copied.
faf_batch faf_batch_from_views(faf_arena *arena, const char *data,
                               const int64_t *starts, const int64_t *ends,
                               size_t n);

// Give the batch's region back: its views and any bytes it made are gone.
void faf_batch_free(faf_batch b);

// ---- Reading a batch ----

size_t faf_batch_len(faf_batch b);
const char *faf_batch_data(faf_batch b);
const int64_t *faf_batch_starts(faf_batch b);
const int64_t *faf_batch_ends(faf_batch b);

// Sum of the lengths.
int64_t faf_batch_total(faf_batch b);

// ---- Per string results: out[i] for each of the n strings ----

void faf_batch_lengths(faf_batch b, int64_t *out);

// Index of the first `needle` in each string, or -1.
void faf_batch_find(faf_batch b, const char *needle, size_t needle_len,
                    int64_t *out);

// Non-overlapping occurrences of `needle` (an empty needle matches len + 1
// times).
void faf_batch_count(faf_batch b, const char *needle, size_t needle_len,
                     int64_t *out);

// 1 / 0 per string; each returns how many are 1. eq_icase compares ASCII
// letters case-insensitively.
size_t faf_batch_contains(faf_batch b, const char *needle, size_t needle_len,
                          uint8_t *out);
size_t faf_batch_starts_with(faf_batch b, const char *prefix, size_t prefix_len,
                             uint8_t *out);
size_t faf_batch_ends_with(faf_batch b, const char *suffix, size_t suffix_len,
                           uint8_t *out);
size_t faf_batch_eq(faf_batch b, const char *other, size_t other_len,
                    uint8_t *out);
size_t faf_batch_eq_icase(faf_batch b, const char *other, size_t other_len,
                          uint8_t *out);

// faf_string_hash_seed of each string.
void faf_batch_hash(faf_batch b, uint64_t seed, uint64_t *out);

// ---- New batches ----
// Each takes a new region from `arena`, and returns 0 if none is free or the
// result doesn't fit in one.

// Views of the strings where mask[i] != 0 (n entries), in order. No bytes
// are copied: the result views the same data.
faf_batch faf_batch_select(faf_arena *arena, faf_batch b, const uint8_t *mask);

// Views of strings idx[0..m) (each < n, repeats allowed).
faf_batch faf_batch_take(faf_arena *arena, faf_batch b, const int64_t *idx,
                         size_t m);

// ASCII lower case (upper != 0: upper case) copies of the strings, in the new
// region. Other bytes are unchanged. Views in order over most of their range
// (a split, Arrow offsets) are converted in one pass over that range;
// scattered ones string by string.
faf_batch faf_batch_ascii_case(faf_arena *arena, faf_batch b, int upper);

// The strings end to end in the new region (Arrow layout).
faf_batch faf_batch_compact(faf_arena *arena, faf_batch b);

// ---- Writing ----

// Convert the strings where they are: only bytes inside views change. The
// batch's data must be writable (the caller's own buffer).
void faf_batch_ascii_case_inplace(faf_batch b, int upper);

// The strings joined by `sep` into `dst`, which holds faf_batch_total(b) +
// (n - 1) * sep_len bytes (0 for n == 0). Returns the bytes written.
int64_t faf_batch_join(faf_batch b, const char *sep, size_t sep_len, char *dst);

#endif // FAF_BATCH_H
