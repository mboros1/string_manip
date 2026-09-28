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

// find_byte entirely in assembly (windowed ABI: a2 = s, a3 = n, a4 = c), so
// the hardware loops and the register use don't depend on GCC. Same shape as
// the C version: SWAR over the lead (64+ bytes to a 64-byte boundary), PIE
// over whole chunks, SWAR over a matching chunk and over the tail. Inputs
// shorter than lead + one chunk are scanned by SWAR only.
//
// Registers: a2 s, a3 n, a4 c, a5 cursor, a6 end of the SWAR range,
// a7 c in every lane, a10 0x7f7f7f7f, a11 -1, a12 nonzero once the scan may
// only end the search (short input, rescan, tail), a8/a9/a13-a15 temporaries.
__asm__(
    ".pushsection .text.faf_k_find_byte,\"ax\",@progbits\n"
    ".align 4\n"
    ".global faf_k_find_byte\n"
    ".type faf_k_find_byte,@function\n"
    "faf_k_find_byte:\n"
    "  entry   a1, 32\n"
    "  extui   a4, a4, 0, 8\n"
    "  slli    a8, a4, 8\n"
    "  or      a7, a4, a8\n"
    "  slli    a8, a7, 16\n"
    "  or      a7, a7, a8\n"
    "  movi    a10, 0x7f7f7f7f\n"
    "  movi    a11, -1\n"
    "  neg     a8, a2\n"
    "  extui   a8, a8, 0, 6\n"           // to_chunk(s)
    "  addi    a8, a8, 64\n"             // head
    "  addi    a9, a8, 64\n"             // head + CHUNK
    "  mov     a5, a2\n"
    "  add     a6, a2, a3\n"             // end
    "  movi    a12, 1\n"
    "  bltu    a3, a9, .Lfb_scan\n"      // short: SWAR over everything
    "  add     a6, a2, a8\n"             // lead end
    "  movi    a12, 0\n"
    // SWAR over [a5, a6): match -> .Lfb_found, else .Lfb_done
    ".Lfb_scan:\n"
    ".Lfb_bytes:\n"
    "  bgeu    a5, a6, .Lfb_done\n"
    "  extui   a8, a5, 0, 2\n"
    "  beqz    a8, .Lfb_words\n"
    "  l8ui    a9, a5, 0\n"
    "  beq     a9, a4, .Lfb_found\n"
    "  addi    a5, a5, 1\n"
    "  j       .Lfb_bytes\n"
    ".Lfb_words:\n"
    "  sub     a8, a6, a5\n"
    "  srli    a13, a8, 3\n"         // pairs of words
    "  beqz    a13, .Lfb_single\n"
    "  loop    a13, .Lfb_pairs_end\n"
    "  l32i    a8, a5, 0\n"
    "  l32i    a9, a5, 4\n"
    "  xor     a8, a8, a7\n"
    "  xor     a9, a9, a7\n"
    "  and     a14, a8, a10\n"
    "  and     a15, a9, a10\n"
    "  add     a14, a14, a10\n"
    "  add     a15, a15, a10\n"
    "  or      a14, a14, a8\n"
    "  or      a15, a15, a9\n"
    "  or      a14, a14, a10\n"
    "  or      a15, a15, a10\n"
    "  bnall   a14, a11, .Lfb_hit0\n"
    "  bnall   a15, a11, .Lfb_hit1\n"
    "  addi    a5, a5, 8\n"
    ".Lfb_pairs_end:\n"
    ".Lfb_single:\n"
    "  sub     a8, a6, a5\n"
    "  bltui   a8, 4, .Lfb_tail\n"
    "  l32i    a8, a5, 0\n"
    "  xor     a8, a8, a7\n"
    "  and     a14, a8, a10\n"
    "  add     a14, a14, a10\n"
    "  or      a14, a14, a8\n"
    "  or      a14, a14, a10\n"
    "  bnall   a14, a11, .Lfb_hit0\n"
    "  addi    a5, a5, 4\n"
    ".Lfb_tail:\n"
    "  bgeu    a5, a6, .Lfb_done\n"
    "  l8ui    a9, a5, 0\n"
    "  beq     a9, a4, .Lfb_found\n"
    "  addi    a5, a5, 1\n"
    "  j       .Lfb_tail\n"
    ".Lfb_hit1:\n"
    "  addi    a5, a5, 4\n"
    "  mov     a14, a15\n"
    ".Lfb_hit0:\n"                        // a14: ~mask, lanes of a match
    "  xor     a14, a14, a11\n"
    "  neg     a8, a14\n"
    "  and     a8, a8, a14\n"             // lowest set bit
    "  nsau    a8, a8\n"
    "  movi    a9, 31\n"
    "  sub     a8, a9, a8\n"
    "  srli    a8, a8, 3\n"
    "  add     a5, a5, a8\n"
    ".Lfb_found:\n"
    "  sub     a2, a5, a2\n"
    "  retw\n"
    ".Lfb_done:\n"
    "  bnez    a12, .Lfb_none\n"
    // PIE over whole chunks from a5 (64-byte aligned)
    "  add     a6, a2, a3\n"
    "  sub     a8, a6, a5\n"
    "  srli    a13, a8, 6\n"         // chunks, at least 1
    "  s8i     a4, a1, 0\n"
    "  ee.vldbc.8 q7, a1\n"
    "  loop    a13, .Lfb_chunks_end\n"
    "  ee.vld.128.ip q0, a5, 16\n"
    "  ee.vld.128.ip q1, a5, 16\n"
    "  ee.vld.128.ip q2, a5, 16\n"
    "  ee.vld.128.ip q3, a5, 16\n"
    "  ee.vcmp.eq.s8 q0, q0, q7\n"
    "  ee.vcmp.eq.s8 q1, q1, q7\n"
    "  ee.vcmp.eq.s8 q2, q2, q7\n"
    "  ee.vcmp.eq.s8 q3, q3, q7\n"
    "  ee.orq  q0, q0, q1\n"
    "  ee.orq  q2, q2, q3\n"
    "  ee.orq  q0, q0, q2\n"
    "  ee.movi.32.a q0, a8, 0\n"
    "  ee.movi.32.a q0, a9, 1\n"
    "  ee.movi.32.a q0, a14, 2\n"
    "  ee.movi.32.a q0, a15, 3\n"
    "  or      a8, a8, a9\n"
    "  or      a14, a14, a15\n"
    "  or      a8, a8, a14\n"
    "  bnez    a8, .Lfb_chunk_hit\n"
    ".Lfb_chunks_end:\n"
    "  movi    a12, 1\n"                 // tail: a6 is the end
    "  j       .Lfb_scan\n"
    ".Lfb_chunk_hit:\n"
    "  addi    a5, a5, -64\n"
    "  addi    a6, a5, 64\n"
    "  movi    a12, 1\n"
    "  j       .Lfb_scan\n"
    ".Lfb_none:\n"
    "  mov     a2, a3\n"
    "  retw\n"
    ".size faf_k_find_byte, .-faf_k_find_byte\n"
    ".popsection\n");

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

