#include "faf_string_tolower.h"
#include "faf_string_mem.h"
#include <simde/x86/sse2.h>
#include <stdint.h>

faf_string* faf_string_tolower(pool_t pool, faf_string str) {
    faf_string* alloc = faf_string_alloc(pool);
    
    const char* data = str.start;
    alloc->start = (const char*)&mempools[pool * BLOCK_LEN + pool_idxs[pool]];
    
    // SIMD constants for lowercase conversion
    simde__m128i upper_A = simde_mm_set1_epi8('A' - 1);
    simde__m128i upper_Z = simde_mm_set1_epi8('Z' + 1);
    simde__m128i diff = simde_mm_set1_epi8('a' - 'A');
    
    // Process 16 bytes at a time
    for (; data + 16 <= str.end; data += 16) {
        // Load 16 characters
        simde__m128i chars = simde_mm_loadu_si128((const simde__m128i*)data);
        
        // Check if characters are in range 'A' to 'Z'
        simde__m128i gt_upper = simde_mm_cmpgt_epi8(chars, upper_A);
        simde__m128i lt_lower = simde_mm_cmplt_epi8(chars, upper_Z);
        simde__m128i mask = simde_mm_and_si128(gt_upper, lt_lower);
        
        // Apply conversion: add difference only for uppercase chars
        simde__m128i adjustment = simde_mm_and_si128(mask, diff);
        simde__m128i result_chars = simde_mm_add_epi8(chars, adjustment);
        
        // Store result in pool
        int idx = pool_idxs[pool]++;
        mempools[pool * BLOCK_LEN + idx].data = result_chars;
    }
    
    // Handle remaining bytes
    int remaining = str.end - data;
    alloc->end = (const char*)(((uintptr_t)&mempools[pool * BLOCK_LEN + pool_idxs[pool]]) + remaining);
    
    if (remaining > 0) {
        int idx = pool_idxs[pool]++;
        char buffer[16] = {0};
        
        // Copy remaining characters and convert to lowercase
        for (int i = 0; i < remaining; ++i) {
            char c = data[i];
            buffer[i] = (c >= 'A' && c <= 'Z') ? c + ('a' - 'A') : c;
        }
        
        simde__m128i chars = simde_mm_loadu_si128((const simde__m128i*)buffer);
        mempools[pool * BLOCK_LEN + idx].data = chars;
    }
    
    return alloc;
}