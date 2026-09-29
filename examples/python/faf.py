"""FaF strings from Python: a ctypes shim over the batch API (src/faf_batch.h).

The shim is the list of declarations in _declare(); the rest ties batches to
Python objects and garbage collection. It needs only the standard library
(ctypes, array, mmap); numpy and pyarrow are used for conversions when
installed.

    import faf
    buf = faf.Buffer.from_file("app.log")      # mmap, nothing is copied
    lines = buf.lines()                         # a batch: views, in C
    errors = lines.filter(lines.contains(b"ERROR"))
    print(len(errors), errors.head(3))
    out = errors.lower().join(b"\\n")           # bytes for Python are made here

Strings are bytes, and case functions are ASCII only, as in the C library.
A Batch is a handle to a batch in a faf arena. Results that make new
strings (lower, upper, compact) live in the arena too; each batch gives its
region back when Python collects it.
"""

import ctypes
import itertools
import mmap
import os
import sys
import threading
from array import array
from ctypes import c_char, c_int, c_int64, c_size_t, c_uint64, c_void_p

__all__ = ["Arena", "Buffer", "Batch", "lib", "to_numpy"]


# ---- The shim: load the library and declare the functions ----

def _find_library():
    if os.environ.get("FAF_LIB"):
        return os.environ["FAF_LIB"]
    ext = "dylib" if sys.platform == "darwin" else "so"
    here = os.path.dirname(os.path.abspath(__file__))
    return os.path.join(here, "..", "..", "obj", f"libfaf.{ext}")


def _declare(lib):
    P, B = c_void_p, c_uint64  # pointers; batch handles
    sig = {
        "faf_arena_size": (c_size_t, []),
        "faf_arena_bytes": (c_size_t, [c_size_t, c_size_t]),
        "faf_arena_init": (ctypes.c_bool, [P, P, c_size_t, c_size_t]),
        "faf_arena_fini": (None, [P]),
        "faf_batch_split": (B, [P, P, c_size_t, c_char]),
        "faf_batch_from_offsets": (B, [P, P, P, c_size_t]),
        "faf_batch_free": (None, [B]),
        "faf_batch_len": (c_size_t, [B]),
        "faf_batch_data": (P, [B]),
        "faf_batch_starts": (P, [B]),
        "faf_batch_ends": (P, [B]),
        "faf_batch_total": (c_int64, [B]),
        "faf_batch_lengths": (None, [B, P]),
        "faf_batch_find": (None, [B, P, c_size_t, P]),
        "faf_batch_count": (None, [B, P, c_size_t, P]),
        "faf_batch_hash": (None, [B, c_uint64, P]),
        "faf_batch_select": (B, [P, B, P]),
        "faf_batch_take": (B, [P, B, P, c_size_t]),
        "faf_batch_ascii_case": (B, [P, B, c_int]),
        "faf_batch_compact": (B, [P, B]),
        "faf_batch_ascii_case_inplace": (None, [B, c_int]),
        "faf_batch_join": (c_int64, [B, P, c_size_t, P]),
    }
    for name in ("contains", "starts_with", "ends_with", "eq", "eq_icase"):
        sig[f"faf_batch_{name}"] = (c_size_t, [B, P, c_size_t, P])
    for name, (restype, argtypes) in sig.items():
        fn = getattr(lib, name)
        fn.restype, fn.argtypes = restype, argtypes
    return lib


# CDLL releases the GIL for the duration of every call.
lib = _declare(ctypes.CDLL(_find_library()))


# ---- Memory ----

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


