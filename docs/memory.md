# Memory

All memory is static: `FAF_NPOOLS` pools of `FAF_POOL_SLOTS` slots of
`FAF_SLOT_BYTES`, 12 × 1024 slots by default. There is no malloc.

- **Regions.** A region is one pool. `faf_region_acquire` takes a free pool,
  allocations bump through it, and `faf_region_release` frees everything in it
  at once. Handles carry a generation number, so a released region can't be
  used by mistake. Functions that allocate return `FAF_STRING_NONE` when the
  region is out of space.
- **Size limits.** `FAF_POOL_SLOTS` × `FAF_SLOT_BYTES` caps the size of any
  single result.
- **Slot size** is the allocation granularity and alignment. It defaults to the
  backend's register width: 16 with SSE2, NEON and PIE, the word size (4 or 8)
  with SWAR, 8 with `ref`. Smaller slots waste less on short strings; any power
  of two at least the alignment of `faf_string` works.
- **Placement.** `FAF_POOL_ATTR` places the pool storage, e.g.
  `-DFAF_POOL_ATTR=EXT_RAM_BSS_ATTR` for PSRAM on an ESP32.

All four are build options, e.g. `-DFAF_NPOOLS=4 -DFAF_POOL_SLOTS=512` for
32 KB on a microcontroller.

Tests adapt to the configured sizes: a test that needs more room than the build
has, or guard pages (`mmap`) that a microcontroller lacks, declares it with
`TEST_REQUIRE(condition, reason)` and is reported as SKIPPED. `make check_small`
runs the suite with 2 × 1 KB pools.

## Rings

Regions are for data whose lifetime you know. For data you can afford to lose
(recent log lines, a cache of rendered text, telemetry), `faf_ring`
(`faf_string_ring.h`) stores strings in a buffer you provide and overwrites
the oldest ones when it is full. Memory is bounded by construction, and pushes
never fail for lack of room. Each push returns a handle; `faf_ring_get` gives
the string back, or `FAF_STRING_NONE` once it has been overwritten.

The check is one compare: the ring counts every byte ever written (64 bits,
so it never wraps), each handle holds that count at its string's start, and a
string is intact while `head - pos <= capacity`. A string returned by
`faf_ring_get` points into the buffer, so use it before pushing more, copy it
out, or check `faf_ring_valid` again afterwards.

## Code placement

On the ESP32, where code runs from flash through a cache, put hot string code
in IRAM (a linker fragment, as in `bench/esp32/main/linker.lf`): flash cache
misses moved benchmark results by up to 80% between builds.
