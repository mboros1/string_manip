#include "faf_kernels.h"
#include "faf_kernels_swar.h"

// PIE kernels for the ESP32-S3 (pie backend): the scanning kernels with its
// 128-bit vector instructions. Everything else is the SWAR backend.
//
// PIE has no C intrinsics, so the vector work is inline assembly. Each asm
// block is self-contained (it loads its own pattern and moves its results to
// address registers), except count_byte's accumulator, see there. GCC never
// uses the q registers; FreeRTOS saves them on task switches like the FPU.
//
// Shape: the SWAR kernels handle the part before the first 32-byte aligned
// address and the part after the last whole chunk; the asm scans aligned
// 32-byte chunks (two blocks), which never cross a page. A chunk with a match
// is searched again with SWAR to find where. PIE has no movemask, so a
// chunk's compare result comes out as four 32-bit words.

#if defined(FAF_BACKEND_PIE)

#define CHUNK 32
// Shorter inputs go straight to SWAR: the setup doesn't pay off
#define PIE_MIN 64

static inline size_t to_chunk(const char *p) {
  return (size_t)(-(uintptr_t)p & (CHUNK - 1));
}

// Nonzero if any byte of the aligned chunk at p equals *pat
static inline uint32_t chunk_has(const char *p, const unsigned char *pat) {
  uint32_t w0, w1, w2, w3;
  __asm__("ee.vldbc.8     q7, %[pat]\n\t"
          "ee.vld.128.ip  q0, %[p], 16\n\t"
          "ee.vld.128.ip  q1, %[p], 16\n\t"
          "ee.vcmp.eq.s8  q0, q0, q7\n\t"
          "ee.vcmp.eq.s8  q1, q1, q7\n\t"
          "ee.orq         q0, q0, q1\n\t"
          "ee.movi.32.a   q0, %[w0], 0\n\t"
          "ee.movi.32.a   q0, %[w1], 1\n\t"
          "ee.movi.32.a   q0, %[w2], 2\n\t"
          "ee.movi.32.a   q0, %[w3], 3"
          : [p] "+r"(p), [w0] "=r"(w0), [w1] "=r"(w1), [w2] "=r"(w2),
            [w3] "=r"(w3)
          : [pat] "r"(pat)
          : "memory");
  return w0 | w1 | w2 | w3;
}

// Nonzero if any byte of the aligned chunk at p is >= 0x80
static inline uint32_t chunk_has_high(const char *p) {
  uint32_t w0, w1, w2, w3;
  __asm__("ee.zero.q      q7\n\t"
          "ee.vld.128.ip  q0, %[p], 16\n\t"
          "ee.vld.128.ip  q1, %[p], 16\n\t"
          "ee.vcmp.lt.s8  q0, q0, q7\n\t" // signed: bytes >= 0x80 are < 0
          "ee.vcmp.lt.s8  q1, q1, q7\n\t"
          "ee.orq         q0, q0, q1\n\t"
          "ee.movi.32.a   q0, %[w0], 0\n\t"
          "ee.movi.32.a   q0, %[w1], 1\n\t"
          "ee.movi.32.a   q0, %[w2], 2\n\t"
          "ee.movi.32.a   q0, %[w3], 3"
          : [p] "+r"(p), [w0] "=r"(w0), [w1] "=r"(w1), [w2] "=r"(w2),
            [w3] "=r"(w3)
          :
          : "memory");
  return w0 | w1 | w2 | w3;
}

FAF_NO_BUILTIN
size_t faf_k_find_byte(const char *s, size_t n, char c) {
  if (n < PIE_MIN)
    return faf_swar_find_byte(s, n, c);
  size_t head = to_chunk(s);
  size_t r = faf_swar_find_byte(s, head, c);
  if (r != head)
    return r;
  const unsigned char pat = (unsigned char)c;
  const char *p = s + head;
  for (size_t chunks = (n - head) / CHUNK; chunks; --chunks, p += CHUNK) {
    if (chunk_has(p, &pat))
      return (size_t)(p - s) + faf_swar_find_byte(p, CHUNK, c);
  }
  size_t i = (size_t)(p - s);
  r = faf_swar_find_byte(p, n - i, c);
  return r == n - i ? n : i + r;
}

// Reads to the end of the aligned chunk holding the NUL, which never crosses
// a page
FAF_NO_ASAN FAF_NO_BUILTIN
size_t faf_k_strlen(const char *s) {
  size_t head = to_chunk(s);
  size_t r = faf_swar_find_byte(s, head, '\0');
  if (r != head)
    return r;
  const unsigned char zero = 0;
  for (const char *p = s + head;; p += CHUNK) {
    if (chunk_has(p, &zero))
      return (size_t)(p - s) + faf_swar_strlen(p);
  }
}

// Sum of the bytes of w, each at most 126
static inline size_t byte_sum(uint32_t w) {
  uint32_t t = (w & 0x00FF00FFu) + ((w >> 8) & 0x00FF00FFu);
  return (t + (t >> 16)) & 0xFFFFu;
}

