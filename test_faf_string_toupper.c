#include "faf_test.h"
#include "faf_string.h"
#include "faf_string_toupper.h"
#include "faf_string_mem.h"
#include <string.h>
#include <stdio.h>
#include <stdint.h>

static pool_t test_pool;

static void setup(void) {
    test_pool = next_pool();
}

static void teardown(void) {
    faf_string_pool_reset(test_pool);
}

static void test_empty_string(void) {
    faf_string empty = faf_string_init("");
    faf_string* result = faf_string_toupper(test_pool, empty);
    
    ASSERT_TRUE(result != NULL, "Result should not be NULL");
    ASSERT_INT_EQ(0, result->end - result->start, "Empty string should remain empty");
}

static void test_single_char_lowercase(void) {
    faf_string str = faf_string_init("a");
    faf_string* result = faf_string_toupper(test_pool, str);
    
    ASSERT_TRUE(result != NULL, "Result should not be NULL");
    ASSERT_INT_EQ(1, result->end - result->start, "Length should be 1");
    ASSERT_TRUE(result->start[0] == 'A', "Character should be uppercase");
}

static void test_single_char_uppercase(void) {
    faf_string str = faf_string_init("A");
    faf_string* result = faf_string_toupper(test_pool, str);
    
    ASSERT_TRUE(result != NULL, "Result should not be NULL");
    ASSERT_INT_EQ(1, result->end - result->start, "Length should be 1");
    ASSERT_TRUE(result->start[0] == 'A', "Character should remain uppercase");
}

static void test_all_lowercase(void) {
    faf_string str = faf_string_init("hello world");
    faf_string* result = faf_string_toupper(test_pool, str);
    
    ASSERT_TRUE(result != NULL, "Result should not be NULL");
    ASSERT_INT_EQ(11, result->end - result->start, "Length should be 11");
    ASSERT_TRUE(strncmp(result->start, "HELLO WORLD", 11) == 0, "Should convert to uppercase");
}

static void test_all_uppercase(void) {
    faf_string str = faf_string_init("HELLO WORLD");
    faf_string* result = faf_string_toupper(test_pool, str);
    
    ASSERT_TRUE(result != NULL, "Result should not be NULL");
    ASSERT_INT_EQ(11, result->end - result->start, "Length should be 11");
    ASSERT_TRUE(strncmp(result->start, "HELLO WORLD", 11) == 0, "Should remain uppercase");
}

static void test_mixed_case(void) {
    faf_string str = faf_string_init("HeLLo WoRLd");
    faf_string* result = faf_string_toupper(test_pool, str);
    
    ASSERT_TRUE(result != NULL, "Result should not be NULL");
    ASSERT_INT_EQ(11, result->end - result->start, "Length should be 11");
    ASSERT_TRUE(strncmp(result->start, "HELLO WORLD", 11) == 0, "Should convert to uppercase");
}

static void test_with_numbers_and_symbols(void) {
    faf_string str = faf_string_init("hello123!@#world");
    faf_string* result = faf_string_toupper(test_pool, str);
    
    ASSERT_TRUE(result != NULL, "Result should not be NULL");
    ASSERT_INT_EQ(16, result->end - result->start, "Length should be 16");
    ASSERT_TRUE(strncmp(result->start, "HELLO123!@#WORLD", 16) == 0, "Should only convert letters");
}

static void test_long_string(void) {
    const char* long_str = "abcdefghijklmnopqrstuvwxyz0123456789abcdefghijklmnopqrstuvwxyz";
    faf_string str = faf_string_init(long_str);
    faf_string* result = faf_string_toupper(test_pool, str);
    
    ASSERT_TRUE(result != NULL, "Result should not be NULL");
    ASSERT_INT_EQ(62, result->end - result->start, "Length should be 62");
    
    // Check conversion
    const char* expected = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    ASSERT_TRUE(strncmp(result->start, expected, 62) == 0, "Should convert all lowercase letters");
}

static void test_various_alignments(void) {
    // Test strings at different alignments to ensure SIMD handles them correctly
    for (int offset = 0; offset < 16; offset++) {
        char buffer[80];
        memset(buffer, 'x', sizeof(buffer));
        
        const char* test = "abcdefghijklmnopqrstuvwxyz";
        size_t test_len = strlen(test);
        memcpy(buffer + offset, test, test_len);
        
        faf_string str = faf_string_init_n(buffer + offset, test_len);
        faf_string* result = faf_string_toupper(test_pool, str);
        
        ASSERT_TRUE(result != NULL, "Result should not be NULL");
        ASSERT_INT_EQ(test_len, result->end - result->start, "Length should match");
        ASSERT_TRUE(strncmp(result->start, "ABCDEFGHIJKLMNOPQRSTUVWXYZ", test_len) == 0, 
                   "Should convert correctly at any alignment");
    }
}

static void test_various_lengths(void) {
    // Test strings of various lengths to ensure edge cases are handled
    const char* test_str = "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnop";
    
    for (size_t len = 0; len <= 65; len++) {
        faf_string str = faf_string_init_n(test_str, len);
        faf_string* result = faf_string_toupper(test_pool, str);
        
        ASSERT_TRUE(result != NULL, "Result should not be NULL");
        ASSERT_INT_EQ(len, result->end - result->start, "Length should match");
        
        // Verify conversion
        for (size_t i = 0; i < len; i++) {
            char expected = (test_str[i] >= 'a' && test_str[i] <= 'z') 
                           ? test_str[i] - ('a' - 'A') 
                           : test_str[i];
            ASSERT_TRUE(result->start[i] == expected, "Character should be converted correctly");
        }
    }
}

int main(int argc, char** argv) {
    test_case_t tests[] = {
        {"test_empty_string", test_empty_string},
        {"test_single_char_lowercase", test_single_char_lowercase},
        {"test_single_char_uppercase", test_single_char_uppercase},
        {"test_all_lowercase", test_all_lowercase},
        {"test_all_uppercase", test_all_uppercase},
        {"test_mixed_case", test_mixed_case},
        {"test_with_numbers_and_symbols", test_with_numbers_and_symbols},
        {"test_long_string", test_long_string},
        {"test_various_alignments", test_various_alignments},
        {"test_various_lengths", test_various_lengths}
    };
    
    test_suite_t suite = TEST_SUITE(
        "faf_string_toupper",
        tests,
        sizeof(tests) / sizeof(tests[0]),
        setup,
        teardown
    );
    
    register_test_suite(suite);
    return test_main(argc, argv);
}