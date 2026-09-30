"""FaF strings from Python: a ctypes shim over the batch API (src/batch/faf_batch.h).

It keeps the library's model: work happens in a region, and everything made
there goes when the region is released.

    import faf
    data = faf.map_file("app.log")              # mmap, nothing is copied
    with faf.region() as r:
        lines = r.lines(data)                   # a batch: views, in C
        errors = lines.filter(lines.contains(b"ERROR"))
        out = errors.lower().join(b"\\n")       # bytes for Python, copied out
    # errors, lines: unusable here; out is a bytearray Python owns

Only the standard library is needed (ctypes, array, mmap); numpy and pyarrow
are used for conversions when installed. Strings are bytes, and case
functions are ASCII only, as in the C library.
"""

import ctypes
import itertools
import mmap
import os
import sys
import threading
from array import array
from ctypes import c_bool, c_char, c_int, c_int64, c_size_t, c_uint16, c_uint64
from ctypes import c_void_p

__all__ = ["region", "map_file", "Region", "Batch", "lib", "to_numpy"]


# ---- The shim: load the library and declare the functions ----

def _find_library():
    if os.environ.get("FAF_LIB"):
        return os.environ["FAF_LIB"]
    ext = "dylib" if sys.platform == "darwin" else "so"
    here = os.path.dirname(os.path.abspath(__file__))
    return os.path.join(here, "..", "..", "obj", f"libfaf.{ext}")


class _Region(ctypes.Structure):
    # faf_region from faf_string_mem.h, passed by value
    _fields_ = [("arena", c_void_p), ("pool", c_uint16), ("gen", c_uint16)]


class _Ctx(ctypes.Structure):
    # faf_ctx from faf_ctx.h, passed by pointer; tuning NULL: the defaults
    _fields_ = [("out", _Region), ("tuning", c_void_p)]


def _declare(lib):
    P, R, C = c_void_p, _Region, ctypes.POINTER(_Ctx)
    sig = {
        "faf_arena_size": (c_size_t, []),
        "faf_arena_init": (c_bool, [P, P, c_size_t, c_size_t]),
        "faf_arena_acquire": (R, [P]),
        "faf_region_release": (None, [R]),
        "faf_batch_split": (P, [C, P, c_size_t, c_char]),
        "faf_batch_from_offsets": (P, [C, P, P, c_size_t]),
        "faf_batch_len": (c_size_t, [P]),
        "faf_batch_data": (P, [P]),
        "faf_batch_starts": (P, [P]),
        "faf_batch_ends": (P, [P]),
        "faf_batch_total": (c_int64, [P]),
        "faf_batch_lengths": (None, [P, P]),
        "faf_batch_find": (None, [P, P, c_size_t, P]),
        "faf_batch_count": (None, [P, P, c_size_t, P]),
        "faf_batch_hash": (None, [P, c_uint64, P]),
        "faf_batch_select": (P, [C, P, P]),
        "faf_batch_take": (P, [C, P, P, c_size_t]),
        "faf_batch_ascii_case": (P, [C, P, c_int]),
        "faf_batch_compact": (P, [C, P]),
        "faf_batch_ascii_case_inplace": (None, [P, c_int]),
        "faf_batch_join": (c_int64, [P, P, c_size_t, P]),
    }
    for name in ("contains", "starts_with", "ends_with", "eq", "eq_icase"):
        sig[f"faf_batch_{name}"] = (c_size_t, [P, P, c_size_t, P])
    for name, (restype, argtypes) in sig.items():
        fn = getattr(lib, name)
        fn.restype, fn.argtypes = restype, argtypes
    return lib


# CDLL releases the GIL for the duration of every call.
lib = _declare(ctypes.CDLL(_find_library()))


# ---- Memory ----

def _address(obj):
    """Address of the bytes of `obj` (bytes, bytearray, array, mmap, ...)."""
    if not len(obj):
        return None
    if isinstance(obj, array):
        return obj.buffer_info()[0]
    if isinstance(obj, bytes):
        # the bytes object's own storage: ctypes doesn't copy it
        return ctypes.cast(ctypes.c_char_p(obj), c_void_p).value
    if hasattr(obj, "address"):  # pyarrow.Buffer
        return obj.address
    # anything writable with the buffer protocol: bytearray, mmap, ...
    return ctypes.addressof(c_char.from_buffer(obj))


def map_file(path):
    """The file's bytes, mapped (copy-on-write, so it has an address; nothing
    is copied since nothing writes). An empty file is b""."""
    with open(path, "rb") as f:
        if os.fstat(f.fileno()).st_size == 0:
            return b""
        return mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_COPY)


