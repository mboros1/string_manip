#include "faf_string_case.h"
#include "faf_kernels.h"

static faf_string convert(faf_region r, faf_string str, bool upper) {
  size_t len = faf_string_len(str);
  faf_span sp = faf_reserve(r, faf_slots_for(len));
  if (!sp.ptr)
    return FAF_STRING_NONE;
  char *dst = (char *)sp.ptr;
  faf_k_ascii_case(dst, str.start, len, upper);
  faf_memset(dst + len, 0, sp.slots * FAF_SLOT_BYTES - len);
  return (faf_string){.start = dst, .end = dst + len};
}

faf_string faf_string_to_lower(faf_region r, faf_string str) {
  return convert(r, str, false);
}

faf_string faf_string_to_upper(faf_region r, faf_string str) {
  return convert(r, str, true);
}

bool faf_string_eq_icase(faf_string a, faf_string b) {
  size_t len = faf_string_len(a);
  return len == faf_string_len(b) &&
         faf_k_mismatch_icase(a.start, b.start, len) == len;
}

static inline unsigned char fold(unsigned char c) {
  return (c >= 'A' && c <= 'Z') ? (unsigned char)(c + ('a' - 'A')) : c;
}

int faf_string_cmp_icase(faf_string a, faf_string b) {
  size_t la = faf_string_len(a), lb = faf_string_len(b);
  size_t len = la < lb ? la : lb;
  size_t at = faf_k_mismatch_icase(a.start, b.start, len);
  if (at < len)
    return fold((unsigned char)a.start[at]) < fold((unsigned char)b.start[at])
               ? -1
               : 1;
  return la < lb ? -1 : la > lb ? 1 : 0;
}
