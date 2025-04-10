#include "faf_test.h"
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

// At the top, with other global variables
static int tests_failed = 0; // Move this into the test framework

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

// Run a specific test suite by name
int run_test_suite(const char* suite_name) {
    int found = 0;
    int result = 0;
    
    stats.start_time = clock();
    
    for (int s = 0; s < suite_count; s++) {
        if (strcmp(test_suites[s].name, suite_name) == 0) {
            found = 1;
            
            printf(ANSI_COLOR_YELLOW "[==========] " ANSI_COLOR_RESET 
                   "Running %d tests from %s\n", 
                   test_suites[s].test_count, test_suites[s].name);
            
            stats.total_tests += test_suites[s].test_count;
            stats.total_suites++;
            
            // Reset tests_failed before each test
            tests_failed = 0;
            
            for (int t = 0; t < test_suites[s].test_count; t++) {
                test_case_t test = test_suites[s].tests[t];
                clock_t test_start, test_end;
                
                // Setup if available
                if (test_suites[s].setup != NULL) {
                    test_suites[s].setup();
                }
                
                // Run the test and track failures
                print_test_header(test_suites[s].name, test.name);
                test_start = clock();
                
                test.func();
                
                test_end = clock();
                double duration = 1000.0 * (test_end - test_start) / CLOCKS_PER_SEC;
                
                if (tests_failed > 0) {
                    print_test_failure(test_suites[s].name, test.name, duration);
                    stats.failed_tests++;
                    result = 1; // Indicate failure
                } else {
                    print_test_success(test_suites[s].name, test.name, duration);
                    stats.passed_tests++;
                }
                
                // Teardown if available
                if (test_suites[s].teardown != NULL) {
                    test_suites[s].teardown();
                }
            }
            
            printf(ANSI_COLOR_YELLOW "[==========] " ANSI_COLOR_RESET 
                   "%d tests from %s completed\n", 
                   test_suites[s].test_count, test_suites[s].name);
            break;
        }
    }
    
    stats.end_time = clock();
    
    if (!found) {
        fprintf(stderr, "Error: Test suite '%s' not found\n", suite_name);
        return -1;
    }
    
    return result;
}

