#include "faf_string_cmp.h"
#include "kernels/faf_kernels.h"

int faf_string_cmp(faf_string str1, faf_string str2) {
  size_t len1 = faf_string_len(str1);
  size_t len2 = faf_string_len(str2);
  size_t len = len1 < len2 ? len1 : len2;

  size_t at = faf_k_mismatch(str1.start, str2.start, len);
  if (at < len) {
    // bytes compare unsigned, like memcmp/strcmp
    unsigned char c1 = (unsigned char)str1.start[at];
    unsigned char c2 = (unsigned char)str2.start[at];
    return c1 < c2 ? -1 : 1;
  }
  return len1 < len2 ? -1 : len1 > len2 ? 1 : 0;
}
