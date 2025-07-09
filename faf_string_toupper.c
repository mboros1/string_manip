#include "faf_string_toupper.h"
#include "faf_string_mem.h"
#include <simde/x86/sse2.h>
#include <stdint.h>

faf_string* faf_string_toupper(pool_t pool, faf_string str) {
    faf_string* alloc = faf_string_alloc(pool);
    
    const char* data = str.start;
    alloc->start = (const char*)&mempools[pool * BLOCK_LEN + pool_idxs[pool]];
    
    // SIMD constants for uppercase conversion
    simde__m128i lower_a = simde_mm_set1_epi8('a' - 1);
    simde__m128i lower_z = simde_mm_set1_epi8('z' + 1);
    simde__m128i diff = simde_mm_set1_epi8('a' - 'A');
    
    // Process 16 bytes at a time
    for (; data + 16 <= str.end; data += 16) {
        // Load 16 characters
        simde__m128i chars = simde_mm_loadu_si128((const simde__m128i*)data);
        
        // Check if characters are in range 'a' to 'z'
        simde__m128i gt_lower = simde_mm_cmpgt_epi8(chars, lower_a);
        simde__m128i lt_upper = simde_mm_cmplt_epi8(chars, lower_z);
        simde__m128i mask = simde_mm_and_si128(gt_lower, lt_upper);
        
        // Apply conversion: subtract difference only for lowercase chars
        simde__m128i adjustment = simde_mm_and_si128(mask, diff);
        simde__m128i result_chars = simde_mm_sub_epi8(chars, adjustment);
        
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
        
        // Copy remaining characters and convert to uppercase
        for (int i = 0; i < remaining; ++i) {
            char c = data[i];
            buffer[i] = (c >= 'a' && c <= 'z') ? c - ('a' - 'A') : c;
        }
        
        simde__m128i chars = simde_mm_loadu_si128((const simde__m128i*)buffer);
        mempools[pool * BLOCK_LEN + idx].data = chars;
    }
    
    return alloc;
}