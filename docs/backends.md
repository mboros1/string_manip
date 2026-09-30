# Backends

All architecture-specific code is a small set of byte kernels
(`src/kernels/faf_kernels.h`): find, count, strlen, compare, case conversion
and so on. The rest of the library is portable C that calls them. The backend
is chosen at compile time (`src/core/faf_backend.h`):

| Backend | When | How |
|---|---|---|
| `sse2` | x86 / x86-64 | 16-byte vectors, C intrinsics |
| `neon` | AArch64, ARMv7 with NEON | 16-byte vectors, C intrinsics |
| `pie` | ESP32-S3 | SWAR, plus the scanning kernels as whole functions in Xtensa assembly using the S3's 128-bit PIE vector unit |
| `swar` | any other little-endian 32- or 64-bit CPU | a machine word as 4 or 8 byte lanes ("SIMD within a register"), portable C |
| `ref` | 8/16-bit and big-endian CPUs, or forced | byte at a time |

Force one with `-DFAF_BACKEND_REF` or `-DFAF_BACKEND_SWAR` (on the S3: plain
SWAR without PIE).

The `ref` kernels are always compiled, and every backend is tested against
them at every length and alignment, with unreadable pages on both sides of the
data (`tests/test_faf_kernels.c`). Reads past a string's end stay inside an
aligned block, so they never cross a page.

Why the S3 kernels are assembly, and what they do, is in the header of
`src/kernels/faf_kernels_pie.c` and in [history.md](history.md).

## Freestanding builds

The library needs no allocator, stdio or locale: memory comes from static pools. Its only external dependencies are `memcpy`, `memset`, `memmove` and `memcmp`, which GCC and Clang require from every environment, freestanding included, since they emit calls to them on their own (struct copies, zero initialization). `faf_memcpy` / `faf_memset` go through the compiler builtins, so small copies are inlined and large ones use the platform's tuned routines.

Kernels, RTOSes, UEFI and embedded toolchains (newlib, picolibc) already provide the four. On a target with none at all, add minimal versions to your own build:

```c
#include <stddef.h>

// Keep the compiler from turning these loops back into calls to themselves.
#if defined(__clang__)
#define NO_BUILTIN __attribute__((no_builtin))
#else
#define NO_BUILTIN __attribute__((optimize("no-tree-loop-distribute-patterns")))
#endif

NO_BUILTIN void *memcpy(void *restrict dst, const void *restrict src, size_t n) {
  unsigned char *d = dst;
  const unsigned char *s = src;
  while (n--)
    *d++ = *s++;
  return dst;
}

NO_BUILTIN void *memset(void *dst, int c, size_t n) {
  unsigned char *d = dst;
  while (n--)
    *d++ = (unsigned char)c;
  return dst;
}

NO_BUILTIN void *memmove(void *dst, const void *src, size_t n) {
  unsigned char *d = dst;
  const unsigned char *s = src;
  if (d < s) {
    while (n--)
      *d++ = *s++;
  } else {
    while (n--)
      d[n] = s[n];
  }
  return dst;
}

NO_BUILTIN int memcmp(const void *a, const void *b, size_t n) {
  const unsigned char *x = a, *y = b;
  for (; n; --n, ++x, ++y)
    if (*x != *y)
      return *x < *y ? -1 : 1;
  return 0;
}
```

`make check_freestanding` builds the library with `-ffreestanding` and fails if it imports anything else.
