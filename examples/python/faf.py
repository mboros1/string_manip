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
import threading
from array import array
from ctypes import c_char, c_int, c_int64, c_size_t, c_uint64, c_void_p

__all__ = ["Arena", "Buffer", "Batch", "lib", "use_arena"]


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
        "faf_batch_span": (c_int64, [P, P, c_size_t, P]),
        "faf_batch_ascii_case_span": (None, [P, P, P, c_size_t, c_int, P, P, P]),
        "faf_batch_ascii_case_inplace": (None, [P, P, P, c_size_t, c_int]),
        "faf_batch_ascii_case_range": (None, [P, c_int64, c_int64, c_int, P]),
    }
    for name in ("contains", "starts_with", "ends_with", "eq", "eq_icase"):
        sig[f"faf_batch_{name}"] = (c_size_t, [P, P, P, c_size_t, P, c_size_t, P])
    if hasattr(lib, "faf_ffi_arena_size"):  # built with arenas (the default)
        sig.update({
            "faf_ffi_arena_size": (c_size_t, []),
            "faf_ffi_arena_bytes": (c_size_t, [c_size_t, c_size_t]),
            "faf_ffi_arena_init": (ctypes.c_bool, [P, P, c_size_t, c_size_t]),
            "faf_ffi_region_acquire": (c_uint64, [P]),
            "faf_ffi_region_capacity": (c_size_t, [P]),
            "faf_ffi_reserve": (P, [P, c_uint64, c_size_t]),
            "faf_ffi_region_release": (None, [P, c_uint64]),
        })
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


# ---- Result memory from a faf arena ----

class Arena:
    """faf arena for results: `npools` regions of `pool_bytes`, over an
    anonymous mapping (the OS maps pages only when they're first written, so
    an unused arena costs address space, not memory). Each result that fits
    takes a region and gives it back when it is garbage collected, so later
    results reuse memory that is already mapped instead of faulting in new
    pages. Pages stay mapped while the arena lives, as in any memory pool."""

    def __init__(self, pool_bytes=256 << 20, npools=8):
        nbytes = lib.faf_ffi_arena_bytes(npools, pool_bytes)
        if not nbytes:
            raise ValueError("arena too large")
        self._mem = mmap.mmap(-1, nbytes)
        self._state = ctypes.create_string_buffer(lib.faf_ffi_arena_size())
        self._base = _address(self._mem)
        if not lib.faf_ffi_arena_init(self._state, self._base, nbytes, npools):
            raise ValueError("arena too small")
        self.capacity = lib.faf_ffi_region_capacity(self._state)
        # results can be dropped (and give back their region) on any thread
        self._lock = threading.Lock()

    def take(self, size):
        """(writable memoryview of `size` bytes, lease), or None if it
        doesn't fit or every region is in use. Keep the lease with the
        memory: dropping it gives the region back."""
        if size > self.capacity:
            return None
        with self._lock:
            h = lib.faf_ffi_region_acquire(self._state)
            if not h:
                return None
            p = lib.faf_ffi_reserve(self._state, h, size)
            if not p:
                lib.faf_ffi_region_release(self._state, h)
                return None
        off = p - self._base
        return memoryview(self._mem)[off:off + size], _Lease(self, h)

    def _release(self, h):
        with self._lock:
            lib.faf_ffi_region_release(self._state, h)


class _Lease:
    __slots__ = ("arena", "handle")

    def __init__(self, arena, handle):
        self.arena, self.handle = arena, handle

    def __del__(self):
        self.arena._release(self.handle)


_arena = None
_arena_on = hasattr(lib, "faf_ffi_arena_size")


def use_arena(arena):
    """Results from `arena` (an Arena), or from new bytearrays (None)."""
    global _arena, _arena_on
    _arena, _arena_on = arena, arena is not None


def _default_arena():
    global _arena
    if _arena is None and _arena_on:
        _arena = Arena()
    return _arena


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

    def __init__(self, obj, lease=None):
        self.obj = obj
        self.lease = lease  # arena memory: given back when this goes away
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
        return Batch(self, _Ints(starts, 0, n), _Ints(ends, 0, n), dense=True)

    def lines(self):
        """Like split(b"\\n"), without the empty piece after a final newline
        (so an empty buffer has no lines, like bytes.splitlines)."""
        b = self.split(b"\n")
        if not self.nbytes or self.view[-1] == ord("\n"):
            b = b._first(len(b) - 1)
        return b


