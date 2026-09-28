#include "faf_string_sort.h"
#include "faf_string_cmp.h"

faf_string faf_string_sort_chars(faf_region r, faf_string str) {
  size_t n = faf_string_len(str);
  faf_span sp = faf_reserve(r, faf_slots_for(n));
  if (!sp.ptr)
    return FAF_STRING_NONE;

  // not `= {0}`: the compiler would emit a libc memset/bzero call for it
  size_t counts[256];
  faf_memset(counts, 0, sizeof(counts));
  for (const char *p = str.start; p < str.end; ++p)
    counts[(unsigned char)*p]++;

  char *dst = (char *)sp.ptr, *out = dst;
  for (int c = 0; c < 256; ++c) {
    faf_memset(out, c, counts[c]);
    out += counts[c];
  }
  faf_memset(dst + n, 0, sp.slots * FAF_SLOT_BYTES - n);
  return (faf_string){.start = dst, .end = dst + n};
}

static inline void swap(faf_string *a, faf_string *b) {
  faf_string t = *a;
  *a = *b;
  *b = t;
}

static void insertion_sort(faf_string *a, size_t n) {
  for (size_t i = 1; i < n; ++i) {
    faf_string x = a[i];
    size_t j = i;
    for (; j > 0 && faf_string_cmp(a[j - 1], x) > 0; --j)
      a[j] = a[j - 1];
    a[j] = x;
  }
}

static void sift_down(faf_string *a, size_t root, size_t n) {
  for (;;) {
    size_t child = 2 * root + 1;
    if (child >= n)
      return;
    if (child + 1 < n && faf_string_cmp(a[child], a[child + 1]) < 0)
      ++child;
    if (faf_string_cmp(a[root], a[child]) >= 0)
      return;
    swap(&a[root], &a[child]);
    root = child;
  }
}

static void heap_sort(faf_string *a, size_t n) {
  for (size_t i = n / 2; i > 0; --i)
    sift_down(a, i - 1, n);
  for (size_t end = n; end > 1; --end) {
    swap(&a[0], &a[end - 1]);
    sift_down(a, 0, end - 1);
  }
}

// Quicksort with Hoare partitioning around the middle element; falls back to
// heapsort when recursion gets too deep, and insertion sort for small runs.
static void intro_sort(faf_string *a, size_t n, int depth) {
  while (n > 16) {
    if (depth-- == 0) {
      heap_sort(a, n);
      return;
    }
    // median of three into the middle, used as the pivot
    size_t mid = n / 2;
    if (faf_string_cmp(a[mid], a[0]) < 0)
      swap(&a[mid], &a[0]);
    if (faf_string_cmp(a[n - 1], a[mid]) < 0) {
      swap(&a[n - 1], &a[mid]);
      if (faf_string_cmp(a[mid], a[0]) < 0)
        swap(&a[mid], &a[0]);
    }
    faf_string pivot = a[mid];

    // Hoare: ends with [0, j] <= pivot <= [j + 1, n), and 0 <= j < n - 1
    size_t i = 0, j = n - 1;
    for (;;) {
      while (faf_string_cmp(a[i], pivot) < 0)
        ++i;
      while (faf_string_cmp(a[j], pivot) > 0)
        --j;
      if (i >= j)
        break;
      swap(&a[i], &a[j]);
      ++i;
      --j;
    }
    // recurse into the smaller side, loop on the larger
    size_t left = j + 1, right = n - left;
    if (left < right) {
      intro_sort(a, left, depth);
      a += left;
      n = right;
    } else {
      intro_sort(a + left, right, depth);
      n = left;
    }
  }
  insertion_sort(a, n);
}

void faf_string_arr_sort(faf_string_arr arr) {
  size_t n = (size_t)(arr.end - arr.start);
  int depth = 0;
  for (size_t m = n; m > 1; m >>= 1)
    depth += 2;
  intro_sort(arr.start, n, depth);
}
