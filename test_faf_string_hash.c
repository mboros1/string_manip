#include "faf_test.h"
#include "faf_string.h"
#include "faf_string_hash.h"
#include <stdint.h>
#include <stdio.h>

// FNV-1a hash constants for reference
#define FNV_OFFSET_BASIS_64 0xcbf29ce484222325ULL

static void test_empty_string(void) {
    faf_string str = faf_string_init("");
    uint64_t result = faf_string_hash(str);
    
    ASSERT_TRUE(result == FNV_OFFSET_BASIS_64, "Empty string should return FNV offset basis");
}

static void test_single_character(void) {
    faf_string str = faf_string_init("a");
    uint64_t result = faf_string_hash(str);
    
    // Should be different from empty string
    ASSERT_TRUE(result != FNV_OFFSET_BASIS_64, "Single character should be different from empty");
}

static void test_case_sensitivity(void) {
    faf_string str1 = faf_string_init("Hello");
    faf_string str2 = faf_string_init("hello");
    uint64_t result1 = faf_string_hash(str1);
    uint64_t result2 = faf_string_hash(str2);
    
    ASSERT_TRUE(result1 != result2, "Hash should be case sensitive");
}

static void test_different_strings_different_hashes(void) {
    faf_string str1 = faf_string_init("hello");
    faf_string str2 = faf_string_init("world");
    uint64_t result1 = faf_string_hash(str1);
    uint64_t result2 = faf_string_hash(str2);
    
    ASSERT_TRUE(result1 != result2, "Different strings should have different hashes");
}

static void test_same_string_same_hash(void) {
    faf_string str1 = faf_string_init("hello world");
    faf_string str2 = faf_string_init("hello world");
    uint64_t result1 = faf_string_hash(str1);
    uint64_t result2 = faf_string_hash(str2);
    
    ASSERT_TRUE(result1 == result2, "Same strings should have same hash");
}

static void test_order_matters(void) {
    faf_string str1 = faf_string_init("abc");
    faf_string str2 = faf_string_init("bca");
    uint64_t result1 = faf_string_hash(str1);
    uint64_t result2 = faf_string_hash(str2);
    
    ASSERT_TRUE(result1 != result2, "Order of characters should matter");
}

static void test_single_byte_differences(void) {
    faf_string str1 = faf_string_init("test");
    faf_string str2 = faf_string_init("best");
    uint64_t result1 = faf_string_hash(str1);
    uint64_t result2 = faf_string_hash(str2);
    
    ASSERT_TRUE(result1 != result2, "Single byte difference should change hash");
}

static void test_exactly_16_bytes(void) {
    faf_string str = faf_string_init("abcdefghijklmnop");  // 16 bytes
    uint64_t result = faf_string_hash(str);
    
    ASSERT_TRUE(result != FNV_OFFSET_BASIS_64, "16-byte string should have valid hash");
}

static void test_17_bytes(void) {
    faf_string str = faf_string_init("abcdefghijklmnopq");  // 17 bytes
    uint64_t result = faf_string_hash(str);
    
    ASSERT_TRUE(result != FNV_OFFSET_BASIS_64, "17-byte string should have valid hash");
}

static void test_long_string(void) {
    const char* long_str = "abcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    faf_string str = faf_string_init(long_str);
    uint64_t result = faf_string_hash(str);
    
    ASSERT_TRUE(result != FNV_OFFSET_BASIS_64, "Long string should have valid hash");
}

static void test_with_whitespace(void) {
    faf_string str = faf_string_init("hello world test");
    uint64_t result = faf_string_hash(str);
    
    ASSERT_TRUE(result != FNV_OFFSET_BASIS_64, "String with whitespace should have valid hash");
}

static void test_with_numbers_and_symbols(void) {
    faf_string str = faf_string_init("abc123!@#def456$%^");
    uint64_t result = faf_string_hash(str);
    
    ASSERT_TRUE(result != FNV_OFFSET_BASIS_64, "String with numbers and symbols should have valid hash");
}

static void test_consistency(void) {
    // Test that the same string always produces the same hash
    faf_string str = faf_string_init("consistency test");
    uint64_t result1 = faf_string_hash(str);
    uint64_t result2 = faf_string_hash(str);
    uint64_t result3 = faf_string_hash(str);
    
    ASSERT_TRUE(result1 == result2, "Hash should be consistent");
    ASSERT_TRUE(result2 == result3, "Hash should be consistent");
}

static void test_various_lengths(void) {
    // Test different lengths to ensure boundary handling
    const char* base = "abcdefghijklmnopqrstuvwxyz0123456789";
    uint64_t prev_hash = FNV_OFFSET_BASIS_64;
    
    for (size_t len = 1; len <= 35; len++) {
        faf_string str = faf_string_init_n(base, len);
        uint64_t result = faf_string_hash(str);
        
        ASSERT_TRUE(result != prev_hash, "Each length should produce different hash");
        prev_hash = result;
    }
}

static void test_hash_distribution(void) {
    // Test that similar strings produce well-distributed hashes
    uint64_t hashes[10];
    char str_buf[20];
    
    for (int i = 0; i < 10; i++) {
        snprintf(str_buf, sizeof(str_buf), "test_string_%d", i);
        faf_string str = faf_string_init(str_buf);
        hashes[i] = faf_string_hash(str);
    }
    
    // Check that all hashes are different
    for (int i = 0; i < 10; i++) {
        for (int j = i + 1; j < 10; j++) {
            ASSERT_TRUE(hashes[i] != hashes[j], "Similar strings should have different hashes");
        }
    }
}

int main(int argc, char** argv) {
    test_case_t tests[] = {
        {"test_empty_string", test_empty_string},
        {"test_single_character", test_single_character},
        {"test_case_sensitivity", test_case_sensitivity},
        {"test_different_strings_different_hashes", test_different_strings_different_hashes},
        {"test_same_string_same_hash", test_same_string_same_hash},
        {"test_order_matters", test_order_matters},
        {"test_single_byte_differences", test_single_byte_differences},
        {"test_exactly_16_bytes", test_exactly_16_bytes},
        {"test_17_bytes", test_17_bytes},
        {"test_long_string", test_long_string},
        {"test_with_whitespace", test_with_whitespace},
        {"test_with_numbers_and_symbols", test_with_numbers_and_symbols},
        {"test_consistency", test_consistency},
        {"test_various_lengths", test_various_lengths},
        {"test_hash_distribution", test_hash_distribution}
    };
    
    test_suite_t suite = TEST_SUITE(
        "faf_string_hash",
        tests,
        sizeof(tests) / sizeof(tests[0]),
        NULL,
        NULL
    );
    
    register_test_suite(suite);
    return test_main(argc, argv);
}