// Counts accumulate per lane in q6, up to 2 per chunk, so q6 is summed and
// cleared every 63 chunks (before a lane passes 127). q6 is the one register
// kept across asm statements, between the zeroing, the chunk loop and the sum;
// nothing in between touches the q registers.
FAF_NO_BUILTIN
size_t faf_k_count_byte(const char *s, size_t n, char c) {
  if (n < PIE_MIN)
    return faf_swar_count_byte(s, n, c);
  size_t head = to_chunk(s);
  size_t count = faf_swar_count_byte(s, head, c);
  const unsigned char pat = (unsigned char)c;
  const char *p = s + head;
  size_t chunks = (n - head) / CHUNK;
  while (chunks) {
    size_t run = chunks < 63 ? chunks : 63;
    chunks -= run;
    __asm__ volatile("ee.zero.q q6" ::: "memory");
    for (; run; --run) {
      __asm__ volatile("ee.vldbc.8     q7, %[pat]\n\t"
                       "ee.vld.128.ip  q0, %[p], 16\n\t"
                       "ee.vld.128.ip  q1, %[p], 16\n\t"
                       "ee.vcmp.eq.s8  q0, q0, q7\n\t" // 0 or -1 per lane
                       "ee.vcmp.eq.s8  q1, q1, q7\n\t"
                       "ee.vadds.s8    q0, q0, q1\n\t" // 0, -1 or -2
                       "ee.vsubs.s8    q6, q6, q0"     // count up
                       : [p] "+r"(p)
                       : [pat] "r"(&pat)
                       : "memory");
    }
    uint32_t w0, w1, w2, w3;
    __asm__ volatile("ee.movi.32.a q6, %[w0], 0\n\t"
                     "ee.movi.32.a q6, %[w1], 1\n\t"
                     "ee.movi.32.a q6, %[w2], 2\n\t"
                     "ee.movi.32.a q6, %[w3], 3"
                     : [w0] "=r"(w0), [w1] "=r"(w1), [w2] "=r"(w2), [w3] "=r"(w3)
                     :
                     : "memory");
    count += byte_sum(w0) + byte_sum(w1) + byte_sum(w2) + byte_sum(w3);
  }
  size_t i = (size_t)(p - s);
  return count + faf_swar_count_byte(p, n - i, c);
}

// All four words of a 16-byte equality mask, ANDed: 0xFFFFFFFF iff equal
static inline uint32_t equal_aligned(const char *a, const char *b) {
  uint32_t w0, w1, w2, w3;
  __asm__("ee.vld.128.ip  q0, %[a], 16\n\t"
          "ee.vld.128.ip  q1, %[b], 16\n\t"
          "ee.vcmp.eq.s8  q0, q0, q1\n\t"
          "ee.movi.32.a   q0, %[w0], 0\n\t"
          "ee.movi.32.a   q0, %[w1], 1\n\t"
          "ee.movi.32.a   q0, %[w2], 2\n\t"
          "ee.movi.32.a   q0, %[w3], 3"
          : [a] "+r"(a), [b] "+r"(b), [w0] "=r"(w0), [w1] "=r"(w1),
            [w2] "=r"(w2), [w3] "=r"(w3)
          :
          : "memory");
  return w0 & w1 & w2 & w3;
}

// Same, with b not 16-byte aligned: its 16 bytes are shifted together from
// the two aligned blocks they straddle (LD.128.USAR sets the byte shift from
// b's address, SRC.Q applies it). Both blocks hold bytes of [b, b + 16).
static inline uint32_t equal_unaligned(const char *a, const char *b) {
  uint32_t w0, w1, w2, w3;
  __asm__("ee.ld.128.usar.ip q1, %[b], 16\n\t" // block holding b; b += 16
          "ee.vld.128.ip     q2, %[b], 0\n\t"  // the next block
          "ee.src.q          q1, q1, q2\n\t"
          "ee.vld.128.ip     q0, %[a], 16\n\t"
          "ee.vcmp.eq.s8     q0, q0, q1\n\t"
          "ee.movi.32.a      q0, %[w0], 0\n\t"
          "ee.movi.32.a      q0, %[w1], 1\n\t"
          "ee.movi.32.a      q0, %[w2], 2\n\t"
          "ee.movi.32.a      q0, %[w3], 3"
          : [a] "+r"(a), [b] "+r"(b), [w0] "=r"(w0), [w1] "=r"(w1),
            [w2] "=r"(w2), [w3] "=r"(w3)
          :
          : "memory");
  return w0 & w1 & w2 & w3;
}

FAF_NO_BUILTIN
size_t faf_k_mismatch(const char *a, const char *b, size_t n) {
  if (n < PIE_MIN)
    return faf_swar_mismatch(a, b, n);
  size_t head = (size_t)(-(uintptr_t)a & 15); // align a to a block
  size_t r = faf_swar_mismatch(a, b, head);
  if (r != head)
    return r;
  size_t i = head;
  bool b_aligned = (((uintptr_t)(b + i)) & 15) == 0;
  for (size_t blocks = (n - i) / 16; blocks; --blocks, i += 16) {
    uint32_t eq = b_aligned ? equal_aligned(a + i, b + i)
                            : equal_unaligned(a + i, b + i);
    if (eq != 0xFFFFFFFFu)
      return i + faf_swar_mismatch(a + i, b + i, 16);
  }
  r = faf_swar_mismatch(a + i, b + i, n - i);
  return i + r; // r == n - i when equal to the end: i + r == n
}

FAF_NO_BUILTIN
size_t faf_k_ascii_prefix(const char *s, size_t n) {
  if (n < PIE_MIN)
    return faf_swar_ascii_prefix(s, n);
  size_t head = to_chunk(s);
  size_t r = faf_swar_ascii_prefix(s, head);
  if (r != head)
    return r;
  const char *p = s + head;
  for (size_t chunks = (n - head) / CHUNK; chunks; --chunks, p += CHUNK) {
    if (chunk_has_high(p))
      return (size_t)(p - s) + faf_swar_ascii_prefix(p, CHUNK);
  }
  size_t i = (size_t)(p - s);
  r = faf_swar_ascii_prefix(p, n - i);
  return i + r;
}

#endif // FAF_BACKEND_PIE
