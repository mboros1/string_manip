#include "faf_string_strlen.h"
#include "kernels/faf_kernels.h"

// 2024-07-22:
// Using SIMD instructions, searches 16 bytes at a time.
//
// 2026-09-27:
// Now a kernel (faf_k_strlen): aligned 16 byte blocks, so it never reads
// into a page the string doesn't reach. See faf_kernels_simd.c.
size_t faf_string_strlen(const char *str) { return faf_k_strlen(str); }
