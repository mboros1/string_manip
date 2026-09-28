Goal of this library is to create a full suite of string functions that operate on `const char*` and two pointer strings using crossplatform
simd operations with no dependencies on the standard library, allowing it to work cross platform and in a free standing enviroment.

The library works on bytes: case functions are ASCII only, and `faf_string_utf8_valid` is the only UTF-8 aware function.

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

### Backends

All architecture specific code is a small set of byte kernels (`faf_kernels.h`), selected at compile time:

| Backend | When |
|---|---|
| `sse2` | x86 / x86-64 |
| `neon` | AArch64, ARMv7 with NEON |
| `ref` | anything else (portable scalar C), or forced with `-DFAF_BACKEND_REF` |

The `ref` kernels are always compiled too, and every SIMD kernel is tested against them (`test_faf_kernels.c`). The library has no dependencies; simde is only used by the standalone `str_len_test` / `str_split_test` experiments.

For a freestanding target with no libc, build with `-DFAF_PROVIDE_LIBC_MEM`: the compiler may emit calls to `memcpy`, `memset`, `memmove` and `memcmp` on its own, and this defines them.

## Building and Testing

This project uses a Makefile build system that can be configured for your local environment.

### The `./faf` helper

`./faf` is a front end to the Makefile. Run it with no arguments for numbered menus (test, build, bench, check, clean; backend, architecture, optimization). The defaults come from your last run, and Enter repeats it. It prints the exact `make` command before running it.

```bash
./faf                        # interactive menus
./faf test case cmp          # run test_faf_string_case and test_faf_string_cmp
./faf test --backend ref     # all tests on the scalar backend
./faf test --arch x86_64     # SSE2 through Rosetta on Apple Silicon
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

# Check the optimized library imports no libc memory/string functions
make check_freestanding

# Build (at -O2) and run the benchmarks
make bench

# Any of the above for another backend or architecture, e.g. SSE2 on Apple Silicon
make OBJ_DIR=obj/x86 BIN_DIR=bin/x86 EXTRA_FLAGS="-arch x86_64" all_tests
```

### Cleaning

```bash
make clean
```
