// Included by faf_kernels_swar.c (not compiled on its own), so the SWAR
// kernels inline.
//
// PIE kernels for the ESP32-S3 (pie backend): the scanning kernels with its
// 128-bit vector instructions. Everything else is the SWAR backend.
//
// PIE has no C intrinsics, so the vector work is assembly. find_byte,
// find_bytes, strlen and mismatch are whole functions in assembly (see
// there); elsewhere each asm block is self-contained (it loads its own
// pattern and moves its results to address registers), except count_byte's
// accumulator, see there. GCC never uses the q registers; FreeRTOS saves
// them on task switches like the FPU.
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

// find_byte entirely in assembly (windowed ABI: a2 = s, a3 = n, a4 = c), so
// the hardware loops and the register use don't depend on GCC. Words over the
// lead (64+ bytes to a 64-byte boundary), PIE over whole chunks, words over a
// matching chunk and over the tail; inputs shorter than lead + one chunk are
// words only. A word is XORed with c in every lane, then each byte tested
// with its own BNONE as in strlen: 7 instructions per word, and the branch
// taken says which byte.
//
// Registers: a2 s, a4 c, a5 cursor, a6 end of the word range, a7 c in every
// lane, a10/a11/a14/a15 the byte masks 0xff << 8k, a12 nonzero once the scan
// may only end the search (short input, rescan, tail), a3 (n, until the end
// pointer is known), a8/a9/a13 temporaries.
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
    "  movi    a10, 0xff\n"
    "  movi    a11, 0xff00\n"
    "  movi    a14, 0xff0000\n"
    "  movi    a15, 0xff000000\n"
    "  neg     a8, a2\n"
    "  extui   a8, a8, 0, 6\n"           // to_chunk(s)
    "  addi    a8, a8, 64\n"             // head
    "  addi    a9, a8, 64\n"             // head + CHUNK
    "  mov     a5, a2\n"
    "  add     a6, a2, a3\n"             // end
    "  movi    a12, 1\n"
    "  bltu    a3, a9, .Lfb_scan\n"      // short: words over everything
    "  add     a6, a2, a8\n"             // lead end
    "  movi    a12, 0\n"
    // words over [a5, a6): match -> .Lfb_found, else .Lfb_done
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
    "  srli    a13, a8, 2\n"
    "  beqz    a13, .Lfb_tail\n"
    "  loop    a13, .Lfb_words_end\n"
    "  l32i    a8, a5, 0\n"
    "  addi    a5, a5, 4\n"              // between the load and its use
    "  xor     a8, a8, a7\n"
    "  bnone   a8, a10, .Lfb_w0\n"
    "  bnone   a8, a11, .Lfb_w1\n"
    "  bnone   a8, a14, .Lfb_w2\n"
    "  bnone   a8, a15, .Lfb_w3\n"
    ".Lfb_words_end:\n"
    ".Lfb_tail:\n"
    "  bgeu    a5, a6, .Lfb_done\n"
    "  l8ui    a9, a5, 0\n"
    "  beq     a9, a4, .Lfb_found\n"
    "  addi    a5, a5, 1\n"
    "  j       .Lfb_tail\n"
    ".Lfb_w0:\n"                         // byte k of the word before a5
    "  addi    a5, a5, -1\n"
    ".Lfb_w1:\n"
    "  addi    a5, a5, -1\n"
    ".Lfb_w2:\n"
    "  addi    a5, a5, -1\n"
    ".Lfb_w3:\n"
    "  addi    a5, a5, -1\n"
    ".Lfb_found:\n"
    "  sub     a2, a5, a2\n"
    "  retw\n"
    ".Lfb_done:\n"
    "  bnez    a12, .Lfb_none\n"
    // PIE over whole chunks from a5 (64-byte aligned)
    "  add     a6, a2, a3\n"             // end, for the tail
    "  sub     a8, a6, a5\n"
    "  srli    a13, a8, 6\n"             // chunks, at least 1
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
    "  ee.movi.32.a q0, a3, 2\n"         // n is no longer needed
    "  ee.movi.32.a q0, a13, 3\n"        // the loop count is in LCOUNT
    "  or      a8, a8, a9\n"
    "  or      a3, a3, a13\n"
    "  or      a8, a8, a3\n"
    "  bnez    a8, .Lfb_chunk_hit\n"
    ".Lfb_chunks_end:\n"
    "  movi    a12, 1\n"                 // tail: a6 is the end
    "  j       .Lfb_scan\n"
    ".Lfb_chunk_hit:\n"
    "  addi    a5, a5, -64\n"
    "  addi    a6, a5, 64\n"
    "  movi    a12, 1\n"
    "  j       .Lfb_words\n"
    ".Lfb_none:\n"                       // a6 is the end: s + n
    "  sub     a2, a6, a2\n"
    "  retw\n"
    ".size faf_k_find_byte, .-faf_k_find_byte\n"
    ".popsection\n");

