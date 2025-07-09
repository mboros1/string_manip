#include "faf_test.h"
#include "faf_string.h"
#include "faf_string_sort.h"
#include <string.h>
#include <stdio.h>
#include <stdbool.h>

// Helper function to check if array is sorted
static bool is_sorted(faf_string* strings, size_t count) {
    for (size_t i = 1; i < count; i++) {
        size_t len_prev = strings[i-1].end - strings[i-1].start;
        size_t len_curr = strings[i].end - strings[i].start;
        size_t min_len = len_prev < len_curr ? len_prev : len_curr;
        
        int cmp = strncmp(strings[i-1].start, strings[i].start, min_len);
        if (cmp > 0) return false;
        if (cmp == 0 && len_prev > len_curr) return false;
    }
    return true;
}

// Helper function to create string array
static faf_string* create_string_array(const char** str_array, size_t count) {
    static faf_string strings[100];  // Static to avoid stack overflow
    for (size_t i = 0; i < count; i++) {
        strings[i] = faf_string_init(str_array[i]);
    }
    return strings;
}

static void test_empty_array(void) {
    faf_string* strings = NULL;
    faf_string_sort(strings, 0);
    // Should not crash
    ASSERT_TRUE(true, "Empty array sort should not crash");
}

static void test_single_element(void) {
    const char* str_array[] = {"hello"};
    faf_string* strings = create_string_array(str_array, 1);
    
    faf_string_sort(strings, 1);
    
    ASSERT_TRUE(is_sorted(strings, 1), "Single element should remain sorted");
    ASSERT_TRUE(strncmp(strings[0].start, "hello", 5) == 0, "Single element should be unchanged");
}

static void test_two_elements_already_sorted(void) {
    const char* str_array[] = {"abc", "def"};
    faf_string* strings = create_string_array(str_array, 2);
    
    faf_string_sort(strings, 2);
    
    ASSERT_TRUE(is_sorted(strings, 2), "Two sorted elements should remain sorted");
    ASSERT_TRUE(strncmp(strings[0].start, "abc", 3) == 0, "First element should be abc");
    ASSERT_TRUE(strncmp(strings[1].start, "def", 3) == 0, "Second element should be def");
}

static void test_two_elements_reverse_order(void) {
    const char* str_array[] = {"def", "abc"};
    faf_string* strings = create_string_array(str_array, 2);
    
    faf_string_sort(strings, 2);
    
    ASSERT_TRUE(is_sorted(strings, 2), "Two reversed elements should be sorted");
    ASSERT_TRUE(strncmp(strings[0].start, "abc", 3) == 0, "First element should be abc");
    ASSERT_TRUE(strncmp(strings[1].start, "def", 3) == 0, "Second element should be def");
}

static void test_identical_strings(void) {
    const char* str_array[] = {"test", "test", "test"};
    faf_string* strings = create_string_array(str_array, 3);
    
    faf_string_sort(strings, 3);
    
    ASSERT_TRUE(is_sorted(strings, 3), "Identical strings should be sorted");
    for (int i = 0; i < 3; i++) {
        ASSERT_TRUE(strncmp(strings[i].start, "test", 4) == 0, "All elements should be 'test'");
    }
}

static void test_small_array(void) {
    const char* str_array[] = {"zebra", "apple", "banana", "cherry"};
    faf_string* strings = create_string_array(str_array, 4);
    
    faf_string_sort(strings, 4);
    
    ASSERT_TRUE(is_sorted(strings, 4), "Small array should be sorted");
    ASSERT_TRUE(strncmp(strings[0].start, "apple", 5) == 0, "First should be apple");
    ASSERT_TRUE(strncmp(strings[1].start, "banana", 6) == 0, "Second should be banana");
    ASSERT_TRUE(strncmp(strings[2].start, "cherry", 6) == 0, "Third should be cherry");
    ASSERT_TRUE(strncmp(strings[3].start, "zebra", 5) == 0, "Fourth should be zebra");
}

