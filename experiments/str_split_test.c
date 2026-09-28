#include <simde/x86/sse2.h>
#include <stdio.h>
#include <time.h>

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

void find_char_indexes(const char *str, char target, int *indexes, int *count) {
  simde__m128i target_vector = simde_mm_set1_epi8(target);
  *count = 0;

  int len = strlen_sse2(str);
  for (int i = 0; i < len; i += 32) {
    simde__m128i chunk = simde_mm_loadu_si128((simde__m128i *)&str[i]);
    simde__m128i cmp = simde_mm_cmpeq_epi8(chunk, target_vector);
    int mask = simde_mm_movemask_epi8(cmp);

    while (mask) {
      int pos = __builtin_ffs(mask) - 1;
      indexes[(*count)++] = i + pos;
      mask &= mask - 1; // clear the least significant bit set
    }
  }
}

double benchmark(const char *filename,
                 void (*func)(const char *, char, int *, int *)) {
  FILE *file = fopen(filename, "rb");
  if (!file) {
    perror("Unable to open file");
    exit(EXIT_FAILURE);
  }

  char *line = NULL;
  size_t len = 0;
  ssize_t read;
  int indexes[4096];
  volatile int total_count = 0;
  int count = 0;
  clock_t start = clock();

  while ((read = getline(&line, &len, file)) != -1) {
    func(line, 'a', indexes, &count);
    total_count += count;
    printf("Line: %s\n", line);
    printf("Indexes of 'a': [");
    for (int i = 0; i < count; ++i) {
      printf("%d, ", indexes[i]);
    }
    printf("]\n");
    printf("Count of 'a': %d\n", count);
  }

  clock_t end = clock();
  double elapsed = (double)(end - start) / CLOCKS_PER_SEC;
    printf("Processing time: %f s\n", elapsed);

  free(line);
  fclose(file);
  return elapsed;
}

int main(int argc, char *argv[]) {
  const char *filename = argv[1];
  int results[100];
  int accum = 0;
  for (int i = 0; i < 1; ++i) {
    double elapsed_time = benchmark(filename, find_char_indexes);
    results[i] = elapsed_time;
    accum += elapsed_time;
  }

  printf("Average processing time: %f s\n", accum / 100.0);

  return 0;
}
