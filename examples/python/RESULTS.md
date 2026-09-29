# Python example: results

`python3 examples/python/bench.py`, full run. Apple M1 Max, macOS 26.0.1,
CPython 3.12.10, pyarrow 19.0.1; library at `95af983` (the bench script as
committed next, in the commit that adds this file). Times are best of 5 (faf,
pyarrow) or 3 (plain Python); every variant's results were checked against
the others. Plain Python works on `bytes` (ASCII semantics, like faf).

## What it shows

- **Scans with small results win big.** `contains`, `find`, `count` and
  filtering are 4-52x faster than plain Python and 1.6-56x faster than pyarrow,
  with the pyarrow gap growing with line length (8x at 128 B, 12x at 4 KB for
  `contains`; 29-56x for `count` at 128 B and up). These are the library's
  best kernels (byte search at 62 GB/s, count at 25 GB/s in C) with one
  byte or one int of output per string.
- **Making new bytes: even with pyarrow when the output memory is reused.**
  After the kernel rewrite and one-pass case conversion (below), `lower(out=)`
  into a reused buffer runs at pyarrow's speed from 32 B lines up (1.00-1.04x;
  0.75x at 8 B). A fresh `lower()` is ~4x slower than pyarrow's best-of-N,
  because allocating and first touching new memory costs more than the
  conversion; in a fresh process, where pyarrow's memory pool is cold too,
  pyarrow is even at 8 B and ~2x faster at 128 B and 4 KB. Short-string
  `lengths` and `startswith` are also slightly faster in pyarrow.
- **Pipelines:** load 529 MB of log lines, keep the 2% with ERROR, lower case
  them, write them out: faf 0.30 s, pyarrow 1.52 s, plain Python 2.03 s
  (streaming line by line: 1.93 s). faf is 5-7x faster end to end.
- **Memory:** faf's peak is about the file size (the mapped file's pages,
  which are clean and can be dropped by the OS, plus 16 bytes per line of
  views); reading the file into Python or pyarrow costs about twice the file
  size; streaming Python needs almost nothing and is the memory winner.
- **Batch size:** a batch call costs a few microseconds of Python overhead,
  so faf wins from about 100 strings per call (`contains` 10x at 100 strings,
  `lower` breaks even at 100); for 10 strings plain Python is faster.
- **Ingest is the worst case:** `Batch.from_list` costs 137-165 ns per string
  (encode, join, offsets, in Python), 3.5-6x pyarrow's `pa.array`, and about
  10x the cost of an operation on the batch afterwards. Data that starts as a
  file or an Arrow array avoids it.

## 1. Per operation (ns per string; ratios are how many times faster faf is)

Line lengths, 32 MB of log-like lines each, ~1% containing ERROR:

| op | 8 B | 32 B | 128 B | 512 B | 4096 B |
|---|---:|---:|---:|---:|---:|
| contains | 4.3 (51.8x / 2.6x) | 13.4 (17.4x / 4.0x) | 29.7 (9.6x / 8.4x) | 97.1 (5.8x / 10.5x) | 669 (4.4x / 12.3x) |
| find | 4.7 (16.9x / 2.1x) | 14.6 (5.6x / 3.5x) | 30.4 (4.5x / 8.1x) | 98.1 (4.2x / 10.3x) | 671 (4.2x / 12.0x) |
| count ',' | 4.3 (18.9x / 2.4x) | 5.8 (15.8x / 7.5x) | 8.3 (19.0x / 29.0x) | 17.5 (20.5x / 56.5x) | 177 (12.6x / 44.9x) |
| startswith | 1.6 (43.7x / 0.6x) | 4.0 (18.7x / 1.4x) | 4.2 (17.9x / 1.4x) | 13.5 (6.2x / 1.0x) | 13.3 (6.7x / 1.0x) |
| lengths | 1.0 (14.9x / 0.3x) | 1.0 (15.2x / 0.3x) | 1.1 (15.6x / 0.4x) | 0.7 (64.1x / 1.0x) | 1.6 (25.2x / 1.7x) |
| lower | 9.5 (3.1x / 0.03x) | 7.1 (5.9x / 0.2x) | 20.6 (4.7x / 0.2x) | 82.2 (3.8x / 0.2x) | 797 (2.6x / 0.2x) |
| hash | 5.0 (8.8x / n/a) | 8.9 (5.8x / n/a) | 29.0 (3.3x / n/a) | 132 (2.0x / n/a) | 1,371 (1.2x / n/a) |
| filter contains | 6.7 (34.1x / 1.6x) | 15.6 (14.7x / 3.5x) | 32.1 (8.8x / 7.9x) | 98.4 (5.6x / 10.4x) | 673 (4.3x / 12.1x) |

Each cell: faf ns per string (vs plain Python / vs pyarrow). pyarrow has no
per-string hash; Python's `hash()` is timed on fresh objects, since `bytes`
caches its hash.

Match rate (128 B lines, 260k): contains at 1% vs 50% matching is 29.1 vs
24.5 ns (pyarrow 249 vs 157, Python 284 vs 268). Short keys (8-24 B, 2M):
contains 8.2 ns (30x / 2.7x), hash 16.4 ns (3.6x), startswith 6.7 ns (11x /
0.8x), lower 17.3 ns (2.1x / 0.03x). UTF-8 heavy text: search stays correct
and fast on bytes >= 0x80 (contains 42 ns, 7x / 3.5x; count 8.6 ns, 18x / 28x).

