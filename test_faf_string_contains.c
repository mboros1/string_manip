#include "faf_test.h"
#include "faf_string.h"
#include "faf_string_contains.h"
#include <string.h>
#include <stdio.h>
#include <stdbool.h>

static void test_empty_needle(void) {
    faf_string haystack = faf_string_init("hello world");
    faf_string needle = faf_string_init("");
    
    bool result = faf_string_contains(haystack, needle);
    ASSERT_TRUE(result == true, "Empty needle should always be found");
}

static void test_empty_haystack(void) {
    faf_string haystack = faf_string_init("");
    faf_string needle = faf_string_init("hello");
    
    bool result = faf_string_contains(haystack, needle);
    ASSERT_TRUE(result == false, "Non-empty needle should not be found in empty haystack");
}

static void test_both_empty(void) {
    faf_string haystack = faf_string_init("");
    faf_string needle = faf_string_init("");
    
    bool result = faf_string_contains(haystack, needle);
    ASSERT_TRUE(result == true, "Empty needle should be found in empty haystack");
}

static void test_single_char_found(void) {
    faf_string haystack = faf_string_init("hello");
    faf_string needle = faf_string_init("e");
    
    bool result = faf_string_contains(haystack, needle);
    ASSERT_TRUE(result == true, "Single character should be found");
}

static void test_single_char_not_found(void) {
    faf_string haystack = faf_string_init("hello");
    faf_string needle = faf_string_init("x");
    
    bool result = faf_string_contains(haystack, needle);
    ASSERT_TRUE(result == false, "Single character should not be found");
}

static void test_needle_at_beginning(void) {
    faf_string haystack = faf_string_init("hello world");
    faf_string needle = faf_string_init("hello");
    
    bool result = faf_string_contains(haystack, needle);
    ASSERT_TRUE(result == true, "Needle at beginning should be found");
}

static void test_needle_at_end(void) {
    faf_string haystack = faf_string_init("hello world");
    faf_string needle = faf_string_init("world");
    
    bool result = faf_string_contains(haystack, needle);
    ASSERT_TRUE(result == true, "Needle at end should be found");
}

static void test_needle_in_middle(void) {
    faf_string haystack = faf_string_init("hello world test");
    faf_string needle = faf_string_init("world");
    
    bool result = faf_string_contains(haystack, needle);
    ASSERT_TRUE(result == true, "Needle in middle should be found");
}

static void test_needle_longer_than_haystack(void) {
    faf_string haystack = faf_string_init("hi");
    faf_string needle = faf_string_init("hello");
    
    bool result = faf_string_contains(haystack, needle);
    ASSERT_TRUE(result == false, "Needle longer than haystack should not be found");
}

static void test_needle_equals_haystack(void) {
    faf_string haystack = faf_string_init("hello");
    faf_string needle = faf_string_init("hello");
    
    bool result = faf_string_contains(haystack, needle);
    ASSERT_TRUE(result == true, "Needle equal to haystack should be found");
}

static void test_partial_match_then_mismatch(void) {
    faf_string haystack = faf_string_init("hello world");
    faf_string needle = faf_string_init("hell0");  // 0 instead of o
    
    bool result = faf_string_contains(haystack, needle);
    ASSERT_TRUE(result == false, "Partial match then mismatch should not be found");
}

static void test_repeated_characters(void) {
    faf_string haystack = faf_string_init("aaaaabaaaa");
    faf_string needle = faf_string_init("aaab");
    
    bool result = faf_string_contains(haystack, needle);
    ASSERT_TRUE(result == true, "Pattern with repeated chars should be found");
}

static void test_repeated_characters_not_found(void) {
    faf_string haystack = faf_string_init("aaaaabaaaa");
    faf_string needle = faf_string_init("aaac");
    
    bool result = faf_string_contains(haystack, needle);
    ASSERT_TRUE(result == false, "Pattern with repeated chars should not be found");
}

static void test_multiple_potential_matches(void) {
    faf_string haystack = faf_string_init("abcabcabcabc");
    faf_string needle = faf_string_init("abcabc");
    
    bool result = faf_string_contains(haystack, needle);
    ASSERT_TRUE(result == true, "Pattern with multiple potential matches should be found");
}

static void test_overlapping_patterns(void) {
    faf_string haystack = faf_string_init("abababa");
    faf_string needle = faf_string_init("ababa");
    
    bool result = faf_string_contains(haystack, needle);
    ASSERT_TRUE(result == true, "Overlapping pattern should be found");
}

static void test_case_sensitive(void) {
    faf_string haystack = faf_string_init("Hello World");
    faf_string needle = faf_string_init("hello");
    
    bool result = faf_string_contains(haystack, needle);
    ASSERT_TRUE(result == false, "Search should be case sensitive");
}

