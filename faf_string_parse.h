#ifndef FAF_STRING_PARSE_H
#define FAF_STRING_PARSE_H

#include "faf_string.h"
#include "faf_string_mem.h"

#include <stdint.h>

// Number parsing. The whole string must be the number: no surrounding
// whitespace (trim first), no trailing characters. Returns false, leaving
// `*out` untouched, on anything else or on overflow.

// [+-]digits
bool faf_string_parse_i64(faf_string str, int64_t *out);
// [+]digits
bool faf_string_parse_u64(faf_string str, uint64_t *out);
// [+-]digits[.digits][(e|E)[+-]digits], or [+-].digits[...]. Correctly
// rounded when there are at most 19 significant digits and the value is an
// integer below 2^53 scaled by 10^-22..10^22 (most everyday numbers);
// otherwise within a few ulp. Returns false if the result overflows.
bool faf_string_parse_f64(faf_string str, double *out);

// Decimal text of `v`, allocated in `r`.
faf_string faf_string_from_i64(faf_region r, int64_t v);
faf_string faf_string_from_u64(faf_region r, uint64_t v);

// Every byte < 0x80. Runtime Complexity: O(n), 16 bytes at a time.
bool faf_string_is_ascii(faf_string str);

// Well formed UTF-8: no overlong encodings, surrogates, or code points above
// U+10FFFF. ASCII runs are skipped 16 bytes at a time.
bool faf_string_utf8_valid(faf_string str);

#endif // FAF_STRING_PARSE_H