// find_bytes in assembly: two words per iteration, each XORed with c in
// every lane and each byte tested with its own BNONE, as in find_byte. A match
// branches out to store its position and jumps back to the next test, still
// inside the hardware loop (the last test's return is the pointer bump, so the
// loop end is reached by falling through).
//
// Registers: a2 s, a3 end of s, a4 c, a5 next position slot, a6 end of the
// slots, a7 c in every lane, a8/a9 the two words, a10/a11/a14/a15 the byte
// masks 0xff << 8k, a12 cursor, a13 temporary; the first slot is at a1.
__asm__(
    ".pushsection .text.faf_k_find_bytes,\"ax\",@progbits\n"
    ".align 4\n"
    ".global faf_k_find_bytes\n"
    ".type faf_k_find_bytes,@function\n"
    "faf_k_find_bytes:\n"
    "  entry   a1, 32\n"
    "  s32i    a5, a1, 0\n"
    "  addx4   a6, a6, a5\n"              // end of the slots
    "  beq     a5, a6, .Lfbs_ret\n"       // max == 0
    "  extui   a4, a4, 0, 8\n"
    "  slli    a8, a4, 8\n"
    "  or      a7, a4, a8\n"
    "  slli    a8, a7, 16\n"
    "  or      a7, a7, a8\n"
    "  movi    a10, 0xff\n"
    "  movi    a11, 0xff00\n"
    "  movi    a14, 0xff0000\n"
    "  movi    a15, 0xff000000\n"
    "  mov     a12, a2\n"
    "  add     a3, a2, a3\n"
    ".Lfbs_head:\n"
    "  bgeu    a12, a3, .Lfbs_ret\n"
    "  extui   a13, a12, 0, 2\n"
    "  beqz    a13, .Lfbs_words\n"
    "  l8ui    a13, a12, 0\n"
    "  addi    a12, a12, 1\n"
    "  bne     a13, a4, .Lfbs_head\n"
    "  sub     a13, a12, a2\n"
    "  addi    a13, a13, -1\n"
    "  s32i    a13, a5, 0\n"
    "  addi    a5, a5, 4\n"
    "  bne     a5, a6, .Lfbs_head\n"
    "  j       .Lfbs_ret\n"
    ".Lfbs_words:\n"
    "  sub     a13, a3, a12\n"
    "  srli    a13, a13, 3\n"
    "  beqz    a13, .Lfbs_tail\n"
    "  loop    a13, .Lfbs_words_end\n"
    "  l32i    a8, a12, 0\n"
    "  l32i    a9, a12, 4\n"
    "  xor     a8, a8, a7\n"
    "  xor     a9, a9, a7\n"
    "  bnone   a8, a10, .Lfbs_m0\n"
    ".Lfbs_r0:\n"
    "  bnone   a8, a11, .Lfbs_m1\n"
    ".Lfbs_r1:\n"
    "  bnone   a8, a14, .Lfbs_m2\n"
    ".Lfbs_r2:\n"
    "  bnone   a8, a15, .Lfbs_m3\n"
    ".Lfbs_r3:\n"
    "  bnone   a9, a10, .Lfbs_m4\n"
    ".Lfbs_r4:\n"
    "  bnone   a9, a11, .Lfbs_m5\n"
    ".Lfbs_r5:\n"
    "  bnone   a9, a14, .Lfbs_m6\n"
    ".Lfbs_r6:\n"
    "  bnone   a9, a15, .Lfbs_m7\n"
    ".Lfbs_r7:\n"
    "  addi    a12, a12, 8\n"
    ".Lfbs_words_end:\n"
    ".Lfbs_tail:\n"                        // up to 7 bytes
    "  bgeu    a12, a3, .Lfbs_ret\n"
    "  l8ui    a13, a12, 0\n"
    "  addi    a12, a12, 1\n"
    "  bne     a13, a4, .Lfbs_tail\n"
    "  sub     a13, a12, a2\n"
    "  addi    a13, a13, -1\n"
    "  s32i    a13, a5, 0\n"
    "  addi    a5, a5, 4\n"
    "  bne     a5, a6, .Lfbs_tail\n"
    ".Lfbs_ret:\n"
    "  l32i    a8, a1, 0\n"
    "  sub     a2, a5, a8\n"
    "  srli    a2, a2, 2\n"
    "  retw\n"
    ".Lfbs_m0:\n"
    "  sub     a13, a12, a2\n"
    "  s32i    a13, a5, 0\n"
    "  addi    a5, a5, 4\n"
    "  beq     a5, a6, .Lfbs_ret\n"
    "  j       .Lfbs_r0\n"
    ".Lfbs_m1:\n"
    "  sub     a13, a12, a2\n"
    "  addi    a13, a13, 1\n"
    "  s32i    a13, a5, 0\n"
    "  addi    a5, a5, 4\n"
    "  beq     a5, a6, .Lfbs_ret\n"
    "  j       .Lfbs_r1\n"
    ".Lfbs_m2:\n"
    "  sub     a13, a12, a2\n"
    "  addi    a13, a13, 2\n"
    "  s32i    a13, a5, 0\n"
    "  addi    a5, a5, 4\n"
    "  beq     a5, a6, .Lfbs_ret\n"
    "  j       .Lfbs_r2\n"
    ".Lfbs_m3:\n"
    "  sub     a13, a12, a2\n"
    "  addi    a13, a13, 3\n"
    "  s32i    a13, a5, 0\n"
    "  addi    a5, a5, 4\n"
    "  beq     a5, a6, .Lfbs_ret\n"
    "  j       .Lfbs_r3\n"
    ".Lfbs_m4:\n"
    "  sub     a13, a12, a2\n"
    "  addi    a13, a13, 4\n"
    "  s32i    a13, a5, 0\n"
    "  addi    a5, a5, 4\n"
    "  beq     a5, a6, .Lfbs_ret\n"
    "  j       .Lfbs_r4\n"
    ".Lfbs_m5:\n"
    "  sub     a13, a12, a2\n"
    "  addi    a13, a13, 5\n"
    "  s32i    a13, a5, 0\n"
    "  addi    a5, a5, 4\n"
    "  beq     a5, a6, .Lfbs_ret\n"
    "  j       .Lfbs_r5\n"
    ".Lfbs_m6:\n"
    "  sub     a13, a12, a2\n"
    "  addi    a13, a13, 6\n"
    "  s32i    a13, a5, 0\n"
    "  addi    a5, a5, 4\n"
    "  beq     a5, a6, .Lfbs_ret\n"
    "  j       .Lfbs_r6\n"
    ".Lfbs_m7:\n"
    "  sub     a13, a12, a2\n"
    "  addi    a13, a13, 7\n"
    "  s32i    a13, a5, 0\n"
    "  addi    a5, a5, 4\n"
    "  beq     a5, a6, .Lfbs_ret\n"
    "  j       .Lfbs_r7\n"
    ".size faf_k_find_bytes, .-faf_k_find_bytes\n"
    ".popsection\n");