static void test_with_whitespace(void) {
    faf_string haystack = faf_string_init("hello world test");
    faf_string needle = faf_string_init("world test");
    
    bool result = faf_string_contains(haystack, needle);
    ASSERT_TRUE(result == true, "Pattern with whitespace should be found");
}

static void test_with_numbers_and_symbols(void) {
    faf_string haystack = faf_string_init("abc123!@#def");
    faf_string needle = faf_string_init("123!@#");
    
    bool result = faf_string_contains(haystack, needle);
    ASSERT_TRUE(result == true, "Pattern with numbers and symbols should be found");
}

static void test_long_haystack_short_needle(void) {
    const char* long_str = "abcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    faf_string haystack = faf_string_init(long_str);
    faf_string needle = faf_string_init("xyz");
    
    bool result = faf_string_contains(haystack, needle);
    ASSERT_TRUE(result == true, "Short needle in long haystack should be found");
}

static void test_long_haystack_long_needle(void) {
    const char* long_str = "abcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    faf_string haystack = faf_string_init(long_str);
    faf_string needle = faf_string_init("uvwxyz0123456789ABCDEF");
    
    bool result = faf_string_contains(haystack, needle);
    ASSERT_TRUE(result == true, "Long needle in long haystack should be found");
}

static void test_needle_at_very_end(void) {
    faf_string haystack = faf_string_init("abcdefgh");
    faf_string needle = faf_string_init("h");
    
    bool result = faf_string_contains(haystack, needle);
    ASSERT_TRUE(result == true, "Needle at very end should be found");
}

static void test_needle_almost_at_end(void) {
    faf_string haystack = faf_string_init("abcdefgh");
    faf_string needle = faf_string_init("fgh");
    
    bool result = faf_string_contains(haystack, needle);
    ASSERT_TRUE(result == true, "Needle almost at end should be found");
}

static void test_performance_worst_case(void) {
    // Test case that could cause quadratic behavior in naive algorithms
    faf_string haystack = faf_string_init("aaaaaaaaaaaaaaaaaaaaaaaaaaaab");
    faf_string needle = faf_string_init("aaaaaaaaab");
    
    bool result = faf_string_contains(haystack, needle);
    ASSERT_TRUE(result == true, "Worst case pattern should be found efficiently");
}

static void test_performance_worst_case_not_found(void) {
    // Test case that could cause quadratic behavior in naive algorithms
    faf_string haystack = faf_string_init("aaaaaaaaaaaaaaaaaaaaaaaaaaaab");
    faf_string needle = faf_string_init("aaaaaaaac");
    
    bool result = faf_string_contains(haystack, needle);
    ASSERT_TRUE(result == false, "Worst case pattern should not be found efficiently");
}

int main(int argc, char** argv) {
    test_case_t tests[] = {
        {"test_empty_needle", test_empty_needle},
        {"test_empty_haystack", test_empty_haystack},
        {"test_both_empty", test_both_empty},
        {"test_single_char_found", test_single_char_found},
        {"test_single_char_not_found", test_single_char_not_found},
        {"test_needle_at_beginning", test_needle_at_beginning},
        {"test_needle_at_end", test_needle_at_end},
        {"test_needle_in_middle", test_needle_in_middle},
        {"test_needle_longer_than_haystack", test_needle_longer_than_haystack},
        {"test_needle_equals_haystack", test_needle_equals_haystack},
        {"test_partial_match_then_mismatch", test_partial_match_then_mismatch},
        {"test_repeated_characters", test_repeated_characters},
        {"test_repeated_characters_not_found", test_repeated_characters_not_found},
        {"test_multiple_potential_matches", test_multiple_potential_matches},
        {"test_overlapping_patterns", test_overlapping_patterns},
        {"test_case_sensitive", test_case_sensitive},
        {"test_with_whitespace", test_with_whitespace},
        {"test_with_numbers_and_symbols", test_with_numbers_and_symbols},
        {"test_long_haystack_short_needle", test_long_haystack_short_needle},
        {"test_long_haystack_long_needle", test_long_haystack_long_needle},
        {"test_needle_at_very_end", test_needle_at_very_end},
        {"test_needle_almost_at_end", test_needle_almost_at_end},
        {"test_performance_worst_case", test_performance_worst_case},
        {"test_performance_worst_case_not_found", test_performance_worst_case_not_found}
    };
    
    test_suite_t suite = TEST_SUITE(
        "faf_string_contains",
        tests,
        sizeof(tests) / sizeof(tests[0]),
        NULL,
        NULL
    );
    
    register_test_suite(suite);
    return test_main(argc, argv);
}