class Batch:
    """n views into one Buffer: string i is data[starts[i]:ends[i]].

    `dense` batches have their views in order with at most a separator
    between them (made by split, from offsets, or from another dense batch
    keeping its views): the range they cover is known without scanning."""

    def __init__(self, buffer, starts, ends, dense=False):
        self.buffer, self.starts, self.ends = buffer, starts, ends
        self.n = len(starts.view)
        self.dense = dense

    # -- constructors --

    @classmethod
    def _from_offsets(cls, buffer, offsets, first, n):
        # Arrow layout: string i is data[off[i]:off[i + 1]]
        return cls(buffer, _Ints(offsets, first, n), _Ints(offsets, first + 1, n),
                   dense=True)

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
                     _Ints(self.ends.owner, self._pos(self.ends), m), self.dense)

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
    # Each takes `out`: a bytearray to write into instead of a new one (it
    # must be big enough; a batch returned earlier that used it sees the new
    # bytes). Reusing one saves allocating, zero filling and first touching
    # fresh memory on every call, which costs more than the conversion.

    @staticmethod
    def _output(out, size):
        """A Buffer of at least `size` writable bytes: `out`, else a region
        of the arena, else a new bytearray."""
        if out is not None:
            if not isinstance(out, bytearray):
                raise TypeError("out must be a bytearray")
            if len(out) < size:
                raise ValueError(f"out is {len(out)} bytes, {size} needed")
            return Buffer(out)
        arena = _default_arena()
        got = arena.take(size) if arena and size else None
        if got:
            return Buffer(got[0], lease=got[1])
        return Buffer(bytearray(size))

    def _into_new(self, fill, out):
        total = self.total()
        dst, offsets = self._output(out, total), _zeros("q", self.n + 1)
        fill(dst.addr, _address(offsets))
        return Batch._from_offsets(dst, offsets, 0, self.n)

    def compact(self, out=None):
        """A copy with the strings end to end (Arrow layout)."""
        return self._into_new(
            lambda d, o: lib.faf_batch_compact(*self._args(), d, o), out)

    def _case(self, upper, out, inplace):
        if inplace:
            # only for memory the caller owns and may change: bytes are
            # immutable, and a mapped file would copy each page on write
            if not isinstance(self.buffer.obj, bytearray):
                raise TypeError("inplace needs a batch over a bytearray")
            lib.faf_batch_ascii_case_inplace(*self._args(), upper)
            return self
        if self.dense and self.n and self.starts.view[0] == 0:
            # the range is known, [0, last end): no scan of the views, one
            # kernel call, and the same views into the result
            span = self.ends.view[self.n - 1]
            dst = self._output(out, span)
            lib.faf_batch_ascii_case_range(self.buffer.addr, 0, span, upper, dst.addr)
            return Batch(dst, self.starts, self.ends, dense=True)
        lo = c_int64()
        span = lib.faf_batch_span(self.starts.addr, self.ends.addr, self.n,
                                  ctypes.byref(lo))
        if span > 2 * self.total():
            # sparse views (e.g. after a selective filter): convert and pack
            # just the strings
            return self._into_new(lambda d, o: lib.faf_batch_ascii_case(
                *self._args(), upper, d, o), out)
        # dense views (e.g. the lines of a file): one pass over the range they
        # cover, and the same views into the result
        dst = self._output(out, span)
        if lo.value == 0:
            lib.faf_batch_ascii_case_span(*self._args(), upper, dst.addr, None, None)
            return Batch(dst, self.starts, self.ends, self.dense)
        starts, ends = _zeros("q", self.n), _zeros("q", self.n)
        lib.faf_batch_ascii_case_span(*self._args(), upper, dst.addr,
                                      _address(starts), _address(ends))
        return Batch(dst, _Ints(starts, 0, self.n), _Ints(ends, 0, self.n))

    def lower(self, out=None, inplace=False):
        """ASCII lower case. inplace=True changes the strings' own bytes
        (only for a batch over a bytearray; other batches over the same
        buffer see the change) and returns this batch."""
        return self._case(0, out, inplace)

    def upper(self, out=None, inplace=False):
        return self._case(1, out, inplace)

    def join(self, sep=b"\n", out=None):
        """The strings joined by sep, as a bytearray (e.g. to write out). With
        `out`, only its first len(result) bytes are written: the return value
        is a memoryview of them."""
        sep, m = _needle(sep)
        size = self.total() + m * max(self.n - 1, 0)
        # the result is the caller's to keep: a bytearray, not arena memory
        dst = bytearray(size) if out is None else self._output(out, size).obj
        lib.faf_batch_join(*self._args(), sep, m, _address(dst) if size else _EMPTY_ADDR)
        return dst if out is None else memoryview(dst)[:size]

    # -- conversions (optional dependencies) --

    def to_arrow(self):
        """A pyarrow large_binary array (copies unless already compact)."""
        import pyarrow as pa
        # arena memory is reused once this batch is gone, and pyarrow could
        # keep it longer: copy into a bytearray the array owns
        c = self if self._is_compact() else \
            self.compact(out=bytearray(self.total()))
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
