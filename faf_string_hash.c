#include "faf_string_hash.h"

#define K1 0x9E3779B97F4A7C15ull // 2^64 / golden ratio
#define K2 0xC2B2AE3D27D4EB4Full

static inline uint64_t fmix64(uint64_t k) {
  k ^= k >> 33;
  k *= 0xFF51AFD7ED558CCDull;
  k ^= k >> 33;
  k *= 0xC4CEB9FE1A85EC53ull;
  k ^= k >> 33;
  return k;
}

static inline uint64_t rotl(uint64_t x, int r) {
  return (x << r) | (x >> (64 - r));
}

// n <= 8 bytes, little-endian regardless of the host
static inline uint64_t load_le(const unsigned char *p, size_t n) {
  uint64_t v = 0;
  for (size_t i = 0; i < n; ++i)
    v |= (uint64_t)p[i] << (8 * i);
  return v;
}

uint64_t faf_string_hash_seed(faf_string str, uint64_t seed) {
  const unsigned char *p = (const unsigned char *)str.start;
  size_t n = faf_string_len(str);
  uint64_t h = seed ^ ((uint64_t)n * K1);
  for (; n >= 8; n -= 8, p += 8) {
    h ^= fmix64(load_le(p, 8) ^ K2);
    h = rotl(h, 29) * K1;
  }
  h ^= fmix64(load_le(p, n) ^ K2);
  return fmix64(h);
}

uint64_t faf_string_hash(faf_string str) { return faf_string_hash_seed(str, 0); }
