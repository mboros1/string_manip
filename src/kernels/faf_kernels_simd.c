#include "faf_kernels.h"
#include "faf_simd.h"

// SIMD kernels, written once against faf_simd.h and compiled for whichever of
// SSE2 / NEON is selected. With the ref backend, the kernels forward to the
// faf_ref_* versions.

#if defined(FAF_BACKEND_SSE2) || defined(FAF_BACKEND_NEON)

static inline const char *align_down(const char *p) {
  return (const char *)((uintptr_t)p & ~(uintptr_t)15);
}

// A 16 byte unaligned load from `p` stays within one page.
static inline bool load_in_page(const void *p) {
  return ((uintptr_t)p & 4095) <= 4096 - 16;
}

/* ---- Block scanners ----
 * Walk the aligned 16 byte blocks covering [s, s + n) and apply a match
 * strategy to each. Aligned loads never cross a page, so this is safe on any
 * memory. Only the first and last blocks need masking; the middle runs
 * unmasked, two blocks per iteration. `match` is a constant at every call
 * site, so it inlines. It returns 0xFF in each matching byte. */

// Small on purpose: kernels build one per call (the set's splatted chars live
// in the caller's array, not here, so there's nothing big to zero).
typedef struct {
  v128 c;
  const v128 *chars;
  int nchars;
  bool in;
} match_ctx;

typedef v128 (*match_fn)(v128 block, const match_ctx *ctx);

static inline size_t scan_first(const char *s, size_t n, match_fn match,
                                const match_ctx *ctx) {
  if (n == 0)
    return 0;
  // Short searches (tokens, fields) usually end in the first 16 bytes: one
  // unaligned load does it when it can't cross into another page.
  if (load_in_page(s)) {
    vmask m = v_mask(match(v_loadu(s), ctx));
    if (n < 16)
      m &= vmask_to(n);
    if (m)
      return vmask_first(m);
    if (n <= 16)
      return n;
  }
  const char *e = s + n;
  const char *blk = align_down(s);
  const char *last = align_down(e - 1);

  vmask m = v_mask(match(v_load(blk), ctx)) & vmask_from((size_t)(s - blk));
  if (blk == last)
    m &= vmask_to((size_t)(e - blk));
  if (m)
    return (size_t)(blk + vmask_first(m) - s);
  if (blk == last)
    return n;

  for (blk += 16; blk + 16 < last; blk += 32) {
    v128 h0 = match(v_load(blk), ctx);
    v128 h1 = match(v_load(blk + 16), ctx);
    if (v_mask(v_or(h0, h1))) {
      m = v_mask(h0);
      return m ? (size_t)(blk + vmask_first(m) - s)
               : (size_t)(blk + 16 + vmask_first(v_mask(h1)) - s);
    }
  }
  for (; blk < last; blk += 16) {
    m = v_mask(match(v_load(blk), ctx));
    if (m)
      return (size_t)(blk + vmask_first(m) - s);
  }
  m = v_mask(match(v_load(last), ctx)) & vmask_to((size_t)(e - last));
  return m ? (size_t)(last + vmask_first(m) - s) : n;
}

static inline size_t scan_last(const char *s, size_t n, match_fn match,
                               const match_ctx *ctx) {
  if (n == 0)
    return 0;
  const char *e = s + n;
  const char *first = align_down(s);
  const char *blk = align_down(e - 1);

  vmask m = v_mask(match(v_load(blk), ctx)) & vmask_to((size_t)(e - blk));
  if (blk == first)
    m &= vmask_from((size_t)(s - blk));
  if (m)
    return (size_t)(blk + vmask_last(m) - s);
  if (blk == first)
    return n;

  for (blk -= 16; blk > first; blk -= 16) {
    m = v_mask(match(v_load(blk), ctx));
    if (m)
      return (size_t)(blk + vmask_last(m) - s);
  }
  m = v_mask(match(v_load(first), ctx)) & vmask_from((size_t)(s - first));
  return m ? (size_t)(first + vmask_last(m) - s) : n;
}

