#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#define INSERTION_SORT_THRESHOLD 24
#define NINTHER_THRESHOLD 128
#define PARTIAL_INSERTION_SORT_LIMIT 8
#define BLOCK_SIZE 64
#define CACHELINE_SIZE 64

int log2_int(int n) {
  int log = 0;
  while (n >>= 1)
    ++log;
  return log;
}

void insertion_sort(int *begin, int *end) {
  if (begin == end)
    return;

  for (int *cur = begin + 1; cur != end; ++cur) {
    int *sift = cur;
    int *sift_1 = cur - 1;

    if (*sift < *sift_1) {
      int tmp = *sift;

      do {
        *sift-- = *sift_1;
      } while (sift != begin && tmp < *(--sift_1));

      *sift = tmp;
    }
  }
}

void unguarded_insertion_sort(int *begin, int *end) {
  if (begin == end)
    return;

  for (int *cur = begin + 1; cur != end; ++cur) {
    int *sift = cur;
    int *sift_1 = cur - 1;

    if (*sift < *sift_1) {
      int tmp = *sift;

      do {
        *sift-- = *sift_1;
      } while (tmp < *(--sift_1));

      *sift = tmp;
    }
  }
}

bool partial_insertion_sort(int *begin, int *end) {
  if (begin == end)
    return true;

  size_t limit = 0;
  for (int *cur = begin + 1; cur != end; ++cur) {
    int *sift = cur;
    int *sift_1 = cur - 1;

    if (*sift < *sift_1) {
      int tmp = *sift;

      do {
        *sift-- = *sift_1;
      } while (sift != begin && tmp < *(--sift_1));

      *sift = tmp;
      limit += cur - sift;
    }

    if (limit > PARTIAL_INSERTION_SORT_LIMIT)
      return false;
  }

  return true;
}

void sort2(int *a, int *b) {
  if (*b < *a) {
    int temp = *a;
    *a = *b;
    *b = temp;
  }
}

void sort3(int *a, int *b, int *c) {
  sort2(a, b);
  sort2(b, c);
  sort2(a, b);
}

int *align_cacheline(int *p) {
  size_t ip = (size_t)p;
  ip = (ip + CACHELINE_SIZE - 1) & -CACHELINE_SIZE;
  return (int *)ip;
}

void swap_offsets(int *first, int *last, int *offsets_l, int *offsets_r,
                  size_t num, bool use_swaps) {
  if (use_swaps) {
    for (size_t i = 0; i < num; ++i) {
      int temp = first[offsets_l[i]];
      first[offsets_l[i]] = last[-offsets_r[i]];
      last[-offsets_r[i]] = temp;
    }
  } else if (num > 0) {
    int *l = first + offsets_l[0];
    int *r = last - offsets_r[0];
    int tmp = *l;
    *l = *r;
    for (size_t i = 1; i < num; ++i) {
      l = first + offsets_l[i];
      *r = *l;
      r = last - offsets_r[i];
      *l = *r;
    }
    *r = tmp;
  }
}

typedef struct {
  int *first;
  bool second;
} pair;


pair partition_right(int *begin, int *end) {
  int pivot = *begin;

  int *first = begin;
  int *last = end;

  while (*(++first) < pivot)
    ;

  if (first - 1 == begin)
    while (first < last && !(*(--last) < pivot))
      ;
  else
    while (!(*(--last) < pivot))
      ;

  bool already_partitioned = first >= last;

  while (first < last) {
    int temp = *first;
    *first = *last;
    *last = temp;
    while (*(++first) < pivot)
      ;
    while (!(*(--last) < pivot))
      ;
  }

  int *pivot_pos = first - 1;
  *begin = *pivot_pos;
  *pivot_pos = pivot;

  pair result = {.first = pivot_pos, .second = already_partitioned};
  return result;
}

int *partition_left(int *begin, int *end) {
  int pivot = *begin;
  int *first = begin;
  int *last = end;

  while (pivot < *(--last))
    ;

  if (last + 1 == end)
    while (first < last && !(pivot < *(++first)))
      ;
  else
    while (!(pivot < *(++first)))
      ;

  while (first < last) {
    int temp = *first;
    *first = *last;
    *last = temp;
    while (pivot < *(--last))
      ;
    while (!(pivot < *(++first)))
      ;
  }

  int *pivot_pos = last;
  *begin = *pivot_pos;
  *pivot_pos = pivot;

  return pivot_pos;
}

