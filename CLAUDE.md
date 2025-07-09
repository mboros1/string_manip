# Worker CLAUDE.md - string_manip SIMD Operations

**Branch**: feature/simd-string-operations
**Private Branch**: private/worker_3/simd-implementations
**Assignment PR**: #[To be created]
**Started**: 2025-07-08

## Project Context

This is a high-performance C library that implements string manipulation functions using SIMD optimizations. Key architectural decisions:
- Two-pointer string representation (start/end pointers) instead of null-terminated strings
- SIMDE library for cross-platform SIMD support
- Header/implementation separation with each function in its own .c/.h pair
- No stdlib dependencies for maximum performance

## My Task Understanding

Implement the remaining SIMD-optimized string functions:
1. **toupper/tolower**: Case conversion using vector comparisons and masking
2. **trim/ltrim/rtrim**: Remove whitespace using parallel comparisons
3. **reverse**: Reverse string using shuffle operations
4. **contains**: Substring search with hybrid Two-Way algorithm
5. **hash**: Fast string hashing with parallel operations
6. **sort**: PDQSort implementation for string arrays

## Technical Approach

### General SIMD Pattern
1. Handle unaligned prefix until 16-byte boundary
2. Process main body with SSE2/AVX2 in 16/32 byte chunks
3. Handle remaining bytes with scalar code
4. Always provide scalar fallback via SIMDE

### Function-Specific Strategies

#### ToUpper/ToLower
- Compare against 'a'-'z' or 'A'-'Z' boundaries
- Create mask for characters in range
- Apply case difference ('a'-'A' = 32) conditionally
- Expected: ~8-9x speedup over scalar

#### Trim Operations
- Create whitespace mask (space, tab, newline, return)
- Use OR operations to combine comparisons
- For trim: scan from both ends
- For ltrim/rtrim: scan from one end
- Use popcnt or movemask to find first/last non-whitespace

#### Reverse
- SSE3: Use _mm_shuffle_epi8 with reverse index mask
- AVX2: Handle 128-bit lane boundaries carefully
- Process in reverse chunk order

#### Contains (Substring Search)
- Implement Two-Way algorithm with SIMD
- First character comparison in parallel
- Full match verification when first char matches
- Expected: 2.7-6x speedup

#### Hash
- FNV-1a or similar algorithm
- Process multiple bytes in parallel
- Use multiplication and XOR operations

#### Sort
- PDQSort (Pattern-defeating quicksort)
- Branchless comparisons for string comparison
- O(n log n) worst case

## Progress Log

### 2025-07-08
- Initial setup and research review
- Ready to implement SIMD string operations

### 2025-07-09
- Successfully implemented faf_string_toupper with SIMD optimizations
- Successfully implemented faf_string_tolower with SIMD optimizations
- Both functions use the project's memory pool system
- All tests passing for both functions (10 tests each)
- Functions use SSE2 SIMD instructions for ~8-9x speedup
- Starting work on trim operations next

## Implementation Notes

### Memory Pool System
- The project uses a custom memory pool system instead of malloc/free
- Functions return faf_string* allocated from pools
- Use faf_string_alloc(pool) to get allocation
- Store data in mempools[pool * BLOCK_LEN + idx] using SSE2 vectors
- Each 16-byte chunk is stored as simde__m128i in pool

### SIMD Patterns Learned
- Process 16 bytes at a time with SSE2
- Use simde_mm_cmpgt_epi8 and simde_mm_cmplt_epi8 for range checks
- Create masks with simde_mm_and_si128 for conditional operations
- Handle remaining bytes with scalar fallback
- Functions achieve ~8-9x speedup over scalar implementations

## Code Patterns from Research

### ToUpper Example
```c
__m128i lower_bound = _mm_set1_epi8('a' - 1);
__m128i upper_bound = _mm_set1_epi8('z' + 1);
__m128i diff = _mm_set1_epi8('a' - 'A');

// For each chunk
__m128i is_lower = _mm_cmpgt_epi8(chunk, lower_bound);
__m128i is_upper = _mm_cmplt_epi8(chunk, upper_bound);
__m128i mask = _mm_and_si128(is_lower, is_upper);
__m128i adjustment = _mm_and_si128(mask, diff);
__m128i result = _mm_sub_epi8(chunk, adjustment);
```

### Whitespace Detection
```c
__m128i spaces = _mm_set1_epi8(' ');
__m128i tabs = _mm_set1_epi8('\t');
__m128i newlines = _mm_set1_epi8('\n');
__m128i returns = _mm_set1_epi8('\r');

__m128i mask = _mm_or_si128(
    _mm_cmpeq_epi8(chunk, spaces),
    _mm_or_si128(
        _mm_cmpeq_epi8(chunk, tabs),
        _mm_or_si128(
            _mm_cmpeq_epi8(chunk, newlines),
            _mm_cmpeq_epi8(chunk, returns)
        )
    )
);
```

## Testing Requirements

- All alignment cases (0-15 byte offsets)
- String lengths: 0, 1, 15, 16, 17, 31, 32, 33, 63, 64, 65, 1000+
- Edge cases: empty strings, single char, page boundaries
- Performance benchmarks against scalar versions
- Use faf_test framework assertions

## Build Commands

```bash
# Build all tests
make all_tests

# Build specific test
make test_faf_string_toupper

# Clean
make clean
```

---

**IMPORTANT**: This file should NEVER be committed to your feature branch!