class _Arena:
    """One faf arena over an anonymous mapping: 16 pools of up to 4 GB. The
    OS maps pages only when they're first written, so this costs address
    space, not memory, until regions use it. If the OS won't reserve that
    much, pools are halved until it does."""

    def __init__(self, pool_bytes=4 << 30, npools=16):
        while True:
            try:
                self.mem = mmap.mmap(-1, pool_bytes * npools + (1 << 20))
                break
            except (OSError, OverflowError):
                if pool_bytes <= 16 << 20:
                    raise
                pool_bytes //= 2
        self.state = ctypes.create_string_buffer(lib.faf_arena_size())
        if not lib.faf_arena_init(self.state, _address(self.mem), len(self.mem), npools):
            raise MemoryError("faf: arena init failed")
        self.lock = threading.Lock()  # regions are taken on any thread


_arena = None


def region():
    """A region for a unit of work: `with faf.region() as r: ...`."""
    global _arena
    if _arena is None:
        _arena = _Arena()
    return Region(_arena)


# ---- Region and Batch ----

class Region:
    """Where batches live. Everything made in it goes when it is released
    (at the end of the `with` block); using a batch after that raises."""

    def __init__(self, arena):
        self.arena = arena
        with arena.lock:
            self.r = lib.faf_arena_acquire(arena.state)
        if not self.r.arena:
            raise MemoryError("faf: every region is in use")
        self.ctx = ctypes.byref(_Ctx(out=self.r))  # where results go
        self.open = True
        self.keep = []  # Python objects batches point into

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.release()

    def release(self):
        if self.open:
            self.open = False
            with self.arena.lock:
                lib.faf_region_release(self.r)

    def _batch(self, ptr, keep=None):
        if not ptr:
            raise MemoryError("faf: the region is full")
        if keep is not None:
            self.keep.append(keep)
        return Batch(self, ptr)

    def split(self, data, sep=b"\n", _len=None):
        """Views of the pieces of `data` between `sep` bytes, like bytes.split."""
        sep = sep if isinstance(sep, bytes) else sep.encode()
        if len(sep) != 1:
            raise ValueError("split separator must be one byte")
        n = len(data) if _len is None else _len
        return self._batch(lib.faf_batch_split(self.ctx, _address(data), n, sep), data)

    def lines(self, data):
        """Like split(b"\\n"), without the empty piece after a final newline
        (so empty data has no lines, like bytes.splitlines)."""
        if not len(data):
            return self.from_list([])
        return self.split(data, b"\n", len(data) - (data[-1] in (10, b"\n")))

    def from_offsets(self, data, offsets, n, first=0):
        """Arrow layout: string i is data[offsets[first + i] : ...[first + i + 1]].
        Nothing is copied."""
        o = _address(offsets) + 8 * first
        return self._batch(lib.faf_batch_from_offsets(self.ctx, _address(data), o, n),
                           (data, offsets))

    def from_list(self, items):
        """From a list of str (UTF-8 encoded) or bytes: this copies."""
        bs = [s.encode() if isinstance(s, str) else s for s in items]
        offsets = array("q", itertools.accumulate(map(len, bs), initial=0))
        return self.from_offsets(b"".join(bs), offsets, len(bs))

    def from_arrow(self, arr):
        """From a pyarrow binary/string array without copying the bytes
        (string/binary arrays are cast to large_*, which copies offsets)."""
        import pyarrow as pa
        if isinstance(arr, pa.ChunkedArray):
            arr = arr.combine_chunks()
        if arr.null_count:
            raise ValueError("arrays with nulls are not supported")
        if pa.types.is_string(arr.type) or pa.types.is_binary(arr.type):
            arr = arr.cast(pa.large_binary())
        _, off, data = arr.buffers()
        data = data if data is not None and data.size else b""
        return self.from_offsets(data, off, len(arr), first=arr.offset)


