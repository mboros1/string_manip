#include "faf_test.h"
#include "faf_string.h"
#include "faf_string_reverse.h"
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
    faf_string* result = faf_string_reverse(test_pool, empty);
    
    ASSERT_TRUE(result != NULL, "Result should not be NULL");
    ASSERT_INT_EQ(0, result->end - result->start, "Empty string should remain empty");
}

static void test_single_character(void) {
    faf_string str = faf_string_init("a");
    faf_string* result = faf_string_reverse(test_pool, str);
    
    ASSERT_TRUE(result != NULL, "Result should not be NULL");
    ASSERT_INT_EQ(1, result->end - result->start, "Length should be 1");
    ASSERT_TRUE(result->start[0] == 'a', "Single character should remain unchanged");
}

static void test_two_characters(void) {
    faf_string str = faf_string_init("ab");
    faf_string* result = faf_string_reverse(test_pool, str);
    
    ASSERT_TRUE(result != NULL, "Result should not be NULL");
    ASSERT_INT_EQ(2, result->end - result->start, "Length should be 2");
    ASSERT_TRUE(strncmp(result->start, "ba", 2) == 0, "Should reverse to 'ba'");
}

static void test_palindrome(void) {
    faf_string str = faf_string_init("racecar");
    faf_string* result = faf_string_reverse(test_pool, str);
    
    ASSERT_TRUE(result != NULL, "Result should not be NULL");
    ASSERT_INT_EQ(7, result->end - result->start, "Length should be 7");
    ASSERT_TRUE(strncmp(result->start, "racecar", 7) == 0, "Palindrome should remain unchanged");
}

static void test_short_string(void) {
    faf_string str = faf_string_init("hello");
    faf_string* result = faf_string_reverse(test_pool, str);
    
    ASSERT_TRUE(result != NULL, "Result should not be NULL");
    ASSERT_INT_EQ(5, result->end - result->start, "Length should be 5");
    ASSERT_TRUE(strncmp(result->start, "olleh", 5) == 0, "Should reverse to 'olleh'");
}

static void test_medium_string(void) {
    faf_string str = faf_string_init("hello world");
    faf_string* result = faf_string_reverse(test_pool, str);
    
    ASSERT_TRUE(result != NULL, "Result should not be NULL");
    ASSERT_INT_EQ(11, result->end - result->start, "Length should be 11");
    ASSERT_TRUE(strncmp(result->start, "dlrow olleh", 11) == 0, "Should reverse correctly");
}

static void test_exactly_16_bytes(void) {
    faf_string str = faf_string_init("abcdefghijklmnop");  // 16 bytes
    faf_string* result = faf_string_reverse(test_pool, str);
    
    ASSERT_TRUE(result != NULL, "Result should not be NULL");
    ASSERT_INT_EQ(16, result->end - result->start, "Length should be 16");
    ASSERT_TRUE(strncmp(result->start, "ponmlkjihgfedcba", 16) == 0, "Should reverse 16-byte string");
}

static void test_17_bytes(void) {
    faf_string str = faf_string_init("abcdefghijklmnopq");  // 17 bytes
    faf_string* result = faf_string_reverse(test_pool, str);
    
    ASSERT_TRUE(result != NULL, "Result should not be NULL");
    ASSERT_INT_EQ(17, result->end - result->start, "Length should be 17");
    ASSERT_TRUE(strncmp(result->start, "qponmlkjihgfedcba", 17) == 0, "Should reverse 17-byte string");
}

static void test_32_bytes(void) {
    faf_string str = faf_string_init("abcdefghijklmnopqrstuvwxyz123456");  // 32 bytes
    faf_string* result = faf_string_reverse(test_pool, str);
    
    ASSERT_TRUE(result != NULL, "Result should not be NULL");
    ASSERT_INT_EQ(32, result->end - result->start, "Length should be 32");
    ASSERT_TRUE(strncmp(result->start, "654321zyxwvutsrqponmlkjihgfedcba", 32) == 0, "Should reverse 32-byte string");
}

static void test_long_string(void) {
    const char* long_str = "abcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";  // 62 bytes
    faf_string str = faf_string_init(long_str);
    faf_string* result = faf_string_reverse(test_pool, str);
    
    ASSERT_TRUE(result != NULL, "Result should not be NULL");
    ASSERT_INT_EQ(62, result->end - result->start, "Length should be 62");
    
    const char* expected = "ZYXWVUTSRQPONMLKJIHGFEDCBA9876543210zyxwvutsrqponmlkjihgfedcba";
    ASSERT_TRUE(strncmp(result->start, expected, 62) == 0, "Should reverse long string correctly");
}

