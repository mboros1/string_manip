#ifndef FAF_STRING_MEM_H

#include "faf_string.h"


faf_string* faf_string_alloc(size_t nblocks);

void faf_string_free(void* begin, void* end);

faf_string* faf_string_copy(faf_string str);

// TODO: maybe an array allocator?
// TODO: would strings shorter then 128 bits make sense?

#endif // FAF_STRING_MEM_H