## 2. Pipelines (seconds, best of 3; memory above the warmed-up process)

| 529 MB log, 5M lines | seconds | peak MB | x file size |
|---|---:|---:|---:|
| faf | 0.303 | 708 | 1.34x |
| plain Python, read + split | 2.034 | 1,229 | 2.32x |
| plain Python, streaming | 1.927 | 0 | 0.00x |
| pyarrow | 1.517 | 1,138 | 2.15x |

| 572 MB `random_strings.txt` | seconds | peak MB | x file size |
|---|---:|---:|---:|
| faf | 0.309 | 575 | 1.01x |
| plain Python, read + split | 0.940 | 1,175 | 2.05x |
| plain Python, streaming | 0.853 | 1 | 0.00x |
| pyarrow | 1.619 | 1,146 | 2.00x |

Each variant runs in a fresh process, first on a tiny file (so pyarrow's
one-time compute engine start, ~0.2 s and ~37 MB, isn't counted), and all
four write byte-identical output.

## 3. Batch size (80 B lines; faf / plain Python ns per string)

| strings | contains | lower | lengths |
|---:|---:|---:|---:|
| 10 | 139 / 265 (1.9x) | 524 / 82.5 (0.16x) | 85.6 / 33.2 (0.39x) |
| 100 | 24.7 / 253 (10.2x) | 57.6 / 60.0 (1.04x) | 10.2 / 21.3 (2.1x) |
| 1,000 | 15.9 / 247 (15.5x) | 12.9 / 55.7 (4.3x) | 1.8 / 16.3 (9.0x) |
| 1,000,000 | 21.8 / 247 (11.3x) | 14.5 / 66.2 (4.6x) | 1.3 / 16.6 (12.3x) |

## 4. Ingest from a Python list (1M strings, ns per string)

| input | faf `from_list` | `pa.array` | faf `lower` afterwards |
|---|---:|---:|---:|
| bytes, 80 B | 137 | 40.0 | 14.3 |
| str, 80 B | 165 | 42.0 | 13.4 |
| str, short keys | 152 | 27.3 | 17.3 |

## After the `ascii_case` rewrite (`c6f6d89`)

The lower case kernel went from 10.7 to 41.4 GB/s in C (`dcbad6c`). In
Python, `lower` changed much less:

| lower, ns per string | 8 B | 32 B | 128 B | 512 B | 4096 B |
|---|---:|---:|---:|---:|---:|
| before (`95af983`) | 9.5 | 7.1 | 20.6 | 82.2 | 797 |
| after (`c6f6d89`) | 4.6 | 7.7 | 23.1 | 75.0 | 638 |
| pyarrow `ascii_lower` | 0.3 | 1.0 | 4.3 | 16.7 | 126 |

Where the time goes, measured separately (same data, ns per string):

| lines | `lower()` | fresh `bytearray` | offsets array | C call into reused buffers |
|---|---:|---:|---:|---:|
| 128 B x 260k | 17.3 | 9.8 | 0.6 | 5.2 |
| 4096 B x 8k | 465 | 298 | 0.2 | 139 |

So the kernel is no longer the problem; allocating (and first touching) a new
output buffer every call is. The fixes are on the Python side: an output
buffer the caller can reuse, and lowering a batch that covers most of its
buffer in one pass with the same views, as pyarrow does. Every other
operation matched the first run within noise.

## After one-pass case conversion and `out=` (`b87c9c1`)

`lower` on a batch whose views are in order (a split, an Arrow array) now
converts the whole range in one call and reuses the views
(`faf_batch_ascii_case_range`); scattered views use `faf_batch_ascii_case_span`
or pack the strings, whichever touches less memory. `out=` takes a buffer to
reuse, and `inplace=True` converts a `bytearray`'s strings where they are.

| lower, ns per string | 8 B | 32 B | 128 B | 512 B | 4096 B |
|---|---:|---:|---:|---:|---:|
| first run (`95af983`) | 9.5 | 7.1 | 20.6 | 82.2 | 797 |
| new kernel (`c6f6d89`) | 4.6 | 7.7 | 23.1 | 75.0 | 638 |
| one pass (`b87c9c1`) | 1.3 | 4.8 | 16.8 | 69.7 | 501 |
| one pass, reused `out=` | 0.3 | 1.1 | 4.0 | 16.5 | 133 |
| pyarrow `ascii_lower` (best of 5) | 0.3 | 1.1 | 4.1 | 17.1 | 133 |

First call in a fresh process, so neither side has warm memory:

| lines | faf `lower()` | pyarrow | pyarrow / faf |
|---|---:|---:|---:|
| 8 B | 1.4 | 1.5 | 1.06x |
| 128 B | 27.3 | 12.2 | 0.45x |
| 4096 B | 603 | 287 | 0.48x |

Allocating fresh output memory is what's left: a new `bytearray` (or an
anonymous `mmap`, measured the same) runs at ~8 GB/s for 33 MB against ~31
GB/s into a reused one; pyarrow's allocator gets fresh memory faster, and
its pool keeps it between calls. Batches of 100 strings now win `lower`
against plain Python 1.9x (was 1.04x). Other operations are unchanged.

<details><summary>Full output</summary>

