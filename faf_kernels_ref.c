#include "faf_kernels.h"

// Reference kernels: plain byte loops, written to be obviously correct.
// They are the fallback backend and the oracle the SIMD backends are tested
// against, so keep them simple.

void faf_byteset_init(faf_byteset *set, const char *chars, size_t n) {
  for (int i = 0; i < 32; ++i)
    set->bits[i] = 0;
  set->nchars = 0;
  for (size_t i = 0; i < n; ++i) {
    unsigned char c = (unsigned char)chars[i];
    if (faf_byteset_has(set, c))
      continue;
    set->bits[c >> 3] |= (uint8_t)(1u << (c & 7));
    if (set->nchars < 16)
      set->chars[set->nchars] = c;
    if (set->nchars != 0xFF)
      set->nchars = set->nchars < 16 ? set->nchars + 1 : 0xFF;
  }
}

static inline unsigned char fold(unsigned char c) {
  return (c >= 'A' && c <= 'Z') ? (unsigned char)(c + ('a' - 'A')) : c;
}

FAF_NO_BUILTIN
size_t faf_ref_strlen(const char *s) {
  size_t n = 0;
  while (s[n])
    ++n;
  return n;
}

FAF_NO_BUILTIN
size_t faf_ref_find_byte(const char *s, size_t n, char c) {
  for (size_t i = 0; i < n; ++i)
    if (s[i] == c)
      return i;
  return n;
}

FAF_NO_BUILTIN
size_t faf_ref_rfind_byte(const char *s, size_t n, char c) {
  for (size_t i = n; i > 0; --i)
    if (s[i - 1] == c)
      return i - 1;
  return n;
}

FAF_NO_BUILTIN
size_t faf_ref_count_byte(const char *s, size_t n, char c) {
  size_t count = 0;
  for (size_t i = 0; i < n; ++i)
    count += s[i] == c;
  return count;
}

FAF_NO_BUILTIN
size_t faf_ref_find_bytes(const char *s, size_t n, char c, size_t *pos,
                          size_t max) {
  size_t k = 0;
  for (size_t i = 0; i < n && k < max; ++i)
    if (s[i] == c)
      pos[k++] = i;
  return k;
}

FAF_NO_BUILTIN
size_t faf_ref_find_set(const char *s, size_t n, const faf_byteset *set,
                        bool in) {
  for (size_t i = 0; i < n; ++i)
    if (faf_byteset_has(set, (unsigned char)s[i]) == in)
      return i;
  return n;
}

FAF_NO_BUILTIN
size_t faf_ref_rfind_set(const char *s, size_t n, const faf_byteset *set,
                         bool in) {
  for (size_t i = n; i > 0; --i)
    if (faf_byteset_has(set, (unsigned char)s[i - 1]) == in)
      return i - 1;
  return n;
}

FAF_NO_BUILTIN
size_t faf_ref_mismatch(const char *a, const char *b, size_t n) {
  for (size_t i = 0; i < n; ++i)
    if (a[i] != b[i])
      return i;
  return n;
}

FAF_NO_BUILTIN
size_t faf_ref_mismatch_icase(const char *a, const char *b, size_t n) {
  for (size_t i = 0; i < n; ++i)
    if (fold((unsigned char)a[i]) != fold((unsigned char)b[i]))
      return i;
  return n;
}

FAF_NO_BUILTIN
void faf_ref_ascii_case(char *dst, const char *src, size_t n, bool upper) {
  for (size_t i = 0; i < n; ++i) {
    unsigned char c = (unsigned char)src[i];
    if (upper && c >= 'a' && c <= 'z')
      c = (unsigned char)(c - ('a' - 'A'));
    else if (!upper && c >= 'A' && c <= 'Z')
      c = (unsigned char)(c + ('a' - 'A'));
    dst[i] = (char)c;
  }
}

FAF_NO_BUILTIN
size_t faf_ref_ascii_prefix(const char *s, size_t n) {
  for (size_t i = 0; i < n; ++i)
    if ((unsigned char)s[i] >= 0x80)
      return i;
  return n;
}

FAF_NO_BUILTIN
void faf_ref_reverse(char *dst, const char *src, size_t n) {
  for (size_t i = 0; i < n; ++i)
    dst[i] = src[n - 1 - i];
}
