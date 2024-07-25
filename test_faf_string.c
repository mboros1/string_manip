#include "faf_string.h"
#include <stdio.h>
#include <string.h>

#define FAF_STRING_ASSERT_IMPL
#include "faf_string_assert.h"


#define NDEBUG

// Helper function to print faf_string
void print_faf_string(faf_string str) {
    printf("faf_string: string=\"%s\", start=%p, end=%p\n", str.start, (void*)str.start, (void *)str.end);
}

// Test function for faf_string_init
void test_faf_string_init() {
    const char *test_str = "Hello, World!";
    faf_string str = faf_string_init(test_str);

    printf("Test faf_string_init:\n");
    print_faf_string(str);


    int expected = strlen(test_str);
    int actual = str.end - str.start;
    char error_str[100];
    sprintf(error_str, "String lengths do not match: actual: %d, expected %d\n", actual, expected);
    assert(actual == expected, error_str);
}

// Test function for faf_string_init with empty string
void test_faf_string_init_empty() {
    const char *test_str = "";
    faf_string str = faf_string_init(test_str);

    printf("Test faf_string_init with empty string:\n");
    print_faf_string(str);

    int expected = strlen(test_str);
    int actual = str.end - str.start;
    char error_str[100];
    sprintf(error_str, "String lengths do not match: actual: %d, expected %d\n", actual, expected);
    assert(actual == expected, error_str);
}

// Test function for faf_string_init_n
void test_faf_string_init_n() {
    const char * test_str = "Hello, World!";
    faf_string str = faf_string_init_n(test_str, strlen(test_str));

    printf("Test faf_string_init_n:\n");
    print_faf_string(str);

    int expected = strlen(test_str);
    int actual = str.end - str.start;
    char error_str[100];
    sprintf(error_str, "String lengths do not match: actual: %d, expected %d\n", actual, expected);
    assert(actual == expected, error_str);
}

// Test function for faf_string_init_n with empty string
void test_faf_string_init_n_empty() {
    const char *test_str = "";
    faf_string str = faf_string_init_n(test_str, strlen(test_str));

    printf("Test faf_string_init_n:\n");
    print_faf_string(str);

    int expected = strlen(test_str);
    int actual = str.end - str.start;
    char error_str[100];
    sprintf(error_str, "String lengths do not match: actual: %d, expected %d\n", actual, expected);
    assert(actual == expected, error_str);
}

// Main function to run all tests
int main() {
    test_faf_string_init();
    test_faf_string_init_empty();
    test_faf_string_init_n();
    test_faf_string_init_n_empty();

    if (tests_failed) {
        printf("%d tests failed!\n", tests_failed);
    } else {
        printf("All tests passed\n");
    }
    return 0;
}
