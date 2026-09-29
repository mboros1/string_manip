#ifndef FAF_BATCH_H
#define FAF_BATCH_H

#include <stddef.h>
#include <stdint.h>

// Batches: one call works on many strings, for use from other languages.
//
// A batch is views into one byte buffer `data`: string i is
// data[starts[i], ends[i]). Views may overlap, leave gaps (the separators of
// a split) or come in any order. An Arrow binary/string array is a batch
// as it is: starts = offsets, ends = offsets + 1.
//
// Made for foreign function interfaces: every function here is out of line,
// takes only pointers and integers, and nothing depends on build options, so
// a binding is a list of declarations (see examples/python). Nothing
// allocates: outputs go to arrays the caller provides, sized as documented
// (from n, or from a counting function).
//
// The caller guarantees 0 <= starts[i] <= ends[i] <= length of data; nothing
// here checks it. Positions and lengths are int64_t, as in Arrow.

// ---- Making batches ----

// Number of pieces faf_batch_split makes: count(sep) + 1. Like
// faf_string_split, and Python's bytes.split(sep): "a\n\nb" is "a", "", "b";
// "" is one empty piece; "a\n" ends with an empty piece.
size_t faf_batch_split_count(const char *data, size_t len, char sep);

// Split data[0, len) on `sep` into starts/ends (views, separators excluded).
// Writes at most `cap` pieces; returns the total number of pieces.
size_t faf_batch_split(const char *data, size_t len, char sep,
                       int64_t *starts, int64_t *ends, size_t cap);

// Views of the strings where mask[i] != 0, in order. out_starts/out_ends need
// room for n entries (only the first ones, as many as are selected, are
// meaningful). Returns how many are selected.
size_t faf_batch_select(const int64_t *starts, const int64_t *ends, size_t n,
                        const uint8_t *mask, int64_t *out_starts,
                        int64_t *out_ends);

// Views of strings idx[0..m) (each < n, repeats allowed).
void faf_batch_take(const int64_t *starts, const int64_t *ends,
                    const int64_t *idx, size_t m, int64_t *out_starts,
                    int64_t *out_ends);

// ---- Per string results: out[i] for each of the n strings ----

void faf_batch_lengths(const int64_t *starts, const int64_t *ends, size_t n,
                       int64_t *out);

// Sum of the lengths: the bytes faf_batch_compact / faf_batch_ascii_case
// write.
int64_t faf_batch_total(const int64_t *starts, const int64_t *ends, size_t n);

// Index of the first `needle` in each string, or -1.
void faf_batch_find(const char *data, const int64_t *starts,
                    const int64_t *ends, size_t n, const char *needle,
                    size_t needle_len, int64_t *out);

// Non-overlapping occurrences of `needle` in each string (an empty needle
// matches len + 1 times).
void faf_batch_count(const char *data, const int64_t *starts,
                     const int64_t *ends, size_t n, const char *needle,
                     size_t needle_len, int64_t *out);

// 1 / 0 per string. Each returns how many are 1.
size_t faf_batch_contains(const char *data, const int64_t *starts,
                          const int64_t *ends, size_t n, const char *needle,
                          size_t needle_len, uint8_t *out);
size_t faf_batch_starts_with(const char *data, const int64_t *starts,
                             const int64_t *ends, size_t n, const char *prefix,
                             size_t prefix_len, uint8_t *out);
size_t faf_batch_ends_with(const char *data, const int64_t *starts,
                           const int64_t *ends, size_t n, const char *suffix,
                           size_t suffix_len, uint8_t *out);
size_t faf_batch_eq(const char *data, const int64_t *starts,
                    const int64_t *ends, size_t n, const char *other,
                    size_t other_len, uint8_t *out);
// Equality with ASCII letters compared case-insensitively.
size_t faf_batch_eq_icase(const char *data, const int64_t *starts,
                          const int64_t *ends, size_t n, const char *other,
                          size_t other_len, uint8_t *out);

// faf_string_hash_seed of each string.
void faf_batch_hash(const char *data, const int64_t *starts,
                    const int64_t *ends, size_t n, uint64_t seed,
                    uint64_t *out);

