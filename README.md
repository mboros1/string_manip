# FaF Strings

*Fast as F\*\*\* strings.* A C string library that is a personal playground
for two questions:

1. **How far can one small set of byte kernels be pushed on very different
   CPUs?** An Apple M1, x86, and two microcontrollers (ESP32, ESP32-S3), each
   with the backend that suits it: SSE2, NEON, word-at-a-time C (SWAR), and
   hand-written Xtensa assembly on the S3's vector unit.
2. **What does pooled memory buy over malloc?** All memory comes from static
   pools, handed out as regions that are freed all at once.

Strings are the workload because they touch everything: scanning, comparing,
copying, allocating, formatting.

It is usable, and tested on all of the above, but still evolving: APIs can
change.

## What it is

- **`faf_string`**: a start/end pointer pair, passed by value. Views and owned
  strings are the same type; owned ones live in a region.
- **Regions over static pools**: no malloc, no free per string, generation
  checked handles, and a result of `FAF_STRING_NONE` when a region is full.
  Pool count, size and placement are build options, down to a few hundred
  bytes for a microcontroller.
- **Byte kernels**: everything architecture-specific is 12 small kernels
  (find, count, strlen, compare, case...). The rest is portable C on top.
- **Freestanding**: no libc beyond `memcpy`/`memset`, which every C compiler
  requires anyway (`make check_freestanding` enforces it).
- **Every backend is checked against a plain byte-at-a-time version**, at every
  length and alignment, on every machine, including the boards.

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

[docs/history.md](docs/history.md) tells the whole story, including what was
tried and thrown away.

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