class Arena:
    """A faf arena over one anonymous mapping: `npools` regions of up to
    `pool_bytes` each (every live batch holds one). The OS maps pages only
    when they're first written, so large pools cost address space, not
    memory; pages a result touched stay mapped for the next one. If the OS
    won't reserve that much, pools are halved until it does."""

    def __init__(self, pool_bytes, npools):
        while True:
            nbytes = lib.faf_arena_bytes(npools, pool_bytes)
            try:
                self._mem = mmap.mmap(-1, nbytes)
                break
            except (OSError, OverflowError):
                if pool_bytes <= 1 << 20:
                    raise
                pool_bytes //= 2
        self.pool_bytes = pool_bytes
        self._state = ctypes.create_string_buffer(lib.faf_arena_size())
        if not lib.faf_arena_init(self._state, _address(self._mem), nbytes, npools):
            raise MemoryError("faf arena: no free arena table entry")

    def __del__(self):
        # batches keep their arena, so none is left when this runs
        if getattr(self, "_state", None) is not None:
            lib.faf_arena_fini(self._state)


# Batches are made and freed (by garbage collection) on any thread; one lock
# covers the arenas' bookkeeping.
_lock = threading.Lock()
_arenas = None


def arenas():
    """The arenas batches come from, tried in order (made on first use):
    many small regions for the common case, a few large ones for big
    results."""
    global _arenas
    if _arenas is None:
        _arenas = (Arena(1 << 20, 4096), Arena(2 << 30, 32))
    return _arenas


def _make(fn, *args):
    """A new batch (handle, arena) from the first arena with a free region it
    fits in."""
    tried = arenas()
    for a in tried:
        with _lock:
            h = fn(a._state, *args)
        if h:
            return h, a
    raise MemoryError("faf: no free region, or the result is larger than one "
                      f"({tried[-1].pool_bytes} bytes)")


def _free(h):
    with _lock:
        lib.faf_batch_free(h)


def _needle(b):
    if isinstance(b, str):
        b = b.encode()
    return b, len(b)


def _zeros(typecode, n):
    return array(typecode, [0]) * n


# ---- Buffer and Batch ----

class Buffer:
    """Bytes to take views of: a file (mapped), bytes, bytearray, ..."""

    def __init__(self, obj):
        self.obj = obj
        self.nbytes = len(obj)
        self.addr = _address(obj) if self.nbytes else None

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

    def split(self, sep=b"\n", _len=None):
        """Views of the pieces between `sep` bytes, like bytes.split(sep)."""
        sep = sep if isinstance(sep, bytes) else sep.encode()
        if len(sep) != 1:
            raise ValueError("split separator must be one byte")
        n = self.nbytes if _len is None else _len
        return Batch(*_make(lib.faf_batch_split, self.addr, n, sep), keep=self)

    def lines(self):
        """Like split(b"\\n"), without the empty piece after a final newline
        (so an empty buffer has no lines, like bytes.splitlines)."""
        if not self.nbytes:
            return Batch.from_list([])
        ends_nl = self.obj[self.nbytes - 1] in (10, b"\n")
        return self.split(b"\n", self.nbytes - 1 if ends_nl else None)


