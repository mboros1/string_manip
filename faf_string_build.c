#include "faf_string_build.h"
#include "faf_kernels.h"
#include "faf_string_search.h"

/* ---- Builder ---- */

faf_builder faf_builder_init(faf_region r) {
  return (faf_builder){.r = r, .sp = FAF_SPAN_NONE, .len = 0, .failed = false};
}

// Make room for `add` more bytes plus a NUL.
static bool builder_room(faf_builder *b, size_t add) {
  if (b->failed)
    return false;
  size_t need = faf_slots_for(b->len + add);
  if (need <= b->sp.slots)
    return true;
  // grow geometrically, in place or not, so appends stay amortized O(1);
  // finish gives the unused tail back
  size_t grow = 2 * b->sp.slots > need ? 2 * b->sp.slots : need;
  if (!b->sp.ptr) {
    b->sp = faf_reserve(b->r, need);
  } else if (!faf_reserve_extend(b->r, &b->sp, grow - b->sp.slots) &&
             !faf_reserve_extend(b->r, &b->sp, need - b->sp.slots)) {
    // can't grow in place, not even by just enough near the region's end:
    // something else was allocated after us, so move
    faf_span moved = faf_reserve(b->r, grow);
    if (!moved.ptr)
      moved = faf_reserve(b->r, need);
    if (moved.ptr)
      faf_memcpy(moved.ptr, b->sp.ptr, b->len);
    b->sp = moved;
  }
  if (!b->sp.ptr)
    b->failed = true;
  return !b->failed;
}

static inline char *builder_end(faf_builder *b) {
  return (char *)b->sp.ptr + b->len;
}

bool faf_builder_append(faf_builder *b, faf_string str) {
  size_t n = faf_string_len(str);
  if (!builder_room(b, n))
    return false;
  faf_memcpy(builder_end(b), str.start, n);
  b->len += n;
  return true;
}

bool faf_builder_append_char(faf_builder *b, char c) {
  if (!builder_room(b, 1))
    return false;
  *builder_end(b) = c;
  b->len += 1;
  return true;
}

// Digits of `v` in `base`, written backwards ending at `end`. Returns start.
static char *u64_digits(char *end, uint64_t v, unsigned base) {
  do {
    unsigned d = (unsigned)(v % base);
    *--end = (char)(d < 10 ? '0' + d : 'a' + d - 10);
    v /= base;
  } while (v);
  return end;
}

static bool append_u64_base(faf_builder *b, uint64_t v, unsigned base,
                            bool negative) {
  char buf[24];
  char *start = u64_digits(buf + sizeof(buf), v, base);
  if (negative)
    *--start = '-';
  return faf_builder_append(
      b, (faf_string){.start = start, .end = buf + sizeof(buf)});
}

bool faf_builder_append_u64(faf_builder *b, uint64_t v) {
  return append_u64_base(b, v, 10, false);
}

bool faf_builder_append_i64(faf_builder *b, int64_t v) {
  // negate in unsigned so INT64_MIN works
  uint64_t mag = v < 0 ? 0 - (uint64_t)v : (uint64_t)v;
  return append_u64_base(b, mag, 10, v < 0);
}

faf_string faf_builder_finish(faf_builder *b) {
  if (!builder_room(b, 0))
    return FAF_STRING_NONE;
  char *dst = (char *)b->sp.ptr;
  size_t slots = faf_slots_for(b->len);
  faf_reserve_shrink(b->r, &b->sp, slots);
  faf_memset(dst + b->len, 0, slots * FAF_SLOT_BYTES - b->len);
  return (faf_string){.start = dst, .end = dst + b->len};
}

/* ---- One-shot builders ---- */

// Reserve a result of exactly `len` bytes, NUL terminated and zero padded.
static char *reserve_result(faf_region r, size_t len) {
  faf_span sp = faf_reserve(r, faf_slots_for(len));
  if (!sp.ptr)
    return NULL;
  char *dst = (char *)sp.ptr;
  faf_memset(dst + len, 0, sp.slots * FAF_SLOT_BYTES - len);
  return dst;
}

faf_string faf_string_join(faf_region r, faf_string_arr items, faf_string sep) {
  size_t count = (size_t)(items.end - items.start);
  size_t lsep = faf_string_len(sep);
  size_t len = count ? lsep * (count - 1) : 0;
  faf_foreach(it, items) len += faf_string_len(*it);

  char *dst = reserve_result(r, len);
  if (!dst)
    return FAF_STRING_NONE;
  char *p = dst;
  faf_foreach(it, items) {
    if (it != items.start) {
      faf_memcpy(p, sep.start, lsep);
      p += lsep;
    }
    faf_memcpy(p, it->start, faf_string_len(*it));
    p += faf_string_len(*it);
  }
  return (faf_string){.start = dst, .end = dst + len};
}

