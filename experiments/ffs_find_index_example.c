#include <simde/x86/sse2.h>
#include <stdio.h>

const char *str = "hello,world,today";

// Function to print binary representation of a number
void printBinary(unsigned int n) {
  // Determine the number of bits in the integer
  unsigned int numBits = sizeof(n) * 8;

  // Iterate over each bit
  for (int i = numBits - 1; i >= 0; i--) {
    // Print 1 if the bit is set, otherwise print 0
    printf("%c", (n & (1 << i)) ? '1' : '0');
  }
  printf("\n");
}

int main(void) {
  char tok = ',';
  simde__m128i target_vector = simde_mm_set1_epi8(tok);
  simde__m128i chars = simde_mm_loadu_si128((simde__m128i *)str);
  simde__m128i cmp = simde_mm_cmpeq_epi8(chars, target_vector);

  int mask = simde_mm_movemask_epi8(cmp);
  printf("binary of mask: ");
  printBinary(mask);

  int ffs = __builtin_ffs(mask);
  printf("binary of ffs: ");
  printBinary(ffs);

  int pos = ffs - 1;

  printf("char at ffs: %c\n", str[pos]);

  printf("shift mask\n");
  mask &= mask - 1;

  ffs = __builtin_ffs(mask);
  printf("binary of ffs: ");
  printBinary(ffs);

  pos = ffs - 1;

  printf("char at ffs: %c\n", str[pos]);
}

// binary of mask: 00000000000000000000000000100000
// binary of ffs:  00000000000000000000000000000110
