#include "faf_string_search.h"
#include "../kernels/faf_kernels.h"

bool faf_string_eq(faf_string a, faf_string b) {
  size_t len = faf_string_len(a);
  return len == faf_string_len(b) && faf_k_mismatch(a.start, b.start, len) == len;
}

bool faf_string_starts_with(faf_string str, faf_string prefix) {
  size_t m = faf_string_len(prefix);
  return m <= faf_string_len(str) &&
         faf_k_mismatch(str.start, prefix.start, m) == m;
}

bool faf_string_ends_with(faf_string str, faf_string suffix) {
  size_t m = faf_string_len(suffix);
  return m <= faf_string_len(str) &&
         faf_k_mismatch(str.end - m, suffix.start, m) == m;
}

size_t faf_string_find_char(faf_string str, char c) {
  size_t n = faf_string_len(str);
  size_t at = faf_k_find_byte(str.start, n, c);
  return at == n ? FAF_NPOS : at;
}

size_t faf_string_rfind_char(faf_string str, char c) {
  size_t n = faf_string_len(str);
  size_t at = faf_k_rfind_byte(str.start, n, c);
  return at == n ? FAF_NPOS : at;
}

// Is `sub` at str.start + at? (sub[0] is already known to match)
static inline bool match_at(faf_string str, size_t at, faf_string sub) {
  size_t m = faf_string_len(sub);
  return faf_k_mismatch(str.start + at + 1, sub.start + 1, m - 1) == m - 1;
}

size_t faf_string_find(faf_string str, faf_string sub) {
  size_t n = faf_string_len(str), m = faf_string_len(sub);
  if (m == 0)
    return 0;
  if (m > n)
    return FAF_NPOS;
  size_t starts = n - m + 1; // positions where sub fits
  for (size_t pos = 0; pos < starts;) {
    size_t at = pos + faf_k_find_byte(str.start + pos, starts - pos, sub.start[0]);
    if (at == starts)
      return FAF_NPOS;
    if (match_at(str, at, sub))
      return at;
    pos = at + 1;
  }
  return FAF_NPOS;
}

size_t faf_string_rfind(faf_string str, faf_string sub) {
  size_t n = faf_string_len(str), m = faf_string_len(sub);
  if (m == 0)
    return n;
  if (m > n)
    return FAF_NPOS;
  for (size_t end = n - m + 1; end > 0;) {
    size_t at = faf_k_rfind_byte(str.start, end, sub.start[0]);
    if (at == end)
      return FAF_NPOS;
    if (match_at(str, at, sub))
      return at;
    end = at;
  }
  return FAF_NPOS;
}

bool faf_string_contains(faf_string str, faf_string sub) {
  return faf_string_find(str, sub) != FAF_NPOS;
}

size_t faf_string_count(faf_string str, faf_string sub) {
  size_t m = faf_string_len(sub);
  if (m == 0)
    return faf_string_len(str) + 1;
  if (m == 1)
    return faf_k_count_byte(str.start, faf_string_len(str), sub.start[0]);
  size_t count = 0;
  faf_string rest = str;
  for (size_t at; (at = faf_string_find(rest, sub)) != FAF_NPOS; ++count)
    rest.start += at + m;
  return count;
}

size_t faf_string_find_any(faf_string str, faf_string chars) {
  faf_byteset set;
  faf_byteset_init(&set, chars.start, faf_string_len(chars));
  size_t n = faf_string_len(str);
  size_t at = faf_k_find_set(str.start, n, &set, true);
  return at == n ? FAF_NPOS : at;
}