faf_string faf_string_repeat(faf_region r, faf_string str, size_t count) {
  size_t n = faf_string_len(str);
  if (n && count > SIZE_MAX / n)
    return FAF_STRING_NONE;
  char *dst = reserve_result(r, n * count);
  if (!dst)
    return FAF_STRING_NONE;
  if (n && count) {
    // copy once, then keep doubling what's already there
    faf_memcpy(dst, str.start, n);
    size_t done = n, total = n * count;
    while (done < total) {
      size_t chunk = done < total - done ? done : total - done;
      faf_memcpy(dst + done, dst, chunk);
      done += chunk;
    }
  }
  return (faf_string){.start = dst, .end = dst + n * count};
}

static faf_string pad(faf_region r, faf_string str, size_t width, char c,
                      bool left) {
  size_t n = faf_string_len(str);
  size_t fill = width > n ? width - n : 0;
  char *dst = reserve_result(r, n + fill);
  if (!dst)
    return FAF_STRING_NONE;
  faf_memset(left ? dst : dst + n, c, fill);
  faf_memcpy(left ? dst + fill : dst, str.start, n);
  return (faf_string){.start = dst, .end = dst + n + fill};
}

faf_string faf_string_pad_left(faf_region r, faf_string str, size_t width,
                               char c) {
  return pad(r, str, width, c, true);
}

faf_string faf_string_pad_right(faf_region r, faf_string str, size_t width,
                                char c) {
  return pad(r, str, width, c, false);
}

faf_string faf_string_replace(faf_region r, faf_string str, faf_string from,
                              faf_string to) {
  size_t lf = faf_string_len(from), lt = faf_string_len(to);
  if (lf == 0)
    return faf_string_copy(r, str);
  // exact size first, so the result is a single reservation
  size_t hits = faf_string_count(str, from);
  size_t len = faf_string_len(str) - hits * lf + hits * lt;
  char *dst = reserve_result(r, len);
  if (!dst)
    return FAF_STRING_NONE;

  char *p = dst;
  faf_string rest = str;
  for (size_t at; (at = faf_string_find(rest, from)) != FAF_NPOS;) {
    faf_memcpy(p, rest.start, at);
    faf_memcpy(p + at, to.start, lt);
    p += at + lt;
    rest.start += at + lf;
  }
  faf_memcpy(p, rest.start, faf_string_len(rest));
  return (faf_string){.start = dst, .end = dst + len};
}

faf_string faf_string_reverse(faf_region r, faf_string str) {
  size_t n = faf_string_len(str);
  char *dst = reserve_result(r, n);
  if (!dst)
    return FAF_STRING_NONE;
  faf_k_reverse(dst, str.start, n);
  return (faf_string){.start = dst, .end = dst + n};
}

/* ---- Format ---- */

faf_string faf_string_vformat(faf_region r, const char *fmt, va_list ap) {
  faf_builder b = faf_builder_init(r);
  const char *p = fmt;
  const char *end = fmt + faf_k_strlen(fmt);
  while (p < end) {
    // literal run up to the next '%'
    size_t run = (size_t)(end - p);
    size_t pct = faf_k_find_byte(p, run, '%');
    faf_builder_append(&b, faf_string_init_n(p, pct));
    p += pct;
    if (p == end)
      break;

    ++p; // '%'
    int longs = 0, shorts = 0;
    bool size = false;
    for (;; ++p) {
      if (*p == 'l')
        ++longs;
      else if (*p == 'h')
        ++shorts;
      else if (*p == 'z')
        size = true;
      else
        break;
    }
    char conv = *p ? *p++ : '\0';
    switch (conv) {
    case '%':
      faf_builder_append_char(&b, '%');
      break;
    case 'c':
      faf_builder_append_char(&b, (char)va_arg(ap, int));
      break;
    case 's':
      faf_builder_append(&b, faf_string_init(va_arg(ap, const char *)));
      break;
    case 'S':
      faf_builder_append(&b, va_arg(ap, faf_string));
      break;
    case 'd':
    case 'i': {
      int64_t v = size        ? (int64_t)va_arg(ap, ptrdiff_t)
                  : longs >= 2 ? (int64_t)va_arg(ap, long long)
                  : longs == 1 ? (int64_t)va_arg(ap, long)
                              : (int64_t)va_arg(ap, int);
      if (!size && !longs && shorts == 1)
        v = (short)v;
      else if (!size && !longs && shorts >= 2)
        v = (signed char)v;
      faf_builder_append_i64(&b, v);
      break;
    }
    case 'u':
    case 'x': {
      uint64_t v = size        ? (uint64_t)va_arg(ap, size_t)
                   : longs >= 2 ? (uint64_t)va_arg(ap, unsigned long long)
                   : longs == 1 ? (uint64_t)va_arg(ap, unsigned long)
                               : (uint64_t)va_arg(ap, unsigned int);
      if (!size && !longs && shorts == 1)
        v = (unsigned short)v;
      else if (!size && !longs && shorts >= 2)
        v = (unsigned char)v;
      append_u64_base(&b, v, conv == 'x' ? 16 : 10, false);
      break;
    }
    default:
      b.failed = true; // unknown conversion
      break;
    }
  }
  return faf_builder_finish(&b);
}

faf_string faf_string_format(faf_region r, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  faf_string out = faf_string_vformat(r, fmt, ap);
  va_end(ap);
  return out;
}