void pdqsort_loop(int *begin, int *end, int bad_allowed, bool leftmost) {
  while (true) {
    size_t size = end - begin;

    if (size < INSERTION_SORT_THRESHOLD) {
      if (leftmost)
        insertion_sort(begin, end);
      else
        unguarded_insertion_sort(begin, end);
      return;
    }

    size_t s2 = size / 2;
    if (size > NINTHER_THRESHOLD) {
      sort3(begin, begin + s2, end - 1);
      sort3(begin + 1, begin + (s2 - 1), end - 2);
      sort3(begin + 2, begin + (s2 + 1), end - 3);
      sort3(begin + (s2 - 1), begin + s2, begin + (s2 + 1));
      int temp = *begin;
      *begin = *(begin + s2);
      *(begin + s2) = temp;
    } else
      sort3(begin + s2, begin, end - 1);

    if (!leftmost && !(*(begin - 1) < *begin)) {
      begin = partition_left(begin, end) + 1;
      continue;
    }

    pair part_result = partition_right(begin, end);
    int *pivot_pos = part_result.first;
    bool already_partitioned = part_result.second;

    size_t l_size = pivot_pos - begin;
    size_t r_size = end - (pivot_pos + 1);
    bool highly_unbalanced = l_size < size / 8 || r_size < size / 8;

    if (highly_unbalanced) {
      if (--bad_allowed == 0) {
        qsort(begin, end - begin, sizeof(int),
              (int (*)(const void *, const void *))strcmp);
        return;
      }

      if (l_size >= INSERTION_SORT_THRESHOLD) {
        int temp = *begin;
        *begin = *(begin + l_size / 4);
        *(begin + l_size / 4) = temp;
        temp = *(pivot_pos - 1);
        *(pivot_pos - 1) = *(pivot_pos - l_size / 4);
        *(pivot_pos - l_size / 4) = temp;

        if (l_size > NINTHER_THRESHOLD) {
          temp = *(begin + 1);
          *(begin + 1) = *(begin + (l_size / 4 + 1));
          *(begin + (l_size / 4 + 1)) = temp;
          temp = *(begin + 2);
          *(begin + 2) = *(begin + (l_size / 4 + 2));
          *(begin + (l_size / 4 + 2)) = temp;
          temp = *(pivot_pos - 2);
          *(pivot_pos - 2) = *(pivot_pos - (l_size / 4 + 1));
          *(pivot_pos - (l_size / 4 + 1)) = temp;
          temp = *(pivot_pos - 3);
          *(pivot_pos - 3) = *(pivot_pos - (l_size / 4 + 2));
          *(pivot_pos - (l_size / 4 + 2)) = temp;
        }
      }

      if (r_size >= INSERTION_SORT_THRESHOLD) {
        int temp = *(pivot_pos + 1);
        *(pivot_pos + 1) = *(pivot_pos + (1 + r_size / 4));
        *(pivot_pos + (1 + r_size / 4)) = temp;
        temp = *(end - 1);
        *(end - 1) = *(end - r_size / 4);
        *(end - r_size / 4) = temp;

        if (r_size > NINTHER_THRESHOLD) {
          temp = *(pivot_pos + 2);
          *(pivot_pos + 2) = *(pivot_pos + (2 + r_size / 4));
          *(pivot_pos + (2 + r_size / 4)) = temp;
          temp = *(pivot_pos + 3);
          *(pivot_pos + 3) = *(pivot_pos + (3 + r_size / 4));
          *(pivot_pos + (3 + r_size / 4)) = temp;
          temp = *(end - 2);
          *(end - 2) = *(end - (1 + r_size / 4));
          *(end - (1 + r_size / 4)) = temp;
          temp = *(end - 3);
          *(end - 3) = *(end - (2 + r_size / 4));
          *(end - (2 + r_size / 4)) = temp;
        }
      }
    } else {
      if (already_partitioned && partial_insertion_sort(begin, pivot_pos) &&
          partial_insertion_sort(pivot_pos + 1, end))
        return;
    }

    pdqsort_loop(begin, pivot_pos, bad_allowed, leftmost);
    begin = pivot_pos + 1;
    leftmost = false;
  }
}

void pdqsort(int *begin, int *end) {
  if (begin == end)
    return;

  pdqsort_loop(begin, end, log2_int(end - begin), true);
}

bool is_sorted(int *arr, int size) {
  for (int i = 1; i < size; ++i) {
    if (arr[i - 1] > arr[i]) {
      return false;
    }
  }
  return true;
}

void print_array(int *arr, int size) {
  for (int i = 0; i < size; ++i) {
    printf("%d ", arr[i]);
  }
  printf("\n");
}

int main() {
    const int array_size = 1000000;
    const int iterations = 10;
    int **arrays = malloc(iterations * sizeof(int *));

    srand((unsigned int)time(NULL));

    for (int i = 0; i < iterations; ++i) {
        arrays[i] = malloc(array_size * sizeof(int));
        for (int j = 0; j < array_size; ++j) {
            arrays[i][j] = rand() % 1000000;
        }
    }

    struct timeval start, end;
    gettimeofday(&start, NULL);

    for (int i = 0; i < iterations; ++i) {
        pdqsort(arrays[i], arrays[i] + array_size);
    }

    gettimeofday(&end, NULL);

    double elapsed = (end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec) / 1000000.0;

    printf("pdqsort execution time: %.6f seconds for 10 iterations\n", elapsed);
    for (int i = 0; i < iterations; ++i) {
        if (!is_sorted(arrays[i], array_size)) {
            printf("Array %d is not sorted!\n", i);
        } else {
            printf("Array %d is sorted.\n", i);
        }
    }
    for (int i = 0; i < iterations; ++i) {
        free(arrays[i]);
    }
    free(arrays);

    return 0;
}
