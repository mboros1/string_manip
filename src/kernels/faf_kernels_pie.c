#include "faf_kernels.h"

#if defined(FAF_BACKEND_PIE)

// PIE kernels for the ESP32-S3 (pie backend): the scanning kernels, each a
// whole function in assembly, using the 128-bit PIE vector instructions for
// long inputs. Everything else is the SWAR backend.
//
// Why assembly: PIE has no C intrinsics, and on this in-order core with no
// branch prediction, short inputs are decided by details GCC does not keep
// stable: whether a loop becomes a hardware loop, what gets inlined, call
// overhead. With the C entry points short searches were up to 19% slower than
// plain SWAR; in assembly they are faster.
//
// Shape: word loops over a lead (64+ bytes, to an aligned address), PIE over
// aligned 64-byte chunks (four blocks; they never cross a page), word loops
// over a chunk with a match (to find where) and over the tail. Most searches
// end in the lead (the next separator, the end of a short string), where
// words are cheaper than a chunk plus a rescan. PIE has no movemask, so a
// chunk's compare result comes out as four 32-bit words.
//
// Word loops test each byte with its own branch (BNONE on a mask, BBSI on a
// bit), like newlib's strlen in the ROM: untaken branches are cheap here, and
// the branch taken says which byte. The pointer bump goes between a load and
// its first use (a load's result is not ready on the next cycle).
//
// Hardware loops: a search that ends a LOOP early leaves LCOUNT nonzero; a
// later branch to that loop's end does not loop back (only falling through
// does; tested by Kernels.after_exit). GCC never uses the q registers;
// FreeRTOS saves them on task switches like the FPU. Functions using a stack
// slot at a1 reserve 32 bytes (entry a1, 32): the 16 above a1 belong to the
// caller's register save area otherwise.

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

