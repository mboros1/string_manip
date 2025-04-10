#include "faf_string.h"
#include "faf_string_strlen.h"
#include "faf_test.h"

#include <stdio.h>
#include <string.h>

#define FAF_STRING_ASSERT_IMPL
#include "faf_string_assert.h"

#define NDEBUG

int tests_run = 0;

char error_str[10 * 1024];

// Helper function to print faf_string
void print_faf_string(faf_string str) {
    printf("faf_string: string=\"%s\", start=%p, end=%p\n", str.start, (void*)str.start, (void *)str.end);
}

// Test function for faf_string_init
void test_faf_string_init() {
    tests_run++;
    printf("Testing faf_string_init...\n");
    const char *test_str = "Hello, World!";
    faf_string str = faf_string_init(test_str);

    print_faf_string(str);

    int expected = strlen(test_str);
    int actual = str.end - str.start;
    ASSERT_INT_EQ(expected, actual, "String lengths do not match");
}

// Test function for faf_string_init with empty string
void test_faf_string_init_empty() {
    tests_run++;
    printf("Testing faf_string_init with empty string...\n");
    const char *test_str = "";
    faf_string str = faf_string_init(test_str);

    print_faf_string(str);

    int expected = strlen(test_str);
    int actual = str.end - str.start;
    ASSERT_INT_EQ(expected, actual, "String lengths do not match");
}

// Test function for faf_string_init_n
void test_faf_string_init_n() {
    tests_run++;
    printf("Testing faf_string_init_n...\n");
    const char * test_str = "Hello, World!";
    faf_string str = faf_string_init_n(test_str, strlen(test_str));

    print_faf_string(str);

    int expected = strlen(test_str);
    int actual = str.end - str.start;
    ASSERT_INT_EQ(expected, actual, "String lengths do not match");
}

// Test function for faf_string_init_n with empty string
void test_faf_string_init_n_empty() {
    tests_run++;
    printf("Testing faf_string_init_n with empty string...\n");
    const char *test_str = "";
    faf_string str = faf_string_init_n(test_str, strlen(test_str));

    print_faf_string(str);

    int expected = strlen(test_str);
    int actual = str.end - str.start;
    ASSERT_INT_EQ(expected, actual, "String lengths do not match");
}

// Test case definitions
test_case_t string_init_tests[] = {
    {"string_init", test_faf_string_init},
    {"string_init_empty", test_faf_string_init_empty},
    {"string_init_n", test_faf_string_init_n},
    {"string_init_n_empty", test_faf_string_init_n_empty}
};

// Setup and teardown functions
void string_init_setup(void) {
    // Any setup code needed before each test
}

void string_init_teardown(void) {
    // Any cleanup code needed after each test
}

// Main function
int main(int argc, char** argv) {
    // Register test suite
    test_suite_t suite = TEST_SUITE(
        "StringInit", 
        string_init_tests,
        sizeof(string_init_tests) / sizeof(string_init_tests[0]),
        string_init_setup,
        string_init_teardown
    );
    register_test_suite(suite);
    
    // Normal execution
    return test_main(argc, argv);
}
