#include "faf_string.h"
#include "faf_string_strlen.h"

faf_string faf_string_init(const char *str) {
  faf_string init = {.start = str, .end = str + faf_string_strlen(str)};
  return init;
}

faf_string faf_string_init_n(const char *str, size_t n) {
  faf_string init = {.start = str, .end = str + n};
  return init;
}