class Batch:
    """A batch in a faf arena: n views into one byte buffer. `keep` holds the
    Python objects its views point into, so they outlive it."""

    def __init__(self, handle, arena, keep=None):
        # the arena too: its memory holds this batch
        self.handle, self.arena, self.keep = handle, arena, keep
        self.n = lib.faf_batch_len(handle)

    def __del__(self):
        if getattr(self, "handle", 0):
            try:
                _free(self.handle)
            except Exception:  # interpreter shutdown: the library may be gone
                pass
            self.handle = 0

    # -- constructors --

    @classmethod
    def from_offsets(cls, data, offsets, n, first=0):
        """Arrow layout: string i is data[offsets[first + i] : ...[first + i + 1]].
        Nothing is copied; the batch keeps `data` and `offsets` alive."""
        d = _address(data) if len(data) else None
        o = _address(offsets) + 8 * first
        return cls(*_make(lib.faf_batch_from_offsets, d, o, n), keep=(data, offsets))

    @classmethod
    def from_list(cls, items):
        """From a list of str (UTF-8 encoded) or bytes: this copies."""
        bs = [s.encode() if isinstance(s, str) else s for s in items]
        offsets = array("q", itertools.accumulate(map(len, bs), initial=0))
        return cls.from_offsets(b"".join(bs), offsets, len(bs))

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
        _, off, data = arr.buffers()
        data = data if data is not None and data.size else b""
        # the batch keeps both buffers, which keep their memory alive
        return cls.from_offsets(data, off, len(arr), first=arr.offset)

    # -- reading --

    def __len__(self):
        return self.n

    def _views(self):
        n = self.n
        s = (c_int64 * n).from_address(lib.faf_batch_starts(self.handle)) if n else ()
        e = (c_int64 * n).from_address(lib.faf_batch_ends(self.handle)) if n else ()
        return lib.faf_batch_data(self.handle), s, e

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
        more = ", ..." if self.n > 3 else ""
        return f"<faf.Batch of {self.n}: {self.head(3)!r}{more}>"

    # -- per string results --

    def _out(self, typecode, fn, *args):
        out = _zeros(typecode, self.n)
        fn(self.handle, *args, _address(out) if self.n else None)
        return out

    def lengths(self):
        return self._out("q", lib.faf_batch_lengths)

    def total(self):
        """Sum of the lengths."""
        return lib.faf_batch_total(self.handle)

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

    # -- new batches --
    # select/take view the same bytes, so the result keeps this batch alive;
    # lower/upper/compact own their bytes.

    def _new(self, fn, *args, keep=None):
        return Batch(*_make(fn, self.handle, *args), keep=keep)

    def filter(self, mask):
        """The strings where mask is non-zero (mask: array('B'), bytes, ...)."""
        if len(mask) != self.n:
            raise ValueError("mask length differs from the batch")
        return self._new(lib.faf_batch_select, _address(mask) if self.n else None,
                         keep=self)

    def take(self, indices):
        """The strings at `indices` (array('q'), or any iterable of ints)."""
        idx = indices if isinstance(indices, array) and indices.typecode == "q" \
            else array("q", indices)
        if idx and (min(idx) < 0 or max(idx) >= self.n):
            raise IndexError("take: index out of range")
        return self._new(lib.faf_batch_take, _address(idx) if idx else None,
                         len(idx), keep=self)

    def lower(self, inplace=False):
        """ASCII lower case. inplace=True changes the strings' own bytes (a
        batch over a bytearray only; other batches over it see the change)."""
        return self._case(0, inplace)

    def upper(self, inplace=False):
        return self._case(1, inplace)

    def _case(self, upper, inplace):
        if inplace:
            owner = self._owner()
            if not isinstance(owner, bytearray):
                raise TypeError("inplace needs a batch over a bytearray")
            lib.faf_batch_ascii_case_inplace(self.handle, upper)
            return self
        return self._new(lib.faf_batch_ascii_case, upper)

    def _owner(self):
        # the Python object this batch's bytes are in, if any
        k = self.keep
        while isinstance(k, Batch):
            k = k.keep
        return k.obj if isinstance(k, Buffer) else None

    def compact(self):
        """A copy with the strings end to end (Arrow layout)."""
        return self._new(lib.faf_batch_compact)

    def join(self, sep=b"\n"):
        """The strings joined by sep, as a bytearray (e.g. to write out)."""
        sep, m = _needle(sep)
        dst = bytearray(self.total() + m * max(self.n - 1, 0))
        lib.faf_batch_join(self.handle, sep, m, _address(dst) if dst else None)
        return dst

    # -- conversions (optional dependencies) --

    def to_arrow(self):
        """A pyarrow large_binary array, copied out of the arena (pyarrow may
        keep it longer than this batch lives)."""
        import pyarrow as pa
        c = self.compact()
        d, s, e = c._views()
        offsets = array("q", s) + array("q", [e[-1] if c.n else 0])
        data = ctypes.string_at(d, c.total()) if c.total() else b""
        return pa.Array.from_buffers(pa.large_binary(), c.n,
                                     [None, pa.py_buffer(offsets), pa.py_buffer(data)])


def to_numpy(result):
    """A numpy view (no copy) of a result array (lengths, masks, hashes...)."""
    import numpy as np
    return np.frombuffer(result, dtype={"q": np.int64, "Q": np.uint64,
                                        "B": np.uint8}[result.typecode])
