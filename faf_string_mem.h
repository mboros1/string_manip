#ifndef FAF_STRING_MEM_H
#define FAF_STRING_MEM_H

#include "faf_string.h"

typedef int pool_t;
typedef int pool_off_t;

faf_string* faf_string_alloc(pool_t pool);

void faf_string_pool_reset(pool_t pool);

pool_t next_pool();

faf_string* faf_string_copy(pool_t pool, faf_string str);

pool_off_t get_pool_offset(pool_t pool);

pool_off_t get_pool_remaining(pool_t pool);

#endif // FAF_STRING_MEM_H
