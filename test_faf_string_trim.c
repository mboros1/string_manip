#include "faf_test.h"
#include "faf_string.h"
#include "faf_string_trim.h"
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

// Test faf_string_ltrim
static void test_ltrim_empty_string(void) {
    faf_string empty = faf_string_init("");
    faf_string* result = faf_string_ltrim(test_pool, empty);
    
    ASSERT_TRUE(result != NULL, "Result should not be NULL");
    ASSERT_INT_EQ(0, result->end - result->start, "Empty string should remain empty");
}

static void test_ltrim_no_whitespace(void) {
    faf_string str = faf_string_init("hello");
    faf_string* result = faf_string_ltrim(test_pool, str);
    
    ASSERT_TRUE(result != NULL, "Result should not be NULL");
    ASSERT_INT_EQ(5, result->end - result->start, "Length should be 5");
    ASSERT_TRUE(strncmp(result->start, "hello", 5) == 0, "Should remain unchanged");
}

static void test_ltrim_leading_spaces(void) {
    faf_string str = faf_string_init("   hello");
    faf_string* result = faf_string_ltrim(test_pool, str);
    
    ASSERT_TRUE(result != NULL, "Result should not be NULL");
    ASSERT_INT_EQ(5, result->end - result->start, "Length should be 5");
    ASSERT_TRUE(strncmp(result->start, "hello", 5) == 0, "Should trim leading spaces");
}

static void test_ltrim_mixed_whitespace(void) {
    faf_string str = faf_string_init(" \t\n\r  hello world");
    faf_string* result = faf_string_ltrim(test_pool, str);
    
    ASSERT_TRUE(result != NULL, "Result should not be NULL");
    ASSERT_INT_EQ(11, result->end - result->start, "Length should be 11");
    ASSERT_TRUE(strncmp(result->start, "hello world", 11) == 0, "Should trim mixed whitespace");
}

static void test_ltrim_all_whitespace(void) {
    faf_string str = faf_string_init("   \t\n\r  ");
    faf_string* result = faf_string_ltrim(test_pool, str);
    
    ASSERT_TRUE(result != NULL, "Result should not be NULL");
    ASSERT_INT_EQ(0, result->end - result->start, "Should return empty string");
}

static void test_ltrim_trailing_whitespace_preserved(void) {
    faf_string str = faf_string_init("  hello   ");
    faf_string* result = faf_string_ltrim(test_pool, str);
    
    ASSERT_TRUE(result != NULL, "Result should not be NULL");
    ASSERT_INT_EQ(8, result->end - result->start, "Length should be 8");
    ASSERT_TRUE(strncmp(result->start, "hello   ", 8) == 0, "Should preserve trailing whitespace");
}

// Test faf_string_rtrim
static void test_rtrim_empty_string(void) {
    faf_string empty = faf_string_init("");
    faf_string* result = faf_string_rtrim(test_pool, empty);
    
    ASSERT_TRUE(result != NULL, "Result should not be NULL");
    ASSERT_INT_EQ(0, result->end - result->start, "Empty string should remain empty");
}

static void test_rtrim_no_whitespace(void) {
    faf_string str = faf_string_init("hello");
    faf_string* result = faf_string_rtrim(test_pool, str);
    
    ASSERT_TRUE(result != NULL, "Result should not be NULL");
    ASSERT_INT_EQ(5, result->end - result->start, "Length should be 5");
    ASSERT_TRUE(strncmp(result->start, "hello", 5) == 0, "Should remain unchanged");
}

static void test_rtrim_trailing_spaces(void) {
    faf_string str = faf_string_init("hello   ");
    faf_string* result = faf_string_rtrim(test_pool, str);
    
    ASSERT_TRUE(result != NULL, "Result should not be NULL");
    ASSERT_INT_EQ(5, result->end - result->start, "Length should be 5");
    ASSERT_TRUE(strncmp(result->start, "hello", 5) == 0, "Should trim trailing spaces");
}

static void test_rtrim_mixed_whitespace(void) {
    faf_string str = faf_string_init("hello world \t\n\r  ");
    faf_string* result = faf_string_rtrim(test_pool, str);
    
    ASSERT_TRUE(result != NULL, "Result should not be NULL");
    ASSERT_INT_EQ(11, result->end - result->start, "Length should be 11");
    ASSERT_TRUE(strncmp(result->start, "hello world", 11) == 0, "Should trim mixed whitespace");
}

static void test_rtrim_all_whitespace(void) {
    faf_string str = faf_string_init("   \t\n\r  ");
    faf_string* result = faf_string_rtrim(test_pool, str);
    
    ASSERT_TRUE(result != NULL, "Result should not be NULL");
    ASSERT_INT_EQ(0, result->end - result->start, "Should return empty string");
}

static void test_rtrim_leading_whitespace_preserved(void) {
    faf_string str = faf_string_init("   hello  ");
    faf_string* result = faf_string_rtrim(test_pool, str);
    
    ASSERT_TRUE(result != NULL, "Result should not be NULL");
    ASSERT_INT_EQ(8, result->end - result->start, "Length should be 8");
    ASSERT_TRUE(strncmp(result->start, "   hello", 8) == 0, "Should preserve leading whitespace");
}

// Test faf_string_trim
static void test_trim_empty_string(void) {
    faf_string empty = faf_string_init("");
    faf_string* result = faf_string_trim(test_pool, empty);
    
    ASSERT_TRUE(result != NULL, "Result should not be NULL");
    ASSERT_INT_EQ(0, result->end - result->start, "Empty string should remain empty");
}

