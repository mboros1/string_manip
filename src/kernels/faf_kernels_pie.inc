// Included by faf_kernels_swar.c (not compiled on its own), so the SWAR
// kernels inline.
//
// PIE kernels for the ESP32-S3 (pie backend): the scanning kernels with its
// 128-bit vector instructions. Everything else is the SWAR backend.
//
// PIE has no C intrinsics, so the vector work is inline assembly. Each asm
// block is self-contained (it loads its own pattern and moves its results to
// address registers), except count_byte's accumulator, see there. GCC never
// uses the q registers; FreeRTOS saves them on task switches like the FPU.
//
// Shape: the SWAR kernels handle the first 64 bytes or so (to a 64-byte
// aligned address) and the part after the last whole chunk; the asm scans
// aligned 64-byte chunks (four blocks, so the loop around each costs little),
// which never cross a page. A chunk with
// a match is searched again with SWAR to find where. PIE has no movemask, so
// a chunk's compare result comes out as four 32-bit words.
//
// The SWAR start is that long on purpose: most searches stop early (the next
// separator, the end of a short string), and there SWAR is cheaper than a
// chunk plus a rescan. Switching after only a few bytes made next_token 28%
// slower.


#define CHUNK 64
// SWAR covers at least this much before the PIE chunks start
#define SWAR_LEAD 64

static inline size_t to_chunk(const char *p) {
  return (size_t)(-(uintptr_t)p & (CHUNK - 1));
}

// Offset of the first PIE chunk: SWAR_LEAD bytes, then to a chunk boundary
static inline size_t lead(const char *s) {
  return SWAR_LEAD + to_chunk(s + SWAR_LEAD);
}

