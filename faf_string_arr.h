#ifndef FAF_STRING_ARR_H
#define  FAF_STRING_ARR_H

#include "faf_string.h"  


typedef struct {
  faf_string* start;
  faf_string* end;
} faf_string_arr;

#define faf_foreach(pos, arr) \
  for(faf_string* pos = arr.start; pos != arr.end; ++pos)

#endif // FAF_STRING_ARR_H
