#ifndef FAF_STRING_BUILD_H
#define FAF_STRING_BUILD_H

#include "../core/faf_string.h"
#include "../core/faf_string_arr.h"
#include "../mem/faf_string_mem.h"

#include <stdarg.h>
#include <stdint.h>

// Building new strings in a region. Every result is NUL terminated and owned
// by the region; every function returns FAF_STRING_NONE if it runs out of
// space.

// ---- Builder ----
//
// Appends into one growing span. While nothing else is allocated from the
// region in between, growing just extends the span in place (no copying);
// otherwise it moves to a new, larger span. After a failed append the builder
// stays failed and finish returns FAF_STRING_NONE.
//
//   faf_builder b = faf_builder_init(r);
//   faf_builder_append(&b, name);
//   faf_builder_append_char(&b, '=');
//   faf_builder_append_i64(&b, value);
//   faf_string s = faf_builder_finish(&b);
typedef struct {
  faf_region r;
  faf_span sp;
  size_t len;
  bool failed;
} faf_builder;

faf_builder faf_builder_init(faf_region r);
bool faf_builder_append_i64(faf_builder *b, int64_t v);
bool faf_builder_append_u64(faf_builder *b, uint64_t v);
// NUL terminates and returns the result; unused space is given back when
// possible. The builder must not be used afterwards.
faf_string faf_builder_finish(faf_builder *b);

// Appending is inline while it fits in the space already reserved; only
// growing calls into the library. The _grow functions handle every case
// out of line: code with many append sites (like faf_string_format) can call
// them to stay small.
bool faf_builder_append_grow(faf_builder *b, faf_string str);
bool faf_builder_append_char_grow(faf_builder *b, char c);

// Room for `add` more bytes and the NUL without growing?
static inline bool faf_builder_fits(const faf_builder *b, size_t add) {
  return !b->failed && b->len + add < b->sp.slots * FAF_SLOT_BYTES;
}

static inline bool faf_builder_append(faf_builder *b, faf_string str) {
  size_t n = faf_string_len(str);
  if (!faf_builder_fits(b, n))
    return faf_builder_append_grow(b, str);
  faf_memcpy((char *)b->sp.ptr + b->len, str.start, n);
  b->len += n;
  return true;
}

static inline bool faf_builder_append_char(faf_builder *b, char c) {
  if (!faf_builder_fits(b, 1))
    return faf_builder_append_char_grow(b, c);
  ((char *)b->sp.ptr)[b->len++] = c;
  return true;
}

// ---- One-shot builders ----

// `items` joined with `sep` between them.
faf_string faf_string_join(faf_region r, faf_string_arr items, faf_string sep);
// `str` repeated `count` times.
faf_string faf_string_repeat(faf_region r, faf_string str, size_t count);
// `str` padded with `c` on the left / right to at least `width` bytes.
faf_string faf_string_pad_left(faf_region r, faf_string str, size_t width,
                               char c);
faf_string faf_string_pad_right(faf_region r, faf_string str, size_t width,
                                char c);
// Every non-overlapping `from` replaced with `to`. An empty `from` replaces
// nothing (the result is a copy).
faf_string faf_string_replace(faf_region r, faf_string str, faf_string from,
                              faf_string to);
// The bytes of `str` in reverse order.
faf_string faf_string_reverse(faf_region r, faf_string str);

// printf-style formatting into `r`. Supported: %% %c %s (C string)
// %S (faf_string, passed by value) and %d %i %u %x with the h, hh, l, ll and
// z length modifiers. No width, precision or floating point. An unknown
// conversion makes the result FAF_STRING_NONE.
faf_string faf_string_format(faf_region r, const char *fmt, ...);
faf_string faf_string_vformat(faf_region r, const char *fmt, va_list ap);

#endif // FAF_STRING_BUILD_H
