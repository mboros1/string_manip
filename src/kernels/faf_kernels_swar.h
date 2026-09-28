#ifndef FAF_KERNELS_SWAR_H
#define FAF_KERNELS_SWAR_H

// Internal: the SWAR kernels that the pie backend (ESP32-S3) replaces stay
// available under these names, for the PIE kernels' unaligned ends and short
// inputs, and for benchmarking PIE against SWAR.

#include "faf_kernels.h"

#if defined(FAF_BACKEND_PIE)
size_t faf_swar_strlen(const char *s);
size_t faf_swar_find_byte(const char *s, size_t n, char c);
size_t faf_swar_find_bytes(const char *s, size_t n, char c, size_t *pos,
                           size_t max);
size_t faf_swar_count_byte(const char *s, size_t n, char c);
size_t faf_swar_mismatch(const char *a, const char *b, size_t n);
size_t faf_swar_ascii_prefix(const char *s, size_t n);
#endif

#endif // FAF_KERNELS_SWAR_H
