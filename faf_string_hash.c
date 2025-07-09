#include "faf_string_hash.h"
#include <simde/x86/sse2.h>
#include <stdint.h>

// FNV-1a hash constants
#define FNV_OFFSET_BASIS_64 0xcbf29ce484222325ULL
#define FNV_PRIME_64 0x100000001b3ULL

uint64_t faf_string_hash(faf_string str) {
    size_t len = str.end - str.start;
    const char* data = str.start;
    
    // Handle empty string
    if (len == 0) {
        return FNV_OFFSET_BASIS_64;
    }
    
    uint64_t hash = FNV_OFFSET_BASIS_64;
    
    // Process 16 bytes at a time with SIMD for better cache performance
    while (len >= 16) {
        // Load 16 bytes at once - this improves cache locality
        simde__m128i chunk = simde_mm_loadu_si128((const simde__m128i*)data);
        
        // Extract bytes and process them in order
        // We process sequentially to maintain FNV-1a semantics
        char bytes[16];
        simde_mm_storeu_si128((simde__m128i*)bytes, chunk);
        
        // Unrolled loop for better performance
        hash ^= (uint64_t)(unsigned char)bytes[0];  hash *= FNV_PRIME_64;
        hash ^= (uint64_t)(unsigned char)bytes[1];  hash *= FNV_PRIME_64;
        hash ^= (uint64_t)(unsigned char)bytes[2];  hash *= FNV_PRIME_64;
        hash ^= (uint64_t)(unsigned char)bytes[3];  hash *= FNV_PRIME_64;
        hash ^= (uint64_t)(unsigned char)bytes[4];  hash *= FNV_PRIME_64;
        hash ^= (uint64_t)(unsigned char)bytes[5];  hash *= FNV_PRIME_64;
        hash ^= (uint64_t)(unsigned char)bytes[6];  hash *= FNV_PRIME_64;
        hash ^= (uint64_t)(unsigned char)bytes[7];  hash *= FNV_PRIME_64;
        hash ^= (uint64_t)(unsigned char)bytes[8];  hash *= FNV_PRIME_64;
        hash ^= (uint64_t)(unsigned char)bytes[9];  hash *= FNV_PRIME_64;
        hash ^= (uint64_t)(unsigned char)bytes[10]; hash *= FNV_PRIME_64;
        hash ^= (uint64_t)(unsigned char)bytes[11]; hash *= FNV_PRIME_64;
        hash ^= (uint64_t)(unsigned char)bytes[12]; hash *= FNV_PRIME_64;
        hash ^= (uint64_t)(unsigned char)bytes[13]; hash *= FNV_PRIME_64;
        hash ^= (uint64_t)(unsigned char)bytes[14]; hash *= FNV_PRIME_64;
        hash ^= (uint64_t)(unsigned char)bytes[15]; hash *= FNV_PRIME_64;
        
        data += 16;
        len -= 16;
    }
    
    // Process remaining bytes (< 16) with scalar code
    while (len > 0) {
        hash ^= (uint64_t)(unsigned char)(*data);
        hash *= FNV_PRIME_64;
        data++;
        len--;
    }
    
    return hash;
}