static void test_trim_no_whitespace(void) {
    faf_string str = faf_string_init("hello");
    faf_string* result = faf_string_trim(test_pool, str);
    
    ASSERT_TRUE(result != NULL, "Result should not be NULL");
    ASSERT_INT_EQ(5, result->end - result->start, "Length should be 5");
    ASSERT_TRUE(strncmp(result->start, "hello", 5) == 0, "Should remain unchanged");
}

static void test_trim_both_ends(void) {
    faf_string str = faf_string_init("   hello world   ");
    faf_string* result = faf_string_trim(test_pool, str);
    
    ASSERT_TRUE(result != NULL, "Result should not be NULL");
    ASSERT_INT_EQ(11, result->end - result->start, "Length should be 11");
    ASSERT_TRUE(strncmp(result->start, "hello world", 11) == 0, "Should trim both ends");
}

static void test_trim_mixed_whitespace_both_ends(void) {
    faf_string str = faf_string_init(" \t\n hello world \r\n\t ");
    faf_string* result = faf_string_trim(test_pool, str);
    
    ASSERT_TRUE(result != NULL, "Result should not be NULL");
    ASSERT_INT_EQ(11, result->end - result->start, "Length should be 11");
    ASSERT_TRUE(strncmp(result->start, "hello world", 11) == 0, "Should trim mixed whitespace");
}

static void test_trim_all_whitespace(void) {
    faf_string str = faf_string_init("   \t\n\r  ");
    faf_string* result = faf_string_trim(test_pool, str);
    
    ASSERT_TRUE(result != NULL, "Result should not be NULL");
    ASSERT_INT_EQ(0, result->end - result->start, "Should return empty string");
}

static void test_trim_internal_whitespace_preserved(void) {
    faf_string str = faf_string_init("  hello   world  ");
    faf_string* result = faf_string_trim(test_pool, str);
    
    ASSERT_TRUE(result != NULL, "Result should not be NULL");
    ASSERT_INT_EQ(13, result->end - result->start, "Length should be 13");
    ASSERT_TRUE(strncmp(result->start, "hello   world", 13) == 0, "Should preserve internal whitespace");
}

// Test edge cases and performance
static void test_trim_long_string(void) {
    const char* long_str = "    abcdefghijklmnopqrstuvwxyz0123456789abcdefghijklmnopqrstuvwxyz    ";
    faf_string str = faf_string_init(long_str);
    faf_string* result = faf_string_trim(test_pool, str);
    
    ASSERT_TRUE(result != NULL, "Result should not be NULL");
    ASSERT_INT_EQ(62, result->end - result->start, "Length should be 62");
    
    const char* expected = "abcdefghijklmnopqrstuvwxyz0123456789abcdefghijklmnopqrstuvwxyz";
    ASSERT_TRUE(strncmp(result->start, expected, 62) == 0, "Should trim long string correctly");
}

static void test_trim_various_alignments(void) {
    // Test different alignment scenarios
    for (int offset = 0; offset < 16; offset++) {
        char buffer[80];
        memset(buffer, ' ', sizeof(buffer));
        
        const char* test = "hello world";
        size_t test_len = strlen(test);
        memcpy(buffer + offset + 4, test, test_len);  // 4 leading spaces
        
        faf_string str = faf_string_init_n(buffer + offset, test_len + 8);  // 4 leading + 4 trailing
        faf_string* result = faf_string_trim(test_pool, str);
        
        ASSERT_TRUE(result != NULL, "Result should not be NULL");
        ASSERT_INT_EQ(test_len, result->end - result->start, "Length should match");
        ASSERT_TRUE(strncmp(result->start, test, test_len) == 0, 
                   "Should trim correctly at any alignment");
    }
}

int main(int argc, char** argv) {
    test_case_t tests[] = {
        {"test_ltrim_empty_string", test_ltrim_empty_string},
        {"test_ltrim_no_whitespace", test_ltrim_no_whitespace},
        {"test_ltrim_leading_spaces", test_ltrim_leading_spaces},
        {"test_ltrim_mixed_whitespace", test_ltrim_mixed_whitespace},
        {"test_ltrim_all_whitespace", test_ltrim_all_whitespace},
        {"test_ltrim_trailing_whitespace_preserved", test_ltrim_trailing_whitespace_preserved},
        
        {"test_rtrim_empty_string", test_rtrim_empty_string},
        {"test_rtrim_no_whitespace", test_rtrim_no_whitespace},
        {"test_rtrim_trailing_spaces", test_rtrim_trailing_spaces},
        {"test_rtrim_mixed_whitespace", test_rtrim_mixed_whitespace},
        {"test_rtrim_all_whitespace", test_rtrim_all_whitespace},
        {"test_rtrim_leading_whitespace_preserved", test_rtrim_leading_whitespace_preserved},
        
        {"test_trim_empty_string", test_trim_empty_string},
        {"test_trim_no_whitespace", test_trim_no_whitespace},
        {"test_trim_both_ends", test_trim_both_ends},
        {"test_trim_mixed_whitespace_both_ends", test_trim_mixed_whitespace_both_ends},
        {"test_trim_all_whitespace", test_trim_all_whitespace},
        {"test_trim_internal_whitespace_preserved", test_trim_internal_whitespace_preserved},
        {"test_trim_long_string", test_trim_long_string},
        {"test_trim_various_alignments", test_trim_various_alignments}
    };
    
    test_suite_t suite = TEST_SUITE(
        "faf_string_trim",
        tests,
        sizeof(tests) / sizeof(tests[0]),
        setup,
        teardown
    );
    
    register_test_suite(suite);
    return test_main(argc, argv);
}