#include "faf_string_concat.h"
#include "faf_string_mem.h"

faf_string* faf_string_concat(pool_t pool, faf_string* str1, faf_string* str2) {
  faf_string* new_str = faf_string_alloc(pool);

  // TODO: IDK if this will work this way? Seems a little too easy?
  faf_string* copy1 = faf_string_copy(pool, *str1);
  faf_string* copy2 = faf_string_copy(pool, *str2);

  new_str->start = copy1->start;
  new_str->end = copy2->end;

  return new_str;
}