// count_byte in assembly (a2 = s, a3 = n, a4 = c): words to a 64-byte
// boundary (inputs under 128 bytes: words only), PIE over whole chunks, words
// over the tail. The word loops add each word's non-matching lanes (bit 7 of
// ((x & 0x7f..) + 0x7f..) | x, with x the word XOR c) as 0/1 bytes into a15;
// a word range is at most 32 words, so a lane never passes 32, and matches
// are 4 * words minus the sum of a15's bytes. The chunk loop counts per lane
// in q6, up to 4 per chunk, in runs of at most 15 chunks: the four words of
// q6 then add up to at most 240 per lane.
//
// Registers: a2 s, a4 c, a5 cursor, a6 end of the word range, a7 c in every
// lane, a10 0x7f7f7f7f, a11 0x01010101, a12 nonzero once only the tail is
// left, a14 the count, a3 (n, then the chunks left), a8/a9/a13/a15
// temporaries.
__asm__(
    ".pushsection .text.faf_k_count_byte,\"ax\",@progbits\n"
    ".align 4\n"
    ".global faf_k_count_byte\n"
    ".type faf_k_count_byte,@function\n"
    "faf_k_count_byte:\n"
    "  entry   a1, 32\n"
    "  extui   a4, a4, 0, 8\n"
    "  slli    a8, a4, 8\n"
    "  or      a7, a4, a8\n"
    "  slli    a8, a7, 16\n"
    "  or      a7, a7, a8\n"
    "  movi    a10, 0x7f7f7f7f\n"
    "  movi    a11, 0x01010101\n"
    "  movi    a14, 0\n"
    "  mov     a5, a2\n"
    "  add     a6, a2, a3\n"             // end
    "  movi    a12, 1\n"
    "  bltui   a3, 128, .Lcb_scan\n"     // short: words over everything
    "  neg     a8, a2\n"
    "  extui   a8, a8, 0, 6\n"
    "  add     a6, a2, a8\n"             // first chunk
    "  movi    a12, 0\n"
    // count over [a5, a6), then .Lcb_done
    ".Lcb_scan:\n"
    "  bgeu    a5, a6, .Lcb_done\n"
    "  extui   a8, a5, 0, 2\n"
    "  beqz    a8, .Lcb_words\n"
    "  l8ui    a9, a5, 0\n"
    "  addi    a5, a5, 1\n"
    "  bne     a9, a4, .Lcb_scan\n"
    "  addi    a14, a14, 1\n"
    "  j       .Lcb_scan\n"
    ".Lcb_words:\n"
    "  sub     a8, a6, a5\n"
    "  srli    a13, a8, 2\n"
    "  beqz    a13, .Lcb_tail\n"
    "  movi    a15, 0\n"
    "  addx4   a14, a13, a14\n"          // + 4 per word, less the non-matches
    "  loop    a13, .Lcb_words_end\n"
    "  l32i    a8, a5, 0\n"
    "  addi    a5, a5, 4\n"              // between the load and its use
    "  xor     a8, a8, a7\n"
    "  and     a9, a8, a10\n"
    "  add     a9, a9, a10\n"
    "  or      a9, a9, a8\n"             // bit 7 of a lane: nonzero
    "  srli    a9, a9, 7\n"
    "  and     a9, a9, a11\n"
    "  add     a15, a15, a9\n"
    ".Lcb_words_end:\n"
    "  mull    a15, a15, a11\n"          // lane sum in the top byte
    "  extui   a15, a15, 24, 8\n"
    "  sub     a14, a14, a15\n"
    ".Lcb_tail:\n"
    "  bgeu    a5, a6, .Lcb_done\n"
    "  l8ui    a9, a5, 0\n"
    "  addi    a5, a5, 1\n"
    "  bne     a9, a4, .Lcb_tail\n"
    "  addi    a14, a14, 1\n"
    "  j       .Lcb_tail\n"
    ".Lcb_done:\n"
    "  bnez    a12, .Lcb_ret\n"
    // PIE over whole chunks from a5 (64-byte aligned)
    "  add     a6, a2, a3\n"             // end, for the tail
    "  sub     a8, a6, a5\n"
    "  srli    a3, a8, 6\n"              // chunks, at least 1
    "  s8i     a4, a1, 0\n"
    "  ee.vldbc.8 q7, a1\n"
    ".Lcb_run:\n"
    "  movi    a13, 15\n"
    "  minu    a13, a13, a3\n"
    "  sub     a3, a3, a13\n"
    "  ee.zero.q q6\n"
    "  loop    a13, .Lcb_chunks_end\n"
    "  ee.vld.128.ip q0, a5, 16\n"
    "  ee.vld.128.ip q1, a5, 16\n"
    "  ee.vld.128.ip q2, a5, 16\n"
    "  ee.vld.128.ip q3, a5, 16\n"
    "  ee.vcmp.eq.s8 q0, q0, q7\n"       // 0 or -1 per lane
    "  ee.vcmp.eq.s8 q1, q1, q7\n"
    "  ee.vcmp.eq.s8 q2, q2, q7\n"
    "  ee.vcmp.eq.s8 q3, q3, q7\n"
    "  ee.vadds.s8 q0, q0, q1\n"
    "  ee.vadds.s8 q2, q2, q3\n"
    "  ee.vadds.s8 q0, q0, q2\n"         // -4 .. 0
    "  ee.vsubs.s8 q6, q6, q0\n"         // count up
    ".Lcb_chunks_end:\n"
    "  ee.movi.32.a q6, a8, 0\n"
    "  ee.movi.32.a q6, a9, 1\n"
    "  ee.movi.32.a q6, a13, 2\n"
    "  ee.movi.32.a q6, a15, 3\n"
    "  add     a8, a8, a9\n"
    "  add     a13, a13, a15\n"
    "  add     a8, a8, a13\n"            // at most 240 per lane
    "  movi    a9, 0x00ff00ff\n"
    "  and     a13, a8, a9\n"
    "  srli    a8, a8, 8\n"
    "  and     a8, a8, a9\n"
    "  add     a8, a8, a13\n"            // two 16-bit sums
    "  extui   a13, a8, 16, 16\n"
    "  add     a8, a8, a13\n"
    "  extui   a8, a8, 0, 16\n"
    "  add     a14, a14, a8\n"
    "  bnez    a3, .Lcb_run\n"
    "  movi    a12, 1\n"                 // tail: a6 is the end
    "  j       .Lcb_scan\n"
    ".Lcb_ret:\n"
    "  mov     a2, a14\n"
    "  retw\n"
    ".size faf_k_count_byte, .-faf_k_count_byte\n"
    ".popsection\n");

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

