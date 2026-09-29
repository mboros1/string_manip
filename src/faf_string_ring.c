#include "faf_string_ring.h"

#include "faf_string_mem.h"

bool faf_ring_init(faf_ring *r, char *buf, size_t cap) {
  bool ok = buf != NULL && cap > 0;
#if SIZE_MAX > UINT32_MAX // offsets are 32 bits; always true on 32-bit targets
  ok = ok && cap <= UINT32_MAX;
#endif
  *r = (faf_ring){.buf = ok ? buf : NULL, .cap = ok ? cap : 0};
  return ok;
}

faf_ring_ref faf_ring_push(faf_ring *r, faf_string s) {
  if (faf_string_is_none(s))
    return FAF_RING_REF_NONE;
  size_t len = faf_string_len(s);
  if (len >= r->cap) // no room for the NUL (also every push to a failed init)
    return FAF_RING_REF_NONE;
  if (len + 1 > r->cap - r->off) { // doesn't fit before the end: wrap
    r->head += r->cap - r->off;
    r->off = 0;
  }
  faf_ring_ref ref = {.pos = r->head, .off = (uint32_t)r->off,
                      .len = (uint32_t)len};
  faf_memcpy(r->buf + r->off, s.start, len);
  r->buf[r->off + len] = '\0';
  r->off += len + 1;
  r->head += len + 1;
  return ref;
}

bool faf_ring_valid(const faf_ring *r, faf_ring_ref ref) {
  // Written by this ring (not in its future), and not lapped since
  return ref.pos <= r->head && r->head - ref.pos <= r->cap &&
         (size_t)ref.off + ref.len < r->cap;
}

faf_string faf_ring_get(const faf_ring *r, faf_ring_ref ref) {
  if (!faf_ring_valid(r, ref))
    return FAF_STRING_NONE;
  const char *p = r->buf + ref.off;
  return (faf_string){.start = p, .end = p + ref.len};
}
