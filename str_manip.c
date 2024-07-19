#include <simde/x86/sse2.h>
#include <simde/arm/neon.h>
#include <simde/arm/sve.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define N 10

typedef struct results {
  double standard[N];
  double sse2[N];
  double stdlib[N];
  double neon[N];
  double standard_avg;
  double sse2_avg;
  double stdlib_avg;
  double neon_avg;
} results;

results res;

size_t strlen_standard(const char *str) {
  const char *s = str;
  while (*s != '\0') {
    ++s;
  }
  return s - str;
}

size_t strlen_sse2(const char *str) {
  const char *start = str;
  simde__m128i zero = simde_mm_setzero_si128(); // Vector of all zeros

  // Process 16 bytes at a time
  while (1) {
    // Load 16 bytes from the string
    simde__m128i chars = simde_mm_loadu_si128((simde__m128i *)str);

    // Compare the loaded bytes with zero
    simde__m128i cmp = simde_mm_cmpeq_epi8(chars, zero);

    // Create a mask from the comparison result
    int mask = simde_mm_movemask_epi8(cmp);

    // Check if any of the 16 bytes were zero
    if (mask != 0) {
      // Find the position of the first zero byte
      return (str - start) + __builtin_ctz(mask);
    }

    // Move to the next 16-byte block
    str += 16;
  }
}

// size_t strlen_sve(const char *str) {
//   const char *start = str;
//   simde_svbool_t pg = simde_svptrue_b8(); // Predicate for all active elements
//   simde_svuint8_t zero = simde_svdup_u8(0); // Vector of all zeros
//
//   // Process in chunks of the SVE vector length
//   while (1) {
//     // Load bytes from the string
//     simde_svuint8_t chars = simde_svld1(pg, (const uint8_t *)str);
//
//     // Compare the loaded bytes with zero
//     uint32_t cmp = simde_vcltq_u8(pg, chars, zero);
//
//     // Create a mask from the comparison result
//     int mask = simde_svcntb(cmp);
//
//     // Check if any of the bytes were zero
//     if (mask != 0) {
//       // Find the position of the first zero byte
//       return (str - start) + smde_svcnt_u8() - mask;
//     }
//
//     // Move to the next chunk
//     str += svcnt_u8();
//   }
// }

uint32_t _mm_movemask_aarch64(simde_uint8x16_t input)
{   
    const uint8_t __attribute__ ((aligned (16))) ucShift[] = {-7,-6,-5,-4,-3,-2,-1,0,-7,-6,-5,-4,-3,-2,-1,0};
    simde_uint8x16_t vshift = simde_vld1q_u8(ucShift);
    simde_uint8x16_t vmask = simde_vandq_u8(input, simde_vdupq_n_u8(0x80));
    uint32_t out;
    
    vmask = simde_vshlq_u8(vmask, vshift);
    out = simde_vaddv_u8(simde_vget_low_u8(vmask));
    out += (simde_vaddv_u8(simde_vget_high_u8(vmask)) << 8);
    
    return out;
}


/*
 * simde_mm_movemask_epi8 implementation for aarch64:
 *
      static const uint8_t md[16] = {
        1 << 0, 1 << 1, 1 << 2, 1 << 3,
        1 << 4, 1 << 5, 1 << 6, 1 << 7,
        1 << 0, 1 << 1, 1 << 2, 1 << 3,
        1 << 4, 1 << 5, 1 << 6, 1 << 7,
      };

       Extend sign bit over entire lane 
      uint8x16_t extended = vreinterpretq_u8_s8(vshrq_n_s8(a_.neon_i8, 7));
      // Clear all but the bit we're interested in. 
      uint8x16_t masked = vandq_u8(vld1q_u8(md), extended);
      // Alternate bytes from low half and high half
      uint8x8x2_t tmp = vzip_u8(vget_low_u8(masked), vget_high_u8(masked));
      uint16x8_t x = vreinterpretq_u16_u8(vcombine_u8(tmp.val[0], tmp.val[1]));
      #if defined(SIMDE_ARM_NEON_A64V8_NATIVE)
        r = vaddvq_u16(x);
      #else
        uint64x2_t t64 = vpaddlq_u32(vpaddlq_u16(x));
        r =
          HEDLEY_STATIC_CAST(int32_t, vgetq_lane_u64(t64, 0)) +
          HEDLEY_STATIC_CAST(int32_t, vgetq_lane_u64(t64, 1));
*/

size_t strlen_neon(const char *str) {
  const char *start = str;
  simde_uint8x16_t zero = simde_vdupq_n_u8(0); // Vector of all zeros

  // Process 16 bytes at a time
  while (1) {
    // Load 16 bytes from the string
    simde_uint8x16_t chars = simde_vld1q_u8((const uint8_t*)str);

    // Compare the loaded bytes with zero
    simde_uint8x16_t cmp = simde_vceqq_u8(chars, zero);

    // Create a mask from the comparison result
    int mask = _mm_movemask_aarch64(cmp);

    // Check if any of the 16 bytes were zero
    if (mask != 0) {
      // Find the position of the first zero byte
      return (str - start) + __builtin_ctz(mask);
    }

    // Move to the next 16-byte block
    str += 16;
  }
}

typedef simde__m128i op_t;
typedef simde__m128i find_t;

// Helper function to repeat a byte value across all bytes of a vector
static inline op_t repeat_bytes(uint8_t byte) {
  return simde_mm_set1_epi8(byte);
}

