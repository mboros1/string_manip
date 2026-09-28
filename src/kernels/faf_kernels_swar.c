#include "faf_kernels.h"
#include "faf_kernels_swar.h"

// SWAR kernels ("SIMD within a register"): a machine word used as 4 or 8 byte
// lanes, for CPUs without SSE2/NEON. The byte tests below never carry from
// one lane into the next, so their masks are exact.
//
// Every kernel works the same way: bytes one at a time until the address is
// word aligned, then aligned words, then the last bytes one at a time. Only
// faf_k_strlen reads past the range (to the end of the aligned word pair
// holding the NUL), and an aligned pair never crosses a page, so that is safe
// on any memory. Aligned loads also suit CPUs that fault on unaligned ones
// (ESP32).
//
// The word loops run a precomputed number of times, two words per trip where
// that saves a branch: on in-order cores (ESP32) taken branches dominate, and
// a counted loop lets GCC use hardware loops (Xtensa LOOP).

#if defined(FAF_BACKEND_SWAR)

// With the pie backend (ESP32-S3), these five are exported as faf_swar_*
// instead (for the benchmarks); the PIE kernels use their inline swar_*
// bodies for the unaligned ends and for short inputs.
#if defined(FAF_BACKEND_PIE)
#define SWAR_KERNEL(name) faf_swar_##name
#else
#define SWAR_KERNEL(name) faf_k_##name
#endif

typedef uintptr_t word;
// may_alias: words are read out of char data
typedef uintptr_t __attribute__((may_alias)) word_alias;

#define W sizeof(word)
#define ONES ((word)-1 / 0xFF) // 0x01 in every lane
#define HIGHS (ONES * 0x80)
#define LOWS7 (ONES * 0x7F)

static inline bool aligned(const void *p) {
  return ((uintptr_t)p & (W - 1)) == 0;
}

static inline bool pair_aligned(const void *p) {
  return ((uintptr_t)p & (2 * W - 1)) == 0;
}

// `p` must be word aligned
static inline word load(const char *p) {
  return *(const word_alias *)(const void *)p;
}

static inline word bcast(unsigned char c) { return ONES * c; }

// 0x80 in every lane of x that is zero
static inline word zero_lanes(word x) {
  return ~(((x & LOWS7) + LOWS7) | x | LOWS7);
}

// 0x80 in every lane of x that is not zero
static inline word nonzero_lanes(word x) {
  return (((x & LOWS7) + LOWS7) | x) & HIGHS;
}

// 0x80 in every lane of x in [lo, hi], for lo, hi < 0x80
static inline word range_lanes(word x, unsigned char lo, unsigned char hi) {
  word t = x & LOWS7;          // at most 0x7F per lane: the adds can't carry
  word ge = t + bcast(0x80 - lo);  // high bit set where t >= lo
  word gt = t + bcast(0x7F - hi);  // high bit set where t > hi
  return ge & ~gt & ~x & HIGHS;    // ~x: bytes >= 0x80 are never in range
}

// ASCII case conversion of every lane: flip bit 5 where the lane is a letter
// of the other case
static inline word to_case(word x, bool upper) {
  word letters = upper ? range_lanes(x, 'a', 'z') : range_lanes(x, 'A', 'Z');
  return x ^ (letters >> 2);
}

// Lane index of the first / last set lane of a mask (little-endian)
static inline size_t first_lane(word m) {
#if FAF_VECTOR_BYTES == 8
  return (size_t)__builtin_ctzll(m) / 8;
#else
  return (size_t)__builtin_ctz(m) / 8;
#endif
}

static inline size_t last_lane(word m) {
#if FAF_VECTOR_BYTES == 8
  return (size_t)(63 - __builtin_clzll(m)) / 8;
#else
  return (size_t)(31 - __builtin_clz(m)) / 8;
#endif
}

// Number of set lanes of a mask: each lane's bit becomes a 0/1 byte, and the
// multiply sums them into the top byte (at most 8, so no overflow)
static inline size_t count_lanes(word m) {
  return (size_t)(((m >> 7) * ONES) >> ((W - 1) * 8));
}

/* ---- Scanning ---- */