// ---- New bytes ----
// These write the strings one after another into `dst` (faf_batch_total
// bytes), and dst_offsets[0..n] (n + 1 entries) so that the result is an
// Arrow-style batch: string i is dst[dst_offsets[i], dst_offsets[i + 1]).

// A contiguous copy.
void faf_batch_compact(const char *data, const int64_t *starts,
                       const int64_t *ends, size_t n, char *dst,
                       int64_t *dst_offsets);

// ASCII lower case (upper != 0: upper case) copy; other bytes unchanged.
void faf_batch_ascii_case(const char *data, const int64_t *starts,
                          const int64_t *ends, size_t n, int upper, char *dst,
                          int64_t *dst_offsets);

// ---- Same layout: one pass over the range the views cover ----
// For batches whose views cover most of their range (the lines of a file):
// one kernel call instead of one per string, and the views can be reused.

// Length of the range the views cover, [min start, max end), and its start in
// *lo. 0 (and *lo = 0) for n == 0.
int64_t faf_batch_span(const int64_t *starts, const int64_t *ends, size_t n,
                       int64_t *lo);

// ASCII case copy of the whole span into `dst` (faf_batch_span bytes; the
// bytes between views are copied too, converted), and the views shifted to
// it: out_starts[i] = starts[i] - lo, same for ends. out_starts / out_ends
// may be NULL when lo is 0: the input views then describe dst as they are.
void faf_batch_ascii_case_span(const char *data, const int64_t *starts,
                               const int64_t *ends, size_t n, int upper,
                               char *dst, int64_t *out_starts,
                               int64_t *out_ends);

// ASCII case copy of data[lo, lo + len) into dst: for a caller that knows
// the span without scanning the views (a split, an Arrow array: views in
// order, starting at starts[0] and ending at ends[n - 1]).
void faf_batch_ascii_case_range(const char *data, int64_t lo, int64_t len,
                                int upper, char *dst);

// Convert the strings in place: only bytes inside views change. Overlapping
// views are fine (converting twice is the same as once).
void faf_batch_ascii_case_inplace(char *data, const int64_t *starts,
                                  const int64_t *ends, size_t n, int upper);

// The strings joined by `sep` (e.g. "\n" to write lines out), into `dst`:
// faf_batch_total + (n - 1) * sep_len bytes (0 for n == 0). Returns the
// bytes written.
int64_t faf_batch_join(const char *data, const int64_t *starts,
                       const int64_t *ends, size_t n, const char *sep,
                       size_t sep_len, char *dst);

// ---- Memory for results, from a faf arena ----
// For bindings that want results in faf memory that is reused, not freshly
// allocated per call: an arena over a buffer the binding provides, regions
// taken per result and given back when the result is dropped. The arena and
// region structs depend on build options, so they don't cross here: the
// arena lives in memory the caller sizes with faf_ffi_arena_size(), and a
// region is an opaque nonzero integer. Only with arenas (FAF_ARENAS, the
// default). One thread at a time per arena.

#include "faf_string_mem.h"
#if FAF_ARENAS

// Bytes the caller provides for the arena itself (its bookkeeping lives in
// `buf`, see faf_arena_init).
size_t faf_ffi_arena_size(void);

// Bytes of `buf` an arena of `npools` pools of at least `pool_bytes` each
// needs (0 if that overflows).
size_t faf_ffi_arena_bytes(size_t npools, size_t pool_bytes);

// Lay an arena of `npools` pools over buf[0, nbytes), in `arena`
// (faf_ffi_arena_size() bytes, aligned for a pointer). False on failure.
bool faf_ffi_arena_init(void *arena, void *buf, size_t nbytes, size_t npools);

// A free region of the arena, or 0 when every pool is in use.
uint64_t faf_ffi_region_acquire(void *arena);

// Bytes one region can hold (every region of an arena holds the same).
size_t faf_ffi_region_capacity(void *arena);

// `bytes` from the region, or NULL if it doesn't have room (or the handle is
// stale). Slot aligned; the region's memory is not cleared.
void *faf_ffi_reserve(void *arena, uint64_t region, size_t bytes);

// Give the region back: everything reserved from it is free again. A stale
// or zero handle is ignored.
void faf_ffi_region_release(void *arena, uint64_t region);

#endif // FAF_ARENAS

#endif // FAF_BATCH_H
