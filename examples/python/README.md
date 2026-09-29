# Python example

FaF from Python through `ctypes`: the batch API
([`src/faf_batch.h`](../../src/faf_batch.h)) and a page of declarations. Only
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

lines = faf.Buffer.from_file("app.log").lines()   # mmap; views in C
errors = lines.filter(lines.contains(b"ERROR"))
print(len(errors), errors.head(3))                # bytes for Python made here
with open("errors.log", "wb") as f:
    f.write(errors.lower().join(b"\n"))
```

- A `Batch` is a handle to a batch in a faf arena: its views, and the bytes of
  results like `lower`, live in C. Python collecting the `Batch` frees it.
- Per-string results (`find`, `count`, `contains`, masks, hashes) come back as
  `array.array`; `faf.to_numpy()` views them without a copy.
- Bytes cross into Python only on indexing, iteration, `head`, `join` and
  `to_arrow`. `Batch.from_arrow` takes Arrow data without copying it.
- The arena is one anonymous mapping of 64 regions of 1 GB: address space,
  not memory, until used. Each live batch holds a region, so one result is at
  most 1 GB, and `MemoryError` means no region was free.
- Strings are bytes; case functions are ASCII only, like `bytes.lower()`.
  `lower(inplace=True)` works on batches over a `bytearray`.

ctypes checks nothing, so `test_faf.py` is the safety net: every method
against plain Python on random data, and bugs planted in the shim to show it
catches them. MicroPython has no ctypes; there the same API would be wrapped
in a C module.

Results: [RESULTS.md](RESULTS.md).
