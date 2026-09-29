"""Tests for the ctypes shim: every Batch method against plain Python on bytes.

ctypes checks nothing, so these are what catch a wrong declaration.
Run: make shared && python3 examples/python/test_faf.py
"""

import os
import random
import sys
import tempfile
from array import array

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import faf  # noqa: E402

rng = random.Random(1234)
ALPHABET = b"abcERROR,;xyz \t\xc3\xa9\xff"  # letters, separators, UTF-8, 0xff
NEEDLES = [b"", b"E", b"ERROR", b"rr", b"ab", b",", b"\xc3\xa9", b"zzzz",
           b"abcERROR,;xyz"]


def random_data(lines, max_len):
    out = []
    for _ in range(lines):
        n = rng.choice([0, 1, 2, rng.randrange(max_len + 1)])
        out.append(bytes(rng.choice(ALPHABET) for _ in range(n)))
    return b"\n".join(out) + rng.choice([b"", b"\n"])


def ascii_lower(b):
    return bytes(c + 32 if 65 <= c <= 90 else c for c in b)


def ascii_upper(b):
    return bytes(c - 32 if 97 <= c <= 122 else c for c in b)


M64 = (1 << 64) - 1


def ref_hash(b, seed):
    """faf_string_hash_seed (src/faf_string_hash.c), in Python."""
    k1, k2 = 0x9E3779B97F4A7C15, 0xC2B2AE3D27D4EB4F

    def fmix(k):
        k ^= k >> 33
        k = k * 0xFF51AFD7ED558CCD & M64
        k ^= k >> 33
        k = k * 0xC4CEB9FE1A85EC53 & M64
        return k ^ (k >> 33)

    h = (seed ^ (len(b) * k1)) & M64
    full = len(b) // 8 * 8
    for i in range(0, full, 8):
        h ^= fmix(int.from_bytes(b[i:i + 8], "little") ^ k2)
        h = (((h << 29) | (h >> 35)) & M64) * k1 & M64
    h ^= fmix(int.from_bytes(b[full:], "little") ^ k2)
    return fmix(h)


# a seed that needs all 64 bits, so a narrower declaration can't pass
SEED = 0xDEADBEEF_CAFEF00D


