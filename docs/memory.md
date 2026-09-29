# Memory

There is no malloc. Regions come from an **arena**: a fixed set of pools of
slots over one block of memory. The default arena is static storage, sized at
build time; others can be laid over any memory you provide, sized at run time.

- **Regions.** A region is one pool. `faf_region_acquire` takes a free pool of
  the default arena (`faf_arena_acquire(a)` of another one), allocations bump
  through it, and `faf_region_release` frees everything in it at once. Handles
  carry their arena and a generation number, so a released region can't be
  used by mistake. Functions that allocate return `FAF_STRING_NONE` when the
  region is out of space.
- **Size limits.** A pool's size caps the size of any single result:
  `FAF_POOL_SLOTS` × `FAF_SLOT_BYTES` in the default arena,
  `faf_region_capacity(r)` slots in general.
- **Slot size** is the allocation granularity and alignment. It defaults to the
  backend's register width: 16 with SSE2, NEON and PIE, the word size (4 or 8)
  with SWAR, 8 with `ref`. Smaller slots waste less on short strings; any power
  of two at least the alignment of `faf_string` works. It is a build option
  for every arena.
- **Placement.** `FAF_POOL_ATTR` places the default arena's storage, e.g.
  `-DFAF_POOL_ATTR=EXT_RAM_BSS_ATTR` for PSRAM on an ESP32.

The default arena's sizes are build options, e.g. `-DFAF_NPOOLS=4
-DFAF_POOL_SLOTS=512` for 32 KB on a microcontroller.

Tests adapt to the configured sizes: a test that needs more room than the build
has, or guard pages (`mmap`) that a microcontroller lacks, declares it with
`TEST_REQUIRE(condition, reason)` and is reported as SKIPPED. `make check_small`
runs the suite with 2 × 1 KB pools.

## Arenas over your memory

```c
static char buf[1 << 20];
static _Alignas(16) char arena_mem[16];  // >= faf_arena_size() bytes
faf_arena *a = (faf_arena *)arena_mem;
faf_arena_init(a, buf, sizeof buf, 8);   // 8 pools, ~128 KB each
faf_region r = faf_arena_acquire(a);
faf_string s = faf_string_to_lower(r, line);
...
faf_region_release(r);
```

`faf_arena_init` puts a few bytes of bookkeeping at the start of the buffer
and splits the rest evenly into pools, aligned to the slot size;
`faf_arena_bytes(npools, pool_slots)` says how much memory a given shape needs.
The memory can come from anywhere: a static array, the stack, `malloc`,
`mmap`, or a buffer owned by another language's runtime.

Nothing is shared between arenas, so the rule for threads is simple: an arena
and its regions belong to one thread at a time, and threads that each have
their own arena never contend. The default arena is one arena like any other,
so it belongs to one thread too.

Re-initializing an arena resets its generations, so handles from before it
are not detected as stale: release them first.

Arenas are registered in a table of `FAF_MAX_ARENAS` entries (16 by
default, the default arena included); `faf_arena_fini` frees an entry. Region
handles are 64-bit integers naming the table entry, the pool and its
generation, the same in every build, so a handle from a released region or a
retired arena is rejected rather than read through. Handles of the default
arena skip the table, so plain C use costs what it did before arenas.

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