```
faf Python example benchmarks, commit 95af983f2
macOS-26.0.1-arm64-arm-64bit, arm, Python 3.12.10 (CPython), pyarrow 19.0.1

== 1. Is a batch op fast? ==========================================

lines of 8 B (3,728,270 lines, ~1% contain ERROR) -- ns per string
  op                faf  python  pyarrow  python/faf  pyarrow/faf
  ----------------  ---  ------  -------  ----------  -----------
  contains          4.3     222     10.9      51.82x        2.55x
  find              4.7    79.4      9.9      16.93x        2.11x
  count ','         4.3    82.0     10.3      18.95x        2.38x
  startswith        1.6    71.0      0.9      43.68x        0.56x
  lengths           1.0    15.2      0.3      14.92x        0.26x
  lower             9.5    29.5      0.3       3.09x        0.03x
  hash              5.0    44.0      n/a       8.75x             
  filter contains   6.7     229     10.9      34.08x        1.62x
  split into lines  3.6    19.1     14.1       5.23x        3.86x

lines of 32 B (1,016,800 lines, ~1% contain ERROR) -- ns per string
  op                 faf  python  pyarrow  python/faf  pyarrow/faf
  ----------------  ----  ------  -------  ----------  -----------
  contains          13.4     233     53.7      17.43x        4.01x
  find              14.6    82.2     51.3       5.64x        3.52x
  count ','          5.8    92.1     43.8      15.78x        7.51x
  startswith         4.0    75.3      5.5      18.75x        1.36x
  lengths            1.0    15.9      0.3      15.22x        0.26x
  lower              7.1    41.7      1.1       5.86x        0.15x
  hash               8.9    51.4      n/a       5.79x             
  filter contains   15.6     229     54.2      14.75x        3.49x
  split into lines   6.0    33.5     29.1       5.59x        4.87x

lines of 128 B (260,111 lines, ~1% contain ERROR) -- ns per string
  op                 faf  python  pyarrow  python/faf  pyarrow/faf
  ----------------  ----  ------  -------  ----------  -----------
  contains          29.7     285      249       9.62x        8.37x
  find              30.4     137      245       4.51x        8.08x
  count ','          8.3     158      241      19.05x       28.96x
  startswith         4.2    75.0      5.8      17.86x        1.38x
  lengths            1.1    17.0      0.4      15.63x        0.36x
  lower             20.6    96.3      4.6       4.68x        0.22x
  hash              29.0    94.7      n/a       3.26x             
  filter contains   32.1     281      252       8.75x        7.85x
  split into lines  12.8    91.5     72.3       7.16x        5.66x

lines of 512 B (65,408 lines, ~1% contain ERROR) -- ns per string
  op                 faf  python  pyarrow  python/faf  pyarrow/faf
  ----------------  ----  ------  -------  ----------  -----------
  contains          97.1     561    1,020       5.77x       10.51x
  find              98.1     416    1,008       4.24x       10.28x
  count ','         17.5     359      987      20.52x       56.46x
  startswith        13.5    83.4     13.4       6.16x        0.99x
  lengths            0.7    42.8      0.7      64.10x        0.98x
  lower             82.2     315     17.0       3.84x        0.21x
  hash               132     265      n/a       2.00x             
  filter contains   98.4     552    1,019       5.61x       10.36x
  split into lines  47.2     305      242       6.46x        5.13x

lines of 4096 B (8,190 lines, ~1% contain ERROR) -- ns per string
  op                  faf  python  pyarrow  python/faf  pyarrow/faf
  ----------------  -----  ------  -------  ----------  -----------
  contains            669   2,943    8,222       4.40x       12.28x
  find                671   2,832    8,076       4.22x       12.04x
  count ','           177   2,232    7,952      12.60x       44.91x
  startswith         13.3    88.4     13.9       6.67x        1.05x
  lengths             1.6    39.1      2.6      25.19x        1.67x
  lower               797   2,075      132       2.60x        0.17x
  hash              1,371   1,613      n/a       1.18x             
  filter contains     673   2,907    8,160       4.32x       12.13x
  split into lines    295   2,046    1,781       6.94x        6.04x

128 B lines, 1% contain ERROR (260,111 lines) -- ns per string
  op                 faf  python  pyarrow  python/faf  pyarrow/faf
  ----------------  ----  ------  -------  ----------  -----------
  contains          29.1     284      249       9.79x        8.59x
  find              30.0     139      246       4.64x        8.20x
  filter contains   31.1     284      250       9.12x        8.04x
  split into lines  12.8    89.4     72.0       7.01x        5.64x

128 B lines, 50% contain ERROR (260,111 lines) -- ns per string
  op                 faf  python  pyarrow  python/faf  pyarrow/faf
  ----------------  ----  ------  -------  ----------  -----------
  contains          24.5     268      157      10.94x        6.43x
  find              25.3     118      152       4.66x        6.01x
  filter contains   26.3     268      166      10.19x        6.31x
  split into lines  12.9    89.2     72.5       6.93x        5.63x

short keys, 8-24 B (2,097,152) -- ns per string
  op                 faf  python  pyarrow  python/faf  pyarrow/faf
  ----------------  ----  ------  -------  ----------  -----------
  contains           8.2     243     21.8      29.71x        2.66x
  startswith         6.7    74.3      5.5      11.03x        0.82x
  lengths            1.1    15.8      0.3      14.63x        0.26x
  hash              16.4    59.6      n/a       3.63x             
  lower             17.3    36.8      0.5       2.13x        0.03x
  split into lines   6.8    31.5     24.7       4.61x        3.62x

UTF-8 heavy text, 128 B (260,111) -- ns per string
  op                 faf  python  pyarrow  python/faf  pyarrow/faf
  ----------------  ----  ------  -------  ----------  -----------
  contains          42.0     295      147       7.01x        3.50x
  find              43.0     137      145       3.18x        3.36x
  count ','          8.6     158      239      18.43x       27.95x
  lower             17.8    89.2      4.3       5.01x        0.24x
  split into lines  12.7    89.5     71.3       7.06x        5.63x

Summary: faf vs plain Python / pyarrow by line length (ns per string)
  op                              8 B                 32 B                128 B                 512 B               4096 B
  ---------------  ------------------  -------------------  -------------------  --------------------  -------------------
  contains         4.3 (51.8x / 2.6x)  13.4 (17.4x / 4.0x)   29.7 (9.6x / 8.4x)   97.1 (5.8x / 10.5x)   669 (4.4x / 12.3x)
  find             4.7 (16.9x / 2.1x)   14.6 (5.6x / 3.5x)   30.4 (4.5x / 8.1x)   98.1 (4.2x / 10.3x)   671 (4.2x / 12.0x)
  count ','        4.3 (18.9x / 2.4x)   5.8 (15.8x / 7.5x)  8.3 (19.0x / 29.0x)  17.5 (20.5x / 56.5x)  177 (12.6x / 44.9x)
  startswith       1.6 (43.7x / 0.6x)   4.0 (18.7x / 1.4x)   4.2 (17.9x / 1.4x)    13.5 (6.2x / 1.0x)   13.3 (6.7x / 1.0x)
  lengths          1.0 (14.9x / 0.3x)   1.0 (15.2x / 0.3x)   1.1 (15.6x / 0.4x)    0.7 (64.1x / 1.0x)   1.6 (25.2x / 1.7x)
  lower             9.5 (3.1x / 0.0x)    7.1 (5.9x / 0.2x)   20.6 (4.7x / 0.2x)    82.2 (3.8x / 0.2x)    797 (2.6x / 0.2x)
  hash               5.0 (8.8x / n/a)     8.9 (5.8x / n/a)    29.0 (3.3x / n/a)      132 (2.0x / n/a)   1,371 (1.2x / n/a)
  filter contains  6.7 (34.1x / 1.6x)  15.6 (14.7x / 3.5x)   32.1 (8.8x / 7.9x)   98.4 (5.6x / 10.4x)   673 (4.3x / 12.1x)

== 2. Does staying in views pay off? ===============================

log lines, 110 B, 2% ERROR: 529 MB -> keep ERROR lines, lower, write (best of 3, fresh process each; memory: peak above the warmed-up process)
  variant           seconds  peak MB  x file size  lines out
  ----------------  -------  -------  -----------  ---------
  faf                 0.303      708        1.34x    100,431
  python              2.034    1,229        2.32x    100,431
  python streaming    1.927        0        0.00x    100,431
  pyarrow             1.517    1,138        2.15x    100,431

random_strings.txt (no ERROR lines): 572 MB -> keep ERROR lines, lower, write (best of 3, fresh process each; memory: peak above the warmed-up process)
  variant           seconds  peak MB  x file size  lines out
  ----------------  -------  -------  -----------  ---------
  faf                 0.309      575        1.01x          1
  python              0.940    1,175        2.05x          1
  python streaming    0.853        1        0.00x          1
  pyarrow             1.619    1,146        2.00x          1

== 3. When is a batch too small? ==================================

ns per string, faf / plain Python, 80 B lines (python/faf > 1: faf wins)
  strings      contains                lower             lengths        
  ---------  ----------  ------  -----------  -----  -----------  ------
  10          139 / 265   1.90x   524 / 82.5  0.16x  85.6 / 33.2   0.39x
  100        24.7 / 253  10.23x  57.6 / 60.0  1.04x  10.2 / 21.3   2.09x
  1,000      15.9 / 247  15.52x  12.9 / 55.7  4.32x   1.8 / 16.3   9.03x
  10,000     22.1 / 247  11.15x  11.9 / 58.7  4.94x   1.2 / 15.2  12.68x
  100,000    22.6 / 249  11.03x  14.0 / 62.5  4.47x   0.7 / 15.2  20.60x
  1,000,000  21.8 / 247  11.34x  14.5 / 66.2  4.57x   1.3 / 16.6  12.32x

== 4. What does ingest from Python objects cost? ==================

ns per string, 1,000,000 strings from a Python list
  input            faf from_list  pyarrow array  faf lower (for scale)
  ---------------  -------------  -------------  ---------------------
  bytes, 80 B                137           40.0                   14.3
  str, 80 B                  165           42.0                   13.4
  str, short keys            152           27.3                   17.3
```

