Goal of this library is to create a full suite of string functions that operate on `const char*` and two pointer strings using crossplatform
simd operations with no dependencies on the standard library beyond `memcpy`, `memset`, `memmove` and `memcmp` (which the compiler requires anyway), allowing it to work cross platform and in a free standing enviroment.

The library works on bytes: case functions are ASCII only, and `faf_string_utf8_valid` is the only UTF-8 aware function.

## Layout

| Directory | What's in it |
|---|---|
| `src/` | the library: sources and headers together |
| `src/kernels/` | the per-architecture byte kernels (see Backends) |
| `tests/` | one test program per module, and the test framework |
| `tests/esp32/` | an ESP-IDF app that runs every test suite on an ESP32 (`make esp32_test`) |
| `bench/` | benchmarks (`make bench`) |
| `tools/` | `gen_compile_commands.py`, random test data generators |
| `experiments/` | standalone experiments, not part of the library (the simde ones, a C port of pdqsort) |

To use the library, compile `src/*.c` and `src/kernels/*.c` (no include paths needed) and add `-Isrc` to your own code.

## Overview

Include `faf.h` for everything, or the individual headers:

| Header | What's in it |
|---|---|
| `faf_string.h` | `faf_string` (a start/end pointer pair, passed by value), `faf_string_init` |
| `faf_string_mem.h` | Region allocator: `faf_region_acquire` / `faf_reserve` / `faf_region_release`, `faf_string_copy`, `faf_memcpy` / `faf_memset` |
| `faf_string_search.h` | `eq`, `starts_with`, `ends_with`, `find` / `rfind` (substring or char), `contains`, `count`, `find_any` |
| `faf_string_view.h` | `slice`, `trim` / `ltrim` / `rtrim`, `next_token` (split iterator), `split_once` |
| `faf_string_strsplit.h` | `split` (array of views), `split_owned` (copies, NUL terminated tokens) |
| `faf_string_build.h` | `faf_builder` (grows in place in its region), `join`, `repeat`, `pad_left` / `pad_right`, `replace`, `reverse`, `format` |
| `faf_string_case.h` | `to_lower`, `to_upper`, `eq_icase`, `cmp_icase` |
| `faf_string_cmp.h`, `faf_string_concat.h` | `cmp` (unsigned bytes, like `memcmp`), `concat` |
| `faf_string_parse.h` | `parse_i64` / `parse_u64` / `parse_f64`, `from_i64` / `from_u64`, `is_ascii`, `utf8_valid` |
| `faf_string_hash.h`, `faf_string_sort.h` | 64 bit `hash`, `sort_chars`, `arr_sort` |

Functions that allocate take a `faf_region` and return `FAF_STRING_NONE` when the region is out of space. Everything allocated from a region is freed at once by `faf_region_release`.

### Memory configuration

All memory is static: `FAF_NPOOLS` pools of `FAF_POOL_SLOTS` 16-byte slots, 12 × 1024 slots (192 KB) by default. A region is one pool, so `FAF_POOL_SLOTS` also caps the size of any single result. Both are set at build time, e.g. `-DFAF_NPOOLS=4 -DFAF_POOL_SLOTS=512` for 32 KB on a microcontroller. `FAF_POOL_ATTR` places the pool storage, e.g. `-DFAF_POOL_ATTR=EXT_RAM_BSS_ATTR` for PSRAM on an ESP32.

### Backends

All architecture specific code is a small set of byte kernels (`src/kernels/faf_kernels.h`), selected at compile time:

| Backend | When |
|---|---|
| `sse2` | x86 / x86-64 |
| `neon` | AArch64, ARMv7 with NEON |
| `ref` | anything else (portable scalar C), or forced with `-DFAF_BACKEND_REF` |

The `ref` kernels are always compiled too, and every SIMD kernel is tested against them (`tests/test_faf_kernels.c`). The library has no dependencies; simde is only used by the standalone `experiments/str_len_test.c` / `str_split_test.c`.

#### Freestanding builds

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

## Building and Testing

This project uses a Makefile build system that can be configured for your local environment.

### The `./faf` helper

`./faf` is a front end to the Makefile. Run it with no arguments for numbered menus (test, build, bench, check, clean; backend, architecture, optimization). The defaults come from your last run, and Enter repeats it. It prints the exact `make` command before running it.

```bash
./faf                        # interactive menus
./faf test case cmp          # run test_faf_string_case and test_faf_string_cmp
./faf test --backend ref     # all tests on the scalar backend
./faf test --arch x86_64     # SSE2 through Rosetta on Apple Silicon
./faf bench alloc io         # only the allocation and I/O benchmarks
./faf again                  # repeat the last run
./faf build -n               # print the make command only
./faf build -- -j8 CC=cc     # arguments after -- go to make
./faf --help                 # everything else
```

Non-default setups build into their own directories (e.g. `obj/x86_64-ref`), so switching between them never reuses stale objects.

### Configuration Options

You can configure the build process in several ways:

1. **Environment Variables**: Set these variables to customize your build:
   - `CC`: Compiler to use (default: gcc)
   - `SIMDE_INCLUDE`: Path to SIMDE include directory (only for the `str_len_test` / `str_split_test` utilities)
   - `CFLAGS`: Compiler flags
   - `TEST_FLAGS`: Compiler flags for test builds

2. **Local Configuration File**: Create a `.env` file in the project root with your configuration.

3. **VSCode Configuration**: If you're using VSCode, the build settings are configured in `.vscode/settings.json`.

### Example Configuration

For macOS with Homebrew:
```bash
CC=gcc-14
SIMDE_INCLUDE=$(brew --prefix simde)/include/
```

### Building

```bash
# Build everything
make

# Build and run all tests
make all_tests

# Build and run a specific test
make test_faf_string

# Run the tests again on the portable scalar backend
make check_backends

# Check the library, built freestanding, imports nothing but memcpy/memset/memmove/memcmp
make check_freestanding

# Run the tests with small pools (2 x 1 KB), like a microcontroller build
make check_small

# Run the tests on an ESP32 over serial (needs ESP-IDF, default ~/esp/esp-idf-v6.1)
make esp32_test                                          # original ESP32
make esp32_test IDF_TARGET=esp32s3 ESPPORT=/dev/cu.usbmodem101

# Build (at -O2) and run the benchmarks
make bench

# Only some benchmark groups: strings, alloc, io, kernels
make bench BENCH_GROUPS="alloc io"

# Any of the above for another backend or architecture, e.g. SSE2 on Apple Silicon
make OBJ_DIR=obj/x86 BIN_DIR=bin/x86 EXTRA_FLAGS="-arch x86_64" all_tests
```

Tests adapt to the configured pool sizes. A test that needs more room than the build has, or guard pages (`mmap`) that a microcontroller lacks, declares it with `TEST_REQUIRE(condition, reason)` and is reported as SKIPPED with the reason instead of failing.

### Cleaning

```bash
make clean
```
