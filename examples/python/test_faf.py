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
    """faf_string_hash_seed (src/text/faf_string_hash.c), in Python."""
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
    mask = array("B", [rng.randrange(2) for _ in ref])
    kept = batch.filter(mask)
    assert list(kept) == [s for s, m in zip(ref, mask) if m], what
    idx = [rng.randrange(len(ref)) for _ in range(len(ref) // 2)] if ref else []
    assert list(batch.take(idx)) == [ref[i] for i in idx], what
    assert list(kept.lower()) == [ascii_lower(s) for s, m in zip(ref, mask) if m]


def test_sources():
    for _ in range(30):
        data = random_data(rng.randrange(0, 60), rng.choice([4, 40, 300]))
        ref = data.split(b"\n")
        with faf.region() as r:
            check_batch(r.split(data), ref, "bytes")
            check_batch(r.split(bytearray(data)), ref, "bytearray")
            check_batch(r.from_list(ref), ref, "from_list bytes")
            check_batch(r.from_list([s.decode("latin-1") for s in ref]),
                        [s.decode("latin-1").encode() for s in ref], "from_list str")
        with tempfile.NamedTemporaryFile(delete=False) as f:
            f.write(data)
        try:
            mapped = faf.map_file(f.name)
            with faf.region() as r:
                check_batch(r.split(mapped), ref, "mmap")
                check_batch(r.lines(mapped), data.splitlines(), "lines")
            del mapped
        finally:
            os.unlink(f.name)


def test_split_separators():
    with faf.region() as r:
        assert list(r.split(b"a,b,,c,", b",")) == b"a,b,,c,".split(b",")
        assert list(r.split(b"", b",")) == [b""]
        assert list(r.lines(b"")) == []
        try:
            r.split(b"a,b", b",,")
            raise AssertionError("two-byte separator accepted")
        except ValueError:
            pass


def test_errors():
    with faf.region() as r:
        b = r.split(b"a\nb")
        for bad in (2, -3):
            try:
                b[bad]
                raise AssertionError("index out of range accepted")
            except IndexError:
                pass
        for call in (lambda: b.take([0, 2]), lambda: b.filter(array("B", [1]))):
            try:
                call()
                raise AssertionError("bad argument accepted")
            except (IndexError, ValueError):
                pass
    # a batch outlives its region only as a Python object: using it raises
    for use in (lambda: list(b), lambda: b.lower(), lambda: b.contains(b"a")):
        try:
            use()
            raise AssertionError("batch used after its region was released")
        except ValueError:
            pass


def test_big_split():
    # many pieces: across the library's 64-separators-per-scan batches
    lines = [bytes([97 + i % 26]) * (i % 7) for i in range(10000)]
    with faf.region() as r:
        got = r.split(b"\n".join(lines))
        assert got.lengths().tolist() == [len(s) for s in lines]
        assert list(got.take([0, 9999, 5000])) == [lines[0], lines[9999], lines[5000]]


def test_case_modes():
    data = random_data(200, 60) + b"\n"
    ref = data.split(b"\n")
    with faf.region() as r:
        lines = r.split(data)
        assert list(lines.lower()) == [ascii_lower(s) for s in ref]  # one pass
        tail = lines.take(range(10, len(ref)))
        assert list(tail.upper()) == [ascii_upper(s) for s in ref[10:]]
        sparse = lines.take([0, len(ref) // 2, len(ref) - 2])  # packed
        assert list(sparse.lower()) == \
            [ascii_lower(ref[i]) for i in (0, len(ref) // 2, len(ref) - 2)]
        # in place: only the strings' own bytes change
        owned = bytearray(b"Abc,DEF,,gH")
        b = r.split(owned, b",")
        kept = b.take([0, 3])
        assert kept.lower(inplace=True) is kept
        assert bytes(owned) == b"abc,DEF,,gh", bytes(owned)
        assert list(b) == [b"abc", b"DEF", b"", b"gh"], "other views see the change"
        try:
            r.split(b"AB\nCD").lower(inplace=True)
            raise AssertionError("inplace on bytes accepted")
        except TypeError:
            pass


def test_regions():
    # everything made in a region lives until it is released, then its
    # memory is the next region's
    with faf.region() as r:
        low = r.split(b"Hello\nWORLD").lower()
        first = faf.lib.faf_batch_data(low.ptr)
        assert list(low) == [b"hello", b"world"]
    with faf.region() as r:
        again = r.split(b"Hello\nWORLD").upper()
        assert faf.lib.faf_batch_data(again.ptr) == first, "released region not reused"
        with faf.region() as inner:  # a second region at the same time
            other = inner.split(b"x\ny").upper()
            assert faf.lib.faf_batch_data(other.ptr) != first
            assert list(again) == [b"HELLO", b"WORLD"]
    # results can go into another region, released on its own
    with faf.region() as r:
        lines = r.split(b"Ab\ncD")
        with faf.region() as step:
            low = lines.lower(into=step)
            assert list(low) == [b"ab", b"cd"]
        assert list(lines) == [b"Ab", b"cD"]
        try:
            list(low)
            raise AssertionError("result used after its region")
        except ValueError:
            pass
    # a full region raises
    small = faf._Arena(pool_bytes=16 << 20, npools=1)
    r = faf.Region(small)
    try:
        r.split(b"x" * (32 << 20)).lower()
        raise AssertionError("a result bigger than the region fit")
    except MemoryError:
        pass
    finally:
        r.release()
    # regions on several threads at once
    import threading
    errors = []

    def work(k):
        try:
            for _ in range(50):
                with faf.region() as r:
                    b = r.split(b"A%d\nb%d" % (k, k)).lower()
                    assert list(b) == [b"a%d" % k, b"b%d" % k]
        except Exception as e:  # noqa: BLE001
            errors.append(e)
    threads = [threading.Thread(target=work, args=(k,)) for k in range(8)]
    for t in threads:
        t.start()
    for t in threads:
        t.join()
    assert not errors, errors


def test_arrow():
    try:
        import pyarrow as pa
    except ImportError:
        print("  (pyarrow not installed: arrow tests skipped)")
        return
    ref = [b"x", b"", b"hello", b"\xc3\xa9t\xc3\xa9"]
    with faf.region() as r:
        for arr in (pa.array(ref, pa.binary()), pa.array(ref, pa.large_binary()),
                    pa.array([s.decode() for s in ref]), pa.array(ref)[1:]):
            want = [v.as_py() if isinstance(v.as_py(), bytes) else v.as_py().encode()
                    for v in arr]
            check_batch(r.from_arrow(arr), want, str(arr.type))
        kept = r.split(b"one\ntwo\nthree").filter(array("B", [1, 0, 1]))
        out = kept.lower().to_arrow()
        try:
            r.from_arrow(pa.array([b"a", None]))
            raise AssertionError("nulls accepted")
        except ValueError:
            pass
    assert out.to_pylist() == [b"one", b"three"], "arrow copy outlives the region"


def test_numpy():
    try:
        import numpy as np
    except ImportError:
        print("  (numpy not installed: numpy test skipped)")
        return
    with faf.region() as r:
        b = r.split(b"aa\nb\n")
        assert faf.to_numpy(b.lengths()).tolist() == [2, 1, 0]
        assert faf.to_numpy(b.contains(b"a")).dtype == np.uint8
        assert faf.to_numpy(b.hash()).dtype == np.uint64


if __name__ == "__main__":
    tests = [(k, v) for k, v in globals().items() if k.startswith("test_")]
    for name, fn in tests:
        fn()
        print(f"  ok  {name}")
    print(f"{len(tests)} tests passed")
