#include "faf_ctx.h"
#include "../core/faf_backend.h"

/* 2026-09-30
 * Tuning values, by key. A default is the same on every backend until
 * recorded runs show backends differ; then it is chosen per FAF_BACKEND_*
 * (faf_backend.h), with the run that chose it.
 */

// Split: where finding separators one at a time starts to beat batches of 64
// per scan (bench_batch's "split, ',' about every N bytes").
//   ESP32-S3 (pie): even at 128 B, one at a time wins from 256
//   M1 (neon): batches win at every gap measured, up to 256
//   ESP32 (swar): batches win to 32 B, one at a time from 64
// sse2 is not measured (as neon), nor ref (as swar).
#if defined(FAF_BACKEND_PIE)
#define SPLIT_BATCH_GAP 128
#elif defined(FAF_BACKEND_NEON) || defined(FAF_BACKEND_SSE2)
#define SPLIT_BATCH_GAP 256
#else
#define SPLIT_BATCH_GAP 48
#endif

enum { NKEYS = FAF_TUNE_SPLIT_BATCH_GAP + 1 };

struct faf_tuning {
  int64_t v[NKEYS];
};

typedef struct {
  int64_t def, min, max;
} key_info;

static const key_info keys[NKEYS] = {
    // one pass over a range up to twice what the strings hold
    [FAF_TUNE_CASE_ONE_PASS_PERCENT] = {.def = 200, .min = 0, .max = 1000000},
    [FAF_TUNE_SPLIT_BATCH_GAP] = {.def = SPLIT_BATCH_GAP,
                                  .min = 0,
                                  .max = 1000000},
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
