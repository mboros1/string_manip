#include "faf_test.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// ANSI color codes for better output formatting
#define ANSI_COLOR_RED     "\x1b[31m"
#define ANSI_COLOR_GREEN   "\x1b[32m"
#define ANSI_COLOR_YELLOW  "\x1b[33m"
#define ANSI_COLOR_BLUE    "\x1b[34m"
#define ANSI_COLOR_RESET   "\x1b[0m"

// Maximum number of test suites we can register
#define MAX_TEST_SUITES 50

// Internal structure to keep track of registered test suites
static test_suite_t test_suites[MAX_TEST_SUITES];
static int suite_count = 0;

// Statistics tracking
typedef struct {
    int total_suites;
    int total_tests;
    int passed_tests;
    int failed_tests;
    int skipped_tests;
    clock_t start_time;
    clock_t end_time;
} test_stats_t;

static test_stats_t stats = {0};

// Did the current test fail an assertion? Set by the test_assert_*
// functions, reset by run_one() before each test.
static bool test_failed = false;

// Register a test suite
void register_test_suite(test_suite_t suite) {
    if (suite_count >= MAX_TEST_SUITES) {
        fprintf(stderr, "Error: Maximum number of test suites reached\n");
        return;
    }
    
    test_suites[suite_count++] = suite;
}

// Helper functions for test execution
static void print_test_header(const char* suite_name, const char* test_name) {
    printf(ANSI_COLOR_BLUE "[ RUN      ] " ANSI_COLOR_RESET "%s.%s\n", suite_name, test_name);
}

static void print_test_success(const char* suite_name, const char* test_name, double duration) {
    printf(ANSI_COLOR_GREEN "[       OK ] " ANSI_COLOR_RESET "%s.%s (%.2f ms)\n", 
           suite_name, test_name, duration);
}

static void print_test_failure(const char* suite_name, const char* test_name, double duration) {
    printf(ANSI_COLOR_RED "[  FAILED  ] " ANSI_COLOR_RESET "%s.%s (%.2f ms)\n", 
           suite_name, test_name, duration);
}

// Run one test with its suite's setup/teardown, print its result line and
// count it. Returns true if it failed. Every runner goes through here, so
// all of them judge a test the same way.
static bool run_one(const test_suite_t* suite, const test_case_t* test) {
    test_failed = false;

    if (suite->setup != NULL) {
        suite->setup();
    }

    print_test_header(suite->name, test->name);
    clock_t test_start = clock();

    test->func();

    double duration = 1000.0 * (clock() - test_start) / CLOCKS_PER_SEC;
    if (test_failed) {
        print_test_failure(suite->name, test->name, duration);
        stats.failed_tests++;
    } else {
        print_test_success(suite->name, test->name, duration);
        stats.passed_tests++;
    }

    if (suite->teardown != NULL) {
        suite->teardown();
    }
    return test_failed;
}

// Run every test in `suite`. Returns true if any failed.
static bool run_suite(const test_suite_t* suite) {
    stats.total_tests += suite->test_count;
    stats.total_suites++;

    bool failed = false;
    for (int t = 0; t < suite->test_count; t++) {
        failed |= run_one(suite, &suite->tests[t]);
    }
    return failed;
}

static const test_suite_t* find_suite(const char* name) {
    for (int s = 0; s < suite_count; s++) {
        if (strcmp(test_suites[s].name, name) == 0) {
            return &test_suites[s];
        }
    }
    return NULL;
}

// Run a specific test suite by name
int run_test_suite(const char* suite_name) {
    const test_suite_t* suite = find_suite(suite_name);
    if (suite == NULL) {
        fprintf(stderr, "Error: Test suite '%s' not found\n", suite_name);
        return -1;
    }

    stats.start_time = clock();
    printf(ANSI_COLOR_YELLOW "[==========] " ANSI_COLOR_RESET
           "Running %d tests from %s\n", suite->test_count, suite->name);

    bool failed = run_suite(suite);

    printf(ANSI_COLOR_YELLOW "[==========] " ANSI_COLOR_RESET
           "%d tests from %s completed\n", suite->test_count, suite->name);
    stats.end_time = clock();

    return failed ? 1 : 0;
}