static inline size_t scan_count(const char *s, size_t n, match_fn match,
                                const match_ctx *ctx) {
  if (n == 0)
    return 0;
  const char *e = s + n;
  const char *blk = align_down(s);
  const char *last = align_down(e - 1);

  vmask m = v_mask(match(v_load(blk), ctx)) & vmask_from((size_t)(s - blk));
  if (blk == last)
    return vmask_count(m & vmask_to((size_t)(e - blk)));
  size_t count = vmask_count(m);

  // middle: subtract the 0xFF (-1) hits to count per byte lane, and flush
  // the lanes before any can pass 255
  blk += 16;
  while (blk < last) {
    v128 acc = v_splat(0);
    for (int k = 0; k < 255 && blk < last; ++k, blk += 16)
      acc = v_sub(acc, match(v_load(blk), ctx));
    count += v_hsum(acc);
  }
  m = v_mask(match(v_load(last), ctx)) & vmask_to((size_t)(e - last));
  return count + vmask_count(m);
}

// Positions of the first `max` matches, in one pass.
static inline size_t scan_positions(const char *s, size_t n, match_fn match,
                                    const match_ctx *ctx, size_t *pos,
                                    size_t max) {
  size_t k = 0;
  if (n == 0 || max == 0)
    return 0;
  const char *e = s + n;
  for (const char *blk = align_down(s); blk < e; blk += 16) {
    vmask m = v_mask(match(v_load(blk), ctx));
    if (!m)
      continue;
    if (blk < s)
      m &= vmask_from((size_t)(s - blk));
    if (e - blk < 16)
      m &= vmask_to((size_t)(e - blk));
    for (m = vmask_one_per_byte(m); m; m &= m - 1) {
      pos[k++] = (size_t)(blk + vmask_first(m) - s);
      if (k == max)
        return k;
    }
  }
  return k;
}

/* ---- Match strategies ---- */

static inline v128 match_byte(v128 b, const match_ctx *ctx) {
  return v_eq(b, ctx->c);
}

static inline v128 match_set(v128 b, const match_ctx *ctx) {
  v128 hit = v_eq(b, ctx->chars[0]);
  for (int i = 1; i < ctx->nchars; ++i)
    hit = v_or(hit, v_eq(b, ctx->chars[i]));
  return ctx->in ? hit : v_not(hit);
}

static inline v128 match_nonascii(v128 b, const match_ctx *ctx) {
  (void)ctx;
  return v_not(v_le_u8(b, v_splat(0x7F)));
}

static inline bool set_ctx(match_ctx *ctx, v128 chars[16],
                           const faf_byteset *set, bool in) {
  if (set->nchars == 0 || set->nchars > 16)
    return false; // empty or large sets: the reference bitmap is simpler
  for (int i = 0; i < set->nchars; ++i)
    chars[i] = v_splat(set->chars[i]);
  ctx->chars = chars;
  ctx->nchars = set->nchars;
  ctx->in = in;
  return true;
}

/* ---- Kernels ---- */

FAF_NO_ASAN
size_t faf_k_strlen(const char *s) {
  const char *blk = align_down(s);
  v128 zero = v_splat(0);
  vmask m = v_mask(v_eq(v_load(blk), zero)) & vmask_from((size_t)(s - blk));
  while (!m) {
    blk += 16;
    m = v_mask(v_eq(v_load(blk), zero));
  }
  return (size_t)(blk + vmask_first(m) - s);
}

size_t faf_k_find_byte(const char *s, size_t n, char c) {
  match_ctx ctx = {.c = v_splat((uint8_t)c)};
  return scan_first(s, n, match_byte, &ctx);
}

size_t faf_k_rfind_byte(const char *s, size_t n, char c) {
  match_ctx ctx = {.c = v_splat((uint8_t)c)};
  return scan_last(s, n, match_byte, &ctx);
}

size_t faf_k_count_byte(const char *s, size_t n, char c) {
  match_ctx ctx = {.c = v_splat((uint8_t)c)};
  return scan_count(s, n, match_byte, &ctx);
}

size_t faf_k_find_bytes(const char *s, size_t n, char c, size_t *pos,
                        size_t max) {
  match_ctx ctx = {.c = v_splat((uint8_t)c)};
  return scan_positions(s, n, match_byte, &ctx, pos, max);
}

size_t faf_k_find_set(const char *s, size_t n, const faf_byteset *set,
                      bool in) {
  match_ctx ctx;
  v128 chars[16];
  if (!set_ctx(&ctx, chars, set, in))
    return faf_ref_find_set(s, n, set, in);
  return scan_first(s, n, match_set, &ctx);
}

