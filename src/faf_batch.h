#ifndef FAF_BATCH_H
#define FAF_BATCH_H

#include "faf_string_mem.h"

#include <stddef.h>
#include <stdint.h>

// Batches: many strings per call, for bindings from other languages (and for
// C code that works on columns of strings).
//
// A batch is views into one byte buffer: string i is data[starts[i], ends[i]).
// Batches are allocated in a region like anything else, and go away when it
// is released. A batch points into its input (the bytes, the arrays given to
// from_offsets / from_views, another batch's views), which must outlive it,
// as for any faf_string. Functions that make a batch return NULL when the
// region is out of space; a NULL batch reads as empty everywhere.
//
// Made for foreign function interfaces: only pointers, integers and the
// region handle; a pointer may be NULL where its length is 0. Positions and
// lengths are int64_t, as in Arrow.

typedef struct faf_batch faf_batch;

// ---- Making batches ----

// Split data[0, len) on `sep`, like Python's bytes.split: "a\n\nb" is "a",
// "", "b"; "" is one empty string; "a\n" ends with an empty string.
faf_batch *faf_batch_split(faf_region r, const char *data, size_t len, char sep);

// Arrow's layout: string i is data[offsets[i], offsets[i + 1]), offsets has
// n + 1 entries. Neither is copied.
faf_batch *faf_batch_from_offsets(faf_region r, const char *data,
                                  const int64_t *offsets, size_t n);

// Views the caller already has, in any order. Nothing is copied.
faf_batch *faf_batch_from_views(faf_region r, const char *data,
                                const int64_t *starts, const int64_t *ends,
                                size_t n);

// ---- Reading a batch ----

size_t faf_batch_len(const faf_batch *b);
const char *faf_batch_data(const faf_batch *b);
const int64_t *faf_batch_starts(const faf_batch *b);
const int64_t *faf_batch_ends(const faf_batch *b);

// Sum of the lengths.
int64_t faf_batch_total(const faf_batch *b);

// ---- Per string results: out[i] for each of the n strings ----

void faf_batch_lengths(const faf_batch *b, int64_t *out);

// Index of the first `needle` in each string, or -1.
void faf_batch_find(const faf_batch *b, const char *needle, size_t needle_len,
                    int64_t *out);

// Non-overlapping occurrences of `needle` (an empty needle matches len + 1
// times).
void faf_batch_count(const faf_batch *b, const char *needle, size_t needle_len,
                     int64_t *out);

// 1 / 0 per string; each returns how many are 1. eq_icase compares ASCII
// letters case-insensitively.
size_t faf_batch_contains(const faf_batch *b, const char *needle,
                          size_t needle_len, uint8_t *out);
size_t faf_batch_starts_with(const faf_batch *b, const char *prefix,
                             size_t prefix_len, uint8_t *out);
size_t faf_batch_ends_with(const faf_batch *b, const char *suffix,
                           size_t suffix_len, uint8_t *out);
size_t faf_batch_eq(const faf_batch *b, const char *other, size_t other_len,
                    uint8_t *out);
size_t faf_batch_eq_icase(const faf_batch *b, const char *other,
                          size_t other_len, uint8_t *out);

// faf_string_hash_seed of each string.
void faf_batch_hash(const faf_batch *b, uint64_t seed, uint64_t *out);

// ---- New batches, in `r` ----

// Views of the strings where mask[i] != 0 (n entries), in order. The bytes
// are not copied.
faf_batch *faf_batch_select(faf_region r, const faf_batch *b,
                            const uint8_t *mask);

// Views of strings idx[0..m) (each < n, repeats allowed).
faf_batch *faf_batch_take(faf_region r, const faf_batch *b, const int64_t *idx,
                          size_t m);

// ASCII lower case (upper != 0: upper case) copies of the strings. Other bytes
// are unchanged. Views in order over most of their range (a split, Arrow
// offsets) are converted in one pass; when they also start at 0 the result
// uses b's views instead of copying them.
faf_batch *faf_batch_ascii_case(faf_region r, const faf_batch *b, int upper);

// The strings end to end (Arrow layout).
faf_batch *faf_batch_compact(faf_region r, const faf_batch *b);

// ---- Writing ----

// Convert the strings where they are: only bytes inside views change. The
// batch's data must be writable.
void faf_batch_ascii_case_inplace(const faf_batch *b, int upper);

// The strings joined by `sep` into `dst`, which holds faf_batch_total(b) +
// (n - 1) * sep_len bytes (0 for n == 0). Returns the bytes written.
int64_t faf_batch_join(const faf_batch *b, const char *sep, size_t sep_len,
                       char *dst);

#endif // FAF_BATCH_H