// strlen in assembly. The word loops test each byte with its own BNONE (branch
// if word & mask == 0), like newlib's Xtensa strlen in the ROM: 6 instructions
// per word, and the branch taken says which byte, where a SWAR zero-lane mask
// costs ~7 per word plus ~8 to turn the mask into a position. Words to the
// first 64-byte boundary at least 128 bytes in (with 64, a third of 85-byte
// lines paid for a chunk and a rescan: 7% slower), then PIE chunks until one
// holds the NUL (reading at most to the end of that aligned chunk, never
// across a page), then words over that chunk.
//
// Registers: a2 s, a3 cursor, a4-a7 the byte masks 0xff << 8k, a11 loop
// count, a12 first chunk, a8-a10/a13 temporaries.
#define FAF_STRLEN_WORDS(end)                                                  \
  "  loop    a11, " end "\n"                                                   \
  "  l32i    a8, a3, 0\n"                                                      \
  "  addi    a3, a3, 4\n" /* between the load and its use: no stall */          \
  "  bnone   a8, a4, .Lsl_w0\n"                                                \
  "  bnone   a8, a5, .Lsl_w1\n"                                                \
  "  bnone   a8, a6, .Lsl_w2\n"                                                \
  "  bnone   a8, a7, .Lsl_w3\n"                                                \
  end ":\n"
