#ifndef FAF_STRING_VIEW_H
#define FAF_STRING_VIEW_H

#include "../core/faf_string.h"

#include <stdint.h>

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

// Batched split iterator: the same tokens as faf_string_next_token, but the
// separators are found FAF_TOKENS_BATCH at a time in one scan, so each token
// costs about two pointer copies instead of a search of its own. Faster
// whenever tokens are short.
//
//   faf_tokens t = faf_tokens_init(line, ',');
//   faf_string field;
//   while (faf_tokens_next(&t, &field)) { ... }
#define FAF_TOKENS_BATCH 16

typedef struct {
  const char *start; // start of the next token
  const char *end;   // end of the input
  const char *base;  // where the current batch was searched from
  size_t pos[FAF_TOKENS_BATCH]; // separators of the batch, relative to base
  uint8_t count;     // separators in the batch
  uint8_t next;      // next one to use
  char tok;
  bool done;
} faf_tokens;

faf_tokens faf_tokens_init(faf_string str, char tok);
// Out of line: search the next batch (or hand out the last token).
bool faf_tokens_refill(faf_tokens *t, faf_string *out);

static inline bool faf_tokens_next(faf_tokens *t, faf_string *out) {
  if (t->next < t->count) {
    const char *sep = t->base + t->pos[t->next++];
    *out = (faf_string){.start = t->start, .end = sep};
    t->start = sep + 1;
    return true;
  }
  return faf_tokens_refill(t, out);
}

// Split at the first `tok`: "key=value" -> "key", "value". Returns false (and
// leaves the outputs untouched) if there is no `tok`.
bool faf_string_split_once(faf_string str, char tok, faf_string *left,
                           faf_string *right);

#endif // FAF_STRING_VIEW_H
