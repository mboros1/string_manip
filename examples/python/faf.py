"""FaF strings from Python: a ctypes shim over the batch API (src/faf_batch.h).

The shim is the list of declarations in _declare(); everything else is a small
convenience layer. It needs only the standard library (ctypes, array, mmap);
numpy and pyarrow are used for conversions when installed.

    import faf
    buf = faf.Buffer.from_file("app.log")      # mmap, nothing is copied
    lines = buf.lines()                         # views: 16 bytes per line
    errors = lines.filter(lines.contains(b"ERROR"))
    print(len(errors), errors.head(3))
    out = errors.lower().join(b"\\n")           # bytes are made here, once

Strings are bytes, and case functions are ASCII only, as in the C library.
A Batch holds views: string i is data[starts[i]:ends[i]] of its Buffer, which
it keeps alive. Only indexing (b[i], iteration, head) and join/compact/lower
create bytes on the Python side.
"""

import ctypes
import itertools
import mmap
import os
import sys
from array import array
from ctypes import c_char, c_int, c_int64, c_size_t, c_uint64, c_void_p

__all__ = ["Buffer", "Batch", "lib"]


# ---- The shim: load the library and declare the functions ----

def _find_library():
    if os.environ.get("FAF_LIB"):
        return os.environ["FAF_LIB"]
    ext = "dylib" if sys.platform == "darwin" else "so"
    here = os.path.dirname(os.path.abspath(__file__))
    return os.path.join(here, "..", "..", "obj", f"libfaf.{ext}")


def _declare(lib):
    P = c_void_p  # every pointer: data, starts/ends, outputs
    sig = {
        "faf_batch_split_count": (c_size_t, [P, c_size_t, c_char]),
        "faf_batch_split": (c_size_t, [P, c_size_t, c_char, P, P, c_size_t]),
        "faf_batch_select": (c_size_t, [P, P, c_size_t, P, P, P]),
        "faf_batch_take": (None, [P, P, P, c_size_t, P, P]),
        "faf_batch_lengths": (None, [P, P, c_size_t, P]),
        "faf_batch_total": (c_int64, [P, P, c_size_t]),
        "faf_batch_find": (None, [P, P, P, c_size_t, P, c_size_t, P]),
        "faf_batch_count": (None, [P, P, P, c_size_t, P, c_size_t, P]),
        "faf_batch_hash": (None, [P, P, P, c_size_t, c_uint64, P]),
        "faf_batch_compact": (None, [P, P, P, c_size_t, P, P]),
        "faf_batch_ascii_case": (None, [P, P, P, c_size_t, c_int, P, P]),
        "faf_batch_join": (c_int64, [P, P, P, c_size_t, P, c_size_t, P]),
    }
    for name in ("contains", "starts_with", "ends_with", "eq", "eq_icase"):
        sig[f"faf_batch_{name}"] = (c_size_t, [P, P, P, c_size_t, P, c_size_t, P])
    for name, (restype, argtypes) in sig.items():
        fn = getattr(lib, name)
        fn.restype, fn.argtypes = restype, argtypes
    return lib


# CDLL releases the GIL for the duration of every call.
lib = _declare(ctypes.CDLL(_find_library()))


# ---- Addresses of Python buffers ----

def _address(obj):
    """Address of the bytes of `obj` (bytes, bytearray, array, mmap, ...)."""
    if isinstance(obj, array):
        return obj.buffer_info()[0]
    if isinstance(obj, bytes):
        # the bytes object's own storage: ctypes doesn't copy it
        return ctypes.cast(ctypes.c_char_p(obj), c_void_p).value
    if hasattr(obj, "address"):  # pyarrow.Buffer
        return obj.address
    # anything writable with the buffer protocol: bytearray, mmap, ...
    return ctypes.addressof(c_char.from_buffer(obj))


# Where empty buffers point: a real byte, never read (lengths are 0).
_EMPTY = b"\0"
_EMPTY_ADDR = ctypes.cast(ctypes.c_char_p(_EMPTY), c_void_p).value


class _Ints:
    """n int64 values starting at element `first` of `owner` (kept alive)."""

    __slots__ = ("owner", "addr", "view")

    def __init__(self, owner, first, n):
        self.owner = owner
        self.addr = _address(owner) + 8 * first if n else 0
        self.view = memoryview(owner).cast("B").cast("q")[first:first + n]


def _zeros(typecode, n):
    return array(typecode, [0]) * n


def _needle(b):
    if isinstance(b, str):
        b = b.encode()
    return b, len(b)


# ---- Buffer and Batch ----

