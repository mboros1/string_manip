#ifndef FAF_STRING_HASH_H
#define FAF_STRING_HASH_H

#include "../core/faf_string.h"

#include <stdint.h>

// 64 bit hash for hash tables. Not cryptographic. The result depends only on
// the bytes and the seed: it is the same on every backend and endianness.
// Built from MurmurHash3's fmix64 finalizer over 8 byte little-endian words.
uint64_t faf_string_hash(faf_string str);
uint64_t faf_string_hash_seed(faf_string str, uint64_t seed);

#endif // FAF_STRING_HASH_H
