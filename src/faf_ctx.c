#include "faf_ctx.h"

/* 2026-09-30
 * Tuning values, by key. A default is the same on every backend until
 * recorded runs show backends differ; then it is chosen per FAF_BACKEND_*
 * (faf_backend.h), with the run that chose it.
 */

enum { NKEYS = FAF_TUNE_CASE_ONE_PASS_PERCENT + 1 };

struct faf_tuning {
  int64_t v[NKEYS];
};

typedef struct {
  int64_t def, min, max;
} key_info;

static const key_info keys[NKEYS] = {
    // one pass over a range up to twice what the strings hold
    [FAF_TUNE_CASE_ONE_PASS_PERCENT] = {.def = 200, .min = 0, .max = 1000000},
};

static bool known(int key) { return key > 0 && key < NKEYS; }

faf_tuning *faf_tuning_new(faf_region r) {
  faf_span sp = faf_reserve(r, sizeof(faf_tuning) / FAF_SLOT_BYTES + 1);
  faf_tuning *t = (faf_tuning *)(void *)sp.ptr;
  for (int k = 1; t && k < NKEYS; ++k)
    t->v[k] = keys[k].def;
  return t;
}

bool faf_tuning_set(faf_tuning *t, int key, int64_t value) {
  if (!t || !known(key) || value < keys[key].min || value > keys[key].max)
    return false;
  t->v[key] = value;
  return true;
}

bool faf_tuning_get(const faf_tuning *t, int key, int64_t *value) {
  if (!known(key))
    return false;
  *value = t ? t->v[key] : keys[key].def;
  return true;
}