FAF_NO_ASAN FAF_NO_BUILTIN __attribute__((always_inline)) static inline size_t
swar_strlen(const char *s) {
  const char *p = s;
  for (; !pair_aligned(p); ++p) {
    if (*p == '\0')
      return (size_t)(p - s);
  }
  for (;; p += 2 * W) {
    word m0 = zero_lanes(load(p)), m1 = zero_lanes(load(p + W));
    if (m0 | m1)
      return (size_t)(p - s) + (m0 ? first_lane(m0) : W + first_lane(m1));
  }
}

FAF_NO_ASAN FAF_NO_BUILTIN
size_t SWAR_KERNEL(strlen)(const char *s) {
  return swar_strlen(s);
}

FAF_NO_BUILTIN __attribute__((always_inline)) static inline size_t
swar_find_byte(const char *s, size_t n, char c) {
  size_t i = 0;
  for (; i < n && !aligned(s + i); ++i) {
    if (s[i] == c)
      return i;
  }
  word pat = bcast((unsigned char)c);
  for (size_t pairs = (n - i) / (2 * W); pairs; --pairs, i += 2 * W) {
    word m0 = zero_lanes(load(s + i) ^ pat);
    word m1 = zero_lanes(load(s + i + W) ^ pat);
    if (m0 | m1)
      return i + (m0 ? first_lane(m0) : W + first_lane(m1));
  }
  if (i + W <= n) {
    word m = zero_lanes(load(s + i) ^ pat);
    if (m)
      return i + first_lane(m);
    i += W;
  }
  for (; i < n; ++i) {
    if (s[i] == c)
      return i;
  }
  return n;
}

FAF_NO_BUILTIN
size_t SWAR_KERNEL(find_byte)(const char *s, size_t n, char c) {
  return swar_find_byte(s, n, c);
}

FAF_NO_BUILTIN
size_t faf_k_rfind_byte(const char *s, size_t n, char c) {
  size_t i = n; // everything at or after i has been searched
  for (; i > 0 && !aligned(s + i); --i) {
    if (s[i - 1] == c)
      return i - 1;
  }
  word pat = bcast((unsigned char)c);
  for (size_t pairs = i / (2 * W); pairs; --pairs, i -= 2 * W) {
    word m1 = zero_lanes(load(s + i - W) ^ pat);
    word m0 = zero_lanes(load(s + i - 2 * W) ^ pat);
    if (m0 | m1)
      return m1 ? i - W + last_lane(m1) : i - 2 * W + last_lane(m0);
  }
  if (i >= W) {
    word m = zero_lanes(load(s + i - W) ^ pat);
    if (m)
      return i - W + last_lane(m);
    i -= W;
  }
  for (; i > 0; --i) {
    if (s[i - 1] == c)
      return i - 1;
  }
  return n;
}

FAF_NO_BUILTIN __attribute__((always_inline)) static inline size_t
swar_count_byte(const char *s, size_t n, char c) {
  size_t i = 0, count = 0;
  for (; i < n && !aligned(s + i); ++i)
    count += s[i] == c;
  word pat = bcast((unsigned char)c);
  for (size_t words = (n - i) / W; words; --words, i += W)
    count += count_lanes(zero_lanes(load(s + i) ^ pat));
  for (; i < n; ++i)
    count += s[i] == c;
  return count;
}

FAF_NO_BUILTIN
size_t SWAR_KERNEL(count_byte)(const char *s, size_t n, char c) {
  return swar_count_byte(s, n, c);
}

FAF_NO_BUILTIN
size_t faf_k_find_bytes(const char *s, size_t n, char c, size_t *pos,
                        size_t max) {
  size_t i = 0, k = 0;
  if (max == 0)
    return 0;
  for (; i < n && !aligned(s + i); ++i) {
    if (s[i] == c && (pos[k++] = i, k == max))
      return k;
  }
  word pat = bcast((unsigned char)c);
  for (size_t words = (n - i) / W; words; --words, i += W) {
    for (word m = zero_lanes(load(s + i) ^ pat); m; m &= m - 1) {
      pos[k++] = i + first_lane(m);
      if (k == max)
        return k;
    }
  }
  for (; i < n; ++i) {
    if (s[i] == c && (pos[k++] = i, k == max))
      return k;
  }
  return k;
}

/* ---- Sets ---- */

// Sets of up to this many bytes are tested a word at a time, one compare per
// member; larger ones go through the bitmap a byte at a time (ref).
#define SET_WORD_MAX 4

