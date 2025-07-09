#include "faf_string_reverse.h"
#include "faf_string_mem.h"
#include <simde/x86/sse2.h>
#include <simde/x86/ssse3.h>
#include <stdint.h>

faf_string* faf_string_reverse(pool_t pool, faf_string str) {
    size_t len = str.end - str.start;
    
    // Handle empty string
    if (len == 0) {
        faf_string* alloc = faf_string_alloc(pool);
        alloc->start = (const char*)&mempools[pool * BLOCK_LEN + pool_idxs[pool]];
        alloc->end = alloc->start;
        return alloc;
    }
    
    faf_string* alloc = faf_string_alloc(pool);
    alloc->start = (const char*)&mempools[pool * BLOCK_LEN + pool_idxs[pool]];
    
    const char* src = str.start;
    
    // Simple byte-by-byte reverse approach
    // We'll build the result directly in the pool, one byte at a time
    char current_chunk[16];
    int chunk_pos = 0;
    
    // Process source string in reverse order
    for (size_t i = 0; i < len; i++) {
        current_chunk[chunk_pos] = src[len - 1 - i];
        chunk_pos++;
        
        // When we have 16 bytes, store them in the pool
        if (chunk_pos == 16) {
            int idx = pool_idxs[pool]++;
            simde__m128i chars = simde_mm_loadu_si128((const simde__m128i*)current_chunk);
            mempools[pool * BLOCK_LEN + idx].data = chars;
            chunk_pos = 0;
        }
    }
    
    // Handle remaining bytes
    alloc->end = (const char*)(((uintptr_t)&mempools[pool * BLOCK_LEN + pool_idxs[pool]]) + chunk_pos);
    
    if (chunk_pos > 0) {
        int idx = pool_idxs[pool]++;
        char buffer[16] = {0};
        for (int i = 0; i < chunk_pos; ++i) {
            buffer[i] = current_chunk[i];
        }
        simde__m128i chars = simde_mm_loadu_si128((const simde__m128i*)buffer);
        mempools[pool * BLOCK_LEN + idx].data = chars;
    }
    
    return alloc;
}