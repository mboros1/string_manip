#include "faf_string_concat.h"
#include "../mem/faf_string_mem.h"

faf_string faf_string_concat(faf_region r, faf_string str1, faf_string str2) {
  size_t len1 = faf_string_len(str1);
  size_t len2 = faf_string_len(str2);

  // one contiguous span for both strings plus the NUL
  faf_span sp = faf_reserve(r, faf_slots_for(len1 + len2));
  if (!sp.ptr)
    return FAF_STRING_NONE;

  char *dst = (char *)sp.ptr;
  char *dst_end = dst + sp.slots * FAF_SLOT_BYTES;
  faf_memcpy(dst, str1.start, len1);
  faf_memcpy(dst + len1, str2.start, len2);

  size_t len = len1 + len2;
  faf_memset(dst + len, 0, (size_t)(dst_end - (dst + len)));
  return (faf_string){.start = dst, .end = dst + len};
}
