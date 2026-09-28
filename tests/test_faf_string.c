#include "faf_string.h"
#include "faf_string_strlen.h"
#include "faf_test.h"

#include <stdio.h>
#include <string.h>
#if FAF_TEST_HAVE_GUARD_PAGES
#include <sys/mman.h>
#include <unistd.h>
#endif

// Helper function to print faf_string
static void print_faf_string(faf_string str) {
    printf("faf_string: string=\"%s\", start=%p, end=%p\n", str.start, (void*)str.start, (void *)str.end);
}

// Test function for faf_string_init
static void test_faf_string_init(void) {
    printf("Testing faf_string_init...\n");
    const char *test_str = "Hello, World!";
    faf_string str = faf_string_init(test_str);

    print_faf_string(str);

    int expected = strlen(test_str);
    int actual = str.end - str.start;
    ASSERT_INT_EQ(expected, actual, "String lengths do not match");
}

// Test function for faf_string_init with empty string
static void test_faf_string_init_empty(void) {
    printf("Testing faf_string_init with empty string...\n");
    const char *test_str = "";
    faf_string str = faf_string_init(test_str);

    print_faf_string(str);

    int expected = strlen(test_str);
    int actual = str.end - str.start;
    ASSERT_INT_EQ(expected, actual, "String lengths do not match");
}

// Test function for faf_string_init_n
static void test_faf_string_init_n(void) {
    printf("Testing faf_string_init_n...\n");
    const char * test_str = "Hello, World!";
    faf_string str = faf_string_init_n(test_str, strlen(test_str));

    print_faf_string(str);

    int expected = strlen(test_str);
    int actual = str.end - str.start;
    ASSERT_INT_EQ(expected, actual, "String lengths do not match");
}

// Test function for faf_string_init_n with empty string
static void test_faf_string_init_n_empty(void) {
    printf("Testing faf_string_init_n with empty string...\n");
    const char *test_str = "";
    faf_string str = faf_string_init_n(test_str, strlen(test_str));

    print_faf_string(str);

    int expected = strlen(test_str);
    int actual = str.end - str.start;
    ASSERT_INT_EQ(expected, actual, "String lengths do not match");
}

static void test_strlen_lengths(void) {
  // every length and start alignment in the first few blocks
  char buf[96];
  for (size_t align = 0; align < 16; ++align) {
    for (size_t len = 0; len < 64; ++len) {
      memset(buf, 'x', sizeof(buf));
      buf[align + len] = '\0';
      ASSERT_INT_EQ((int)len, (int)faf_string_strlen(buf + align),
                    "strlen incorrect");
    }
  }
}

static void test_strlen_page_boundary(void) {
  // Put each string so its NUL is the last byte of a readable page, followed
  // by an unreadable page. A 16 byte load that crosses into it would crash.
  TEST_REQUIRE(FAF_TEST_HAVE_GUARD_PAGES, "needs guard pages (mmap/mprotect)");
#if FAF_TEST_HAVE_GUARD_PAGES
  long page = sysconf(_SC_PAGESIZE);
  char *mem = mmap(NULL, 2 * page, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANON, -1, 0);
  ASSERT_TRUE(mem != MAP_FAILED, "mmap failed");
  if (mem == MAP_FAILED)
    return;
  ASSERT_INT_EQ(0, mprotect(mem + page, page, PROT_NONE), "mprotect failed");

  memset(mem, 'x', page);
  mem[page - 1] = '\0';
  for (size_t len = 0; len < 40; ++len) {
    char *str = mem + page - 1 - len;
    ASSERT_INT_EQ((int)len, (int)faf_string_strlen(str),
                  "strlen at page boundary incorrect");
  }
  munmap(mem, 2 * page);
#endif
}

// Test case definitions
static test_case_t string_init_tests[] = {
    {"string_init", test_faf_string_init},
    {"string_init_empty", test_faf_string_init_empty},
    {"string_init_n", test_faf_string_init_n},
    {"string_init_n_empty", test_faf_string_init_n_empty},
    {"strlen_lengths", test_strlen_lengths},
    {"strlen_page_boundary", test_strlen_page_boundary},
};

// Setup and teardown functions
static void string_init_setup(void) {
    // Any setup code needed before each test
}

static void string_init_teardown(void) {
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
