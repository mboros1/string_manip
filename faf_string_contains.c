#include "faf_string_contains.h"
#include <simde/x86/sse2.h>
#include <stdint.h>

// Helper function to find first character matches using SIMD
static const char* find_first_char_simd(const char* haystack, const char* hay_end, char needle_first) {
    const char* current = haystack;
    simde__m128i needle_vec = simde_mm_set1_epi8(needle_first);
    
    // Process 16 bytes at a time
    while (current + 16 <= hay_end) {
        simde__m128i hay_vec = simde_mm_loadu_si128((const simde__m128i*)current);
        simde__m128i mask = simde_mm_cmpeq_epi8(hay_vec, needle_vec);
        int result = simde_mm_movemask_epi8(mask);
        
        if (result != 0) {
            // Found potential match, find exact position
            for (int i = 0; i < 16; i++) {
                if (current[i] == needle_first) {
                    return current + i;
                }
            }
        }
        current += 16;
    }
    
    // Handle remaining bytes
    while (current < hay_end) {
        if (*current == needle_first) {
            return current;
        }
        current++;
    }
    
    return NULL;
}

// Simple string comparison
static bool string_equal(const char* s1, const char* s2, size_t len) {
    for (size_t i = 0; i < len; i++) {
        if (s1[i] != s2[i]) {
            return false;
        }
    }
    return true;
}

// Compute failure function for needle (KMP preprocessing)
static void compute_failure_function(const char* needle, size_t needle_len, int* failure) {
    failure[0] = 0;
    int j = 0;
    
    for (size_t i = 1; i < needle_len; i++) {
        while (j > 0 && needle[i] != needle[j]) {
            j = failure[j - 1];
        }
        if (needle[i] == needle[j]) {
            j++;
        }
        failure[i] = j;
    }
}

// Two-Way algorithm implementation with SIMD optimization
bool faf_string_contains(faf_string haystack, faf_string needle) {
    size_t hay_len = haystack.end - haystack.start;
    size_t needle_len = needle.end - needle.start;
    
    // Handle edge cases
    if (needle_len == 0) return true;  // Empty needle is always found
    if (needle_len > hay_len) return false;  // Needle longer than haystack
    
    const char* hay_start = haystack.start;
    const char* hay_end = haystack.end;
    const char* needle_start = needle.start;
    
    // For very short needles, use simple approach
    if (needle_len == 1) {
        return find_first_char_simd(hay_start, hay_end, needle_start[0]) != NULL;
    }
    
    // For longer needles, use Two-Way algorithm with SIMD for first character
    char needle_first = needle_start[0];
    const char* hay_pos = hay_start;
    
    // Compute failure function for KMP-like behavior
    int failure[needle_len];
    compute_failure_function(needle_start, needle_len, failure);
    
    while (hay_pos <= hay_end - needle_len) {
        // Use SIMD to find next occurrence of first character
        const char* first_match = find_first_char_simd(hay_pos, hay_end - needle_len + 1, needle_first);
        
        if (first_match == NULL) {
            return false;  // No more potential matches
        }
        
        // Check if full needle matches at this position
        if (string_equal(first_match, needle_start, needle_len)) {
            return true;
        }
        
        // Move to next position using failure function for efficiency
        size_t match_len = 0;
        while (match_len < needle_len && first_match[match_len] == needle_start[match_len]) {
            match_len++;
        }
        
        if (match_len == 0) {
            hay_pos = first_match + 1;
        } else {
            // Use failure function to determine next position
            int skip = match_len - failure[match_len - 1];
            hay_pos = first_match + skip;
        }
    }
    
    return false;
}