class Batch:
    """n views into one byte buffer, living in a Region."""

    def __init__(self, region_, ptr):
        self.region, self.ptr = region_, ptr
        self.n = lib.faf_batch_len(ptr)

    def _p(self):
        if not self.region.open:
            raise ValueError("faf: batch used after its region was released")
        return self.ptr

    # -- reading (copies bytes out) --

    def __len__(self):
        return self.n

    def _views(self):
        p = self._p()
        n = self.n
        s = (c_int64 * n).from_address(lib.faf_batch_starts(p)) if n else ()
        e = (c_int64 * n).from_address(lib.faf_batch_ends(p)) if n else ()
        return lib.faf_batch_data(p) or 0, s, e

    def __getitem__(self, i):
        if i < 0:
            i += self.n
        if not 0 <= i < self.n:
            raise IndexError(i)
        d, s, e = self._views()
        return ctypes.string_at(d + s[i], e[i] - s[i]) if e[i] > s[i] else b""

    def __iter__(self):
        d, s, e = self._views()
        for i in range(self.n):
            yield ctypes.string_at(d + s[i], e[i] - s[i]) if e[i] > s[i] else b""

    def head(self, k=5):
        return [self[i] for i in range(min(k, self.n))]

    def __repr__(self):
        if not self.region.open:
            return f"<faf.Batch of {self.n}, region released>"
        more = ", ..." if self.n > 3 else ""
        return f"<faf.Batch of {self.n}: {self.head(3)!r}{more}>"

    def join(self, sep=b"\n"):
        """The strings joined by sep, as a bytearray Python owns."""
        sep, m = _needle(sep)
        dst = bytearray(self.total() + m * max(self.n - 1, 0))
        lib.faf_batch_join(self._p(), sep, m, _address(dst))
        return dst

    def to_arrow(self):
        """A pyarrow large_binary array, copied out of the region."""
        import pyarrow as pa
        c = self.compact()
        d, s, e = c._views()
        offsets = array("q", s) + array("q", [e[-1] if c.n else 0])
        data = ctypes.string_at(d, c.total()) if c.total() else b""
        return pa.Array.from_buffers(pa.large_binary(), c.n,
                                     [None, pa.py_buffer(offsets), pa.py_buffer(data)])

    # -- per string results --

    def _out(self, typecode, fn, *args):
        out = _zeros(typecode, self.n)
        fn(self._p(), *args, _address(out))
        return out

    def total(self):
        """Sum of the lengths."""
        return lib.faf_batch_total(self._p())

    def lengths(self):
        return self._out("q", lib.faf_batch_lengths)

    def find(self, needle):
        """Index of the first needle in each string, or -1."""
        return self._out("q", lib.faf_batch_find, *_needle(needle))

    def count(self, needle):
        """Non-overlapping occurrences of needle in each string."""
        return self._out("q", lib.faf_batch_count, *_needle(needle))

    def contains(self, needle):
        """A mask: 1 where the string contains needle."""
        return self._out("B", lib.faf_batch_contains, *_needle(needle))

    def startswith(self, prefix):
        return self._out("B", lib.faf_batch_starts_with, *_needle(prefix))

    def endswith(self, suffix):
        return self._out("B", lib.faf_batch_ends_with, *_needle(suffix))

    def eq(self, other):
        return self._out("B", lib.faf_batch_eq, *_needle(other))

    def eq_icase(self, other):
        return self._out("B", lib.faf_batch_eq_icase, *_needle(other))

    def hash(self, seed=0):
        return self._out("Q", lib.faf_batch_hash, seed)

    # -- new batches: in this batch's region, or `into` another one (which
    # this batch must outlive: results may point into it) --

    def _new(self, fn, *args, into=None):
        r = into or self.region
        if not r.open:
            raise ValueError("faf: region already released")
        return r._batch(fn(r.ctx, self._p(), *args))

    def filter(self, mask, into=None):
        """The strings where mask is non-zero (mask: array('B'), bytes, ...)."""
        if len(mask) != self.n:
            raise ValueError("mask length differs from the batch")
        return self._new(lib.faf_batch_select, _address(mask), into=into)

    def take(self, indices, into=None):
        """The strings at `indices` (array('q'), or any iterable of ints)."""
        idx = indices if isinstance(indices, array) and indices.typecode == "q" \
            else array("q", indices)
        if idx and (min(idx) < 0 or max(idx) >= self.n):
            raise IndexError("take: index out of range")
        return self._new(lib.faf_batch_take, _address(idx), len(idx), into=into)

    def lower(self, inplace=False, into=None):
        """ASCII lower case. inplace=True changes the strings where they are
        (only for a batch whose bytes are a bytearray)."""
        return self._case(0, inplace, into)

    def upper(self, inplace=False, into=None):
        return self._case(1, inplace, into)

    def _case(self, upper, inplace, into):
        if not inplace:
            return self._new(lib.faf_batch_ascii_case, upper, into=into)
        data = lib.faf_batch_data(self._p())
        if not any(isinstance(k, bytearray) and _address(k) == data
                   for k in self.region.keep):
            raise TypeError("inplace needs a batch over a bytearray")
        lib.faf_batch_ascii_case_inplace(self.ptr, upper)
        return self

    def compact(self, into=None):
        """A copy with the strings end to end (Arrow layout)."""
        return self._new(lib.faf_batch_compact, into=into)


def _needle(b):
    if isinstance(b, str):
        b = b.encode()
    return b, len(b)


def _zeros(typecode, n):
    return array(typecode, [0]) * n


def to_numpy(result):
    """A numpy view (no copy) of a result array (lengths, masks, hashes...)."""
    import numpy as np
    return np.frombuffer(result, dtype={"q": np.int64, "Q": np.uint64,
                                        "B": np.uint8}[result.typecode])
