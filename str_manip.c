#include <simde/x86/sse2.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

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

void benchmark_strlen(const char *filename, size_t (*strlen_func)(const char *),
                      const char *func_name) {
  FILE *file = fopen(filename, "rb");
  if (!file) {
    perror("Unable to open file");
    exit(EXIT_FAILURE);
  }

  char buffer[256];
  size_t total_length = 0;
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

  printf("Total length computed by %s: %zu\n", func_name, total_length);
  printf("Time taken by %s: %.6f seconds\n", func_name, elapsed);

  fclose(file);
}

int main(int argc, char *argv[]) {
  if (argc != 2) {
    fprintf(stderr, "Usage: %s <input_file>\n", argv[0]);
    return EXIT_FAILURE;
  }

  const char *filename = argv[1];

  for (int i = 0; i < 10; ++i) {
    benchmark_strlen(filename, strlen_standard, "standard strlen");
    benchmark_strlen(filename, strlen_sse2, "SSE2 optimized strlen");
  }

  printf("Reverse the order....\n");

  for (int i = 0; i < 10; ++i) {
    benchmark_strlen(filename, strlen_sse2, "SSE2 optimized strlen");
    benchmark_strlen(filename, strlen_standard, "standard strlen");
  }


  return 0;
}