class Buffer:
    """Bytes to take views of: a file (mapped), bytes, bytearray, ..."""

    def __init__(self, obj):
        self.obj = obj
        self.nbytes = len(obj)
        self.addr = _address(obj) if self.nbytes else _EMPTY_ADDR
        self.view = memoryview(obj).cast("B")

    @classmethod
    def from_file(cls, path):
        with open(path, "rb") as f:
            if os.fstat(f.fileno()).st_size == 0:
                return cls(b"")
            # copy-on-write: a writable mapping (so ctypes can take its
            # address), and nothing is copied because nothing writes
            return cls(mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_COPY))

    @classmethod
    def from_bytes(cls, data):
        return cls(data)

    def split(self, sep=b"\n"):
        """Views of the pieces between `sep` bytes, like bytes.split(sep)."""
        sep = sep if isinstance(sep, bytes) else sep.encode()
        if len(sep) != 1:
            raise ValueError("split separator must be one byte")
        n = lib.faf_batch_split_count(self.addr, self.nbytes, sep)
        starts, ends = _zeros("q", n), _zeros("q", n)
        lib.faf_batch_split(self.addr, self.nbytes, sep, _address(starts),
                            _address(ends), n)
        return Batch(self, _Ints(starts, 0, n), _Ints(ends, 0, n))

    def lines(self):
        """Like split(b"\\n"), without the empty piece after a final newline
        (so an empty buffer has no lines, like bytes.splitlines)."""
        b = self.split(b"\n")
        if not self.nbytes or self.view[-1] == ord("\n"):
            b = b._first(len(b) - 1)
        return b