size_t faf_k_rfind_set(const char *s, size_t n, const faf_byteset *set,
                       bool in) {
  match_ctx ctx;
  v128 chars[16];
  if (!set_ctx(&ctx, chars, set, in))
    return faf_ref_rfind_set(s, n, set, in);
  return scan_last(s, n, match_set, &ctx);
}

size_t faf_k_ascii_prefix(const char *s, size_t n) {
  return scan_first(s, n, match_nonascii, NULL);
}

/* Two stream kernels: the inputs have unrelated alignment, so these use
 * unaligned loads, finish with one overlapping block when n >= 16, and use a
 * single page-safe load (or the reference loop) when n < 16. */

typedef v128 (*transform_fn)(v128 v);

static inline v128 identity(v128 v) { return v; }

// ASCII 'A'-'Z' -> 'a'-'z'
static inline v128 fold_lower(v128 v) {
  v128 is_upper = v_le_u8(v_sub(v, v_splat('A')), v_splat(25));
  return v_add(v, v_and(is_upper, v_splat(0x20)));
}

FAF_NO_ASAN
static inline size_t mismatch_with(const char *a, const char *b, size_t n,
                                   transform_fn f) {
  size_t i = 0;
  for (; i + 16 <= n; i += 16) {
    vmask ne = ~v_mask(v_eq(f(v_loadu(a + i)), f(v_loadu(b + i)))) &
               FAF_VMASK_ALL;
    if (ne)
      return i + vmask_first(ne);
  }
  if (i == n)
    return n;
  if (n >= 16) {
    // bytes before i are already known equal
    size_t j = n - 16;
    vmask ne = ~v_mask(v_eq(f(v_loadu(a + j)), f(v_loadu(b + j)))) &
               FAF_VMASK_ALL;
    return ne ? j + vmask_first(ne) : n;
  }
  if (load_in_page(a) && load_in_page(b)) {
    vmask ne = ~v_mask(v_eq(f(v_loadu(a)), f(v_loadu(b)))) & vmask_to(n);
    return ne ? vmask_first(ne) : n;
  }
  return f == identity ? faf_ref_mismatch(a, b, n)
                       : faf_ref_mismatch_icase(a, b, n);
}

size_t faf_k_mismatch(const char *a, const char *b, size_t n) {
  return mismatch_with(a, b, n, identity);
}

size_t faf_k_mismatch_icase(const char *a, const char *b, size_t n) {
  return mismatch_with(a, b, n, fold_lower);
}

FAF_NO_BUILTIN
// ASCII case of the 8 bytes of x, a word at a time: a byte is a letter to
// convert if its high bit is clear and it is in [lo, hi]. Adding to the low 7
// bits can't carry into the next byte, and sets the high bit exactly when the
// byte is >= lo (resp. > hi).
// Forced inline: as a call it made the kernel save registers on every entry.
__attribute__((always_inline)) static inline uint64_t
case_word(uint64_t x, uint64_t lo_add, uint64_t hi_add) {
  const uint64_t high = 0x8080808080808080ull;
  uint64_t low7 = x & ~high;
  uint64_t in_range = ((low7 + lo_add) ^ (low7 + hi_add)) & ~x & high;
  return x ^ (in_range >> 2); // 0x80 >> 2 == 0x20, the case bit
}

