# Assignment: SIMD-Optimized String Operations

## Worker Assignment

**Assignment Date**: 2025-07-08  
**Worker Instance**: Worker 3  
**Branch**: `feature/simd-string-operations`  
**Priority**: High

## Mission Brief

Implement the remaining SIMD-optimized string manipulation functions for the string_manip library. These functions are critical for achieving the library's performance goals and completing the comprehensive string manipulation API. Your implementations should leverage SIMD instructions through the SIMDE library to achieve significant speedups over scalar code.

## Current Context

### What Exists
- Core faf_string structure with two-pointer representation (start/end)
- SIMDE library integration for cross-platform SIMD support
- Existing pattern of header/implementation separation
- faf_test framework for comprehensive testing
- Make-based build system with pattern rules

### What's Needed
- SIMD-optimized implementations for: toupper, tolower, trim, ltrim, rtrim, reverse, contains, hash, and sort
- Comprehensive test coverage for each function
- Performance benchmarks demonstrating SIMD speedup
- Proper handling of alignment and edge cases

## Technical Approach

### Recommended Strategy
1. **Start with toupper/tolower** - These are the simplest SIMD conversions
   - Implement scalar version first for correctness
   - Add SSE2 optimization using range comparisons and masking
   - Verify ~8-9x speedup over scalar code

2. **Implement trim operations** - Build on whitespace detection patterns
   - Create reusable whitespace mask detection
   - Implement trim (both ends), ltrim (left only), rtrim (right only)
   - Use movemask/popcnt for efficient boundary detection

3. **Tackle reverse and contains** - More complex SIMD patterns
   - Reverse: Use shuffle operations, handle chunk ordering
   - Contains: Implement Two-Way algorithm with SIMD first-char matching

4. **Finish with hash and sort** - Advanced implementations
   - Hash: Parallel multiplication/XOR for speed
   - Sort: PDQSort with branchless string comparisons

### Key Considerations
- **Alignment handling**: Process unaligned bytes before main SIMD loop
- **Edge cases**: Strings shorter than 16 bytes, page boundaries
- **Performance**: SIMD overhead not worth it for very short strings (<16 bytes)
- **Portability**: All SIMD code must work through SIMDE for cross-platform support

### Resources
- **Pattern Examples**: See CLAUDE.md for SIMD code patterns from research
- **Existing Code**: Review faf_string.h for structure definition
- **Test Framework**: Study existing test files for testing patterns
- **SIMDE Docs**: https://github.com/simd-everywhere/simde

## Worker Guidelines

### Initial Setup
1. Your branch is already set up with customized ONBOARDING.md
2. Review the detailed SIMD patterns in your CLAUDE.md
3. Study existing faf_string functions for coding style
4. Set up your private branch for knowledge persistence

### During Development
- Follow the pattern: implement scalar first, then optimize
- Test each alignment case (0-15 byte offsets)
- Benchmark performance vs scalar implementation
- Handle all edge cases properly

## Implementation Details

### Function Signatures (following project patterns)
```c
// Case conversion
faf_string faf_string_toupper(faf_string s);
faf_string faf_string_tolower(faf_string s);

// Trimming
faf_string faf_string_trim(faf_string s);
faf_string faf_string_ltrim(faf_string s);
faf_string faf_string_rtrim(faf_string s);

// String operations
faf_string faf_string_reverse(faf_string s);
bool faf_string_contains(faf_string haystack, faf_string needle);
uint64_t faf_string_hash(faf_string s);

// Sorting
void faf_string_sort(faf_string* strings, size_t count);
```

### Performance Targets
- toupper/tolower: 8-9x speedup
- trim operations: 10-14x speedup
- reverse: 5-7x speedup
- contains: 2.7-6x speedup
- hash: 4-6x speedup
- sort: 2-3x speedup on random data

## Expected Outcomes

### Deliverables
- Complete implementations of all 9 functions
- Comprehensive test suite for each function
- Performance benchmarks showing SIMD speedup
- Clean, well-documented code following project patterns

### Quality Standards
- All tests pass including edge cases
- No memory leaks or undefined behavior
- SIMD code properly handles alignment
- Scalar fallbacks work correctly

## Definition of Done

- [ ] All 9 functions implemented with SIMD optimizations
- [ ] Test coverage includes all edge cases and alignments
- [ ] Performance benchmarks show expected speedups
- [ ] Code follows existing project patterns
- [ ] Make build system updated for new files
- [ ] Ready for review comment posted on PR

## Notes

- The existing CLAUDE.md has detailed SIMD patterns from research - use them!
- Start with simpler functions (toupper/tolower) to build confidence
- Don't forget to handle the scalar fallback cases
- Benchmark regularly to ensure you're achieving expected speedups
- The two-pointer string representation (start/end) simplifies many operations

---

*Remember to save your context regularly and communicate proactively through PR comments.*