static void test_with_whitespace(void) {
    faf_string str = faf_string_init("hello world test");
    faf_string* result = faf_string_reverse(test_pool, str);
    
    ASSERT_TRUE(result != NULL, "Result should not be NULL");
    ASSERT_INT_EQ(16, result->end - result->start, "Length should be 16");
    ASSERT_TRUE(strncmp(result->start, "tset dlrow olleh", 16) == 0, "Should reverse with whitespace");
}

static void test_with_numbers_and_symbols(void) {
    faf_string str = faf_string_init("abc123!@#");
    faf_string* result = faf_string_reverse(test_pool, str);
    
    ASSERT_TRUE(result != NULL, "Result should not be NULL");
    ASSERT_INT_EQ(9, result->end - result->start, "Length should be 9");
    ASSERT_TRUE(strncmp(result->start, "#@!321cba", 9) == 0, "Should reverse with numbers and symbols");
}

static void test_various_alignments(void) {
    // Test strings at different alignments to ensure SIMD handles them correctly
    for (int offset = 0; offset < 16; offset++) {
        char buffer[80];
        memset(buffer, 'X', sizeof(buffer));
        
        const char* test = "abcdefghijklmnopqrstuvwxyz";
        size_t test_len = strlen(test);
        memcpy(buffer + offset, test, test_len);
        
        faf_string str = faf_string_init_n(buffer + offset, test_len);
        faf_string* result = faf_string_reverse(test_pool, str);
        
        ASSERT_TRUE(result != NULL, "Result should not be NULL");
        ASSERT_INT_EQ(test_len, result->end - result->start, "Length should match");
        ASSERT_TRUE(strncmp(result->start, "zyxwvutsrqponmlkjihgfedcba", test_len) == 0, 
                   "Should reverse correctly at any alignment");
    }
}

static void test_various_lengths(void) {
    // Test strings of various lengths to ensure edge cases are handled
    const char* test_str = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    
    for (size_t len = 0; len <= 65; len++) {
        faf_string str = faf_string_init_n(test_str, len);
        faf_string* result = faf_string_reverse(test_pool, str);
        
        ASSERT_TRUE(result != NULL, "Result should not be NULL");
        ASSERT_INT_EQ(len, result->end - result->start, "Length should match");
        
        // Verify reversal
        for (size_t i = 0; i < len; i++) {
            char expected = test_str[len - 1 - i];
            ASSERT_TRUE(result->start[i] == expected, "Character should be reversed correctly");
        }
    }
}

static void test_double_reverse(void) {
    // Test that reversing twice gives original string
    faf_string str = faf_string_init("hello world 123");
    faf_string* first_reverse = faf_string_reverse(test_pool, str);
    faf_string* second_reverse = faf_string_reverse(test_pool, *first_reverse);
    
    ASSERT_TRUE(first_reverse != NULL, "First reverse should not be NULL");
    ASSERT_TRUE(second_reverse != NULL, "Second reverse should not be NULL");
    ASSERT_INT_EQ(str.end - str.start, second_reverse->end - second_reverse->start, "Length should match original");
    
    size_t len = str.end - str.start;
    ASSERT_TRUE(strncmp(str.start, second_reverse->start, len) == 0, "Double reverse should equal original");
}

int main(int argc, char** argv) {
    test_case_t tests[] = {
        {"test_empty_string", test_empty_string},
        {"test_single_character", test_single_character},
        {"test_two_characters", test_two_characters},
        {"test_palindrome", test_palindrome},
        {"test_short_string", test_short_string},
        {"test_medium_string", test_medium_string},
        {"test_exactly_16_bytes", test_exactly_16_bytes},
        {"test_17_bytes", test_17_bytes},
        {"test_32_bytes", test_32_bytes},
        {"test_long_string", test_long_string},
        {"test_with_whitespace", test_with_whitespace},
        {"test_with_numbers_and_symbols", test_with_numbers_and_symbols},
        {"test_various_alignments", test_various_alignments},
        {"test_various_lengths", test_various_lengths},
        {"test_double_reverse", test_double_reverse}
    };
    
    test_suite_t suite = TEST_SUITE(
        "faf_string_reverse",
        tests,
        sizeof(tests) / sizeof(tests[0]),
        setup,
        teardown
    );
    
    register_test_suite(suite);
    return test_main(argc, argv);
}