static void test_already_sorted(void) {
    const char* str_array[] = {"apple", "banana", "cherry", "date", "elderberry"};
    faf_string* strings = create_string_array(str_array, 5);
    
    faf_string_sort(strings, 5);
    
    ASSERT_TRUE(is_sorted(strings, 5), "Already sorted array should remain sorted");
    ASSERT_TRUE(strncmp(strings[0].start, "apple", 5) == 0, "First should be apple");
    ASSERT_TRUE(strncmp(strings[4].start, "elderberry", 10) == 0, "Last should be elderberry");
}

static void test_reverse_sorted(void) {
    const char* str_array[] = {"zebra", "yellow", "world", "hello", "apple"};
    faf_string* strings = create_string_array(str_array, 5);
    
    faf_string_sort(strings, 5);
    
    ASSERT_TRUE(is_sorted(strings, 5), "Reverse sorted array should be sorted");
    ASSERT_TRUE(strncmp(strings[0].start, "apple", 5) == 0, "First should be apple");
    ASSERT_TRUE(strncmp(strings[4].start, "zebra", 5) == 0, "Last should be zebra");
}

static void test_with_empty_strings(void) {
    const char* str_array[] = {"", "hello", "", "world", ""};
    faf_string* strings = create_string_array(str_array, 5);
    
    faf_string_sort(strings, 5);
    
    ASSERT_TRUE(is_sorted(strings, 5), "Array with empty strings should be sorted");
    ASSERT_TRUE(strings[0].end - strings[0].start == 0, "First should be empty");
    ASSERT_TRUE(strings[1].end - strings[1].start == 0, "Second should be empty");
    ASSERT_TRUE(strings[2].end - strings[2].start == 0, "Third should be empty");
}

static void test_single_character_strings(void) {
    const char* str_array[] = {"z", "a", "m", "b", "y"};
    faf_string* strings = create_string_array(str_array, 5);
    
    faf_string_sort(strings, 5);
    
    ASSERT_TRUE(is_sorted(strings, 5), "Single character strings should be sorted");
    ASSERT_TRUE(strings[0].start[0] == 'a', "First should be 'a'");
    ASSERT_TRUE(strings[1].start[0] == 'b', "Second should be 'b'");
    ASSERT_TRUE(strings[4].start[0] == 'z', "Last should be 'z'");
}

static void test_case_sensitivity(void) {
    const char* str_array[] = {"apple", "Apple", "APPLE", "banana", "Banana"};
    faf_string* strings = create_string_array(str_array, 5);
    
    faf_string_sort(strings, 5);
    
    ASSERT_TRUE(is_sorted(strings, 5), "Case sensitive sort should be sorted");
    // Capital letters come before lowercase in ASCII
    ASSERT_TRUE(strncmp(strings[0].start, "APPLE", 5) == 0, "APPLE should come first");
    ASSERT_TRUE(strncmp(strings[1].start, "Apple", 5) == 0, "Apple should come second");
}

static void test_different_lengths(void) {
    const char* str_array[] = {"a", "aa", "aaa", "aaaa", "aaaaa"};
    faf_string* strings = create_string_array(str_array, 5);
    
    faf_string_sort(strings, 5);
    
    ASSERT_TRUE(is_sorted(strings, 5), "Different length strings should be sorted");
    ASSERT_TRUE(strings[0].end - strings[0].start == 1, "First should be length 1");
    ASSERT_TRUE(strings[4].end - strings[4].start == 5, "Last should be length 5");
}

static void test_prefixes(void) {
    const char* str_array[] = {"test", "testing", "te", "tests", "test123"};
    faf_string* strings = create_string_array(str_array, 5);
    
    faf_string_sort(strings, 5);
    
    ASSERT_TRUE(is_sorted(strings, 5), "Strings with common prefixes should be sorted");
    ASSERT_TRUE(strncmp(strings[0].start, "te", 2) == 0, "Shortest prefix should be first");
}

