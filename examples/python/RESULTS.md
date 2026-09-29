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
- **Making new bytes loses to pyarrow.** `lower` is 3-6x faster than plain
  Python but 4-30x slower than pyarrow: faf's lower case kernel is its
  slowest (10.7 GB/s in C, where pyarrow gets about 30 GB/s), it is called
  once per string instead of once over the buffer, and the Python side
  allocates a fresh zero-filled output every call. Short-string `lengths` and
  `startswith` are also slightly faster in pyarrow.
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

</details>
