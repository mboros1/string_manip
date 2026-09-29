# Building, testing, benchmarking

## The `./faf` helper

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

## Configuration Options

You can configure the build process in several ways:

1. **Environment Variables**: Set these variables to customize your build:
   - `CC`: Compiler to use (default: gcc)
   - `SIMDE_INCLUDE`: Path to SIMDE include directory (only for the `str_len_test` / `str_split_test` utilities)
   - `CFLAGS`: Compiler flags
   - `TEST_FLAGS`: Compiler flags for test builds

2. **Local Configuration File**: Create a `.env` file in the project root with your configuration.

3. **VSCode Configuration**: If you're using VSCode, the build settings are configured in `.vscode/settings.json`.

## Example Configuration

For macOS with Homebrew:
```bash
CC=gcc-15
SIMDE_INCLUDE=$(brew --prefix simde)/include/
```

## Building

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

# Record a benchmark run in bench/results/ (summarized in bench/RESULTS.md);
# commit the code first so the run is tied to a clean commit
make bench_record
make esp32_bench_record IDF_TARGET=esp32s3 ESPPORT=/dev/cu.usbmodem101
python3 tools/bench_track.py compare --machine esp32   # latest two runs

# Any of the above for another backend or architecture, e.g. SSE2 on Apple Silicon
make OBJ_DIR=obj/x86 BIN_DIR=bin/x86 EXTRA_FLAGS="-arch x86_64" all_tests
```

Tests adapt to the configured pool sizes. A test that needs more room than the build has, or guard pages (`mmap`) that a microcontroller lacks, declares it with `TEST_REQUIRE(condition, reason)` and is reported as SKIPPED with the reason instead of failing.

## Cleaning

```bash
make clean
```