// Run all registered test suites
int run_all_tests(void) {
    stats = (test_stats_t){0}; // Reset statistics
    stats.start_time = clock();
    
    int result = 0;
    printf(ANSI_COLOR_YELLOW "[==========] " ANSI_COLOR_RESET 
           "Running all tests from %d test suites\n", suite_count);
    
    for (int s = 0; s < suite_count; s++) {
        // Run each test suite
        printf(ANSI_COLOR_YELLOW "[----------] " ANSI_COLOR_RESET 
               "Running %d tests from %s\n", 
               test_suites[s].test_count, test_suites[s].name);
        
        stats.total_tests += test_suites[s].test_count;
        stats.total_suites++;
        
        // Reset tests_failed before each test
        tests_failed = 0;
        
        for (int t = 0; t < test_suites[s].test_count; t++) {
            test_case_t test = test_suites[s].tests[t];
            clock_t test_start, test_end;
            
            // Setup if available
            if (test_suites[s].setup != NULL) {
                test_suites[s].setup();
            }
            
            // Run the test
            print_test_header(test_suites[s].name, test.name);
            test_start = clock();
            
            test.func();
            
            test_end = clock();
            double duration = 1000.0 * (test_end - test_start) / CLOCKS_PER_SEC;
            
            if (tests_failed > 0) {
                print_test_failure(test_suites[s].name, test.name, duration);
                stats.failed_tests++;
                result = 1; // Indicate failure
            } else {
                print_test_success(test_suites[s].name, test.name, duration);
                stats.passed_tests++;
            }
            
            // Teardown if available
            if (test_suites[s].teardown != NULL) {
                test_suites[s].teardown();
            }
        }
        
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
    
    return result;
}

// Assertion implementations to replace the ones in faf_string_assert.h
void test_assert_true(int condition, const char* file, int line, const char* message) {
    if (!condition) {
        tests_failed++; // Use the local variable
        
        printf(ANSI_COLOR_RED "    Assertion failed at %s:%d\n" ANSI_COLOR_RESET, file, line);
        if (message && *message) {
            printf("    Message: %s\n", message);
        }
    }
}

void test_assert_str_eq(const char* expected, const char* actual, 
                        const char* file, int line, const char* message) {
    if (strcmp(expected, actual) != 0) {
        tests_failed++; // Use the local variable
        
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
        tests_failed++; // Use the local variable
        
        printf(ANSI_COLOR_RED "    Integer equality assertion failed at %s:%d\n" ANSI_COLOR_RESET, file, line);
        printf("        Expected: %d\n", expected);
        printf("        Actual:   %d\n", actual);
        if (message && *message) {
            printf("    Message: %s\n", message);
        }
    }
}

void run_all_tests_xml_output(void) {
    // Reset statistics
    stats = (test_stats_t){0};
    stats.start_time = clock();
    
    printf("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
    printf("<testsuites>\n");
    
    for (int s = 0; s < suite_count; s++) {
        printf("  <testsuite name=\"%s\" tests=\"%d\">\n", 
               test_suites[s].name, test_suites[s].test_count);
        
        stats.total_tests += test_suites[s].test_count;
        stats.total_suites++;
        
        // Reset test failure counter before each suite
        tests_failed = 0;
        
        for (int t = 0; t < test_suites[s].test_count; t++) {
            test_case_t test = test_suites[s].tests[t];
            clock_t test_start, test_end;
            
            // Setup if available
            if (test_suites[s].setup != NULL) {
                test_suites[s].setup();
            }
            
            // Run the test
            test_start = clock();
            int fail_before = tests_failed;
            
            test.func();
            
            test_end = clock();
            double duration = 1000.0 * (test_end - test_start) / CLOCKS_PER_SEC;
            
            printf("    <testcase name=\"%s\" classname=\"%s\" time=\"%.3f\"", 
                   test.name, test_suites[s].name, duration/1000.0);
            
            if (tests_failed > fail_before) {
                printf(">\n");
                printf("      <failure message=\"Test failed\"></failure>\n");
                printf("    </testcase>\n");
                stats.failed_tests++;
            } else {
                printf("/>\n");
                stats.passed_tests++;
            }
            
            // Teardown if available
            if (test_suites[s].teardown != NULL) {
                test_suites[s].teardown();
            }
        }
        
        printf("  </testsuite>\n");
    }
    
    printf("</testsuites>\n");
    
    stats.end_time = clock();
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
            const char* suite_name = argv[1];
            const char* test_name = dot + 1;
            
            // Find and run the specific test
            for (int s = 0; s < suite_count; s++) {
                if (strcmp(test_suites[s].name, suite_name) == 0) {
                    for (int t = 0; t < test_suites[s].test_count; t++) {
                        if (strcmp(test_suites[s].tests[t].name, test_name) == 0) {
                            // Run just this test
                            printf(ANSI_COLOR_YELLOW "[==========] " ANSI_COLOR_RESET 
                                  "Running 1 test from %s\n", suite_name);
                            
                            if (test_suites[s].setup) test_suites[s].setup();
                            
                            clock_t test_start = clock();
                            print_test_header(suite_name, test_name);
                            
                            // Reset test failures before running test
                            tests_failed = 0;
                            
                            test_suites[s].tests[t].func();
                            
                            clock_t test_end = clock();
                            double duration = 1000.0 * (test_end - test_start) / CLOCKS_PER_SEC;
                            
                            if (tests_failed > 0) {
                                print_test_failure(suite_name, test_name, duration);
                            } else {
                                print_test_success(suite_name, test_name, duration);
                            }
                            
                            if (test_suites[s].teardown) test_suites[s].teardown();
                            
                            printf(ANSI_COLOR_YELLOW "[==========] " ANSI_COLOR_RESET 
                                  "1 test from %s completed\n", suite_name);
                            
                            return tests_failed > 0 ? 1 : 0;
                        }
                    }
                    
                    printf("Test %s.%s not found\n", suite_name, test_name);
                    return 1;
                }
            }
            
            printf("Test suite %s not found\n", suite_name);
            return 1;
        }
        
        // Support for running a specific test suite
        for (int s = 0; s < suite_count; s++) {
            if (strcmp(test_suites[s].name, argv[1]) == 0) {
                return run_test_suite(argv[1]);
            }
        }
    }
    
    // Normal execution - run all tests
    return run_all_tests();
}