Second run, after the kernel rewrite:

```
faf Python example benchmarks, commit c6f6d8954
macOS-26.0.1-arm64-arm-64bit, arm, Python 3.12.10 (CPython), pyarrow 19.0.1

== 1. Is a batch op fast? ==========================================

lines of 8 B (3,728,270 lines, ~1% contain ERROR) -- ns per string
  op                faf  python  pyarrow  python/faf  pyarrow/faf
  ----------------  ---  ------  -------  ----------  -----------
  contains          4.3     224     10.9      52.14x        2.54x
  find              4.7    79.4      9.9      16.96x        2.12x
  count ','         4.4    82.3     10.2      18.88x        2.35x
  startswith        1.6    69.6      0.9      42.59x        0.56x
  lengths           1.3    14.6      0.3      11.26x        0.21x
  lower             4.6    29.3      0.3       6.35x        0.06x
  hash              5.5    45.7      n/a       8.30x             
  filter contains   6.9     228     10.9      33.13x        1.58x
  split into lines  4.0    19.8     14.9       4.96x        3.73x

lines of 32 B (1,016,800 lines, ~1% contain ERROR) -- ns per string
  op                 faf  python  pyarrow  python/faf  pyarrow/faf
  ----------------  ----  ------  -------  ----------  -----------
  contains          14.9     240     53.5      16.16x        3.60x
  find              14.4    82.3     51.5       5.71x        3.58x
  count ','          5.9    92.7     44.1      15.58x        7.41x
  startswith         4.0    76.1      5.5      19.16x        1.39x
  lengths            1.3    15.8      0.3      11.81x        0.22x
  lower              7.7    42.6      1.0       5.49x        0.13x
  hash               9.0    51.6      n/a       5.71x             
  filter contains   15.9     227     54.6      14.30x        3.44x
  split into lines   6.5    33.6     29.8       5.20x        4.61x

lines of 128 B (260,111 lines, ~1% contain ERROR) -- ns per string
  op                 faf  python  pyarrow  python/faf  pyarrow/faf
  ----------------  ----  ------  -------  ----------  -----------
  contains          29.5     286      249       9.68x        8.44x
  find              30.2     139      246       4.60x        8.13x
  count ','          8.4     159      240      19.00x       28.64x
  startswith         4.5    75.2      5.8      16.67x        1.29x
  lengths            1.3    16.6      0.4      13.17x        0.34x
  lower             23.1    91.4      4.3       3.96x        0.19x
  hash              28.2    96.7      n/a       3.43x             
  filter contains   32.3     282      251       8.71x        7.77x
  split into lines  13.3    90.3     72.5       6.79x        5.45x

lines of 512 B (65,408 lines, ~1% contain ERROR) -- ns per string
  op                 faf  python  pyarrow  python/faf  pyarrow/faf
  ----------------  ----  ------  -------  ----------  -----------
  contains          96.8     563    1,020       5.81x       10.53x
  find              96.9     413    1,006       4.26x       10.38x
  count ','         17.7     354      993      20.05x       56.25x
  startswith        20.1     106     17.8       5.30x        0.89x
  lengths            0.9    61.0      0.7      69.65x        0.83x
  lower             75.0     315     16.7       4.20x        0.22x
  hash               133     310      n/a       2.33x             
  filter contains   96.9     556    1,024       5.74x       10.57x
  split into lines  47.9     312      245       6.52x        5.11x

lines of 4096 B (8,190 lines, ~1% contain ERROR) -- ns per string
  op                  faf  python  pyarrow  python/faf  pyarrow/faf
  ----------------  -----  ------  -------  ----------  -----------
  contains            674   2,958    8,185       4.39x       12.14x
  find                674   2,784    8,165       4.13x       12.12x
  count ','           175   2,231    8,006      12.75x       45.77x
  startswith         18.9     107     25.0       5.67x        1.32x
  lengths             2.2    49.6      4.1      22.19x        1.84x
  lower               638   2,057      126       3.23x        0.20x
  hash              1,373   1,994      n/a       1.45x             
  filter contains     699   2,946    8,168       4.21x       11.68x
  split into lines    302   2,116    1,802       7.02x        5.97x

128 B lines, 1% contain ERROR (260,111 lines) -- ns per string
  op                 faf  python  pyarrow  python/faf  pyarrow/faf
  ----------------  ----  ------  -------  ----------  -----------
  contains          29.4     284      250       9.64x        8.49x
  find              30.3     138      245       4.56x        8.07x
  filter contains   31.0     281      251       9.05x        8.08x
  split into lines  13.4    91.2     72.6       6.82x        5.43x

128 B lines, 50% contain ERROR (260,111 lines) -- ns per string
  op                 faf  python  pyarrow  python/faf  pyarrow/faf
  ----------------  ----  ------  -------  ----------  -----------
  contains          24.4     265      157      10.88x        6.43x
  find              24.7     120      152       4.87x        6.17x
  filter contains   26.3     267      166      10.17x        6.33x
  split into lines  12.9    90.0     74.0       7.00x        5.75x

short keys, 8-24 B (2,097,152) -- ns per string
  op                 faf  python  pyarrow  python/faf  pyarrow/faf
  ----------------  ----  ------  -------  ----------  -----------
  contains           8.2     241     21.9      29.53x        2.69x
  startswith         6.7    75.9      5.5      11.32x        0.82x
  lengths            1.2    15.4      0.3      12.75x        0.23x
  hash              16.4    61.4      n/a       3.74x             
  lower              8.7    37.5      0.5       4.32x        0.05x
  split into lines   7.2    32.4     24.4       4.49x        3.39x

UTF-8 heavy text, 128 B (260,111) -- ns per string
  op                 faf  python  pyarrow  python/faf  pyarrow/faf
  ----------------  ----  ------  -------  ----------  -----------
  contains          42.8     292      148       6.83x        3.45x
  find              42.6     139      145       3.26x        3.40x
  count ','          8.6     159      240      18.53x       27.90x
  lower             22.4    94.7      4.5       4.23x        0.20x
  split into lines  12.8    88.8     72.8       6.95x        5.70x

Summary: faf vs plain Python / pyarrow by line length (ns per string)
  op                              8 B                 32 B                128 B                 512 B               4096 B
  ---------------  ------------------  -------------------  -------------------  --------------------  -------------------
  contains         4.3 (52.1x / 2.5x)  14.9 (16.2x / 3.6x)   29.5 (9.7x / 8.4x)   96.8 (5.8x / 10.5x)   674 (4.4x / 12.1x)
  find             4.7 (17.0x / 2.1x)   14.4 (5.7x / 3.6x)   30.2 (4.6x / 8.1x)   96.9 (4.3x / 10.4x)   674 (4.1x / 12.1x)
  count ','        4.4 (18.9x / 2.4x)   5.9 (15.6x / 7.4x)  8.4 (19.0x / 28.6x)  17.7 (20.0x / 56.2x)  175 (12.8x / 45.8x)
  startswith       1.6 (42.6x / 0.6x)   4.0 (19.2x / 1.4x)   4.5 (16.7x / 1.3x)    20.1 (5.3x / 0.9x)   18.9 (5.7x / 1.3x)
  lengths          1.3 (11.3x / 0.2x)   1.3 (11.8x / 0.2x)   1.3 (13.2x / 0.3x)    0.9 (69.6x / 0.8x)   2.2 (22.2x / 1.8x)
  lower             4.6 (6.4x / 0.1x)    7.7 (5.5x / 0.1x)   23.1 (4.0x / 0.2x)    75.0 (4.2x / 0.2x)    638 (3.2x / 0.2x)
  hash               5.5 (8.3x / n/a)     9.0 (5.7x / n/a)    28.2 (3.4x / n/a)      133 (2.3x / n/a)   1,373 (1.5x / n/a)
  filter contains  6.9 (33.1x / 1.6x)  15.9 (14.3x / 3.4x)   32.3 (8.7x / 7.8x)   96.9 (5.7x / 10.6x)   699 (4.2x / 11.7x)

== 2. Does staying in views pay off? ===============================

log lines, 110 B, 2% ERROR: 529 MB -> keep ERROR lines, lower, write (best of 3, fresh process each; memory: peak above the warmed-up process)
  variant           seconds  peak MB  x file size  lines out
  ----------------  -------  -------  -----------  ---------
  faf                 0.294      708        1.34x    100,431
  python              2.016    1,229        2.32x    100,431
  python streaming    1.981        0        0.00x    100,431
  pyarrow             1.545    1,138        2.15x    100,431

random_strings.txt (no ERROR lines): 572 MB -> keep ERROR lines, lower, write (best of 3, fresh process each; memory: peak above the warmed-up process)
  variant           seconds  peak MB  x file size  lines out
  ----------------  -------  -------  -----------  ---------
  faf                 0.335      575        1.01x          1
  python              0.960    1,175        2.05x          1
  python streaming    0.881        0        0.00x          1
  pyarrow             1.645    1,146        2.00x          1

== 3. When is a batch too small? ==================================

ns per string, faf / plain Python, 80 B lines (python/faf > 1: faf wins)
  strings      contains                lower             lengths        
  ---------  ----------  ------  -----------  -----  -----------  ------
  10          140 / 275   1.97x   527 / 83.2  0.16x  85.5 / 32.7   0.38x
  100        24.9 / 263  10.55x  60.1 / 65.6  1.09x  10.5 / 20.9   1.98x
  1,000      17.0 / 257  15.12x  12.0 / 56.9  4.75x   1.6 / 16.0   9.87x
  10,000     22.1 / 257  11.61x   9.2 / 58.2  6.34x   1.1 / 15.2  14.23x
  100,000    22.1 / 256  11.57x  14.0 / 61.5  4.39x   1.1 / 14.4  13.14x
  1,000,000  22.0 / 256  11.64x  13.5 / 63.5  4.69x   1.2 / 16.1  13.42x

== 4. What does ingest from Python objects cost? ==================

ns per string, 1,000,000 strings from a Python list
  input            faf from_list  pyarrow array  faf lower (for scale)
  ---------------  -------------  -------------  ---------------------
  bytes, 80 B                131           35.4                   11.9
  str, 80 B                  153           36.0                   12.1
  str, short keys            151           27.2                    7.3
```

