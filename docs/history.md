# History

How the library got to where it is, from `32c9a3e` (2026-09-27) on: what was
tried, what the numbers were, and what was kept. Reverted experiments are
included; they are the part git doesn't show.

**Sources.** Code: the git log. Numbers: from `d6237e9` on, recorded runs in
`bench/results/` (reproducible: each is tied to a commit, see
[RESULTS.md](../bench/RESULTS.md)). Before that, and for anything reverted,
numbers come from the working sessions and are marked *unrecorded*: same
machines and method, but no JSON record to rerun against.

Machines: Apple M1 Max (NEON), ESP32 (Xtensa LX6, 240 MHz), ESP32-S3 (Xtensa
LX7 with the PIE vector extension, 240 MHz).

## 0. Design (09-27, before the code)

The allocator was designed in a conversation about arena allocators, before
any of the commits below; the code then followed it closely.

**The starting point** was the library as it had been left a year earlier,
with a Claude Code review listing 20 issues. Pools were one static array; a
pool was "free" when its bump offset was 0, so `next_pool()` could hand the
same pool out twice, and with all pools in use returned a used one. Nothing was
bounds-checked (writes ran into the next pool), copies weren't reliably NUL
terminated (tests passed only because static memory starts zeroed), a reset
left old pointers looking valid, `concat` ignored its second string, and split
relied on consecutive allocations being adjacent.

**The model: three layers that never share state.**
1. *Backing memory*: where regions come from (the static pools).
2. *Region lifetime*: who owns a region and whether a pointer into it is still
   valid (acquire/release, generations).
3. *Allocation within a region*: a bump cursor, alignment, contiguous spans.

String operations are clients of layer 3 only. Almost every issue in the review
was two layers bleeding into each other: "offset == 0 means free" merged
ownership into the cursor; `concat` reached into the pool arrays directly.

**Decisions that came out of it**, all in `f8b675b`:
- Explicit `faf_region_acquire` / `faf_region_release`, failing with
  `FAF_REGION_NONE` when every pool is taken, instead of inferring ownership.
- **Generation checks** rather than compile-time lifetimes: C can't see
  lifetimes in types (Cyclone and Rust can), but a per-region generation that
  changes on release catches use-after-release for a load, a compare and a
  branch.
- A bounds-checked `faf_reserve(region, n)` that returns a contiguous span or
  fails without moving the cursor; copy, concat and split are built on it.
- `faf_string` as a 16-byte start/end *value* held by the caller, so only the
  bytes live in the region, and one span for split's array. Views and owned
  strings are the same type; the difference is only whether the bytes are in
  a region.
- One owner per region, so allocation inside it never needs synchronization;
  only acquire/release would need atomics, chosen at build time rather than by
  a runtime flag on the hot path. (Not built yet: the library is
  single-threaded today.)