def check_batch(batch, ref, what):
    """`batch` must behave exactly like the list of bytes `ref`."""
    assert len(batch) == len(ref), what
    assert list(batch) == ref, what
    assert [batch[i] for i in range(len(ref))] == ref, what
    if ref:
        assert batch[-1] == ref[-1], what
    assert batch.lengths().tolist() == [len(s) for s in ref], what
    assert batch.total() == sum(map(len, ref)), what
    for nd in NEEDLES:
        assert batch.find(nd).tolist() == [s.find(nd) for s in ref], (what, nd)
        assert batch.count(nd).tolist() == [s.count(nd) for s in ref], (what, nd)
        assert batch.contains(nd).tolist() == [int(nd in s) for s in ref], (what, nd)
        assert batch.startswith(nd).tolist() == [int(s.startswith(nd)) for s in ref]
        assert batch.endswith(nd).tolist() == [int(s.endswith(nd)) for s in ref]
        assert batch.eq(nd).tolist() == [int(s == nd) for s in ref]
        assert batch.eq_icase(nd).tolist() == \
            [int(ascii_lower(s) == ascii_lower(nd)) for s in ref]
    assert list(batch.lower()) == [ascii_lower(s) for s in ref], what
    assert list(batch.upper()) == [ascii_upper(s) for s in ref], what
    assert list(batch.compact()) == ref, what
    assert bytes(batch.join(b"\r\n")) == b"\r\n".join(ref), what
    assert bytes(batch.join(b"")) == b"".join(ref), what
    assert batch.hash(seed=SEED).tolist() == [ref_hash(s, SEED) for s in ref], what
    assert batch.hash().tolist() == [ref_hash(s, 0) for s in ref], what
    # selection keeps views of the same buffer
    mask = array("B", [rng.randrange(2) for _ in ref])
    kept = batch.filter(mask)
    assert list(kept) == [s for s, m in zip(ref, mask) if m], what
    if ref:  # views of the same bytes, not a copy
        assert faf.lib.faf_batch_data(kept.handle) == faf.lib.faf_batch_data(batch.handle)
    idx = [rng.randrange(len(ref)) for _ in range(len(ref) // 2)] if ref else []
    assert list(batch.take(idx)) == [ref[i] for i in idx], what
    # chained: filter then transform
    assert list(kept.lower()) == [ascii_lower(s) for s, m in zip(ref, mask) if m]


def test_sources():
    for round_ in range(30):
        data = random_data(rng.randrange(0, 60), rng.choice([4, 40, 300]))
        ref = data.split(b"\n")
        ref_lines = data.splitlines() if b"\r" not in data else None
        check_batch(faf.Buffer.from_bytes(data).split(b"\n"), ref, "bytes")
        check_batch(faf.Buffer(bytearray(data)).split(b"\n"), ref, "bytearray")
        with tempfile.NamedTemporaryFile(delete=False) as f:
            f.write(data)
        try:
            buf = faf.Buffer.from_file(f.name)
            check_batch(buf.split(b"\n"), ref, "mmap")
            lines = buf.lines()
            want = ref[:-1] if data.endswith(b"\n") or not data else ref
            check_batch(lines, want, "lines")
            if ref_lines is not None:
                assert list(lines) == ref_lines, "lines vs splitlines"
            del buf, lines
        finally:
            os.unlink(f.name)
        check_batch(faf.Batch.from_list(ref), ref, "from_list bytes")
        check_batch(faf.Batch.from_list([s.decode("latin-1") for s in ref]),
                    [s.decode("latin-1").encode() for s in ref], "from_list str")


def test_split_separators():
    data = b"a,b,,c,"
    assert list(faf.Buffer.from_bytes(data).split(b",")) == data.split(b",")
    assert list(faf.Buffer.from_bytes(b"").split(b",")) == [b""]
    assert list(faf.Buffer.from_bytes(b"").lines()) == []
    try:
        faf.Buffer.from_bytes(data).split(b",,")
        raise AssertionError("two-byte separator accepted")
    except ValueError:
        pass


def test_errors():
    b = faf.Buffer.from_bytes(b"a\nb").split()
    for bad in (2, -3):
        try:
            b[bad]
            raise AssertionError("index out of range accepted")
        except IndexError:
            pass
    try:
        b.take([0, 2])
        raise AssertionError("take out of range accepted")
    except IndexError:
        pass
    try:
        b.filter(array("B", [1]))
        raise AssertionError("short mask accepted")
    except ValueError:
        pass


def test_big_split():
    # many pieces: across the library's 64-separators-per-scan batches
    lines = [bytes([97 + i % 26]) * (i % 7) for i in range(10000)]
    data = b"\n".join(lines)
    got = faf.Buffer.from_bytes(data).split()
    assert len(got) == len(lines) and got.lengths().tolist() == [len(s) for s in lines]
    assert list(got.take([0, 9999, 5000])) == [lines[0], lines[9999], lines[5000]]


def test_case_modes():
    data = random_data(200, 60) + b"\n"
    ref = data.split(b"\n")
    lines = faf.Buffer.from_bytes(data).split()
    assert list(lines.lower()) == [ascii_lower(s) for s in ref]  # one pass
    tail = lines.take(range(10, len(ref)))
    assert list(tail.upper()) == [ascii_upper(s) for s in ref[10:]]
    sparse = lines.take([0, len(ref) // 2, len(ref) - 2])  # packed
    assert list(sparse.lower()) == \
        [ascii_lower(ref[i]) for i in (0, len(ref) // 2, len(ref) - 2)]

    # in place: only the strings' own bytes change
    owned = bytearray(b"Abc,DEF,,gH")
    b = faf.Buffer(owned).split(b",")
    kept = b.take([0, 3])
    assert kept.lower(inplace=True) is kept
    assert bytes(owned) == b"abc,DEF,,gh", bytes(owned)
    assert list(b) == [b"abc", b"DEF", b"", b"gh"], "other views see the change"
    try:
        faf.Buffer.from_bytes(b"AB\nCD").split().lower(inplace=True)
        raise AssertionError("inplace on bytes accepted")
    except TypeError:
        pass
    with tempfile.NamedTemporaryFile(delete=False) as f:
        f.write(b"AB\nCD\n")
    try:
        mapped = faf.Buffer.from_file(f.name).lines()
        try:
            mapped.lower(inplace=True)
            raise AssertionError("inplace on a mapped file accepted")
        except TypeError:
            pass
        del mapped
    finally:
        os.unlink(f.name)


def test_arena():
    saved, faf._arenas = faf._arenas, (faf.Arena(pool_bytes=4096, npools=4),)
    try:
        lines = faf.Buffer.from_bytes(b"Hello\nWORLD\nAbc").split()  # region 1
        r = lines.lower()                                             # region 2
        assert list(r) == [b"hello", b"world", b"abc"]
        addr = faf.lib.faf_batch_data(r.handle)
        del r  # gives the region back ...
        again = lines.upper()
        assert faf.lib.faf_batch_data(again.handle) == addr, "dropped region not reused"
        other, extra = lines.lower(), lines.lower()                  # regions 3, 4
        assert list(again) == [b"HELLO", b"WORLD", b"ABC"]
        try:  # every region in use
            lines.lower()
            raise AssertionError("a fifth batch fit in four regions")
        except MemoryError:
            pass
        del again, other, extra
        try:  # a result bigger than a region
            faf.Buffer.from_bytes(b"A" * 5000).split().lower()
            raise AssertionError("a 5000 byte result fit in 4096")
        except MemoryError:
            pass
        # a filter keeps its parent (whose bytes it views) until it goes: a
        # new batch must not get the parent's region and overwrite them
        r = lines.lower()
        kept = r.filter(array("B", [1, 0, 1]))
        del r
        overwrite = lines.upper()
        assert list(kept) == [b"hello", b"abc"], list(kept)
        del kept, overwrite
        # to_arrow copies out of the arena
        try:
            import pyarrow  # noqa: F401
            arr = lines.lower().to_arrow()
            lines.upper(), lines.upper()  # reuse the regions
            assert arr.to_pylist() == [b"hello", b"world", b"abc"]
        except ImportError:
            pass
        # a batch dropped on another thread gives its region back
        import threading
        holder = [lines.lower()]
        addr = faf.lib.faf_batch_data(holder[0].handle)
        t = threading.Thread(target=holder.clear)
        t.start()
        t.join()
        again = lines.lower()  # held: a temporary would be collected at once
        assert faf.lib.faf_batch_data(again.handle) == addr
    finally:
        faf._arenas = saved


def test_two_arenas():
    # small results in the first arena, a big one in the second
    small, big = faf.Arena(4096, 3), faf.Arena(64 << 10, 1)
    saved, faf._arenas = faf._arenas, (small, big)
    try:
        lines = faf.Buffer.from_bytes(b"a\nb").split()
        wide = faf.Buffer.from_bytes(b"x" * 20000).split()
        in_small = lambda b: faf.lib.faf_batch_data(b.handle) - faf._address(small._mem)
        low = lines.upper()
        assert 0 <= in_small(low) < len(small._mem), "small result not in the small arena"
        w = wide.upper()   # 20000 bytes: only the big arena holds it
        assert list(w) == [b"X" * 20000]
        assert not 0 <= in_small(w) < len(small._mem)
        try:  # small arena full (lines, wide, low), big one's region taken
            wide.lower()
            raise AssertionError("a second big result fit")
        except MemoryError:
            pass
    finally:
        faf._arenas = saved


def test_arrow():
    try:
        import pyarrow as pa
    except ImportError:
        print("  (pyarrow not installed: arrow tests skipped)")
        return
    ref = [b"x", b"", b"hello", b"\xc3\xa9t\xc3\xa9"]
    for arr in (pa.array(ref, pa.binary()), pa.array(ref, pa.large_binary()),
                pa.array([s.decode() for s in ref]), pa.array(ref)[1:]):
        want = [bytes(v.as_py() if isinstance(v.as_py(), bytes) else v.as_py().encode())
                for v in arr]
        check_batch(faf.Batch.from_arrow(arr), want, str(arr.type))
    b = faf.Buffer.from_bytes(b"one\ntwo\nthree").split()
    kept = b.filter(array("B", [1, 0, 1]))
    assert kept.to_arrow().to_pylist() == [b"one", b"three"]
    compact = b.compact()
    assert compact.to_arrow().to_pylist() == [b"one", b"two", b"three"]
    assert faf.Batch.from_arrow(compact.to_arrow()).head() == [b"one", b"two", b"three"]
    try:
        faf.Batch.from_arrow(pa.array([b"a", None]))
        raise AssertionError("nulls accepted")
    except ValueError:
        pass


def test_numpy():
    try:
        import numpy as np
    except ImportError:
        print("  (numpy not installed: numpy test skipped)")
        return
    b = faf.Buffer.from_bytes(b"aa\nb\n").split()
    assert faf.to_numpy(b.lengths()).tolist() == [2, 1, 0]
    assert faf.to_numpy(b.contains(b"a")).dtype == np.uint8
    assert faf.to_numpy(b.hash()).dtype == np.uint64


if __name__ == "__main__":
    tests = [(k, v) for k, v in globals().items() if k.startswith("test_")]
    for name, fn in tests:
        fn()
        print(f"  ok  {name}")
    print(f"{len(tests)} tests passed")
