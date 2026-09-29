# Python example

FaF from Python through `ctypes`: the library's batch API
([`src/faf_batch.h`](../../src/faf_batch.h)) and a page of declarations.
It needs only the standard library; numpy and pyarrow are used for
conversions when they are installed.

This is an example of how small a binding can be, not a Python package. The
same declarations work in any language that can call C (Rust `extern "C"`,
Go cgo, Julia `ccall`, Java's FFM API, C# P/Invoke, Node koffi): the batch API
takes only pointers and integers, allocates nothing, and doesn't depend on
how the library was built.

```sh
make shared                           # obj/libfaf.dylib or .so
python3 examples/python/test_faf.py   # every method against plain Python
python3 examples/python/bench.py      # vs plain Python and pyarrow (--quick)
```

```python
import faf

buf = faf.Buffer.from_file("app.log")        # mmap: nothing is copied
lines = buf.lines()                           # views, 16 bytes per line
errors = lines.filter(lines.contains(b"ERROR"))
print(len(errors), errors.head(3))            # bytes are made only here
with open("errors.log", "wb") as f:
    f.write(errors.lower().join(b"\n"))
```

## How it works

- A `Buffer` holds bytes: a file mapped with `mmap` (copy-on-write, so it has
  an address and is never copied), `bytes`, or a `bytearray`.
- A `Batch` is views into one buffer, as two int64 arrays: string i is
  `data[starts[i]:ends[i]]`, the same start/end pair as a `faf_string`.
  Splitting, filtering and taking make new views, not new bytes.
- Every operation is one call into C for the whole batch, and ctypes
  releases the GIL for it. Results are `array.array`s (int64 positions and
  counts, uint8 masks, uint64 hashes); `faf.to_numpy()` views them in numpy
  without a copy.
- Bytes are created only by indexing (`b[i]`, iteration, `head`) and by the
  operations that write new strings: `lower`, `upper`, `compact`, `join`.
- `Batch.from_arrow` / `to_arrow` convert pyarrow binary and string arrays
  without copying the bytes (Arrow's offsets are a batch as they are:
  `starts = offsets`, `ends = offsets + 1`). `Batch.from_list` copies a list
  of `str`/`bytes` in.

Strings are bytes, and case functions are ASCII only, like `bytes.lower()`
and pyarrow's `ascii_lower`, not `str.lower()`.

## Why ctypes

`ctypes` checks nothing: a wrong declaration gives wrong results or a crash.
`test_faf.py` is what guards against that, by checking every method against
plain Python on random data (including a hash reference written in Python and
a seed that needs all 64 bits, so a narrower declaration can't pass). For a
first-class binding, cffi's API mode or a CPython extension would check the
declarations at build time; the batch API would stay the same.

The ~1 µs a ctypes call costs is paid once per batch, not per string. See the
crossover section of the results for where that stops mattering.

MicroPython has no ctypes; there, the same batch API would be wrapped in a
user C module compiled into the firmware.

## Results

See [RESULTS.md](RESULTS.md).