- Compile-time strategies without macro soup: logic in an ordinary
  `static inline` core, policies as small functions, thin wrappers per variant
  (the pattern the SIMD kernels' `scan_*` functions and match strategies use).
- Regions suit work whose lifetimes follow control flow (per record, per
  parse, per request); data with event-driven lifetimes needs handles or
  compaction. That is why the churn benchmark compacts live strings into a
  fresh region when one fills.

**Left for later:** allocation across pools (chaining regions), and a
ring-buffer region where old data is overwritten and handles detect it with a
monotonic 64-bit write position (valid while `offset >= write_pos -
capacity`). The ring is `faf_ring`, still not started.

## 1. Foundations (09-27)

**Test framework** (`32c9a3e`, `57db2bf`, `95d80f6`, `9eb8029`). After one
failed test, every later test in the run reported FAILED: the failure counter
was reset per suite instead of per test. The same bug was copy-pasted in two of
the four runner functions, so the fix was structural: one `run_one()` that
resets, runs and judges a single test, with the runners only choosing which
tests to run. The failure counter became a bool (nothing read the count), and
the old `faf_string_assert.h` went away: its `assert()` counted failures in a
second variable the framework never read, so it could fail silently.

**Kernel layer and region allocator** (`f8b675b`). All architecture-specific
code moved behind 13 byte kernels (`faf_kernels.h`): SSE2 and NEON written once
over a ~15-operation layer, plus scalar `ref` kernels that are both the fallback
and the test oracle. simde was dropped from the library: it emulates SSE2
semantics, which is not the best code elsewhere (NEON has no `movemask`; a
shift-and-narrow trick does the same natively). The allocator became
generation-checked regions over static pools. Existing functions were moved
onto both, fixing several bugs (`strlen` reading into the next page, `cmp`
reading past the end, `concat` not appending).

**The string API and first benchmarks** (`57c271d`, `c565add`): case, trim,
concat, repeat, sort, reverse, hash, split variants, `next_token`, the builder,
and a benchmark comparing regions with malloc.

## 2. Benchmarks and the memory path (09-28 morning)

**Benchmark groups** (`f49182b`): size sweep (region vs malloc vs a 20-line
bump arena, as the fair baseline), region overhead, churn with mixed lifetimes,
string growth (builder vs realloc vs `open_memstream`), and I/O (CSV, log
lines, config parsing).

**Small copies and builder growth** (`64f207b`, *unrecorded*, M1):
overlapping 8/4/2-byte copies below 16 bytes, and doubling growth in place.

| | before | after |
|---|---:|---:|
| copy 8 / 16 / 32 B | 11.7 / 16.6 / 10.2 ns | 4.4 / 5.0 / 5.4 ns |
| record processing | 146 ns | 106 ns (2.9x faster than malloc, was 2.1x) |
| churn, 64 / 256 live | 99 / 125 ns | 68 / 79 ns |
| builder, 16-byte appends | 1892 ns | 1310 ns |

**Why was the bump arena faster at 256 B and up?** Measured, not guessed: the
copy loop (16 bytes per iteration vs libc's 64) and a fixed ~3 ns of region
bookkeeping. The kernels weren't inlined across files (no LTO), and `no_builtin`
blocks inlining even with LTO. The decision (`a784fbc`): use the platform's
`memcpy`/`memset` through inline wrappers, since every environment GCC and
Clang target must provide them anyway, and drop `FAF_PROVIDE_LIBC_MEM`.
`check_freestanding` now fails the build if the library imports anything else.

- Wrapping the builtins alone made short copies 2x slower (every copy became a
  libc call, since lengths are rarely constant); inline paths up to 64 bytes
  fixed that. Measured with 7 alternating runs per build (*unrecorded*): copies
  of 32 B to 4 KB 20-43% faster, 8 B copies 14% *slower*.
- Checking the small sizes first fixed the 8 B regression (5.0 -> 3.9 ns) but
  cost 7-10% at 17-256 B. Kept, because the mixed-size workloads favored it
  (CSV +4%, `format` +7%). Differences of a few cycles on an out-of-order core
  are code layout as much as code.
- Integer formatting writes digits in place (`f503ef7`): `from_i64` had
  regressed 32% through the builder change.

**Layout** (`db96ee8`): `src/`, `src/kernels/`, `tests/`, `bench/`, `tools/`,
`experiments/` (pdqsort's C port lives there; the library's sort stayed
simple).

## 3. Microcontrollers (09-28 midday)

**Memory model for small machines** (`72c51c4`, `df64493`). Pool count, pool
size (`FAF_POOL_SLOTS`), slot size (`FAF_SLOT_BYTES`, default: the backend's
register width) and placement (`FAF_POOL_ATTR`, e.g. PSRAM) are build options.
Tests declare what they need (`TEST_REQUIRE`) and skip when the build is too
small; everything down to one 256-byte pool passes (`make check_small`).
Running on hardware found tests assuming 64-bit pointers and 16-byte slots.

**ESP32 and S3 runners** (`72c51c4`, `10cdc71`): `make esp32_test` /
`esp32_bench` build, flash, reset and read results over serial. The S3's
USB-JTAG port needs a watchdog reset (a line reset leaves it in the ROM
downloader).

First board results (*unrecorded*): at the same clock the S3 is only 1.1-1.3x
faster than the ESP32 on scalar code. Byte-loop kernels cost 6-25 cycles per
byte; the ROM's `strlen` was 5x faster than ours. ESP-IDF's malloc + free costs
4.7-8.7 µs against 0.7 µs for a region copy.

**SWAR backend** (`c0c5bcc`, `28dc687`). A 32- or 64-bit word as 4 or 8 byte
lanes, in portable C: the default on any CPU without SSE2/NEON. `ref` stays as
the oracle and for 8/16-bit CPUs. The first version was only 1.1x faster on
`find_byte`: two taken branches per word. Counted two-word loops, which GCC
turns into Xtensa zero-overhead hardware loops, made it 2.5x (*unrecorded*,
cycles per byte on the ESP32):

| kernel | byte loop | SWAR | chip's libc |
|---|---:|---:|---:|
| find_byte | 6.0 | 2.4 | `memchr` 7-9 |
| strlen | 10.8 | 2.0 | 1.5-1.8 |
| mismatch | 8.0 | 4.0 | `memcmp` 16 |
| count_byte | 12.0 | 4.3 | |
| to_lower | 24.7 | 6.3 | |

Record processing 1.55x, split 1.83x, churn 2.2x faster on the ESP32. A
branchless `find_bytes` extraction was tried and reverted (3778 vs 2918 ns).

## 4. Tracking, and why the boards needed IRAM (09-28 afternoon)

**Benchmark tracking** (`d6237e9`): `make bench_record` / `esp32_bench_record`
save every run with its commit, dirty flag, machine, compiler and
configuration; `compare` diffs two runs; `RESULTS.md` is generated. From here
on each code commit is followed by a `bench:` commit with its runs.

**Flash vs IRAM** (`76e2bcd`). Between two builds of the same library code,
churn on the ESP32 moved 80%. The ESP32 executes from external flash through a
small cache, and unrelated code changes moved the hot loop into conflict with
itself. A linker fragment puts the library and benchmarks in IRAM; runs now
repeat within 0.03%. The two affected baselines were deleted and re-recorded.
The same applies to real ESP32 applications: hot string code belongs in IRAM.

**Builder fast path** (`4a38e3d`, `2335dc5`): appends inline while the reserved
span has room. One-byte appends 2x faster on both boards (ESP32 1,451,534 ->
703,694 ns for 4000 appends). Inlining the appends into `format` made it 9%
slower, so `format` calls the out-of-line versions.

**`faf_tokens`** (`7b7a721`): a batched split iterator, finding up to 16
separators per kernel call. 1.8x faster than `next_token` on the M1, only 1.14x
on the boards, where `find_bytes` cost ~50 cycles per match.

## 5. The S3's vector unit (09-28 afternoon)

**PIE backend in C with inline asm** (`c9faf3b`, `38bd90f`). The S3 has eight
128-bit vector registers but no C intrinsics, no movemask (a compare result
comes out as four 32-bit words) and aligned-only plain loads. SWAR handles the
first 64+ bytes and the tail; PIE scans aligned 64-byte chunks. Long inputs got
3-10x faster than SWAR (`count_byte` 60 -> 584 MB/s, `find_byte` 112 -> 439).

Short inputs got *slower*: `find_byte` and `strlen` on 85-byte lines ~19%,
`next_token` 8% (5035 -> 5440 ns). The cause was GCC: whether the SWAR lead
became a hardware loop decided it, and that flipped on details like `?:` vs
`if`. Tried and discarded (*unrecorded*): a 128-byte lead, single-copy `?:` and
`if` forms (the `if` form restored the hardware loop but made `next_token` 25%
worse), calls across files (~23 cycles each, fixed by building PIE in the SWAR
file). The trade-off was documented and left.

## 6. Whole kernels in assembly (09-28 evening)

The fix for compiler fragility: write each PIE kernel as one assembly
function (lead, vector loop, tail), so hardware loops and register use are
fixed. All numbers S3, recorded.

**`find_byte`** (`aecfc6f`): 1522 -> 1140 ns on short lines (now faster than
SWAR's 1280), 439 -> 717 MB/s. The first version crashed after the kernel
tests: it stored a byte at `a1+0` with `entry a1, 16`, which is the caller's
register save area. `entry a1, 32` reserves local space.

**`strlen`**, three iterations:
- As a second entry into `find_byte` with n = SIZE_MAX (`f564a6f`): 446 -> 707
  MB/s, but short lines stayed at 1372 ns, behind SWAR's 1135.
- Its own function without length tracking (`52f9ed2`): 1092 ns, 729 MB/s.
- **What the ROM does:** disassembling newlib's `strlen` in the S3's ROM showed
  a word loop with one `bnone` (branch if `x & mask == 0`) per byte: 6
  instructions per word, and the branch taken says which byte, where the SWAR
  zero-lane mask costs ~7 per word plus ~8 to turn a mask into a position. On
  an in-order core, untaken branches are cheap.
- `bnone` loop (`3a98ba0`): no change at first (1092 ns), because the loop
  branched on the loaded word in the next instruction, and a load's result
  isn't ready then. Moving the pointer bump between them: 990 ns. A 128-byte
  lead for `strlen` only, so an 85-byte line never pays for a chunk plus a
  rescan: 917 ns, against the ROM's 897 (and 4.5x the ROM's throughput).
- Tried and reverted (*unrecorded*): the ROM's unaligned-start trick (one
  aligned load, bytes before `s` forced nonzero): 940 ns. A 128-byte lead for
  `find_byte` too: -4% throughput.

**`find_byte` with `bnone`** (`a0eea12`): XOR with the pattern, then one
`bnone` per byte. 1140 -> 1080 ns; `next_token` 4848 -> 4375 ns. A new test
(`Kernels.after_exit`) pins down a hardware-loop detail the code relies on: a
search that exits a `loop` early leaves its counter nonzero, and a later
*branch* to that loop's end must not loop back (only falling through does).
Verified on the S3.

**`find_bytes`** (`7d4b0dd`, no PIE version existed): two words per
iteration, a `bnone` per byte, matches stored out of line and jumping back into
the loop. 2570 -> 1638 ns; `faf_tokens` 4452 -> 3455, `split` 3832 -> 2900,
`split_owned` 4645 -> 3715 ns.

**`mismatch`** (`e52885f`): for unaligned `b`, `ssa8l` sets the shift once and
`src` funnel-shifts each word from two aligned loads. The PIE 32-byte loop in
the same function. 1670 -> 918 ns, 234 -> 415 MB/s; faster than the ROM's
`memcmp` (1533 ns, 87 MB/s), which only says whether the buffers differ.

**`count_byte` and `ascii_prefix`** (`7b638d5`, short-line rows added first in
`d5eacf2` for a clean baseline): `ascii_prefix` tests bits 7/15/23/31 with
`bbsi`, one instruction per byte with no masks; `count_byte` accumulates 0/1
lanes branch-free and counts PIE chunks in runs of 15. `count_byte` 1970 ->
1220 ns and 584 -> 963 MB/s; `ascii_prefix` 1315 -> 915 ns; `utf8_valid`
483 -> 842 MB/s. With no C left in the PIE kernels they moved to their own
file, `src/kernels/faf_kernels_pie.c`.

**Net, S3, C + PIE (`38bd90f`) -> assembly (`7b638d5`):**

| | before | after |
|---|---:|---:|
| `find_byte`, 85-byte lines | 1522 ns | 1080 ns |
| `strlen`, 85-byte lines | 1355 ns | 918 ns |
| `mismatch`, 85-byte lines | 1675 ns | 918 ns |
| `find_bytes`, 85-byte lines | 2570 ns | 1638 ns |
| `next_token` | 5440 ns | 4375 ns |
| `faf_tokens` | 4452 ns | 3455 ns |
| `find_byte` / `count_byte` / `mismatch`, 64 KB | 439 / 584 / 231 MB/s | 724 / 963 / 415 MB/s |

## 7. `faf_ring` (09-29)

The ring-buffer region from the design conversation (section 0), for data
that is safe to lose (`2af1959`). It stores strings in a caller-provided
buffer and overwrites the oldest. A handle is `{pos, off, len}` (16 bytes),
where `pos` is a 64-bit count of all bytes ever written: a string is intact
while `head - pos <= capacity`, with no generations and no per-entry header.
A string that doesn't fit before the end of the buffer starts over at the
beginning, and the skipped tail counts as written, which keeps `head` and the
buffer offset in step.

Tested against a model that records which push last wrote each byte:
validity must match it exactly, over random lengths, a buffer size that
isn't a power of two, and a count starting past 2^32. Two planted bugs (an
off-by-one in the check, a wrap that doesn't count the skipped tail) both
fail it.

Keeping recent lines (store one, read the one from 32 steps earlier), M1:
ring 13.7 ns per step in a fixed 4 KB, `strdup` + `free` 85.8 ns (6.3x
slower) and 7.5 KB at peak.

## 8. Arenas over caller memory (09-29)

Bindings and threads need pools they size and own, so the static pools became
the default *arena* and more can be laid over any buffer (`5a3bd81`). A
first version put the arena pointer in the region handle; a build option
(`FAF_ARENAS=0`) then removed it again for the boards, where it cost ~2% of
region work and 10-18% of acquire/release. Both were replaced in section 11.

## 9. Batches, and Python through ctypes (09-29)

A binding should be a page of declarations, so the loops over many strings
live in C (`faf_batch`, `21948ae`): one call per batch, only pointers and
integers, nothing that depends on build options. Arrow's offsets can't hold
the result of a split (separators sit between the strings), so a batch is
separate `starts[]` and `ends[]`; Arrow data is still a batch without a copy.
The Python example (`examples/python`) is ctypes and the standard library,
checked against plain Python with bugs planted in the shim.

On the boards the new tests' static buffers left the ESP32 no RAM for
FreeRTOS (now allocated per test, `e14f0c2`), and the kernels benchmark group
turned out to leak 44 KB per run (`1bb9d06`).

## 10. The lower case kernel (09-29)

Python's first results lost lower casing to pyarrow 4-30x. The kernel was
the library's slowest: NEON did 16 bytes per iteration with the tail test in
the loop; it now does 64 in four vectors, and strings under 16 bytes as
overlapping words (`dcbad6c`): 10.7 -> 41.4 GB/s on the M1. The SWAR loop
computed both cases and branched per word; with the constants hoisted it
doubled on both boards (38 -> 76 MB/s). Twice a small helper wasn't inlined,
which cost register saves on every call.

The faster kernel barely moved Python's `lower`: 57-64% of the time was the
shim allocating a new `bytearray` per result. Converting the whole range in
one pass (as pyarrow does) and taking results from a faf arena, reused
across calls, brought `lower` level with pyarrow (`aedefe5`). A remaining
"cold" 2x gap was traced with page fault counts to pyarrow reusing pages its
own input builder had freed; with fresh memory on both sides they are even.

## 11. Consolidation (09-29)

An audit of the session found layers patching earlier choices: a `faf_ffi_*`
wrapper existed because region handles changed layout with `FAF_ARENAS`, and
the shim carried layout and strategy logic every binding would repeat. So:

- **One handle layout** (`6e7ad99`): arenas are opaque and registered in a
  small table; a region is a 64-bit integer (table entry, epoch, generation,
  pool). A handle from a retired arena is rejected instead of read through.
  `FAF_ARENAS` and `faf_ffi_*` went. The table cost a dependent load per
  region operation (acquire + release 5.6 -> 11 ns on the M1); default-arena
  handles now skip it, leaving acquire + release at 6.0 ns and whole-record
  work within 2%.
- **Batches are handles** (`616d07a`): a batch lives in a region (a header,
  its views, the bytes of results), C knows whether its views are in order
  and picks the lower case strategy, and one `faf_batch_free` releases it.
  Four lower case entry points became one plus in place.
- **The shim** keeps handles, garbage collection and interop only
  (`6f00703`): 393 lines, down from 505, over one arena of 64 x 1 GB of
  address space.

## Lessons
## Lessons

- **On in-order cores, codegen details are the performance.** A taken branch,
  a lost hardware loop or a call is 10-30% of a byte-scanning loop. The M1
  hides the same changes within ±3%.
- **Where code lives matters as much as what it does** (flash cache vs IRAM).
- **Measure alternately and against controls.** Several early "wins" were
  within the spread once old and new builds were run interleaved.
- **Read the platform's own code.** The ROM's `strlen` showed the right
  instruction for the job (`bnone`), which the SWAR habit had missed.
- **Check that the test can fail.** A mutant that passes may not have been
  built: with second-resolution timestamps, `make` can skip the rebuild.
- **Count what the OS does, not just the time.** Page faults explained in one
  run a cold gap that timing alone had attributed to the wrong thing twice.
- **Keep an oracle.** Every backend is checked against the `ref` kernels at
  every length and alignment; it caught each assembly bug before a benchmark
  could.