static void test_medium_array(void) {
    const char* str_array[] = {
        "zebra", "apple", "banana", "cherry", "date", "elderberry",
        "fig", "grape", "honeydew", "kiwi", "lemon", "mango",
        "nectarine", "orange", "papaya", "quince", "raspberry", "strawberry",
        "tangerine", "ugli", "vanilla", "watermelon", "xigua", "yam"
    };
    faf_string* strings = create_string_array(str_array, 24);
    
    faf_string_sort(strings, 24);
    
    ASSERT_TRUE(is_sorted(strings, 24), "Medium array should be sorted");
    ASSERT_TRUE(strncmp(strings[0].start, "apple", 5) == 0, "First should be apple");
    ASSERT_TRUE(strncmp(strings[23].start, "zebra", 5) == 0, "Last should be zebra");
}

static void test_with_duplicates(void) {
    const char* str_array[] = {"apple", "banana", "apple", "cherry", "banana", "apple"};
    faf_string* strings = create_string_array(str_array, 6);
    
    faf_string_sort(strings, 6);
    
    ASSERT_TRUE(is_sorted(strings, 6), "Array with duplicates should be sorted");
    // Should have 3 apples, 2 bananas, 1 cherry
    ASSERT_TRUE(strncmp(strings[0].start, "apple", 5) == 0, "First should be apple");
    ASSERT_TRUE(strncmp(strings[1].start, "apple", 5) == 0, "Second should be apple");
    ASSERT_TRUE(strncmp(strings[2].start, "apple", 5) == 0, "Third should be apple");
    ASSERT_TRUE(strncmp(strings[5].start, "cherry", 6) == 0, "Last should be cherry");
}

static void test_special_characters(void) {
    const char* str_array[] = {"hello!", "hello", "hello?", "hello123", "hello@"};
    faf_string* strings = create_string_array(str_array, 5);
    
    faf_string_sort(strings, 5);
    
    ASSERT_TRUE(is_sorted(strings, 5), "Strings with special characters should be sorted");
    ASSERT_TRUE(strncmp(strings[0].start, "hello", 5) == 0, "Plain 'hello' should be first");
}

static void test_numbers_in_strings(void) {
    const char* str_array[] = {"test10", "test2", "test1", "test20", "test3"};
    faf_string* strings = create_string_array(str_array, 5);
    
    faf_string_sort(strings, 5);
    
    ASSERT_TRUE(is_sorted(strings, 5), "Strings with numbers should be sorted lexicographically");
    // Note: lexicographic sort, not numeric sort
    ASSERT_TRUE(strncmp(strings[0].start, "test1", 5) == 0, "test1 should be first");
    ASSERT_TRUE(strncmp(strings[1].start, "test10", 6) == 0, "test10 should be second");
}

int main(int argc, char** argv) {
    test_case_t tests[] = {
        {"test_empty_array", test_empty_array},
        {"test_single_element", test_single_element},
        {"test_two_elements_already_sorted", test_two_elements_already_sorted},
        {"test_two_elements_reverse_order", test_two_elements_reverse_order},
        {"test_identical_strings", test_identical_strings},
        {"test_small_array", test_small_array},
        {"test_already_sorted", test_already_sorted},
        {"test_reverse_sorted", test_reverse_sorted},
        {"test_with_empty_strings", test_with_empty_strings},
        {"test_single_character_strings", test_single_character_strings},
        {"test_case_sensitivity", test_case_sensitivity},
        {"test_different_lengths", test_different_lengths},
        {"test_prefixes", test_prefixes},
        {"test_medium_array", test_medium_array},
        {"test_with_duplicates", test_with_duplicates},
        {"test_special_characters", test_special_characters},
        {"test_numbers_in_strings", test_numbers_in_strings}
    };
    
    test_suite_t suite = TEST_SUITE(
        "faf_string_sort",
        tests,
        sizeof(tests) / sizeof(tests[0]),
        NULL,
        NULL
    );
    
    register_test_suite(suite);
    return test_main(argc, argv);
}