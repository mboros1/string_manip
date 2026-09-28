#ifndef FAF_SIMD_H
#define FAF_SIMD_H

// Private: the thin per-architecture layer under faf_kernels_simd.c. Only the
// handful of 16 byte operations the kernels need, nothing general purpose.
//
// Comparisons return a vector with each byte 0xFF (true) or 0x00 (false).
// v_mask() turns one into an integer with FAF_VBITS bits per byte, byte 0 in
// the lowest bits: SSE2 has movemask (1 bit per byte); NEON has no movemask,
// so it narrows with a shift (4 bits per byte), which is the cheapest
// equivalent there.

#include "faf_kernels.h"

#if defined(__clang__) || defined(__GNUC__)
// Kernels deliberately read past a string's ends (never past its pages, see
// faf_kernels.h). The loads are here, so this is what ASan must skip.
#define FAF_NO_ASAN __attribute__((no_sanitize_address))
#else
#define FAF_NO_ASAN
#endif

#if defined(FAF_BACKEND_SSE2)

#include <emmintrin.h>

typedef __m128i v128;
typedef uint32_t vmask;
#define FAF_VSHIFT 0 // log2(bits per byte in a vmask)
#define FAF_VMASK_ALL ((vmask)0xFFFF)

FAF_NO_ASAN static inline v128 v_load(const void *p) { return _mm_load_si128((const v128 *)p); }
FAF_NO_ASAN static inline v128 v_loadu(const void *p) { return _mm_loadu_si128((const v128 *)p); }
static inline void v_storeu(void *p, v128 v) { _mm_storeu_si128((v128 *)p, v); }
static inline v128 v_splat(uint8_t c) { return _mm_set1_epi8((char)c); }
static inline v128 v_eq(v128 a, v128 b) { return _mm_cmpeq_epi8(a, b); }
static inline v128 v_or(v128 a, v128 b) { return _mm_or_si128(a, b); }
static inline v128 v_and(v128 a, v128 b) { return _mm_and_si128(a, b); }
static inline v128 v_add(v128 a, v128 b) { return _mm_add_epi8(a, b); }
static inline v128 v_sub(v128 a, v128 b) { return _mm_sub_epi8(a, b); }
// unsigned a <= b
static inline v128 v_le_u8(v128 a, v128 b) {
  return _mm_cmpeq_epi8(_mm_min_epu8(a, b), a);
}
static inline v128 v_not(v128 a) { return _mm_xor_si128(a, _mm_set1_epi8(-1)); }
static inline vmask v_mask(v128 m) { return (vmask)_mm_movemask_epi8(m); }
// sum of the 16 bytes, unsigned
static inline size_t v_hsum(v128 v) {
  v128 s = _mm_sad_epu8(v, _mm_setzero_si128());
  return (size_t)_mm_cvtsi128_si32(s) +
         (size_t)_mm_cvtsi128_si32(_mm_srli_si128(s, 8));
}
static inline v128 v_reverse(v128 v) {
  // swap bytes within 16 bit lanes, then reverse the 8 lanes
  v = _mm_or_si128(_mm_slli_epi16(v, 8), _mm_srli_epi16(v, 8));
  v = _mm_shufflelo_epi16(v, 0x1B);
  v = _mm_shufflehi_epi16(v, 0x1B);
  return _mm_shuffle_epi32(v, 0x4E);
}

#elif defined(FAF_BACKEND_NEON)

#include <arm_neon.h>

typedef uint8x16_t v128;
typedef uint64_t vmask;
#define FAF_VSHIFT 2
#define FAF_VMASK_ALL (~(vmask)0)

FAF_NO_ASAN static inline v128 v_load(const void *p) { return vld1q_u8((const uint8_t *)p); }
FAF_NO_ASAN static inline v128 v_loadu(const void *p) { return vld1q_u8((const uint8_t *)p); }
static inline void v_storeu(void *p, v128 v) { vst1q_u8((uint8_t *)p, v); }
static inline v128 v_splat(uint8_t c) { return vdupq_n_u8(c); }
static inline v128 v_eq(v128 a, v128 b) { return vceqq_u8(a, b); }
static inline v128 v_or(v128 a, v128 b) { return vorrq_u8(a, b); }
static inline v128 v_and(v128 a, v128 b) { return vandq_u8(a, b); }
static inline v128 v_add(v128 a, v128 b) { return vaddq_u8(a, b); }
static inline v128 v_sub(v128 a, v128 b) { return vsubq_u8(a, b); }
static inline v128 v_le_u8(v128 a, v128 b) { return vcleq_u8(a, b); }
static inline v128 v_not(v128 a) { return vmvnq_u8(a); }
static inline size_t v_hsum(v128 v) {
#if defined(__aarch64__)
  return vaddlvq_u8(v);
#else
  uint64x2_t s = vpaddlq_u32(vpaddlq_u16(vpaddlq_u8(v)));
  return (size_t)(vgetq_lane_u64(s, 0) + vgetq_lane_u64(s, 1));
#endif
}
static inline vmask v_mask(v128 m) {
  // shift each 16 bit lane right by 4 and narrow: 4 bits per input byte
  uint8x8_t narrowed = vshrn_n_u16(vreinterpretq_u16_u8(m), 4);
  return vget_lane_u64(vreinterpret_u64_u8(narrowed), 0);
}
static inline v128 v_reverse(v128 v) {
  v = vrev64q_u8(v);
  return vextq_u8(v, v, 8);
}

#endif

#if defined(FAF_BACKEND_SSE2) || defined(FAF_BACKEND_NEON)

// index of the first / last set byte in a nonzero mask
static inline size_t vmask_first(vmask m) {
  return (size_t)__builtin_ctzll((unsigned long long)m) >> FAF_VSHIFT;
}
static inline size_t vmask_last(vmask m) {
  return (size_t)(63 - __builtin_clzll((unsigned long long)m)) >> FAF_VSHIFT;
}
static inline size_t vmask_count(vmask m) {
  return (size_t)__builtin_popcountll((unsigned long long)m) >> FAF_VSHIFT;
}
// keep one bit per byte, so `m &= m - 1` steps from one byte to the next
static inline vmask vmask_one_per_byte(vmask m) {
  return FAF_VSHIFT ? (m & (vmask)0x1111111111111111ull) : m;
}
// mask with bytes [from, 16) set, 0 <= from < 16
static inline vmask vmask_from(size_t from) {
  return (FAF_VMASK_ALL << (from << FAF_VSHIFT)) & FAF_VMASK_ALL;
}
// mask with bytes [0, to) set, 0 < to <= 16
static inline vmask vmask_to(size_t to) {
  return FAF_VMASK_ALL >> ((16 - to) << FAF_VSHIFT);
}
#endif

#endif // FAF_SIMD_H
