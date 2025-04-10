#ifndef FAF_TEST_H
#define FAF_TEST_H

#include <stddef.h>

// Test function type
typedef void (*test_func_t)(void);

// Test case structure
typedef struct {
    const char* name;
    test_func_t func;
} test_case_t;

// Test suite structure
typedef struct {
    const char* name;
    test_case_t* tests;
    int test_count;
    void (*setup)(void);
    void (*teardown)(void);
} test_suite_t;

// Main test registration and runner
void register_test_suite(test_suite_t suite);
int run_all_tests(void);
int run_test_suite(const char* suite_name);

// TestMate/GTest integration support
void run_tests_gtest_format(void);

// Main function to be used by all test files
int test_main(int argc, char** argv);

// Assertion functions
void test_assert_true(int condition, const char* file, int line, const char* message);
void test_assert_str_eq(const char* expected, const char* actual, 
                       const char* file, int line, const char* message);
void test_assert_int_eq(int expected, int actual, 
                       const char* file, int line, const char* message);

// Assertion macros
#define ASSERT_TRUE(condition, message) \
    test_assert_true((condition), __FILE__, __LINE__, (message))

#define ASSERT_FALSE(condition, message) \
    test_assert_true(!(condition), __FILE__, __LINE__, (message))

#define ASSERT_STR_EQ(expected, actual, message) \
    test_assert_str_eq((expected), (actual), __FILE__, __LINE__, (message))

#define ASSERT_INT_EQ(expected, actual, message) \
    test_assert_int_eq((expected), (actual), __FILE__, __LINE__, (message))

// Helper macro to create an array of test cases
#define TEST_CASES(...) (test_case_t[]){__VA_ARGS__}

// Helper macro to define a test suite
#define TEST_SUITE(name, tests_array, count, setup_func, teardown_func) \
    { (name), (tests_array), (count), (setup_func), (teardown_func) }

#endif // FAF_TEST_H