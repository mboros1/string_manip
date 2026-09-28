#include "faf_string_view.h"
#include "faf_kernels.h"

faf_string faf_string_slice(faf_string str, size_t from, size_t to) {
  size_t n = faf_string_len(str);
  if (to > n)
    to = n;
  if (from > to)
    from = to;
  return (faf_string){.start = str.start + from, .end = str.start + to};
}

// " \t\n\v\f\r": 0x09-0x0D and 0x20
static const faf_byteset whitespace = {
    .bits = {[1] = 0x3E, [4] = 0x01},
    .chars = {' ', '\t', '\n', '\v', '\f', '\r'},
    .nchars = 6,
};

faf_string faf_string_ltrim(faf_string str) {
  size_t n = faf_string_len(str);
  str.start += faf_k_find_set(str.start, n, &whitespace, false);
  return str;
}

faf_string faf_string_rtrim(faf_string str) {
  size_t n = faf_string_len(str);
  size_t last = faf_k_rfind_set(str.start, n, &whitespace, false);
  str.end = last == n ? str.start : str.start + last + 1;
  return str;
}

faf_string faf_string_trim(faf_string str) {
  return faf_string_rtrim(faf_string_ltrim(str));
}

bool faf_string_next_token(faf_string *rest, char tok, faf_string *out) {
  if (rest->start == NULL)
    return false;
  size_t n = faf_string_len(*rest);
  size_t at = faf_k_find_byte(rest->start, n, tok);
  if (at == n) {
    *out = *rest;
    *rest = FAF_STRING_NONE; // that was the last token
  } else {
    *out = (faf_string){.start = rest->start, .end = rest->start + at};
    rest->start += at + 1;
  }
  return true;
}

bool faf_string_split_once(faf_string str, char tok, faf_string *left,
                           faf_string *right) {
  size_t n = faf_string_len(str);
  size_t at = faf_k_find_byte(str.start, n, tok);
  if (at == n)
    return false;
  *left = (faf_string){.start = str.start, .end = str.start + at};
  *right = (faf_string){.start = str.start + at + 1, .end = str.end};
  return true;
}