// Nonzero if any byte of the aligned chunk at p equals *pat
static inline uint32_t chunk_has(const char *p, const unsigned char *pat) {
  uint32_t w0, w1, w2, w3;
  __asm__("ee.vldbc.8     q7, %[pat]\n\t"
          "ee.vld.128.ip  q0, %[p], 16\n\t"
          "ee.vld.128.ip  q1, %[p], 16\n\t"
          "ee.vld.128.ip  q2, %[p], 16\n\t"
          "ee.vld.128.ip  q3, %[p], 16\n\t"
          "ee.vcmp.eq.s8  q0, q0, q7\n\t"
          "ee.vcmp.eq.s8  q1, q1, q7\n\t"
          "ee.vcmp.eq.s8  q2, q2, q7\n\t"
          "ee.vcmp.eq.s8  q3, q3, q7\n\t"
          "ee.orq         q0, q0, q1\n\t"
          "ee.orq         q2, q2, q3\n\t"
          "ee.orq         q0, q0, q2\n\t"
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
          "ee.vld.128.ip  q2, %[p], 16\n\t"
          "ee.vld.128.ip  q3, %[p], 16\n\t"
          "ee.orq         q0, q0, q1\n\t" // a byte's high bit survives the ORs
          "ee.orq         q2, q2, q3\n\t"
          "ee.orq         q0, q0, q2\n\t"
          "ee.vcmp.lt.s8  q0, q0, q7\n\t" // signed: bytes >= 0x80 are < 0
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

// Each early-exit kernel is a small entry that runs the SWAR lead inline, and
// an out-of-line function with the PIE loop: most searches end in the lead,
// and a bigger function (more registers to set up per call) cost them ~5%.

__attribute__((noinline)) FAF_NO_BUILTIN static size_t
find_byte_long(const char *s, size_t n, char c, size_t head) {
  const unsigned char pat = (unsigned char)c;
  const char *p = s + head;
  for (size_t chunks = (n - head) / CHUNK; chunks; --chunks, p += CHUNK) {
    if (chunk_has(p, &pat))
      return (size_t)(p - s) + swar_find_byte(p, CHUNK, c);
  }
  size_t i = (size_t)(p - s);
  size_t r = swar_find_byte(p, n - i, c);
  return r == n - i ? n : i + r;
}

FAF_NO_BUILTIN
size_t faf_k_find_byte(const char *s, size_t n, char c) {
  size_t head = lead(s);
  if (n < head + CHUNK)
    return swar_find_byte(s, n, c);
  size_t r = swar_find_byte(s, head, c);
  return r != head ? r : find_byte_long(s, n, c, head);
}

// Reads to the end of the aligned chunk holding the NUL, which never crosses
// a page
__attribute__((noinline)) FAF_NO_ASAN FAF_NO_BUILTIN static size_t
strlen_long(const char *s, size_t head) {
  const unsigned char zero = 0;
  for (const char *p = s + head;; p += CHUNK) {
    if (chunk_has(p, &zero))
      return (size_t)(p - s) + swar_strlen(p);
  }
}

FAF_NO_ASAN FAF_NO_BUILTIN
size_t faf_k_strlen(const char *s) {
  size_t head = lead(s);
  size_t r = swar_find_byte(s, head, '\0');
  return r != head ? r : strlen_long(s, head);
}

// Sum of the bytes of w, each at most 126
static inline size_t byte_sum(uint32_t w) {
  uint32_t t = (w & 0x00FF00FFu) + ((w >> 8) & 0x00FF00FFu);
  return (t + (t >> 16)) & 0xFFFFu;
}

// Counts accumulate per lane in q6, up to 4 per chunk, so q6 is summed and
// cleared every 31 chunks (before a lane passes 127). q6 is the one register
// kept across asm statements, between the zeroing, the chunk loop and the sum;
// nothing in between touches the q registers.
FAF_NO_BUILTIN
size_t faf_k_count_byte(const char *s, size_t n, char c) {
  // no early exit: a short SWAR start is fine here
  if (n < 2 * CHUNK)
    return swar_count_byte(s, n, c);
  size_t head = to_chunk(s);
  size_t count = swar_count_byte(s, head, c);
  const unsigned char pat = (unsigned char)c;
  const char *p = s + head;
  size_t chunks = (n - head) / CHUNK;
  while (chunks) {
    size_t run = chunks < 31 ? chunks : 31;
    chunks -= run;
    __asm__ volatile("ee.zero.q q6" ::: "memory");
    for (; run; --run) {
      __asm__ volatile("ee.vldbc.8     q7, %[pat]\n\t"
                       "ee.vld.128.ip  q0, %[p], 16\n\t"
                       "ee.vld.128.ip  q1, %[p], 16\n\t"
                       "ee.vld.128.ip  q2, %[p], 16\n\t"
                       "ee.vld.128.ip  q3, %[p], 16\n\t"
                       "ee.vcmp.eq.s8  q0, q0, q7\n\t" // 0 or -1 per lane
                       "ee.vcmp.eq.s8  q1, q1, q7\n\t"
                       "ee.vcmp.eq.s8  q2, q2, q7\n\t"
                       "ee.vcmp.eq.s8  q3, q3, q7\n\t"
                       "ee.vadds.s8    q0, q0, q1\n\t"
                       "ee.vadds.s8    q2, q2, q3\n\t"
                       "ee.vadds.s8    q0, q0, q2\n\t" // -4 .. 0
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
  return count + swar_count_byte(p, n - i, c);
}

// The 32 bytes at a (16-byte aligned) and at b compared: 0xFFFFFFFF iff
// equal (the four words of the two equality masks, ANDed)
static inline uint32_t equal_aligned(const char *a, const char *b) {
  uint32_t w0, w1, w2, w3;
  __asm__("ee.vld.128.ip  q0, %[a], 16\n\t"
          "ee.vld.128.ip  q1, %[a], 16\n\t"
          "ee.vld.128.ip  q2, %[b], 16\n\t"
          "ee.vld.128.ip  q3, %[b], 16\n\t"
          "ee.vcmp.eq.s8  q0, q0, q2\n\t"
          "ee.vcmp.eq.s8  q1, q1, q3\n\t"
          "ee.andq        q0, q0, q1\n\t"
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

// Same, with b not 16-byte aligned: its 32 bytes are shifted together from
// the three aligned blocks they straddle (LD.128.USAR sets the byte shift from
// b's address, SRC.Q applies it). All three blocks hold bytes of [b, b + 32).
static inline uint32_t equal_unaligned(const char *a, const char *b) {
  uint32_t w0, w1, w2, w3;
  __asm__("ee.ld.128.usar.ip q2, %[b], 16\n\t" // block holding b
          "ee.vld.128.ip     q3, %[b], 16\n\t"
          "ee.vld.128.ip     q4, %[b], 0\n\t"
          "ee.src.q          q2, q2, q3\n\t"    // b[0, 16)
          "ee.src.q          q3, q3, q4\n\t"    // b[16, 32)
          "ee.vld.128.ip     q0, %[a], 16\n\t"
          "ee.vld.128.ip     q1, %[a], 16\n\t"
          "ee.vcmp.eq.s8     q0, q0, q2\n\t"
          "ee.vcmp.eq.s8     q1, q1, q3\n\t"
          "ee.andq           q0, q0, q1\n\t"
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

__attribute__((noinline)) FAF_NO_BUILTIN static size_t
mismatch_long(const char *a, const char *b, size_t n, size_t i) {
  bool b_aligned = (((uintptr_t)(b + i)) & 15) == 0;
  for (size_t steps = (n - i) / 32; steps; --steps, i += 32) {
    uint32_t eq = b_aligned ? equal_aligned(a + i, b + i)
                            : equal_unaligned(a + i, b + i);
    if (eq != 0xFFFFFFFFu)
      return i + swar_mismatch(a + i, b + i, 32);
  }
  return i + swar_mismatch(a + i, b + i, n - i); // n when equal to the end
}

FAF_NO_BUILTIN
size_t faf_k_mismatch(const char *a, const char *b, size_t n) {
  // SWAR_LEAD bytes, then a aligned to a block
  size_t head = SWAR_LEAD + (size_t)(-(uintptr_t)(a + SWAR_LEAD) & 15);
  if (n < head + 32)
    return swar_mismatch(a, b, n);
  size_t r = swar_mismatch(a, b, head);
  return r != head ? r : mismatch_long(a, b, n, head);
}

__attribute__((noinline)) FAF_NO_BUILTIN static size_t
ascii_prefix_long(const char *s, size_t n, size_t head) {
  const char *p = s + head;
  for (size_t chunks = (n - head) / CHUNK; chunks; --chunks, p += CHUNK) {
    if (chunk_has_high(p))
      return (size_t)(p - s) + swar_ascii_prefix(p, CHUNK);
  }
  size_t i = (size_t)(p - s);
  return i + swar_ascii_prefix(p, n - i);
}

FAF_NO_BUILTIN
size_t faf_k_ascii_prefix(const char *s, size_t n) {
  size_t head = lead(s);
  if (n < head + CHUNK)
    return swar_ascii_prefix(s, n);
  size_t r = swar_ascii_prefix(s, head);
  return r != head ? r : ascii_prefix_long(s, n, head);
}