// 2026-09-29: 64 bytes per iteration in four independent vectors, with the
// tail outside the loop (it used to be 16 at a time with the tail test in
// the loop: 10.7 GB/s on the M1), and strings under 16 bytes as overlapping
// words instead of a byte at a time.
void faf_k_ascii_case(char *dst, const char *src, size_t n, bool upper) {
  if (n < 16) {
    const uint64_t ones = 0x0101010101010101ull;
    uint64_t lo_add = ones * (0x80 - (upper ? 'a' : 'A'));
    uint64_t hi_add = ones * (0x7F - (upper ? 'z' : 'Z'));
    if (n >= 8) {
      // two overlapping words cover 8..15 bytes; both loaded before either
      // store, so dst may equal src
      uint64_t a, b;
      __builtin_memcpy(&a, src, 8);
      __builtin_memcpy(&b, src + n - 8, 8);
      a = case_word(a, lo_add, hi_add);
      b = case_word(b, lo_add, hi_add);
      __builtin_memcpy(dst, &a, 8);
      __builtin_memcpy(dst + n - 8, &b, 8);
    } else if (n >= 4) {
      uint32_t a, b;
      __builtin_memcpy(&a, src, 4);
      __builtin_memcpy(&b, src + n - 4, 4);
      a = (uint32_t)case_word(a, lo_add, hi_add);
      b = (uint32_t)case_word(b, lo_add, hi_add);
      __builtin_memcpy(dst, &a, 4);
      __builtin_memcpy(dst + n - 4, &b, 4);
    } else {
      unsigned char lo = upper ? 'a' : 'A';
      for (size_t i = 0; i < n; ++i) {
        unsigned char c = (unsigned char)src[i];
        dst[i] = (char)((unsigned char)(c - lo) < 26 ? c ^ 0x20 : c);
      }
    }
    return;
  }
  v128 lo = v_splat(upper ? 'a' : 'A');
  v128 delta = v_splat(upper ? (uint8_t)-0x20 : 0x20);
  v128 span = v_splat(25);
#define CASE(v) v_add((v), v_and(v_le_u8(v_sub((v), lo), span), delta))
  // the last 16 bytes, loaded first: if dst == src the loop would overwrite
  // them, and converting them again later is harmless either way
  v128 last = v_loadu(src + n - 16);
  if (n <= 32) { // two overlapping blocks, no loop
    v128 first = v_loadu(src);
    v_storeu(dst, CASE(first));
    v_storeu(dst + n - 16, CASE(last));
    return;
  }
  size_t i = 0;
  for (; i + 64 <= n; i += 64) {
    v128 a = v_loadu(src + i), b = v_loadu(src + i + 16);
    v128 c = v_loadu(src + i + 32), d = v_loadu(src + i + 48);
    v_storeu(dst + i, CASE(a));
    v_storeu(dst + i + 16, CASE(b));
    v_storeu(dst + i + 32, CASE(c));
    v_storeu(dst + i + 48, CASE(d));
  }
  for (; i + 16 <= n; i += 16) {
    v128 a = v_loadu(src + i);
    v_storeu(dst + i, CASE(a));
  }
  v_storeu(dst + n - 16, CASE(last)); // overlaps the loop's last block
#undef CASE
}

FAF_NO_BUILTIN
void faf_k_reverse(char *dst, const char *src, size_t n) {
  size_t i = 0;
  for (; i + 16 <= n; i += 16) {
    v_storeu(dst + i, v_reverse(v_loadu(src + n - i - 16)));
  }
  for (; i < n; ++i) {
    dst[i] = src[n - 1 - i];
  }
}

#elif defined(FAF_BACKEND_REF)

size_t faf_k_strlen(const char *s) { return faf_ref_strlen(s); }
size_t faf_k_find_byte(const char *s, size_t n, char c) {
  return faf_ref_find_byte(s, n, c);
}
size_t faf_k_rfind_byte(const char *s, size_t n, char c) {
  return faf_ref_rfind_byte(s, n, c);
}
size_t faf_k_count_byte(const char *s, size_t n, char c) {
  return faf_ref_count_byte(s, n, c);
}
size_t faf_k_find_bytes(const char *s, size_t n, char c, size_t *pos,
                        size_t max) {
  return faf_ref_find_bytes(s, n, c, pos, max);
}
size_t faf_k_find_set(const char *s, size_t n, const faf_byteset *set,
                      bool in) {
  return faf_ref_find_set(s, n, set, in);
}
size_t faf_k_rfind_set(const char *s, size_t n, const faf_byteset *set,
                       bool in) {
  return faf_ref_rfind_set(s, n, set, in);
}
size_t faf_k_mismatch(const char *a, const char *b, size_t n) {
  return faf_ref_mismatch(a, b, n);
}
size_t faf_k_mismatch_icase(const char *a, const char *b, size_t n) {
  return faf_ref_mismatch_icase(a, b, n);
}
void faf_k_ascii_case(char *dst, const char *src, size_t n, bool upper) {
  faf_ref_ascii_case(dst, src, n, upper);
}
size_t faf_k_ascii_prefix(const char *s, size_t n) {
  return faf_ref_ascii_prefix(s, n);
}
void faf_k_reverse(char *dst, const char *src, size_t n) {
  faf_ref_reverse(dst, src, n);
}

#endif
