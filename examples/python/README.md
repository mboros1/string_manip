# Python example

FaF from Python through `ctypes`: the batch API
([`src/batch/faf_batch.h`](../../src/batch/faf_batch.h)) and a page of declarations. Only
the standard library is needed; numpy and pyarrow are used for conversions
when installed. It is an example of how small a binding can be, not a
package: the same declarations work from any language that can call C.

```sh
make shared                           # obj/libfaf.dylib or .so
python3 examples/python/test_faf.py   # every method against plain Python
python3 examples/python/bench.py      # vs plain Python and pyarrow (--quick)
```

```python
import faf

data = faf.map_file("app.log")                  # mmap; nothing is copied
with faf.region() as r:
    lines = r.lines(data)                        # views, in C
    errors = lines.filter(lines.contains(b"ERROR"))
    out = errors.lower().join(b"\n")            # bytes for Python, copied out
with open("errors.log", "wb") as f:
    f.write(out)
```

- It keeps the library's model: a region is a unit of work. Batches made in
  it (and their results) live until the `with` block ends; using one after
  that raises. `lower(into=other_region)` puts a result in a region that is
  released separately.
- Per-string results (`find`, `count`, `contains`, masks, hashes) come back as
  `array.array`; `faf.to_numpy()` views them without a copy.
- Bytes cross into Python only as copies: indexing, iteration, `head`, `join`
  and `to_arrow`. `r.from_arrow` takes Arrow data without copying it.
- Regions come from one arena over an anonymous mapping (16 regions of up to
  4 GB: address space, not memory, until used). `MemoryError` means a region
  is full, or all are in use.
- Strings are bytes; case functions are ASCII only, like `bytes.lower()`.
  `lower(inplace=True)` works on batches over a `bytearray`.

A friendlier Python library, one that manages lifetimes for you, belongs on
top of this rather than in it.

ctypes checks nothing, so `test_faf.py` is the safety net: every method
against plain Python on random data, and bugs planted in the shim to show it
catches them. MicroPython has no ctypes; there the same API would be wrapped
in a C module.

Results: [RESULTS.md](RESULTS.md).
