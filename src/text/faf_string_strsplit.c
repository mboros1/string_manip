#include "faf_string_strsplit.h"
#include "../kernels/faf_kernels.h"
#include "../mem/faf_string_mem.h"

// 2024-07-22:
// I think my general strategy will be to just find the start/end of
// each string split and return an array of the pointers, no copying.
//
// 2026-09-27:
// Find the separators (one pass for up to 64), reserve the whole array as one
// span, then fill it.

#define SPLIT_BATCH 64

// If `writable` is non-NULL it aliases `str.start`, and each separator in it
// is overwritten with '\0'.
static faf_string_arr split_impl(faf_region r, faf_string str, char tok,
                                 char *writable) {
  const char *s = str.start;
  size_t len = faf_string_len(str);

  // One pass finds up to SPLIT_BATCH separators; only longer inputs need a
  // separate count before the array can be reserved.
  size_t pos[SPLIT_BATCH];
  size_t got = faf_k_find_bytes(s, len, tok, pos, SPLIT_BATCH);
  size_t count =
      (got < SPLIT_BATCH ? got : faf_k_count_byte(s, len, tok)) + 1;

  size_t slots =
      (count * sizeof(faf_string) + FAF_SLOT_BYTES - 1) / FAF_SLOT_BYTES;
  faf_span sp = faf_reserve(r, slots);
  if (!sp.ptr)
    return (faf_string_arr){.start = NULL, .end = NULL};

  faf_string *tail = (faf_string *)sp.ptr;
  size_t from = 0; // start of the current token
  for (;;) {
    for (size_t k = 0; k < got; ++k) {
      *tail++ = (faf_string){.start = s + from, .end = s + pos[k]};
      if (writable)
        writable[pos[k]] = '\0';
      from = pos[k] + 1;
    }
    if (got < SPLIT_BATCH)
      break;
    got = faf_k_find_bytes(s + from, len - from, tok, pos, SPLIT_BATCH);
    for (size_t k = 0; k < got; ++k)
      pos[k] += from;
  }
  *tail = (faf_string){.start = s + from, .end = str.end};

  return (faf_string_arr){.start = (faf_string *)sp.ptr, .end = tail + 1};
}

faf_string_arr faf_string_split(faf_region r, faf_string str, char tok) {
  return split_impl(r, str, tok, NULL);
}

faf_string_arr faf_string_split_owned(faf_region r, faf_string str, char tok) {
  faf_string own = faf_string_copy(r, str);
  if (faf_string_is_none(own))
    return (faf_string_arr){.start = NULL, .end = NULL};
  // `own` is region memory we just reserved, so writing into it is fine
  return split_impl(r, own, tok, (char *)own.start);
}