Third run, after one-pass case conversion and `out=`:

```
faf Python example benchmarks, commit b87c9c170
macOS-26.0.1-arm64-arm-64bit, arm, Python 3.12.10 (CPython), pyarrow 19.0.1

== 1. Is a batch op fast? ==========================================

lines of 8 B (3,728,270 lines, ~1% contain ERROR) -- ns per string
  op                  faf  python  pyarrow  python/faf  pyarrow/faf
  ------------------  ---  ------  -------  ----------  -----------
  contains            4.3     228     10.9      52.99x        2.54x
  find                4.8    79.3      9.9      16.64x        2.07x
  count ','           4.4    82.2     10.3      18.60x        2.32x
  startswith          1.6    69.3      0.9      42.05x        0.56x
  lengths             1.2    14.7      0.3      12.04x        0.24x
  lower               1.3    31.8      0.3      24.39x        0.21x
  lower (reused out)  0.3    31.2      0.3      91.14x        0.75x
  hash                5.0    41.6      n/a       8.25x             
  filter contains     6.8     225     10.9      32.91x        1.60x
  split into lines    3.9    19.7     14.9       5.02x        3.79x

lines of 32 B (1,016,800 lines, ~1% contain ERROR) -- ns per string
  op                   faf  python  pyarrow  python/faf  pyarrow/faf
  ------------------  ----  ------  -------  ----------  -----------
  contains            13.5     238     54.8      17.67x        4.06x
  find                14.4    84.1     51.0       5.84x        3.54x
  count ','            6.1    92.0     44.0      15.07x        7.21x
  startswith           4.0    76.3      5.5      18.99x        1.36x
  lengths              1.2    15.4      0.3      13.27x        0.26x
  lower                4.8    40.5      1.1       8.46x        0.23x
  lower (reused out)   1.1    40.7      1.1      38.05x        1.01x
  hash                 8.9    51.1      n/a       5.76x             
  filter contains     16.3     223     54.8      13.61x        3.35x
  split into lines     6.2    34.5     29.7       5.55x        4.77x

lines of 128 B (260,111 lines, ~1% contain ERROR) -- ns per string
  op                   faf  python  pyarrow  python/faf  pyarrow/faf
  ------------------  ----  ------  -------  ----------  -----------
  contains            29.5     287      250       9.74x        8.48x
  find                30.4     138      246       4.54x        8.09x
  count ','            8.5     158      240      18.64x       28.31x
  startswith           4.4    75.7      5.9      17.36x        1.36x
  lengths              1.4    15.7      0.4      10.97x        0.28x
  lower               16.8    92.1      4.1       5.47x        0.24x
  lower (reused out)   4.0    91.2      4.1      22.58x        1.01x
  hash                29.2    94.6      n/a       3.24x             
  filter contains     32.3     280      252       8.68x        7.82x
  split into lines    13.2    90.0     72.1       6.81x        5.46x

lines of 512 B (65,408 lines, ~1% contain ERROR) -- ns per string
  op                   faf  python  pyarrow  python/faf  pyarrow/faf
  ------------------  ----  ------  -------  ----------  -----------
  contains            96.6     563    1,020       5.83x       10.56x
  find                96.8     408    1,011       4.22x       10.44x
  count ','           18.0     356      997      19.76x       55.28x
  startswith          18.2     106     18.0       5.86x        0.99x
  lengths              1.5    63.5      1.0      41.24x        0.67x
  lower               69.7     318     17.1       4.55x        0.25x
  lower (reused out)  16.5     308     17.1      18.68x        1.04x
  hash                 133     303      n/a       2.28x             
  filter contains     97.5     555    1,016       5.69x       10.42x
  split into lines    47.9     307      244       6.41x        5.09x

lines of 4096 B (8,190 lines, ~1% contain ERROR) -- ns per string
  op                    faf  python  pyarrow  python/faf  pyarrow/faf
  ------------------  -----  ------  -------  ----------  -----------
  contains              676   2,944    8,238       4.35x       12.18x
  find                  679   2,799    8,149       4.12x       12.00x
  count ','             176   2,233    7,984      12.65x       45.25x
  startswith           18.2    92.5     21.0       5.07x        1.15x
  lengths               3.6    51.1      5.4      14.20x        1.50x
  lower                 501   2,086      136       4.16x        0.27x
  lower (reused out)    133   1,943      133      14.56x        1.00x
  hash                1,373   1,603      n/a       1.17x             
  filter contains       674   2,919    8,185       4.33x       12.15x
  split into lines      300   2,225    1,800       7.42x        6.00x

128 B lines, 1% contain ERROR (260,111 lines) -- ns per string
  op                 faf  python  pyarrow  python/faf  pyarrow/faf
  ----------------  ----  ------  -------  ----------  -----------
  contains          29.2     284      251       9.73x        8.58x
  find              30.1     141      246       4.69x        8.17x
  filter contains   32.6     283      252       8.69x        7.72x
  split into lines  12.9    94.5     72.9       7.34x        5.66x

128 B lines, 50% contain ERROR (260,111 lines) -- ns per string
  op                 faf  python  pyarrow  python/faf  pyarrow/faf
  ----------------  ----  ------  -------  ----------  -----------
  contains          24.9     264      156      10.62x        6.29x
  find              25.2     120      152       4.78x        6.04x
  filter contains   27.3     267      167       9.78x        6.11x
  split into lines  13.4    92.4     72.7       6.89x        5.42x

short keys, 8-24 B (2,097,152) -- ns per string
  op                 faf  python  pyarrow  python/faf  pyarrow/faf
  ----------------  ----  ------  -------  ----------  -----------
  contains           8.2     245     22.0      29.75x        2.67x
  startswith         6.6    77.1      5.6      11.65x        0.84x
  lengths            1.1    14.9      0.3      12.98x        0.26x
  hash              16.4    61.1      n/a       3.74x             
  lower              2.0    37.4      0.5      18.28x        0.22x
  split into lines   7.1    32.8     24.9       4.64x        3.54x

UTF-8 heavy text, 128 B (260,111) -- ns per string
  op                 faf  python  pyarrow  python/faf  pyarrow/faf
  ----------------  ----  ------  -------  ----------  -----------
  contains          43.0     295      148       6.86x        3.45x
  find              43.8     140      146       3.19x        3.33x
  count ','          8.7     158      241      18.13x       27.55x
  lower             18.7    94.4      4.1       5.04x        0.22x
  split into lines  12.8    90.2     72.5       7.04x        5.65x

lower, first call in a fresh process (ns per string)
  lines    faf  pyarrow  pyarrow/faf
  ------  ----  -------  -----------
  8 B      1.4      1.5        1.06x
  128 B   27.3     12.2        0.45x
  4096 B   603      287        0.48x

Summary: faf vs plain Python / pyarrow by line length (ns per string)
  op                                 8 B                 32 B                128 B                 512 B               4096 B
  ------------------  ------------------  -------------------  -------------------  --------------------  -------------------
  contains            4.3 (53.0x / 2.5x)  13.5 (17.7x / 4.1x)   29.5 (9.7x / 8.5x)   96.6 (5.8x / 10.6x)   676 (4.4x / 12.2x)
  find                4.8 (16.6x / 2.1x)   14.4 (5.8x / 3.5x)   30.4 (4.5x / 8.1x)   96.8 (4.2x / 10.4x)   679 (4.1x / 12.0x)
  count ','           4.4 (18.6x / 2.3x)   6.1 (15.1x / 7.2x)  8.5 (18.6x / 28.3x)  18.0 (19.8x / 55.3x)  176 (12.7x / 45.3x)
  startswith          1.6 (42.1x / 0.6x)   4.0 (19.0x / 1.4x)   4.4 (17.4x / 1.4x)    18.2 (5.9x / 1.0x)   18.2 (5.1x / 1.2x)
  lengths             1.2 (12.0x / 0.2x)   1.2 (13.3x / 0.3x)   1.4 (11.0x / 0.3x)    1.5 (41.2x / 0.7x)   3.6 (14.2x / 1.5x)
  lower               1.3 (24.4x / 0.2x)    4.8 (8.5x / 0.2x)   16.8 (5.5x / 0.2x)    69.7 (4.6x / 0.2x)    501 (4.2x / 0.3x)
  lower (reused out)  0.3 (91.1x / 0.8x)   1.1 (38.1x / 1.0x)   4.0 (22.6x / 1.0x)   16.5 (18.7x / 1.0x)   133 (14.6x / 1.0x)
  hash                  5.0 (8.3x / n/a)     8.9 (5.8x / n/a)    29.2 (3.2x / n/a)      133 (2.3x / n/a)   1,373 (1.2x / n/a)
  filter contains     6.8 (32.9x / 1.6x)  16.3 (13.6x / 3.4x)   32.3 (8.7x / 7.8x)   97.5 (5.7x / 10.4x)   674 (4.3x / 12.2x)

== 2. Does staying in views pay off? ===============================

log lines, 110 B, 2% ERROR: 529 MB -> keep ERROR lines, lower, write (best of 3, fresh process each; memory: peak above the warmed-up process)
  variant           seconds  peak MB  x file size  lines out
  ----------------  -------  -------  -----------  ---------
  faf                 0.320      708        1.34x    100,431
  python              2.060    1,229        2.32x    100,431
  python streaming    1.967        0        0.00x    100,431
  pyarrow             1.541    1,138        2.15x    100,431

random_strings.txt (no ERROR lines): 572 MB -> keep ERROR lines, lower, write (best of 3, fresh process each; memory: peak above the warmed-up process)
  variant           seconds  peak MB  x file size  lines out
  ----------------  -------  -------  -----------  ---------
  faf                 0.327      575        1.01x          1
  python              0.956    1,175        2.05x          1
  python streaming    0.863        0        0.00x          1
  pyarrow             1.646    1,146        2.00x          1

== 3. When is a batch too small? ==================================

ns per string, faf / plain Python, 80 B lines (python/faf > 1: faf wins)
  strings      contains                lower              lengths        
  ---------  ----------  ------  -----------  ------  -----------  ------
  10          140 / 269   1.92x   271 / 84.2   0.31x  85.3 / 32.1   0.38x
  100        25.9 / 252   9.73x  33.3 / 62.2   1.86x  10.8 / 20.7   1.91x
  1,000      16.5 / 248  15.01x   7.7 / 60.0   7.83x   2.0 / 16.2   7.95x
  10,000     22.9 / 247  10.77x   5.5 / 58.1  10.63x   2.2 / 15.7   6.99x
  100,000    22.5 / 249  11.05x  11.4 / 64.9   5.68x   0.8 / 14.6  17.27x
  1,000,000  22.3 / 247  11.06x  10.9 / 63.1   5.79x   1.3 / 15.7  12.30x

== 4. What does ingest from Python objects cost? ==================

ns per string, 1,000,000 strings from a Python list
  input            faf from_list  pyarrow array  faf lower (for scale)
  ---------------  -------------  -------------  ---------------------
  bytes, 80 B                135           41.7                   10.5
  str, 80 B                  164           43.3                   10.7
  str, short keys            153           27.1                    2.0
```

</details>