// ascii_prefix in assembly (a2 = s, a3 = n), the same shape as find_byte:
// words over the lead, PIE over whole chunks, words over a chunk holding a
// high byte and over the tail. A word's bytes are tested with BBSI on bits 7,
// 15, 23 and 31: one instruction each, no masks.
//
// Registers: a2 s, a5 cursor, a6 end of the word range, a12 nonzero once the
// scan may only end the search, a3 (n, until the end pointer is known),
// a8/a9/a13 temporaries.
__asm__(
    ".pushsection .text.faf_k_ascii_prefix,\"ax\",@progbits\n"
    ".align 4\n"
    ".global faf_k_ascii_prefix\n"
    ".type faf_k_ascii_prefix,@function\n"
    "faf_k_ascii_prefix:\n"
    "  entry   a1, 32\n"
    "  neg     a8, a2\n"
    "  extui   a8, a8, 0, 6\n"           // to_chunk(s)
    "  addi    a8, a8, 64\n"             // head
    "  addi    a9, a8, 64\n"             // head + CHUNK
    "  mov     a5, a2\n"
    "  add     a6, a2, a3\n"             // end
    "  movi    a12, 1\n"
    "  bltu    a3, a9, .Lap_scan\n"      // short: words over everything
    "  add     a6, a2, a8\n"             // lead end
    "  movi    a12, 0\n"
    // words over [a5, a6): high byte -> .Lap_found, else .Lap_done
    ".Lap_scan:\n"
    "  bgeu    a5, a6, .Lap_done\n"
    "  extui   a8, a5, 0, 2\n"
    "  beqz    a8, .Lap_words\n"
    "  l8ui    a9, a5, 0\n"
    "  bbsi    a9, 7, .Lap_found\n"
    "  addi    a5, a5, 1\n"
    "  j       .Lap_scan\n"
    ".Lap_words:\n"
    "  sub     a8, a6, a5\n"
    "  srli    a13, a8, 2\n"
    "  beqz    a13, .Lap_tail\n"
    "  loop    a13, .Lap_words_end\n"
    "  l32i    a8, a5, 0\n"
    "  addi    a5, a5, 4\n"              // between the load and its use
    "  bbsi    a8, 7, .Lap_w0\n"
    "  bbsi    a8, 15, .Lap_w1\n"
    "  bbsi    a8, 23, .Lap_w2\n"
    "  bbsi    a8, 31, .Lap_w3\n"
    ".Lap_words_end:\n"
    ".Lap_tail:\n"
    "  bgeu    a5, a6, .Lap_done\n"
    "  l8ui    a9, a5, 0\n"
    "  bbsi    a9, 7, .Lap_found\n"
    "  addi    a5, a5, 1\n"
    "  j       .Lap_tail\n"
    ".Lap_w0:\n"                         // byte k of the word before a5
    "  addi    a5, a5, -1\n"
    ".Lap_w1:\n"
    "  addi    a5, a5, -1\n"
    ".Lap_w2:\n"
    "  addi    a5, a5, -1\n"
    ".Lap_w3:\n"
    "  addi    a5, a5, -1\n"
    ".Lap_found:\n"
    "  sub     a2, a5, a2\n"
    "  retw\n"
    ".Lap_done:\n"
    "  bnez    a12, .Lap_none\n"
    // PIE over whole chunks from a5 (64-byte aligned)
    "  add     a6, a2, a3\n"             // end, for the tail
    "  sub     a8, a6, a5\n"
    "  srli    a13, a8, 6\n"             // chunks, at least 1
    "  ee.zero.q q7\n"
    "  loop    a13, .Lap_chunks_end\n"
    "  ee.vld.128.ip q0, a5, 16\n"
    "  ee.vld.128.ip q1, a5, 16\n"
    "  ee.vld.128.ip q2, a5, 16\n"
    "  ee.vld.128.ip q3, a5, 16\n"
    "  ee.orq  q0, q0, q1\n"             // a byte's high bit survives the ORs
    "  ee.orq  q2, q2, q3\n"
    "  ee.orq  q0, q0, q2\n"
    "  ee.vcmp.lt.s8 q0, q0, q7\n"       // signed: bytes >= 0x80 are < 0
    "  ee.movi.32.a q0, a8, 0\n"
    "  ee.movi.32.a q0, a9, 1\n"
    "  ee.movi.32.a q0, a3, 2\n"         // n is no longer needed
    "  ee.movi.32.a q0, a13, 3\n"        // the loop count is in LCOUNT
    "  or      a8, a8, a9\n"
    "  or      a3, a3, a13\n"
    "  or      a8, a8, a3\n"
    "  bnez    a8, .Lap_chunk_hit\n"
    ".Lap_chunks_end:\n"
    "  movi    a12, 1\n"                 // tail: a6 is the end
    "  j       .Lap_scan\n"
    ".Lap_chunk_hit:\n"
    "  addi    a5, a5, -64\n"
    "  addi    a6, a5, 64\n"
    "  movi    a12, 1\n"
    "  j       .Lap_words\n"
    ".Lap_none:\n"                       // a6 is the end: s + n
    "  sub     a2, a6, a2\n"
    "  retw\n"
    ".size faf_k_ascii_prefix, .-faf_k_ascii_prefix\n"
    ".popsection\n");

#endif // FAF_BACKEND_PIE
