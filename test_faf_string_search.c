#include "faf.h"
#include "faf_test.h"

#include <stdio.h>
#include <string.h>

#define S(lit) faf_string_init(lit)

void test_eq_prefix_suffix(void) {
  ASSERT_TRUE(faf_string_eq(S("hello"), S("hello")), "eq");
  ASSERT_FALSE(faf_string_eq(S("hello"), S("hellO")), "eq differs");
  ASSERT_FALSE(faf_string_eq(S("hello"), S("hello!")), "eq length");
  ASSERT_TRUE(faf_string_eq(S(""), S("")), "eq empty");
  ASSERT_TRUE(faf_string_starts_with(S("key=value"), S("key")), "starts_with");
  ASSERT_TRUE(faf_string_starts_with(S("key"), S("")), "starts_with empty");
  ASSERT_FALSE(faf_string_starts_with(S("ke"), S("key")), "starts_with longer");
  ASSERT_TRUE(faf_string_ends_with(S("file.tar.gz"), S(".gz")), "ends_with");
  ASSERT_FALSE(faf_string_ends_with(S("file.tar.gz"), S(".tar")), "ends_with no");
  ASSERT_FALSE(faf_string_ends_with(S("gz"), S(".gz")), "ends_with longer");
}

// naive reference search
static size_t naive_find(const char *s, const char *sub, int last) {
  size_t n = strlen(s), m = strlen(sub), found = FAF_NPOS;
  if (m > n)
    return FAF_NPOS;
  for (size_t i = 0; i + m <= n; ++i)
    if (memcmp(s + i, sub, m) == 0) {
      found = i;
      if (!last)
        return i;
    }
  return found;
}

void test_find_rfind(void) {
  const char *hay = "the cat sat on the mat with the other cat; cathedral catalog";
  const char *subs[] = {"cat", "the", "t", "catalog", "g", "dog", "cat;",
                        "the cat sat on the mat with the other cat; cathedral catalog",
                        "xthe"};
  for (size_t i = 0; i < sizeof(subs) / sizeof(subs[0]); ++i) {
    ASSERT_TRUE(faf_string_find(S(hay), S(subs[i])) == naive_find(hay, subs[i], 0),
                "find differs from naive");
    ASSERT_TRUE(faf_string_rfind(S(hay), S(subs[i])) == naive_find(hay, subs[i], 1),
                "rfind differs from naive");
  }
  ASSERT_TRUE(faf_string_find(S("abc"), S("")) == 0, "find empty");
  ASSERT_TRUE(faf_string_rfind(S("abc"), S("")) == 3, "rfind empty");
  ASSERT_TRUE(faf_string_find(S(""), S("a")) == FAF_NPOS, "find in empty");
  ASSERT_TRUE(faf_string_find(S("aaab"), S("aab")) == 1, "overlapping prefix");
  ASSERT_TRUE(faf_string_find_char(S("a,b"), ',') == 1, "find_char");
  ASSERT_TRUE(faf_string_find_char(S("ab"), ',') == FAF_NPOS, "find_char none");
  ASSERT_TRUE(faf_string_rfind_char(S("a,b,c"), ',') == 3, "rfind_char");
  ASSERT_TRUE(faf_string_contains(S(hay), S("cathedral")), "contains");
  ASSERT_FALSE(faf_string_contains(S(hay), S("dogs")), "contains none");
}

void test_count_find_any(void) {
  ASSERT_INT_EQ(4, (int)faf_string_count(S("cat cat cat cathedral"), S("cat")), "count");
  ASSERT_INT_EQ(2, (int)faf_string_count(S("aaaa"), S("aa")), "count non-overlapping");
  ASSERT_INT_EQ(3, (int)faf_string_count(S("a,b,c,"), S(",")), "count char");
  ASSERT_INT_EQ(4, (int)faf_string_count(S("abc"), S("")), "count empty");
  ASSERT_INT_EQ(0, (int)faf_string_count(S("abc"), S("d")), "count none");
  ASSERT_TRUE(faf_string_find_any(S("key: value; x"), S(";:")) == 3, "find_any");
  ASSERT_TRUE(faf_string_find_any(S("abc"), S(";:")) == FAF_NPOS, "find_any none");
  ASSERT_TRUE(faf_string_find_any(S("abc"), S("")) == FAF_NPOS, "find_any empty set");
}

// Test case definitions
test_case_t search_tests[] = {
    {"eq_prefix_suffix", test_eq_prefix_suffix},
    {"find_rfind", test_find_rfind},
    {"count_find_any", test_count_find_any},
};

void search_setup(void) {}

void search_teardown(void) {}

int main(int argc, char **argv) {
  test_suite_t suite = TEST_SUITE("Search", search_tests,
                                  sizeof(search_tests) / sizeof(search_tests[0]),
                                  search_setup, search_teardown);
  register_test_suite(suite);
  return test_main(argc, argv);
}
