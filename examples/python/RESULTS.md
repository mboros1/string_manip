# Python example: results

`python3 examples/python/bench.py` at `68a4669` (regions: `with faf.region()`):
Apple M1 Max, macOS 26.0.1, CPython 3.12.10, pyarrow 19.0.1. Best of 5 (faf,
pyarrow) or 3 (plain Python); every variant's results are checked against the
others. Plain Python works on `bytes`, with the same ASCII semantics as faf.
Rerun the script at that commit for the full output.

## Summary

- **Scans** (`contains`, `find`, `count`, filtering) are 4-52x faster than
  plain Python and 2-57x faster than pyarrow; the pyarrow gap grows with line
  length.
- **Lower case** is 15-98x faster than plain Python and even with pyarrow at
  every length (0.9-1.0x).
- **A 529 MB grep-lower-write pipeline** takes 0.28 s, against 1.49 s for
  pyarrow and 2.0 s for plain Python, with about half their peak memory.
- **Batches pay off from ~100 strings per call**; below that the per-call cost
  (a few microseconds) dominates.
- **Ingest from a Python list** costs 131-158 ns a string, 4-6x pyarrow's.

## Per operation (ns per string: faf, then how many times faster than plain Python / pyarrow)

| op | 8 B | 32 B | 128 B | 512 B | 4096 B |
|---|---:|---:|---:|---:|---:|
| contains | 4.3 (52x / 2.6x) | 15.9 (14x / 3.4x) | 30.1 (9.7x / 8.3x) | 97.3 (5.8x / 10x) | 670 (4.3x / 12x) |
| find | 4.5 (17x / 2.2x) | 14.5 (5.6x / 3.5x) | 31.0 (4.4x / 7.9x) | 97.0 (4.2x / 10x) | 672 (4.1x / 12x) |
| count ',' | 4.4 (18x / 2.3x) | 5.9 (15x / 7.4x) | 8.7 (18x / 28x) | 17.5 (20x / 57x) | 172 (13x / 46x) |
| startswith | 1.6 (42x / 0.6x) | 4.0 (19x / 1.4x) | 4.2 (18x / 1.4x) | 16.6 (5.4x / 0.9x) | 11.5 (7.5x / 1.2x) |
| lengths | 1.1 (12x / 0.2x) | 1.2 (13x / 0.2x) | 1.3 (12x / 0.3x) | 0.8 (66x / 0.9x) | 1.7 (21x / 1.2x) |
| lower | 0.3 (98x / 0.9x) | 1.1 (40x / 1.0x) | 4.3 (22x / 1.0x) | 16.7 (19x / 1.0x) | 131 (15x / 1.0x) |
| hash | 4.8 (8.9x / -) | 9.0 (5.8x / -) | 29.2 (3.1x / -) | 132 (2.0x / -) | 1,371 (1.1x / -) |
| filter contains | 4.8 (47x / 2.3x) | 14.5 (16x / 3.8x) | 31.0 (9.3x / 8.0x) | 97.6 (5.7x / 10x) | 673 (4.3x / 12x) |

32 MB of log-like lines per column, ~1% containing ERROR. pyarrow has no
per-string hash; Python's `hash()` is timed on fresh objects, since `bytes`
caches its hash. Other inputs, 128 B lines unless noted:

| input | faf | vs Python | vs pyarrow |
|---|---:|---:|---:|
| contains, 50% of lines match | 24.9 | 11x | 6.2x |
| short keys (8-24 B): contains | 8.5 | 28x | 2.6x |
| short keys: startswith | 6.9 | 11x | 0.8x |
| short keys: hash | 15.6 | 3.8x | - |
| short keys: lower | 0.5 | 77x | 0.9x |
| UTF-8 heavy text: contains | 43.0 | 6.7x | 3.4x |
| UTF-8 heavy text: count ',' | 8.5 | 18x | 28x |
| UTF-8 heavy text: lower | 4.2 | 21x | 1.0x |

## Pipelines: keep lines with ERROR, lower case them, write them

| input | faf | pyarrow | Python | Python, streaming |
|---|---:|---:|---:|---:|
| 529 MB log (5M lines, 2% match): seconds | 0.28 | 1.49 | 2.00 | 1.89 |
| peak memory, x file size | 1.20 | 2.15 | 2.32 | 0.00 |
| 572 MB `random_strings.txt`: seconds | 0.30 | 1.60 | 0.95 | 0.85 |
| peak memory, x file size | 1.00 | 2.00 | 2.05 | 0.00 |

Best of 3, each in a fresh process after a warm-up on a tiny file; all
variants write identical output. faf's memory is mostly the mapped file's own
pages, which the OS can drop; streaming Python holds one line at a time.

## Batch size (80 B lines; ns per string, faf / Python)

| strings | contains | lower | lengths |
|---:|---:|---:|---:|
| 10 | 131 / 269 (2.1x) | 114 / 73.8 (0.65x) | 77.6 / 28.1 (0.36x) |
| 100 | 23.7 / 261 (11x) | 14.3 / 63.3 (4.4x) | 9.3 / 20.0 (2.1x) |
| 10,000 | 23.1 / 257 (11x) | 2.7 / 56.6 (21x) | 1.3 / 14.7 (11x) |
| 1,000,000 | 22.6 / 259 (11x) | 2.7 / 78.4 (29x) | 1.2 / 16.1 (13x) |

## Ingest from a Python list (1M strings, ns per string)

| input | `from_list` | `pa.array` | faf `lower` afterwards |
|---|---:|---:|---:|
| bytes, 80 B | 131 | 35.2 | 2.6 |
| str, 80 B | 158 | 36.4 | 2.6 |
| str, short keys | 150 | 27.1 | 0.4 |

## The first call in a fresh process

| lines | faf | pyarrow, built from a list | pyarrow, fresh memory |
|---|---:|---:|---:|
| 8 B | 1.1 ns (2,048 faults) | 1.3 (701) | 1.4 (1,823) |
| 128 B | 21.0 (2,045) | 12.9 (96) | 16.8 (2,033) |
| 4096 B | 466 (2,047) | 242 (95) | 519 (2,050) |

Median of 5 processes, page faults during the call in brackets. Built from a
list, pyarrow's `lower` reuses pages its own builder freed, so it isn't cold;
with fresh memory on both sides the two are about even.