// Unsigned saturated subtraction from 1 in each byte
static inline find_t find_zero_all(op_t x) {
  op_t ones = repeat_bytes(1);                   // Set all bytes to 1
  op_t subtracted = simde_mm_subs_epu8(ones, x); // Saturating subtract from 1
  return subtracted;
}

size_t strlen_simd2(const char *str) {
  const char *start = str;
  const op_t zero = simde_mm_setzero_si128();

  while (1) {
    op_t chars = simde_mm_loadu_si128((op_t *)str);
    find_t cmp = find_zero_all(chars);

    int mask = simde_mm_movemask_epi8(cmp);
    if (mask != 0) {
      return (str - start) + __builtin_ctz(mask);
    }

    str += 16;
  }
}

double benchmark_strlen(const char *filename,
                        size_t (*strlen_func)(const char *),
                        const char *func_name) {
  FILE *file = fopen(filename, "rb");
  if (!file) {
    perror("Unable to open file");
    exit(EXIT_FAILURE);
  }

  char buffer[256];
  volatile size_t total_length = 0;
  clock_t start = clock();

  while (fread(buffer, sizeof(char), sizeof(buffer), file)) {
    for (size_t i = 0; i < sizeof(buffer); i++) {
      if (buffer[i] == '\0') {
        total_length += strlen_func(buffer + i + 1);
      }
    }
  }

  clock_t end = clock();
  double elapsed = (double)(end - start) / CLOCKS_PER_SEC;

  // printf("Total length computed by %s: %zu\n", func_name, total_length);
  // printf("Time taken by %s: %.6f seconds\n", func_name, elapsed);

  fclose(file);
  return elapsed;
}

int main(int argc, char *argv[]) {
  if (argc != 2) {
    fprintf(stderr, "Usage: %s <input_file>\n", argv[0]);
    return EXIT_FAILURE;
  }

  memset(&res, 0, sizeof(res));

  const char *filename = argv[1];

  for (int i = 0; i < N; ++i) {
    res.standard[i] =
        benchmark_strlen(filename, strlen_standard, "my standard strlen");
    res.standard_avg += res.standard[i];
    res.stdlib[i] =
        benchmark_strlen(filename, strlen, "standard library strlen");
    res.stdlib_avg += res.stdlib[i];
    res.sse2[i] =
        benchmark_strlen(filename, strlen_sse2, "SSE2 optimized strlen");
    res.sse2_avg += res.sse2[i];
    res.neon[i] =
        benchmark_strlen(filename, strlen_neon, "SSE2 optimized strlen");
    res.neon_avg += res.neon[i];
  }

  printf("Final results...\n\n\n");
  printf("Naive implementation average time: %f s\n", res.standard_avg / N);
  printf("Standard library average time: %f s\n", res.stdlib_avg / N);
  printf("SSE2 average time: %f s\n", res.sse2_avg / N);
  printf("Neon average time: %f s\n", res.neon_avg / N);

  printf("\n\n\n");

  printf("Change the order....\n");

  res.sse2_avg = 0;
  res.standard_avg = 0;
  res.stdlib_avg = 0;
  res.neon_avg = 0;

  for (int i = 0; i < N; ++i) {
    res.sse2[i] =
        benchmark_strlen(filename, strlen_sse2, "SSE2 optimized strlen");
    res.neon[i] =
        benchmark_strlen(filename, strlen_neon, "SSE2 optimized strlen");
    res.neon_avg += res.neon[i];
    res.sse2_avg += res.sse2[i];
    res.standard[i] =
        benchmark_strlen(filename, strlen_standard, "my standard strlen");
    res.standard_avg += res.standard[i];
    res.stdlib[i] =
        benchmark_strlen(filename, strlen, "standard library strlen");
    res.stdlib_avg += res.stdlib[i];
  }

  printf("Final results...\n\n\n");
  printf("Naive implementation average time: %f s\n", res.standard_avg / N);
  printf("Standard library average time: %f s\n", res.stdlib_avg / N);
  printf("SSE2 average time: %f s\n", res.sse2_avg / N);
  printf("Neon average time: %f s\n", res.neon_avg / N);

  printf("\n\n\n");

  printf("Change the order....\n");

  res.sse2_avg = 0;
  res.standard_avg = 0;
  res.stdlib_avg = 0;
  res.neon_avg = 0;

  for (int i = 0; i < N; ++i) {
    res.neon[i] =
        benchmark_strlen(filename, strlen_neon, "SSE2 optimized strlen");
    res.neon_avg += res.neon[i];
    res.stdlib[i] =
        benchmark_strlen(filename, strlen, "standard library strlen");
    res.stdlib_avg += res.stdlib[i];
    res.sse2[i] =
        benchmark_strlen(filename, strlen_sse2, "SSE2 optimized strlen");
    res.sse2_avg += res.sse2[i];
    res.standard[i] =
        benchmark_strlen(filename, strlen_standard, "my standard strlen");
    res.standard_avg += res.standard[i];
  }

  printf("Final results...\n\n\n");
  printf("Naive implementation average time: %f s\n", res.standard_avg / N);
  printf("Standard library average time: %f s\n", res.stdlib_avg / N);
  printf("SSE2 average time: %f s\n", res.sse2_avg / N);
  printf("Neon average time: %f s\n", res.neon_avg / N);

  return 0;
}