// 0x80 in every lane whose membership in the set of `np` bytes equals `in`.
// A lane is in the set unless it differs from every member.
__attribute__((always_inline)) static inline word
set_lanes(word x, const word *pats, int np, bool in) {
  word out = HIGHS;
  for (int j = 0; j < np; ++j) // np is a constant after inlining: unrolled
    out &= nonzero_lanes(x ^ pats[j]);
  return in ? ~out & HIGHS : out;
}

// The word loops of find_set/rfind_set for a set of exactly `np` bytes;
// always inlined with a constant np, so the member loop unrolls
__attribute__((always_inline)) static inline size_t
find_set_words(const char *s, size_t *i, size_t n, const word *pats, int np,
               bool in) {
  for (size_t words = (n - *i) / W; words; --words, *i += W) {
    word m = set_lanes(load(s + *i), pats, np, in);
    if (m)
      return *i + first_lane(m);
  }
  return n;
}

__attribute__((always_inline)) static inline size_t
rfind_set_words(const char *s, size_t *i, const word *pats, int np, bool in,
                size_t n) {
  for (size_t words = *i / W; words; --words, *i -= W) {
    word m = set_lanes(load(s + *i - W), pats, np, in);
    if (m)
      return *i - W + last_lane(m);
  }
  return n;
}

static int set_patterns(const faf_byteset *set, word pats[SET_WORD_MAX]) {
  if (set->nchars > SET_WORD_MAX) // includes 0xFF: more than 16
    return -1;
  for (int j = 0; j < set->nchars; ++j)
    pats[j] = bcast(set->chars[j]);
  return set->nchars;
}

FAF_NO_BUILTIN
size_t faf_k_find_set(const char *s, size_t n, const faf_byteset *set,
                      bool in) {
  word pats[SET_WORD_MAX];
  int npats = set_patterns(set, pats);
  if (npats < 0)
    return faf_ref_find_set(s, n, set, in);
  size_t i = 0;
  for (; i < n && !aligned(s + i); ++i) {
    if (faf_byteset_has(set, (unsigned char)s[i]) == in)
      return i;
  }
  size_t r;
  switch (npats) {
  case 0: r = find_set_words(s, &i, n, pats, 0, in); break;
  case 1: r = find_set_words(s, &i, n, pats, 1, in); break;
  case 2: r = find_set_words(s, &i, n, pats, 2, in); break;
  case 3: r = find_set_words(s, &i, n, pats, 3, in); break;
  default: r = find_set_words(s, &i, n, pats, 4, in); break;
  }
  if (r != n)
    return r;
  for (; i < n; ++i) {
    if (faf_byteset_has(set, (unsigned char)s[i]) == in)
      return i;
  }
  return n;
}

FAF_NO_BUILTIN
size_t faf_k_rfind_set(const char *s, size_t n, const faf_byteset *set,
                       bool in) {
  word pats[SET_WORD_MAX];
  int npats = set_patterns(set, pats);
  if (npats < 0)
    return faf_ref_rfind_set(s, n, set, in);
  size_t i = n;
  for (; i > 0 && !aligned(s + i); --i) {
    if (faf_byteset_has(set, (unsigned char)s[i - 1]) == in)
      return i - 1;
  }
  size_t r;
  switch (npats) {
  case 0: r = rfind_set_words(s, &i, pats, 0, in, n); break;
  case 1: r = rfind_set_words(s, &i, pats, 1, in, n); break;
  case 2: r = rfind_set_words(s, &i, pats, 2, in, n); break;
  case 3: r = rfind_set_words(s, &i, pats, 3, in, n); break;
  default: r = rfind_set_words(s, &i, pats, 4, in, n); break;
  }
  if (r != n)
    return r;
  for (; i > 0; --i) {
    if (faf_byteset_has(set, (unsigned char)s[i - 1]) == in)
      return i - 1;
  }
  return n;
}

/* ---- Comparing ---- */

static inline unsigned char fold(unsigned char c) {
  return (c >= 'A' && c <= 'Z') ? (unsigned char)(c + ('a' - 'A')) : c;
}

