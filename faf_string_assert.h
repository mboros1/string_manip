#ifndef FAF_STRING_ASSERT_H
#define FAF_STRING_ASSERT_H

#include <stdarg.h>
#include <stdio.h>

void _assert(const char *snippet, const char *file, int line,
             const char *message, ...);

#define assert(cond, ...)                                                      \
  if (!(cond))                                                                 \
  _assert(#cond, __FILE__, __LINE__, #__VA_ARGS__ __VA_OPT__(, )##__VA_ARGS__)


#ifdef FAF_STRING_ASSERT_IMPL

static int tests_failed = 0;

void _assert(const char *snippet, const char *file, int line,
             const char *message, ...) {
  printf("assert failed %s:%d %s\n", file, line, snippet);
  tests_failed++;

  if (*message) {
    va_list arg;
    va_start(arg, message);
    char *data = va_arg(arg, char *);
    vprintf(data, arg);
  }
}
#endif // FAF_STRING_ASSERT_IMPL

#endif // FAF_STRING_ASSERT_H
