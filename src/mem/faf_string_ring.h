#ifndef FAF_STRING_RING_H
#define FAF_STRING_RING_H

#include "../core/faf_string.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// A ring of strings over a buffer you provide: pushes never fail for lack of
// room, the oldest strings are overwritten instead, and a handle says whether
// its string is still there. For data that is safe to lose: recent log lines,
// a cache of rendered text, telemetry. Regions (faf_string_mem.h) are for
// data with a known lifetime; a ring bounds memory for data without one.
//
// Strings are stored contiguously and NUL terminated. One that doesn't fit
// before the end of the buffer starts again at the beginning, and the unused
// tail counts as written (the strings in it are lost a little early).
//
// Validity is one compare: `head` counts every byte written since init and
// never wraps (64 bits), each handle holds the count at its string's start,
// and a string is intact while head - pos <= capacity.
//
//   static char buf[4096];
//   faf_ring log;
//   faf_ring_init(&log, buf, sizeof buf);
//   faf_ring_ref r = faf_ring_push(&log, line);
//   ...
//   faf_string s = faf_ring_get(&log, r);   // FAF_STRING_NONE if overwritten
//
// A string returned by faf_ring_get points into the buffer: it stays intact
// until later pushes reach it. Use it before pushing more, or copy it out, or
// check faf_ring_valid again after using it. One owner at a time; no locking.

typedef struct {
  char *buf;
  size_t cap;    // bytes in buf
  size_t off;    // where the next string goes
  uint64_t head; // bytes written since init, including skipped tails
} faf_ring;

// A pushed string: where it went (`off`), its length, and `pos`, the ring's
// head when it was written.
typedef struct {
  uint64_t pos;
  uint32_t off;
  uint32_t len;
} faf_ring_ref;

#define FAF_RING_REF_NONE ((faf_ring_ref){.pos = UINT64_MAX, .off = 0, .len = 0})

static inline bool faf_ring_ref_is_none(faf_ring_ref ref) {
  return ref.pos == UINT64_MAX;
}

// Use buf[0, cap) as an empty ring. Returns false (and leaves *r empty, so
// every push fails) if buf is NULL, cap is 0 or cap doesn't fit in 32 bits.
bool faf_ring_init(faf_ring *r, char *buf, size_t cap);

// Copy `s` in, overwriting the oldest strings as needed. Returns
// FAF_RING_REF_NONE, and changes nothing, if `s` is none or `s` plus its NUL
// is longer than the whole buffer.
faf_ring_ref faf_ring_push(faf_ring *r, faf_string s);

// Is the string behind `ref` still intact?
bool faf_ring_valid(const faf_ring *r, faf_ring_ref ref);

// The string behind `ref`, or FAF_STRING_NONE if it has been overwritten.
faf_string faf_ring_get(const faf_ring *r, faf_ring_ref ref);

#endif // FAF_STRING_RING_H