class Batch:
    """n views into one Buffer: string i is data[starts[i]:ends[i]]."""

    def __init__(self, buffer, starts, ends):
        self.buffer, self.starts, self.ends = buffer, starts, ends
        self.n = len(starts.view)

    # -- constructors --

    @classmethod
    def _from_offsets(cls, buffer, offsets, first, n):
        # Arrow layout: string i is data[off[i]:off[i + 1]]
        return cls(buffer, _Ints(offsets, first, n), _Ints(offsets, first + 1, n))

    @classmethod
    def from_list(cls, items):
        """From a list of str (UTF-8 encoded) or bytes: this copies."""
        bs = [s.encode() if isinstance(s, str) else s for s in items]
        offsets = array("q", itertools.accumulate(map(len, bs), initial=0))
        return cls._from_offsets(Buffer(b"".join(bs)), offsets, 0, len(bs))

    @classmethod
    def from_arrow(cls, arr):
        """From a pyarrow binary/string array without copying the bytes
        (string/binary arrays are cast to large_*, which copies offsets)."""
        import pyarrow as pa
        if isinstance(arr, pa.ChunkedArray):
            arr = arr.combine_chunks()
        if arr.null_count:
            raise ValueError("arrays with nulls are not supported")
        if pa.types.is_string(arr.type) or pa.types.is_binary(arr.type):
            arr = arr.cast(pa.large_binary())
        _, off_buf, data_buf = arr.buffers()
        data = Buffer(data_buf if data_buf is not None and data_buf.size else b"")
        return cls._from_offsets(data, off_buf, arr.offset, len(arr))

    # -- views --

    def __len__(self):
        return self.n

    def __getitem__(self, i):
        if i < 0:
            i += self.n
        if not 0 <= i < self.n:
            raise IndexError(i)
        return bytes(self.buffer.view[self.starts.view[i]:self.ends.view[i]])

    def __iter__(self):
        v, s, e = self.buffer.view, self.starts.view, self.ends.view
        for i in range(self.n):
            yield bytes(v[s[i]:e[i]])

    def head(self, k=5):
        return [self[i] for i in range(min(k, self.n))]

    def __repr__(self):
        more = ", ..." if self.n > 3 else ""
        return f"<faf.Batch of {self.n}: {self.head(3)!r}{more}>"

    def _first(self, m):
        return Batch(self.buffer, _Ints(self.starts.owner, self._pos(self.starts), m),
                     _Ints(self.ends.owner, self._pos(self.ends), m))

    @staticmethod
    def _pos(ints):
        # element index of ints.view[0] in its owner
        return (ints.addr - _address(ints.owner)) // 8 if ints.addr else 0

    def _args(self):
        return self.buffer.addr, self.starts.addr, self.ends.addr, self.n

    # -- per string results --

    def lengths(self):
        out = _zeros("q", self.n)
        lib.faf_batch_lengths(self.starts.addr, self.ends.addr, self.n, _address(out))
        return out

    def total(self):
        """Sum of the lengths."""
        return lib.faf_batch_total(self.starts.addr, self.ends.addr, self.n)

    def find(self, needle):
        """Index of the first needle in each string, or -1."""
        out = _zeros("q", self.n)
        lib.faf_batch_find(*self._args(), *_needle(needle), _address(out))
        return out

    def count(self, needle):
        """Non-overlapping occurrences of needle in each string."""
        out = _zeros("q", self.n)
        lib.faf_batch_count(*self._args(), *_needle(needle), _address(out))
        return out

    def _test(self, fn, needle):
        out = _zeros("B", self.n)
        fn(*self._args(), *_needle(needle), _address(out))
        return out

    def contains(self, needle):
        """A mask: 1 where the string contains needle."""
        return self._test(lib.faf_batch_contains, needle)

    def startswith(self, prefix):
        return self._test(lib.faf_batch_starts_with, prefix)

    def endswith(self, suffix):
        return self._test(lib.faf_batch_ends_with, suffix)

    def eq(self, other):
        return self._test(lib.faf_batch_eq, other)

    def eq_icase(self, other):
        return self._test(lib.faf_batch_eq_icase, other)

    def hash(self, seed=0):
        out = _zeros("Q", self.n)
        lib.faf_batch_hash(*self._args(), seed, _address(out))
        return out

    # -- selection: new views, no bytes copied --

    def filter(self, mask):
        """The strings where mask is non-zero (mask: array('B'), bytes, ...)."""
        if len(mask) != self.n:
            raise ValueError("mask length differs from the batch")
        starts, ends = _zeros("q", self.n), _zeros("q", self.n)
        m = lib.faf_batch_select(self.starts.addr, self.ends.addr, self.n,
                                 _address(mask) if self.n else 0,
                                 _address(starts), _address(ends))
        return Batch(self.buffer, _Ints(starts, 0, m), _Ints(ends, 0, m))

    def take(self, indices):
        """The strings at `indices` (array('q'), or any iterable of ints)."""
        idx = indices if isinstance(indices, array) and indices.typecode == "q" \
            else array("q", indices)
        m = len(idx)
        if m and (min(idx) < 0 or max(idx) >= self.n):
            raise IndexError("take: index out of range")
        starts, ends = _zeros("q", m), _zeros("q", m)
        lib.faf_batch_take(self.starts.addr, self.ends.addr, _address(idx) if m else 0,
                           m, _address(starts), _address(ends))
        return Batch(self.buffer, _Ints(starts, 0, m), _Ints(ends, 0, m))

    # -- new bytes --

    def _into_new(self, fill):
        total = self.total()
        dst, offsets = bytearray(total), _zeros("q", self.n + 1)
        fill(_address(dst) if total else _EMPTY_ADDR, _address(offsets))
        return Batch._from_offsets(Buffer(dst), offsets, 0, self.n)

    def compact(self):
        """A copy with the strings end to end (Arrow layout)."""
        return self._into_new(lambda d, o: lib.faf_batch_compact(*self._args(), d, o))

    def lower(self):
        return self._into_new(lambda d, o: lib.faf_batch_ascii_case(*self._args(), 0, d, o))

    def upper(self):
        return self._into_new(lambda d, o: lib.faf_batch_ascii_case(*self._args(), 1, d, o))

    def join(self, sep=b"\n"):
        """The strings joined by sep, as a bytearray (e.g. to write out)."""
        sep, m = _needle(sep)
        size = self.total() + m * max(self.n - 1, 0)
        dst = bytearray(size)
        lib.faf_batch_join(*self._args(), sep, m, _address(dst) if size else _EMPTY_ADDR)
        return dst

    # -- conversions (optional dependencies) --

    def to_arrow(self):
        """A pyarrow large_binary array (copies unless already compact)."""
        import pyarrow as pa
        c = self if self._is_compact() else self.compact()
        off = pa.py_buffer(c.starts.owner)
        data = pa.py_buffer(c.buffer.obj)
        return pa.Array.from_buffers(pa.large_binary(), c.n, [None, off, data],
                                     offset=self._pos(c.starts))

    def _is_compact(self):
        return (self.starts.owner is self.ends.owner and
                self.ends.addr == self.starts.addr + 8 and
                isinstance(self.starts.owner, array) and
                isinstance(self.buffer.obj, (bytes, bytearray)))


def to_numpy(result):
    """A numpy view (no copy) of a result array (lengths, masks, hashes...)."""
    import numpy as np
    return np.frombuffer(result, dtype={"q": np.int64, "Q": np.uint64,
                                        "B": np.uint8}[result.typecode])
