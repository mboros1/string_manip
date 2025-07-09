#include "faf_string_trim.h"
#include "faf_string_mem.h"
#include <simde/x86/sse2.h>
#include <stdint.h>

// Helper function to create whitespace mask for 16 bytes
static simde__m128i create_whitespace_mask(simde__m128i chars) {
    simde__m128i spaces = simde_mm_set1_epi8(' ');
    simde__m128i tabs = simde_mm_set1_epi8('\t');
    simde__m128i newlines = simde_mm_set1_epi8('\n');
    simde__m128i returns = simde_mm_set1_epi8('\r');
    
    simde__m128i mask = simde_mm_or_si128(
        simde_mm_cmpeq_epi8(chars, spaces),
        simde_mm_or_si128(
            simde_mm_cmpeq_epi8(chars, tabs),
            simde_mm_or_si128(
                simde_mm_cmpeq_epi8(chars, newlines),
                simde_mm_cmpeq_epi8(chars, returns)
            )
        )
    );
    
    return mask;
}

// Helper function to check if character is whitespace
static int is_whitespace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

// Find first non-whitespace character from start
static const char* find_first_non_whitespace(const char* start, const char* end) {
    const char* current = start;
    
    // Process 16 bytes at a time
    while (current + 16 <= end) {
        simde__m128i chars = simde_mm_loadu_si128((const simde__m128i*)current);
        simde__m128i mask = create_whitespace_mask(chars);
        
        // Check if any byte is NOT whitespace (inverted mask)
        simde__m128i non_whitespace = simde_mm_xor_si128(mask, simde_mm_set1_epi8(0xFF));
        int result = simde_mm_movemask_epi8(non_whitespace);
        
        if (result != 0) {
            // Found non-whitespace, find exact position
            for (int i = 0; i < 16; i++) {
                if (!is_whitespace(current[i])) {
                    return current + i;
                }
            }
        }
        current += 16;
    }
    
    // Handle remaining bytes
    while (current < end && is_whitespace(*current)) {
        current++;
    }
    
    return current;
}

// Find last non-whitespace character from end
static const char* find_last_non_whitespace(const char* start, const char* end) {
    const char* current = end - 1;
    
    // Process backwards byte by byte (SIMD reverse processing is complex)
    while (current >= start && is_whitespace(*current)) {
        current--;
    }
    
    return current + 1; // Return one past the last non-whitespace
}

faf_string* faf_string_ltrim(pool_t pool, faf_string str) {
    const char* trimmed_start = find_first_non_whitespace(str.start, str.end);
    
    if (trimmed_start >= str.end) {
        // All whitespace, return empty string
        faf_string* alloc = faf_string_alloc(pool);
        alloc->start = (const char*)&mempools[pool * BLOCK_LEN + pool_idxs[pool]];
        alloc->end = alloc->start;
        return alloc;
    }
    
    // Create trimmed string
    faf_string trimmed = {.start = trimmed_start, .end = str.end};
    
    // Copy the trimmed string to pool
    faf_string* alloc = faf_string_alloc(pool);
    const char* data = trimmed.start;
    alloc->start = (const char*)&mempools[pool * BLOCK_LEN + pool_idxs[pool]];
    
    // Copy 16 bytes at a time
    for (; data + 16 <= trimmed.end; data += 16) {
        simde__m128i chars = simde_mm_loadu_si128((const simde__m128i*)data);
        int idx = pool_idxs[pool]++;
        mempools[pool * BLOCK_LEN + idx].data = chars;
    }
    
    // Handle remaining bytes
    int remaining = trimmed.end - data;
    alloc->end = (const char*)(((uintptr_t)&mempools[pool * BLOCK_LEN + pool_idxs[pool]]) + remaining);
    
    if (remaining > 0) {
        int idx = pool_idxs[pool]++;
        char buffer[16] = {0};
        for (int i = 0; i < remaining; ++i) {
            buffer[i] = data[i];
        }
        simde__m128i chars = simde_mm_loadu_si128((const simde__m128i*)buffer);
        mempools[pool * BLOCK_LEN + idx].data = chars;
    }
    
    return alloc;
}

faf_string* faf_string_rtrim(pool_t pool, faf_string str) {
    const char* trimmed_end = find_last_non_whitespace(str.start, str.end);
    
    if (trimmed_end <= str.start) {
        // All whitespace, return empty string
        faf_string* alloc = faf_string_alloc(pool);
        alloc->start = (const char*)&mempools[pool * BLOCK_LEN + pool_idxs[pool]];
        alloc->end = alloc->start;
        return alloc;
    }
    
    // Create trimmed string
    faf_string trimmed = {.start = str.start, .end = trimmed_end};
    
    // Copy the trimmed string to pool
    faf_string* alloc = faf_string_alloc(pool);
    const char* data = trimmed.start;
    alloc->start = (const char*)&mempools[pool * BLOCK_LEN + pool_idxs[pool]];
    
    // Copy 16 bytes at a time
    for (; data + 16 <= trimmed.end; data += 16) {
        simde__m128i chars = simde_mm_loadu_si128((const simde__m128i*)data);
        int idx = pool_idxs[pool]++;
        mempools[pool * BLOCK_LEN + idx].data = chars;
    }
    
    // Handle remaining bytes
    int remaining = trimmed.end - data;
    alloc->end = (const char*)(((uintptr_t)&mempools[pool * BLOCK_LEN + pool_idxs[pool]]) + remaining);
    
    if (remaining > 0) {
        int idx = pool_idxs[pool]++;
        char buffer[16] = {0};
        for (int i = 0; i < remaining; ++i) {
            buffer[i] = data[i];
        }
        simde__m128i chars = simde_mm_loadu_si128((const simde__m128i*)buffer);
        mempools[pool * BLOCK_LEN + idx].data = chars;
    }
    
    return alloc;
}

faf_string* faf_string_trim(pool_t pool, faf_string str) {
    const char* trimmed_start = find_first_non_whitespace(str.start, str.end);
    
    if (trimmed_start >= str.end) {
        // All whitespace, return empty string
        faf_string* alloc = faf_string_alloc(pool);
        alloc->start = (const char*)&mempools[pool * BLOCK_LEN + pool_idxs[pool]];
        alloc->end = alloc->start;
        return alloc;
    }
    
    const char* trimmed_end = find_last_non_whitespace(trimmed_start, str.end);
    
    // Create trimmed string
    faf_string trimmed = {.start = trimmed_start, .end = trimmed_end};
    
    // Copy the trimmed string to pool
    faf_string* alloc = faf_string_alloc(pool);
    const char* data = trimmed.start;
    alloc->start = (const char*)&mempools[pool * BLOCK_LEN + pool_idxs[pool]];
    
    // Copy 16 bytes at a time
    for (; data + 16 <= trimmed.end; data += 16) {
        simde__m128i chars = simde_mm_loadu_si128((const simde__m128i*)data);
        int idx = pool_idxs[pool]++;
        mempools[pool * BLOCK_LEN + idx].data = chars;
    }
    
    // Handle remaining bytes
    int remaining = trimmed.end - data;
    alloc->end = (const char*)(((uintptr_t)&mempools[pool * BLOCK_LEN + pool_idxs[pool]]) + remaining);
    
    if (remaining > 0) {
        int idx = pool_idxs[pool]++;
        char buffer[16] = {0};
        for (int i = 0; i < remaining; ++i) {
            buffer[i] = data[i];
        }
        simde__m128i chars = simde_mm_loadu_si128((const simde__m128i*)buffer);
        mempools[pool * BLOCK_LEN + idx].data = chars;
    }
    
    return alloc;
}