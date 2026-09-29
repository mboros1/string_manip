# Python example: results

`python3 examples/python/bench.py` at `dc1a72e`: Apple M1 Max, macOS 26.0.1,
CPython 3.12.10, pyarrow 19.0.1. Best of 5 (faf, pyarrow) or 3 (plain Python),
every variant's results checked against the others. Plain Python works on
`bytes`, with the same ASCII semantics as faf. Rerun the script at that commit
for the full output. The lower case rows and the cold table are from
`2297f37` (results share their input's views), the rest from `dc1a72e`.

## Summary

- **Scans** (`contains`, `find`, `count`, filtering) are 4-54x faster than
  plain Python and 2-56x faster than pyarrow; the pyarrow gap grows with line
  length.
- **Lower case** is 15-95x faster than plain Python and even with pyarrow at
  every length (0.9-1.0x): a result shares its input's views, as pyarrow's
  shares its offsets.
- **A 529 MB grep-lower-write pipeline** takes 0.31 s, against 1.50 s for
  pyarrow and 2.0 s for plain Python, with half their peak memory.
- **Batches pay off from ~100 strings per call**; below that the per-call cost
  (a few microseconds) dominates.
- **Ingest from a Python list** costs 134-159 ns a string, 4-6x pyarrow's.

## Per operation (ns per string: faf, then how many times faster than plain Python / pyarrow)

| op | 8 B | 32 B | 128 B | 512 B | 4096 B |
|---|---:|---:|---:|---:|---:|
| contains | 4.3 (54x / 2.5x) | 14.2 (16x / 3.8x) | 30.3 (9.3x / 8.2x) | 98.0 (5.6x / 10x) | 672 (4.3x / 12x) |
| find | 4.5 (17x / 2.2x) | 14.8 (5.5x / 3.4x) | 30.8 (4.4x / 7.9x) | 97.9 (4.2x / 10x) | 674 (4.1x / 12x) |
| count ',' | 4.3 (19x / 2.4x) | 6.0 (15x / 7.3x) | 8.5 (19x / 28x) | 17.5 (20x / 57x) | 172 (13x / 46x) |
| startswith | 1.6 (43x / 0.6x) | 3.9 (19x / 1.4x) | 4.2 (18x / 1.4x) | 13.8 (6.0x / 1.0x) | 11.1 (8.0x / 1.2x) |
| lengths | 1.2 (12x / 0.2x) | 1.1 (14x / 0.2x) | 0.7 (22x / 0.5x) | 0.8 (54x / 0.7x) | 1.8 (21x / 1.3x) |
| lower | 0.3 (95x / 0.9x) | 1.1 (38x / 1.0x) | 4.2 (21x / 1.0x) | 16.5 (19x / 1.0x) | 131 (15x / 1.0x) |
| hash | 4.9 (9.0x / -) | 8.8 (6.1x / -) | 29.0 (3.2x / -) | 132 (1.9x / -) | 1,371 (1.2x / -) |
| filter contains | 4.9 (46x / 2.2x) | 14.8 (15x / 3.6x) | 30.9 (8.9x / 8.0x) | 98.8 (5.6x / 10x) | 675 (4.3x / 12x) |

32 MB of log-like lines per column, ~1% containing ERROR. pyarrow has no
per-string hash; Python's `hash()` is timed on fresh objects, since `bytes`
caches its hash. Other inputs, 128 B lines unless noted:

| input | faf | vs Python | vs pyarrow |
|---|---:|---:|---:|
| contains, 50% of lines match | 25.4 | 10x | 6.1x |
| short keys (8-24 B): contains | 8.7 | 28x | 2.5x |
| short keys: startswith | 6.4 | 12x | 0.8x |
| short keys: hash | 15.6 | 3.9x | - |
| short keys: lower | 0.5 | 76x | 0.9x |
| UTF-8 heavy text: contains | 43.4 | 6.6x | 3.4x |
| UTF-8 heavy text: count ',' | 8.4 | 19x | 28x |
| UTF-8 heavy text: lower | 4.2 | 21x | 1.0x |

## Pipelines: keep lines with ERROR, lower case them, write them

| input | faf | pyarrow | Python | Python, streaming |
|---|---:|---:|---:|---:|
| 529 MB log (5M lines, 2% match): seconds | 0.31 | 1.50 | 2.02 | 1.88 |
| peak memory, x file size | 1.20 | 2.15 | 2.33 | 0.00 |
| 572 MB `random_strings.txt`: seconds | 0.33 | 1.60 | 0.94 | 0.85 |
| peak memory, x file size | 1.00 | 2.00 | 2.05 | 0.00 |

Best of 3, each in a fresh process after a warm-up on a tiny file; all
variants write identical output. faf's memory is mostly the mapped file's own
pages, which the OS can drop; streaming Python holds one line at a time.

## Batch size (80 B lines; ns per string, faf / Python)

| strings | contains | lower | lengths |
|---:|---:|---:|---:|
| 10 | 138 / 261 (1.9x) | 236 / 75.0 (0.3x) | 85.2 / 28.8 (0.3x) |
| 100 | 25.1 / 253 (10x) | 26.1 / 63.4 (2.4x) | 10.1 / 20.1 (2.0x) |
| 10,000 | 23.2 / 245 (11x) | 3.5 / 57.7 (17x) | 1.1 / 14.7 (14x) |
| 1,000,000 | 22.6 / 248 (11x) | 3.5 / 79.4 (23x) | 1.2 / 16.1 (14x) |

## Ingest from a Python list (1M strings, ns per string)

| input | `Batch.from_list` | `pa.array` | faf `lower` afterwards |
|---|---:|---:|---:|
| bytes, 80 B | 134 | 36.3 | 3.3 |
| str, 80 B | 159 | 38.5 | 3.3 |
| str, short keys | 155 | 27.2 | 1.1 |

## The first call in a fresh process

| lines | faf | pyarrow, built from a list | pyarrow, fresh memory |
|---|---:|---:|---:|
| 8 B | 1.4 ns (2,050 faults) | 1.1 (701) | 1.4 (1,823) |
| 128 B | 19.0 (2,047) | 13.1 (96) | 17.4 (2,035) |
| 4096 B | 552 (2,048) | 250 (95) | 553 (2,050) |

Median of 5 processes, page faults during the call in brackets. Built from a
list, pyarrow's `lower` reuses pages its own builder freed, so it isn't cold;
with fresh memory on both sides the two are even.
