# FaF Strings

*Fast as F\*\*\* strings.* A native library demonstrating how much can be gained
by tuning basic building blocks like memory allocation and kernel operations, applied
to one of the most basic data structures in computer science, strings.

Currently has optimized builds for Apple Silicon, x86_64 with SSE2, and ESP32 architectures, including
both the simpler RISC-V architecture and more advanced features available in the
XTensa architecture that have SIMD-like behavior.

## Basic Design

- **`faf_string`**: a start/end pointer pair, passed by value, which can represent
  both a view and an owned string.
- **Regions over static pools**: Keeps memory bounded, avoids costly mallocs/frees,
  and memory is managed as a regional object that is easier to reason about and has
  clean failure messages when memory is exhausted.
- **Rings**: A variant for data that is safe to lose in high throughput systems
  like logs or caches, that safely detects overwritten data with hard memory exhaustion
  failures.
- **Byte kernels**: Give system specific optimizations over byte operations, that can
  be easily wrapped by portable code at a higher level.
- **Freestanding**: Can be used on any system, from the smallest microcontrollers to
  operating systems with advanced hardware acceleration. Only requirement is a C compiler.
- **Benchmarking as first class**: Detailed benchmarking analysis is done on all kernels,
  to log performance progress as the library evolves and more systems are added.
  
## Some results

From [bench/RESULTS.md](bench/RESULTS.md) (recorded, reproducible runs):

- **Regions vs malloc**, processing a record (split, lowercase, concat): 3x
  faster on the M1 (103 vs 312 ns), **7.5x on the ESP32** (7.6 vs 57.4 µs),
  where malloc is expensive.
- **ESP32-S3 assembly vs the chip's own ROM libc**: `strlen` 4.5x the ROM's
  throughput (716 vs 159 MB/s), and about 2% behind it on short strings; compare
  (`mismatch`) 1.7x faster on short inputs than the ROM's `memcmp` and 4.8x
  on long ones.
- **The same code, placed differently**: on the ESP32, code runs from flash
  through a cache, and unrelated changes moved a benchmark by 80%. From IRAM,
  runs repeat within 0.03%.
- **On in-order cores, compiler details are the performance**: whether GCC
  emits a hardware loop decided a 19% difference on the S3. Writing whole
  kernels in assembly fixed it: every S3 kernel is now faster than portable C
  on both short and long inputs.
- **Clean API for FFI**: Easy and (mostly) stable native foreign function
  interface makes creating bindings in any programming language straightforward.
- **Historic documentation**: available at [docs/history.md](docs/history.md),
  best effort documentation of what was tried, what worked, what didn't,
  and the thought process behind it.

## Quick start

```bash
make all_tests      # build and run the tests
make bench          # benchmarks on this machine
./faf               # interactive menus for the above and more
make esp32_test IDF_TARGET=esp32s3   # on a board (needs ESP-IDF)
```

To use it, compile `src/*.c` and `src/kernels/*.c`, add `-Isrc`, and include
`faf.h`.

## Docs

- [docs/api.md](docs/api.md): the headers and functions, and the source layout
- [docs/memory.md](docs/memory.md): pools, regions and their build options
- [docs/backends.md](docs/backends.md): the kernel backends, and freestanding builds
- [docs/building.md](docs/building.md): building, testing, benchmarks, the `./faf` helper
- [bench/RESULTS.md](bench/RESULTS.md): recorded benchmark runs
- [docs/history.md](docs/history.md): how it got here
