#ifndef FAF_STRING_VIEW_H
#define FAF_STRING_VIEW_H

#include "faf_string.h"

// Views: sub-ranges of an existing string. Nothing here allocates; results
// point into the input and are valid as long as its bytes are.

// str[from, to), with both clamped to [0, len] and `to` to at least `from`.
faf_string faf_string_slice(faf_string str, size_t from, size_t to);

// Strip ASCII whitespace (" \t\n\v\f\r") from both ends / the left / right.
faf_string faf_string_trim(faf_string str);
faf_string faf_string_ltrim(faf_string str);
faf_string faf_string_rtrim(faf_string str);

// Split iterator: yields the tokens of `*rest` separated by `tok` one at a
// time, with no array. Tokens match faf_string_split: "a,,b" gives "a", "",
// "b" and "" gives one empty token. Returns false when done.
//
//   faf_string rest = line, field;
//   while (faf_string_next_token(&rest, ',', &field)) { ... }
bool faf_string_next_token(faf_string *rest, char tok, faf_string *out);

// Split at the first `tok`: "key=value" -> "key", "value". Returns false (and
// leaves the outputs untouched) if there is no `tok`.
bool faf_string_split_once(faf_string str, char tok, faf_string *left,
                           faf_string *right);

#endif // FAF_STRING_VIEW_H
