#ifndef FAF_KERNELS_H
#define FAF_KERNELS_H

// Byte kernels: the only code in the library that is architecture specific.
// Everything else is written against these functions.
//
// Backends (chosen at compile time):
//   sse2  x86 / x86-64 (SSE2 is part of the x86-64 baseline)
//   neon  AArch64 / ARMv7 with NEON
//   ref   portable scalar C: any other target, or forced with FAF_BACKEND_REF
//
// The ref kernels are always compiled as faf_ref_* too, and serve as the
// oracle the SIMD backends are tested against.
//
// Conventions: lengths and indices are byte counts, and "not found" is
// returned as `n` (the length searched).
//
// SIMD kernels may read outside [s, s + n), but only within aligned 16 byte
// blocks that contain part of the range, or with loads that don't cross a
// page. Either way they never touch a page the range doesn't, so they are
// safe on any memory (the same technique libc's memchr/strlen use).

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if defined(FAF_BACKEND_REF)
#define FAF_BACKEND_NAME "ref"
#elif defined(__SSE2__) || defined(_M_X64)
#define FAF_BACKEND_SSE2 1
#define FAF_BACKEND_NAME "sse2"
#elif defined(__ARM_NEON) || defined(__aarch64__)
#define FAF_BACKEND_NEON 1
#define FAF_BACKEND_NAME "neon"
#else
#define FAF_BACKEND_REF 1
#define FAF_BACKEND_NAME "ref"
#endif

// Keep the compiler from recognizing loops as strlen/memchr/... and replacing
// them with libc calls: the library may only depend on memcpy, memset, memmove
// and memcmp (see faf_string_mem.h), so it links freestanding.
#if defined(__clang__)
#define FAF_NO_BUILTIN __attribute__((no_builtin))
#elif defined(__GNUC__)
#define FAF_NO_BUILTIN                                                         \
  __attribute__((optimize("no-tree-loop-distribute-patterns")))
#else
#define FAF_NO_BUILTIN
#endif

// A set of bytes, for find_set/rfind_set. Build once, search many times.
typedef struct {
  uint8_t bits[32];  // membership bitmap
  uint8_t chars[16]; // distinct members, when there are at most 16
  uint8_t nchars;    // number of entries in `chars`, or 0xFF if more than 16
} faf_byteset;

void faf_byteset_init(faf_byteset *set, const char *chars, size_t n);

static inline bool faf_byteset_has(const faf_byteset *set, unsigned char c) {
  return (set->bits[c >> 3] >> (c & 7)) & 1;
}

// Length of the NUL terminated string `s`.
size_t faf_k_strlen(const char *s);
// Index of the first / last `c` in s[0, n), or n.
size_t faf_k_find_byte(const char *s, size_t n, char c);
size_t faf_k_rfind_byte(const char *s, size_t n, char c);
// Number of `c` in s[0, n).
size_t faf_k_count_byte(const char *s, size_t n, char c);
// Indices of the first (up to) `max` occurrences of `c` in s[0, n), written
// to `pos` in order. Returns how many were written; fewer than `max` means
// that was all of them. One pass, however many matches there are.
size_t faf_k_find_bytes(const char *s, size_t n, char c, size_t *pos,
                        size_t max);
// Index of the first / last byte whose membership in `set` equals `in`, or n.
size_t faf_k_find_set(const char *s, size_t n, const faf_byteset *set, bool in);
size_t faf_k_rfind_set(const char *s, size_t n, const faf_byteset *set,
                       bool in);
// Index of the first differing byte of a[0, n) and b[0, n), or n.
size_t faf_k_mismatch(const char *a, const char *b, size_t n);
// Same, treating ASCII 'A'-'Z' and 'a'-'z' as equal.
size_t faf_k_mismatch_icase(const char *a, const char *b, size_t n);
// dst[i] = ASCII lower (or upper) case of src[i]. dst may equal src.
void faf_k_ascii_case(char *dst, const char *src, size_t n, bool upper);
// Index of the first byte >= 0x80, or n.
size_t faf_k_ascii_prefix(const char *s, size_t n);
// dst[i] = src[n - 1 - i]. dst and src must not overlap.
void faf_k_reverse(char *dst, const char *src, size_t n);

// Reference implementations (always available).
size_t faf_ref_strlen(const char *s);
size_t faf_ref_find_byte(const char *s, size_t n, char c);
size_t faf_ref_rfind_byte(const char *s, size_t n, char c);
size_t faf_ref_count_byte(const char *s, size_t n, char c);
size_t faf_ref_find_bytes(const char *s, size_t n, char c, size_t *pos,
                          size_t max);
size_t faf_ref_find_set(const char *s, size_t n, const faf_byteset *set,
                        bool in);
size_t faf_ref_rfind_set(const char *s, size_t n, const faf_byteset *set,
                         bool in);
size_t faf_ref_mismatch(const char *a, const char *b, size_t n);
size_t faf_ref_mismatch_icase(const char *a, const char *b, size_t n);
void faf_ref_ascii_case(char *dst, const char *src, size_t n, bool upper);
size_t faf_ref_ascii_prefix(const char *s, size_t n);
void faf_ref_reverse(char *dst, const char *src, size_t n);

#endif // FAF_KERNELS_H
