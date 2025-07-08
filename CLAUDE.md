# CLAUDE.md - Worker Knowledge Base

**Branch**: feature/implement-remaining-string-functions
**Private Branch**: private/worker_1/implement-remaining-string-functions
**Assignment PR**: #[PR-number - to be created]
**Started**: 2025-07-08

## Project Context

I'm working on the string_manip library - a high-performance, cross-platform string manipulation library using SIMD operations. The library uses a two-pointer string representation (faf_string) and has no standard library dependencies.

### Key Project Characteristics
- Language: C with SIMD optimizations
- Dependencies: SIMDE library only (for portable SIMD)
- String representation: Two pointers (start/end) instead of null-terminated
- Build system: Makefile
- Test framework: Custom faf_test
- Default branch: trunk

## Feature Context

The Orchestrator has provided extensive research on SIMD string manipulation best practices:

### SIMD Performance Targets
- ToUpper/ToLower: ~8-9x speedup over scalar
- String trimming: ~14x speedup (Daniel Lemire's approach)
- String search: 2.7-6x speedup
- General guideline: Process 16 bytes (SSE2) or 32 bytes (AVX2) at once

### Key Implementation Patterns
1. Memory alignment handling - process unaligned bytes before SIMD
2. Branchless design using masking and blending
3. Handle remaining bytes after SIMD processing
4. Provide scalar fallback for short strings (<16 bytes)

## My Task Understanding

I need to implement the remaining string manipulation functions from TODO.md:
1. **toupper** - Convert strings to uppercase (inverse of existing tolower)
2. **ltrim, rtrim, trim** - Remove whitespace from start/end/both
3. **reverse** - Reverse string contents
4. **contains** - Substring search
5. **hash** - String hashing function
6. **sort** - Sort strings and string arrays using pdqsort
7. **format** - String formatting (lower priority)
8. **strmult** - String multiplication/repetition (optional)

Each function needs:
- Header file with function declaration and complexity documentation
- SIMD-optimized implementation with scalar fallback
- Comprehensive test file using faf_test framework

## Technical Approach

### 1. ToUpper (faf_string_to_upper)
- Mirror the existing to_lower implementation
- Use SSE2 to check range 'a'-'z'
- Subtract 32 from lowercase characters
- Handle alignment and remainder

### 2. Trim Functions (faf_string_trim)
- Define whitespace set: ' ', '\t', '\n', '\r', '\f', '\v'
- ltrim: Scan from start using SIMD to find first non-whitespace
- rtrim: Scan from end backwards
- trim: Combine ltrim and rtrim operations

### 3. Reverse (faf_string_reverse)
- Use SSSE3 `_mm_shuffle_epi8` for 16-byte chunks
- Create reverse index mask
- Handle alignment and odd-length strings
- Consider in-place vs new allocation

### 4. Contains (faf_string_contains)
- Implement Two-Way algorithm with SIMD acceleration
- First character filtering using SIMD
- Return boolean or pointer to match

### 5. Hash (faf_string_hash)
- Implement FNV-1a or similar algorithm
- Use SIMD for parallel multiplication/XOR
- Return size_t hash value

### 6. Sort (faf_string_sort)
- Integrate existing pdqsort.h
- Implement comparison function for faf_string
- Support both single string sort and array sort

## Key Decisions

1. **Memory Allocation**: Following existing patterns, avoid allocations where possible
2. **Error Handling**: Use existing assert patterns from faf_string_assert.h
3. **SIMD Baseline**: Target SSE2 for maximum compatibility
4. **Testing**: Match existing test coverage patterns

## Progress Log

### 2025-07-08
- Analyzed codebase structure and existing implementations
- Studied SIMD patterns in existing functions
- Created implementation plan
- Set up workspace and branches

## Challenges & Solutions

(To be filled as I encounter and solve challenges)

## Code Patterns

### Standard Function Header Documentation
```c
// Function description
// Runtime Complexity: O(n)
// Memory Complexity: O(1)
```

### SIMD Processing Loop Pattern
```c
// Handle unaligned prefix
while (((uintptr_t)ptr & 15) && ptr < end) {
    // scalar processing
}

// SIMD main loop
while (ptr + 16 <= end) {
    __m128i chunk = _mm_loadu_si128((__m128i*)ptr);
    // SIMD processing
    ptr += 16;
}

// Handle remainder
while (ptr < end) {
    // scalar processing
}
```

### Test Structure Pattern
```c
FAF_TEST_BEGIN("function_name")
    // Test empty string
    // Test single character
    // Test aligned/unaligned
    // Test various lengths
    // Test edge cases
FAF_TEST_END()
```

## Notes for Future Work

- The codebase is well-structured with clear patterns to follow
- SIMDE makes cross-platform SIMD straightforward
- The two-pointer string design is elegant and efficient
- Pay attention to page boundary handling in SIMD code
- The test framework makes it easy to verify correctness

---

**IMPORTANT**: This file should NEVER be committed to your feature branch!
- Keep it in your working directory for Claude Code to read
- Optionally back it up to a private branch
- Add to .git/info/exclude to hide from git status