// Index of the first differing byte of a[0, n) and b[0, n), comparing
// case-folded when `icase`. a is walked in aligned words; b generally has a
// different alignment, so each of its words is shifted together from the two
// aligned words it straddles. Every load holds at least one byte in range.
// always_inline: each caller gets a copy with `icase` constant, so the loops
// carry no test for it (and can become hardware loops)
__attribute__((always_inline)) static inline size_t
mismatch(const char *a, const char *b, size_t n, bool icase) {
  size_t i = 0;
  for (; i < n && !aligned(a + i); ++i) {
    unsigned char x = (unsigned char)a[i], y = (unsigned char)b[i];
    if (icase ? fold(x) != fold(y) : x != y)
      return i;
  }
  size_t off = (uintptr_t)(b + i) & (W - 1);
  if (off == 0) {
    for (size_t words = (n - i) / W; words; --words, i += W) {
      word x = load(a + i), y = load(b + i);
      word d = icase ? to_case(x, false) ^ to_case(y, false) : x ^ y;
      if (d)
        return i + first_lane(nonzero_lanes(d));
    }
  } else if (i + W <= n) {
    const char *bw = b + i - off; // aligned word holding b[i]
    word lo = load(bw);
    for (size_t words = (n - i) / W; words; --words, i += W) {
      bw += W;
      word hi = load(bw);
      word y = (lo >> (8 * off)) | (hi << (8 * (W - off)));
      lo = hi;
      word x = load(a + i);
      word d = icase ? to_case(x, false) ^ to_case(y, false) : x ^ y;
      if (d)
        return i + first_lane(nonzero_lanes(d));
    }
  }
  for (; i < n; ++i) {
    unsigned char x = (unsigned char)a[i], y = (unsigned char)b[i];
    if (icase ? fold(x) != fold(y) : x != y)
      return i;
  }
  return n;
}

FAF_NO_BUILTIN __attribute__((always_inline)) static inline size_t
swar_mismatch(const char *a, const char *b, size_t n) {
  return mismatch(a, b, n, false);
}

FAF_NO_BUILTIN
size_t SWAR_KERNEL(mismatch)(const char *a, const char *b, size_t n) {
  return swar_mismatch(a, b, n);
}

FAF_NO_BUILTIN
size_t faf_k_mismatch_icase(const char *a, const char *b, size_t n) {
  return mismatch(a, b, n, true);
}

/* ---- Transforms ---- */

FAF_NO_BUILTIN
void faf_k_ascii_case(char *dst, const char *src, size_t n, bool upper) {
  size_t i = 0;
  unsigned char lo = upper ? 'a' : 'A', hi = upper ? 'z' : 'Z';
  for (; i < n && !aligned(src + i); ++i) {
    unsigned char c = (unsigned char)src[i];
    dst[i] = (char)(c >= lo && c <= hi ? c ^ 0x20 : c);
  }
  size_t words = (n - i) / W;
  if (aligned(dst + i)) {
    for (; words; --words, i += W)
      *(word_alias *)(void *)(dst + i) = to_case(load(src + i), upper);
  } else {
    for (; words; --words, i += W) {
      word x = to_case(load(src + i), upper);
      __builtin_memcpy(dst + i, &x, W); // unaligned store
    }
  }
  for (; i < n; ++i) {
    unsigned char c = (unsigned char)src[i];
    dst[i] = (char)(c >= lo && c <= hi ? c ^ 0x20 : c);
  }
}

FAF_NO_BUILTIN __attribute__((always_inline)) static inline size_t
swar_ascii_prefix(const char *s, size_t n) {
  size_t i = 0;
  for (; i < n && !aligned(s + i); ++i) {
    if ((unsigned char)s[i] >= 0x80)
      return i;
  }
  for (size_t words = (n - i) / W; words; --words, i += W) {
    word m = load(s + i) & HIGHS;
    if (m)
      return i + first_lane(m);
  }
  for (; i < n; ++i) {
    if ((unsigned char)s[i] >= 0x80)
      return i;
  }
  return n;
}

FAF_NO_BUILTIN
size_t SWAR_KERNEL(ascii_prefix)(const char *s, size_t n) {
  return swar_ascii_prefix(s, n);
}

// A word-wide byte swap needs a bswap instruction, which not every CPU has
// (some get a library call), so reverse stays a byte at a time.
void faf_k_reverse(char *dst, const char *src, size_t n) {
  faf_ref_reverse(dst, src, n);
}

// The pie backend (ESP32-S3) builds its kernels in this file, so they can
// inline the SWAR ones above for their ends: a call per search cost ~23
// cycles, which made short searches (next_token) 16% slower than plain SWAR.
#if defined(FAF_BACKEND_PIE)
#include "faf_kernels_pie.inc"
#endif

#endif // FAF_BACKEND_SWAR