// Run all registered test suites
int run_all_tests(void) {
    stats = (test_stats_t){0}; // Reset statistics
    stats.start_time = clock();

    printf(ANSI_COLOR_YELLOW "[==========] " ANSI_COLOR_RESET
           "Running all tests from %d test suites\n", suite_count);

    bool failed = false;
    for (int s = 0; s < suite_count; s++) {
        printf(ANSI_COLOR_YELLOW "[----------] " ANSI_COLOR_RESET
               "Running %d tests from %s\n",
               test_suites[s].test_count, test_suites[s].name);

        failed |= run_suite(&test_suites[s]);

        printf(ANSI_COLOR_YELLOW "[----------] " ANSI_COLOR_RESET
               "%d tests from %s completed\n",
               test_suites[s].test_count, test_suites[s].name);
    }

    stats.end_time = clock();
    double total_duration = 1000.0 * (stats.end_time - stats.start_time) / CLOCKS_PER_SEC;

    // Print final summary
    printf(ANSI_COLOR_YELLOW "[==========] " ANSI_COLOR_RESET
           "%d tests from %d test suites ran. (%.2f ms total)\n",
           stats.total_tests, stats.total_suites, total_duration);

    printf(ANSI_COLOR_GREEN "[  PASSED  ] " ANSI_COLOR_RESET
           "%d tests\n", stats.passed_tests);

    if (stats.failed_tests > 0) {
        printf(ANSI_COLOR_RED "[  FAILED  ] " ANSI_COLOR_RESET
               "%d tests\n", stats.failed_tests);
    }

    if (stats.skipped_tests > 0) {
        printf(ANSI_COLOR_YELLOW "[  SKIPPED ] " ANSI_COLOR_RESET
               "%d tests\n", stats.skipped_tests);
    }

    return failed ? 1 : 0;
}

// Assertion implementations
void test_assert_true(int condition, const char* file, int line, const char* message) {
    if (!condition) {
        test_failed = true;
        
        printf(ANSI_COLOR_RED "    Assertion failed at %s:%d\n" ANSI_COLOR_RESET, file, line);
        if (message && *message) {
            printf("    Message: %s\n", message);
        }
    }
}

void test_assert_str_eq(const char* expected, const char* actual, 
                        const char* file, int line, const char* message) {
    if (strcmp(expected, actual) != 0) {
        test_failed = true;
        
        printf(ANSI_COLOR_RED "    String equality assertion failed at %s:%d\n" ANSI_COLOR_RESET, file, line);
        printf("        Expected: \"%s\"\n", expected);
        printf("        Actual:   \"%s\"\n", actual);
        if (message && *message) {
            printf("    Message: %s\n", message);
        }
    }
}

void test_assert_int_eq(int expected, int actual, 
                       const char* file, int line, const char* message) {
    if (expected != actual) {
        test_failed = true;
        
        printf(ANSI_COLOR_RED "    Integer equality assertion failed at %s:%d\n" ANSI_COLOR_RESET, file, line);
        printf("        Expected: %d\n", expected);
        printf("        Actual:   %d\n", actual);
        if (message && *message) {
            printf("    Message: %s\n", message);
        }
    }
}

void run_tests_gtest_format(void) {
    for (int s = 0; s < suite_count; s++) {
        for (int t = 0; t < test_suites[s].test_count; t++) {
            test_case_t test = test_suites[s].tests[t];
            
            // Print in Google Test format for discovery
            printf("%s.%s\n", test_suites[s].name, test.name);
        }
    }
}

// Run the single test `suite_name`.`test_name`
static int run_single_test(const char* suite_name, const char* test_name) {
    const test_suite_t* suite = find_suite(suite_name);
    if (suite == NULL) {
        printf("Test suite %s not found\n", suite_name);
        return 1;
    }
    for (int t = 0; t < suite->test_count; t++) {
        if (strcmp(suite->tests[t].name, test_name) == 0) {
            printf(ANSI_COLOR_YELLOW "[==========] " ANSI_COLOR_RESET
                   "Running 1 test from %s\n", suite_name);

            bool failed = run_one(suite, &suite->tests[t]);

            printf(ANSI_COLOR_YELLOW "[==========] " ANSI_COLOR_RESET
                   "1 test from %s completed\n", suite_name);
            return failed ? 1 : 0;
        }
    }
    printf("Test %s.%s not found\n", suite_name, test_name);
    return 1;
}

int test_main(int argc, char** argv) {
    // Check for special modes
    if (argc > 1) {
        if (strcmp(argv[1], "--gtest_format") == 0 ||
            strcmp(argv[1], "--gtest_list_tests") == 0) {
            // Just output test names in GTest format for discovery
            run_tests_gtest_format();
            return 0;
        }

        // Support for running individual tests
        // Format: test_executable "SuiteName.TestName"
        char* dot = strchr(argv[1], '.');
        if (dot) {
            *dot = '\0'; // Split the string
            return run_single_test(argv[1], dot + 1);
        }

        // Support for running a specific test suite
        if (find_suite(argv[1]) != NULL) {
            return run_test_suite(argv[1]);
        }
    }

    // Normal execution - run all tests
    return run_all_tests();
}