__asm__(
    ".pushsection .text.faf_k_strlen,\"ax\",@progbits\n"
    ".align 4\n"
    ".global faf_k_strlen\n"
    ".type faf_k_strlen,@function\n"
    "faf_k_strlen:\n"
    "  entry   a1, 32\n"
    "  mov     a3, a2\n"
    ".Lsl_bytes:\n"
    "  extui   a8, a3, 0, 2\n"
    "  beqz    a8, .Lsl_aligned\n"
    "  l8ui    a9, a3, 0\n"
    "  beqz    a9, .Lsl_found\n"
    "  addi    a3, a3, 1\n"
    "  j       .Lsl_bytes\n"
    ".Lsl_aligned:\n"
    "  movi    a4, 0xff\n"
    "  movi    a5, 0xff00\n"
    "  movi    a6, 0xff0000\n"
    "  movi    a7, 0xff000000\n"
    "  addi    a12, a2, 128\n"
    "  neg     a8, a12\n"
    "  extui   a8, a8, 0, 6\n"
    "  add     a12, a12, a8\n"          // first chunk
    "  sub     a11, a12, a3\n"
    "  srli    a11, a11, 2\n"           // words to it, at least 32
    FAF_STRLEN_WORDS(".Lsl_lead_end")
    "  ee.zero.q q7\n"
    "  movi    a11, 0\n"                // 2^32 chunks: until the NUL
    "  loop    a11, .Lsl_chunks_end\n"
    "  ee.vld.128.ip q0, a3, 16\n"
    "  ee.vld.128.ip q1, a3, 16\n"
    "  ee.vld.128.ip q2, a3, 16\n"
    "  ee.vld.128.ip q3, a3, 16\n"
    "  ee.vcmp.eq.s8 q0, q0, q7\n"
    "  ee.vcmp.eq.s8 q1, q1, q7\n"
    "  ee.vcmp.eq.s8 q2, q2, q7\n"
    "  ee.vcmp.eq.s8 q3, q3, q7\n"
    "  ee.orq  q0, q0, q1\n"
    "  ee.orq  q2, q2, q3\n"
    "  ee.orq  q0, q0, q2\n"
    "  ee.movi.32.a q0, a8, 0\n"
    "  ee.movi.32.a q0, a9, 1\n"
    "  ee.movi.32.a q0, a10, 2\n"
    "  ee.movi.32.a q0, a13, 3\n"
    "  or      a8, a8, a9\n"
    "  or      a10, a10, a13\n"
    "  or      a8, a8, a10\n"
    "  bnez    a8, .Lsl_chunk_hit\n"
    ".Lsl_chunks_end:\n"
    ".Lsl_chunk_hit:\n"
    "  addi    a3, a3, -64\n"
    "  movi    a11, 16\n"
    FAF_STRLEN_WORDS(".Lsl_rescan_end") // finds the NUL
    ".Lsl_w0:\n"                        // byte k of the word before a3
    "  addi    a3, a3, -1\n"
    ".Lsl_w1:\n"
    "  addi    a3, a3, -1\n"
    ".Lsl_w2:\n"
    "  addi    a3, a3, -1\n"
    ".Lsl_w3:\n"
    "  addi    a3, a3, -1\n"
    ".Lsl_found:\n"
    "  sub     a2, a3, a2\n"
    "  retw\n"
    ".size faf_k_strlen, .-faf_k_strlen\n"
    ".popsection\n");
#undef FAF_STRLEN_WORDS

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

