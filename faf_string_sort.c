#include "faf_string_sort.h"
#include <simde/x86/sse2.h>
#include <stdint.h>
#include <stdbool.h>

// Threshold for switching to insertion sort
#define INSERTION_SORT_THRESHOLD 24

// Threshold for switching to heap sort
#define HEAP_SORT_THRESHOLD 80

// Maximum recursion depth before switching to heap sort
#define MAX_RECURSION_DEPTH 2 * sizeof(size_t) * 8

// SIMD-optimized string comparison
static int string_compare_simd(faf_string a, faf_string b) {
    size_t len_a = a.end - a.start;
    size_t len_b = b.end - b.start;
    size_t min_len = len_a < len_b ? len_a : len_b;
    
    const char* ptr_a = a.start;
    const char* ptr_b = b.start;
    
    // Compare 16 bytes at a time using SIMD
    while (min_len >= 16) {
        simde__m128i chunk_a = simde_mm_loadu_si128((const simde__m128i*)ptr_a);
        simde__m128i chunk_b = simde_mm_loadu_si128((const simde__m128i*)ptr_b);
        
        // Compare chunks
        simde__m128i eq = simde_mm_cmpeq_epi8(chunk_a, chunk_b);
        int mask = simde_mm_movemask_epi8(eq);
        
        if (mask != 0xFFFF) {
            // Found difference, find exact position
            for (int i = 0; i < 16; i++) {
                unsigned char ca = (unsigned char)ptr_a[i];
                unsigned char cb = (unsigned char)ptr_b[i];
                if (ca != cb) {
                    return ca < cb ? -1 : 1;
                }
            }
        }
        
        ptr_a += 16;
        ptr_b += 16;
        min_len -= 16;
    }
    
    // Compare remaining bytes
    while (min_len > 0) {
        unsigned char ca = (unsigned char)*ptr_a;
        unsigned char cb = (unsigned char)*ptr_b;
        if (ca != cb) {
            return ca < cb ? -1 : 1;
        }
        ptr_a++;
        ptr_b++;
        min_len--;
    }
    
    // All compared bytes are equal, compare lengths
    if (len_a < len_b) return -1;
    if (len_a > len_b) return 1;
    return 0;
}

// Swap two strings
static void swap_strings(faf_string* a, faf_string* b) {
    faf_string temp = *a;
    *a = *b;
    *b = temp;
}

// Insertion sort for small arrays
static void insertion_sort(faf_string* strings, size_t count) {
    for (size_t i = 1; i < count; i++) {
        faf_string key = strings[i];
        size_t j = i;
        
        while (j > 0 && string_compare_simd(strings[j - 1], key) > 0) {
            strings[j] = strings[j - 1];
            j--;
        }
        strings[j] = key;
    }
}

// Heapify function for heap sort
static void heapify(faf_string* strings, size_t count, size_t root) {
    size_t largest = root;
    size_t left = 2 * root + 1;
    size_t right = 2 * root + 2;
    
    if (left < count && string_compare_simd(strings[left], strings[largest]) > 0) {
        largest = left;
    }
    
    if (right < count && string_compare_simd(strings[right], strings[largest]) > 0) {
        largest = right;
    }
    
    if (largest != root) {
        swap_strings(&strings[root], &strings[largest]);
        heapify(strings, count, largest);
    }
}

// Heap sort implementation
static void heap_sort(faf_string* strings, size_t count) {
    // Build heap
    for (size_t i = count / 2; i > 0; i--) {
        heapify(strings, count, i - 1);
    }
    
    // Extract elements from heap
    for (size_t i = count - 1; i > 0; i--) {
        swap_strings(&strings[0], &strings[i]);
        heapify(strings, i, 0);
    }
}

// Choose pivot using median-of-three
static size_t choose_pivot(faf_string* strings, size_t left, size_t right) {
    size_t mid = left + (right - left) / 2;
    
    if (string_compare_simd(strings[mid], strings[left]) < 0) {
        swap_strings(&strings[left], &strings[mid]);
    }
    if (string_compare_simd(strings[right], strings[left]) < 0) {
        swap_strings(&strings[left], &strings[right]);
    }
    if (string_compare_simd(strings[right], strings[mid]) < 0) {
        swap_strings(&strings[mid], &strings[right]);
    }
    
    return mid;
}

// Partition function for quicksort
static size_t partition(faf_string* strings, size_t left, size_t right) {
    size_t pivot_idx = choose_pivot(strings, left, right);
    faf_string pivot = strings[pivot_idx];
    
    // Move pivot to end
    swap_strings(&strings[pivot_idx], &strings[right]);
    
    size_t i = left;
    for (size_t j = left; j < right; j++) {
        if (string_compare_simd(strings[j], pivot) < 0) {
            swap_strings(&strings[i], &strings[j]);
            i++;
        }
    }
    
    swap_strings(&strings[i], &strings[right]);
    return i;
}

// Check if array is nearly sorted
static bool is_nearly_sorted(faf_string* strings, size_t count) {
    if (count <= 1) return true;
    
    size_t inversions = 0;
    for (size_t i = 1; i < count; i++) {
        if (string_compare_simd(strings[i - 1], strings[i]) > 0) {
            inversions++;
            if (inversions > count / 8) {
                return false;
            }
        }
    }
    return true;
}

// Main PDQSort implementation
static void pdqsort_impl(faf_string* strings, size_t left, size_t right, int depth) {
    while (left < right) {
        size_t count = right - left + 1;
        
        // Use insertion sort for small arrays
        if (count <= INSERTION_SORT_THRESHOLD) {
            insertion_sort(strings + left, count);
            return;
        }
        
        // Use heap sort if too deep or array is large
        if (depth == 0 || count > HEAP_SORT_THRESHOLD) {
            heap_sort(strings + left, count);
            return;
        }
        
        // Check if array is nearly sorted
        if (is_nearly_sorted(strings + left, count)) {
            insertion_sort(strings + left, count);
            return;
        }
        
        // Partition array
        size_t pivot = partition(strings, left, right);
        
        // Recursively sort smaller partition first to limit stack depth
        if (pivot - left < right - pivot) {
            pdqsort_impl(strings, left, pivot - 1, depth - 1);
            left = pivot + 1;
        } else {
            pdqsort_impl(strings, pivot + 1, right, depth - 1);
            right = pivot - 1;
        }
    }
}

// Public sort function
void faf_string_sort(faf_string* strings, size_t count) {
    if (count <= 1) return;
    
    pdqsort_impl(strings, 0, count - 1, MAX_RECURSION_DEPTH);
}