// mismatch in assembly (a2 = a, a3 = b, a4 = n), the same shape as find_byte:
// words over the lead (64+ bytes, to a 16-byte aligned a), PIE over 32-byte
// steps, words over a differing step and over the tail; inputs shorter than
// lead + one step are words only. When b is not word aligned, each of its
// words is funnel-shifted from the two aligned words it straddles (SSA8L sets
// the shift from b's address once, SRC shifts): every load holds a byte of b.
// In the PIE loop an unaligned b is shifted from three 16-byte blocks the
// same way (LD.128.USAR, SRC.Q).
//
// Registers: a2 a, a3 b, a4 n, a5 cursor in a, a6 cursor in b (kept in step
// only where used, else recomputed), a7 end of the word range, a12 nonzero
// once the scan may only end the search (short input, rescan, tail), a14 -1,
// a8-a11/a13 temporaries.
__asm__(
    ".pushsection .text.faf_k_mismatch,\"ax\",@progbits\n"
    ".align 4\n"
    ".global faf_k_mismatch\n"
    ".type faf_k_mismatch,@function\n"
    "faf_k_mismatch:\n"
    "  entry   a1, 32\n"
    "  mov     a5, a2\n"
    "  mov     a6, a3\n"
    "  addi    a8, a2, 64\n"
    "  neg     a8, a8\n"
    "  extui   a8, a8, 0, 4\n"
    "  addi    a8, a8, 64\n"             // head: a + head is 16-byte aligned
    "  addi    a9, a8, 32\n"
    "  add     a7, a2, a4\n"             // end
    "  movi    a12, 1\n"
    "  bltu    a4, a9, .Lmm_scan\n"      // short: words over everything
    "  add     a7, a2, a8\n"             // lead end
    "  movi    a12, 0\n"
    // words over [a5, a7): difference -> .Lmm_found, else .Lmm_done
    ".Lmm_scan:\n"
    "  bgeu    a5, a7, .Lmm_done\n"
    "  extui   a8, a5, 0, 2\n"
    "  beqz    a8, .Lmm_words\n"
    "  l8ui    a8, a5, 0\n"
    "  l8ui    a9, a6, 0\n"
    "  bne     a8, a9, .Lmm_found\n"
    "  addi    a5, a5, 1\n"
    "  addi    a6, a6, 1\n"
    "  j       .Lmm_scan\n"
    ".Lmm_words:\n"
    "  sub     a8, a7, a5\n"
    "  srli    a13, a8, 2\n"
    "  beqz    a13, .Lmm_tail\n"
    "  extui   a8, a6, 0, 2\n"
    "  bnez    a8, .Lmm_uwords\n"
    "  loop    a13, .Lmm_awords_end\n"
    "  l32i    a8, a5, 0\n"
    "  l32i    a9, a6, 0\n"
    "  addi    a5, a5, 4\n"
    "  addi    a6, a6, 4\n"
    "  bne     a8, a9, .Lmm_whit\n"
    ".Lmm_awords_end:\n"
    "  j       .Lmm_tail\n"
    ".Lmm_uwords:\n"
    "  ssa8l   a6\n"
    "  sub     a10, a6, a8\n"            // aligned word holding b's byte
    "  l32i    a11, a10, 0\n"
    "  loop    a13, .Lmm_uwords_end\n"
    "  l32i    a9, a10, 4\n"
    "  l32i    a8, a5, 0\n"
    "  addi    a10, a10, 4\n"
    "  src     a13, a9, a11\n"           // b's word: (hi:lo) >> 8 * offset
    "  mov     a11, a9\n"
    "  addi    a5, a5, 4\n"
    "  bne     a8, a13, .Lmm_uwhit\n"
    ".Lmm_uwords_end:\n"
    ".Lmm_tail:\n"
    "  sub     a8, a5, a2\n"
    "  add     a6, a3, a8\n"
    ".Lmm_tail_bytes:\n"
    "  bgeu    a5, a7, .Lmm_done\n"
    "  l8ui    a8, a5, 0\n"
    "  l8ui    a9, a6, 0\n"
    "  bne     a8, a9, .Lmm_found\n"
    "  addi    a5, a5, 1\n"
    "  addi    a6, a6, 1\n"
    "  j       .Lmm_tail_bytes\n"
    ".Lmm_uwhit:\n"
    "  mov     a9, a13\n"
    ".Lmm_whit:\n"                       // a8 != a9, the word before a5
    "  addi    a5, a5, -4\n"
    "  xor     a8, a8, a9\n"
    "  neg     a9, a8\n"
    "  and     a8, a8, a9\n"             // lowest set bit
    "  nsau    a8, a8\n"
    "  movi    a9, 31\n"
    "  sub     a8, a9, a8\n"
    "  srli    a8, a8, 3\n"
    "  add     a5, a5, a8\n"
    ".Lmm_found:\n"
    "  sub     a2, a5, a2\n"
    "  retw\n"
    ".Lmm_none:\n"
    "  mov     a2, a4\n"
    "  retw\n"
    ".Lmm_done:\n"
    "  bnez    a12, .Lmm_none\n"
    // PIE over 32-byte steps from a5 (16-byte aligned)
    "  sub     a8, a5, a2\n"
    "  add     a6, a3, a8\n"
    "  add     a7, a2, a4\n"             // end, for the tail
    "  sub     a8, a7, a5\n"
    "  srli    a13, a8, 5\n"             // steps, at least 1
    "  movi    a14, -1\n"
    "  extui   a8, a6, 0, 4\n"
    "  bnez    a8, .Lmm_vu\n"
    "  loop    a13, .Lmm_va_end\n"
    "  ee.vld.128.ip q0, a5, 16\n"
    "  ee.vld.128.ip q1, a5, 16\n"
    "  ee.vld.128.ip q2, a6, 16\n"
    "  ee.vld.128.ip q3, a6, 16\n"
    "  ee.vcmp.eq.s8 q0, q0, q2\n"
    "  ee.vcmp.eq.s8 q1, q1, q3\n"
    "  ee.andq q0, q0, q1\n"
    "  ee.movi.32.a q0, a8, 0\n"
    "  ee.movi.32.a q0, a9, 1\n"
    "  ee.movi.32.a q0, a10, 2\n"
    "  ee.movi.32.a q0, a11, 3\n"
    "  and     a8, a8, a9\n"
    "  and     a10, a10, a11\n"
    "  and     a8, a8, a10\n"
    "  bnall   a8, a14, .Lmm_vhit\n"
    ".Lmm_va_end:\n"
    "  j       .Lmm_vtail\n"
    ".Lmm_vu:\n"
    "  loop    a13, .Lmm_vu_end\n"
    "  ee.ld.128.usar.ip q2, a6, 16\n"   // block holding b
    "  ee.vld.128.ip q3, a6, 16\n"
    "  ee.vld.128.ip q4, a6, 0\n"
    "  ee.src.q q2, q2, q3\n"            // b[0, 16)
    "  ee.src.q q3, q3, q4\n"            // b[16, 32)
    "  ee.vld.128.ip q0, a5, 16\n"
    "  ee.vld.128.ip q1, a5, 16\n"
    "  ee.vcmp.eq.s8 q0, q0, q2\n"
    "  ee.vcmp.eq.s8 q1, q1, q3\n"
    "  ee.andq q0, q0, q1\n"
    "  ee.movi.32.a q0, a8, 0\n"
    "  ee.movi.32.a q0, a9, 1\n"
    "  ee.movi.32.a q0, a10, 2\n"
    "  ee.movi.32.a q0, a11, 3\n"
    "  and     a8, a8, a9\n"
    "  and     a10, a10, a11\n"
    "  and     a8, a8, a10\n"
    "  bnall   a8, a14, .Lmm_vhit\n"
    ".Lmm_vu_end:\n"
    ".Lmm_vtail:\n"
    "  movi    a12, 1\n"                 // tail: a7 is the end
    "  sub     a8, a5, a2\n"
    "  add     a6, a3, a8\n"
    "  j       .Lmm_scan\n"
    ".Lmm_vhit:\n"                       // the step before a5 differs
    "  addi    a5, a5, -32\n"
    "  sub     a8, a5, a2\n"
    "  add     a6, a3, a8\n"
    "  addi    a7, a5, 32\n"
    "  movi    a12, 1\n"
    "  j       .Lmm_words\n"
    ".size faf_k_mismatch, .-faf_k_mismatch\n"
    ".popsection\n");

// ascii_prefix is still C: a small entry that runs the SWAR lead inline, and
// an out-of-line function with the PIE loop (most calls end in the lead, and
// a bigger function cost them ~5% in register setup)
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

