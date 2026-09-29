"""Benchmarks for the Python example: faf vs plain Python vs pyarrow.

Each section answers one question:
  1. ops        Is a batch operation fast? Per op, over line lengths and
                match rates.
  2. pipeline   Does staying in views pay off? A whole pipeline, time and
                peak memory (each variant in its own process).
  3. crossover  When is a batch too small to be worth a call into C?
  4. ingest     What does getting Python objects into a batch cost?

Plain Python works on bytes, with the same ASCII semantics as faf
(bytes.lower, `in`, find, count). pyarrow is used when installed. Every
variant's results are checked against the others before its time counts.

    make shared && python3 examples/python/bench.py [--quick] [sections...]
"""

import argparse
import gc
import json
import os
import platform
import random
import resource
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import faf  # noqa: E402

try:
    import pyarrow as pa
    import pyarrow.compute as pc
except ImportError:
    pa = pc = None

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..", "..")


# ---- timing and output ----

def best(fn, runs):
    """Best time of `runs` calls, and the last result."""
    t, out = float("inf"), None
    for _ in range(runs):
        gc.collect()
        t0 = time.perf_counter()
        out = fn()
        t = min(t, time.perf_counter() - t0)
    return t, out


def fmt_ns(ns):
    return f"{ns:,.1f}" if ns < 100 else f"{ns:,.0f}"


def table(title, header, rows):
    print(f"\n{title}")
    widths = [max(len(str(r[i])) for r in [header] + rows) for i in range(len(header))]
    line = lambda r: "  " + "  ".join(  # noqa: E731
        str(c).ljust(w) if i == 0 else str(c).rjust(w)
        for i, (c, w) in enumerate(zip(r, widths)))
    print(line(header))
    print("  " + "  ".join("-" * w for w in widths))
    for r in rows:
        print(line(r))


def ratio(t, ref):
    return f"{t / ref:.2f}x" if ref else ""


def agree(name, *results):
    """All non-None results equal (as lists), or the benchmark is wrong."""
    rs = [list(r) for r in results if r is not None]
    for r in rs[1:]:
        if r != rs[0]:
            raise AssertionError(f"{name}: variants disagree")


# ---- data ----

LEVELS = [b"INFO", b"DEBUG", b"WARN"]
WORDS = [b"request", b"cache", b"user", b"session", b"db", b"retry", b"GET",
         b"POST", b"timeout", b"ok", b"payload", b"queue"]


def log_lines(n, length, error_rate, rng):
    """n log-like lines of about `length` bytes; `error_rate` of them ERROR."""
    out = []
    for i in range(n):
        level = b"ERROR" if rng.random() < error_rate else rng.choice(LEVELS)
        head = b"2026-09-29T12:%02d:%02d.%03dZ %s svc-%d: " % (
            i // 3600 % 60, i // 60 % 60, i % 1000, level, i % 17)
        body = []
        size = len(head)
        while size < length:
            w = rng.choice(WORDS) if rng.random() < 0.7 else b"id=%x" % rng.getrandbits(32)
            body.append(w)
            size += len(w) + 1
        out.append((head + b" ".join(body))[:max(length, 1)])
    return out


def short_keys(n, rng):
    """IDs of 8-24 bytes, e.g. user:1a2b3c."""
    pre = [b"user:", b"order:", b"sku-", b"k"]
    return [rng.choice(pre) + b"%x" % rng.getrandbits(rng.randrange(12, 64))
            for _ in range(n)]


def utf8_lines(n, length, rng):
    """Text with plenty of multi-byte UTF-8: accents, CJK, emoji."""
    pieces = ["café", "naïve", "Straße", "日本語", "текст", "ERROR", "données",
              "😀", "ok", "Ωmega", "résumé", "ñandú"]
    out = []
    for _ in range(n):
        s = ""
        while len(s.encode()) < length:
            s += rng.choice(pieces) + " "
        out.append(s.encode()[:length].decode("utf-8", "ignore").encode())
    return out


# ---- the three implementations of each op ----

def faf_ops():
    return {
        "contains": lambda b, nd: b.contains(nd),
        "find": lambda b, nd: b.find(nd),
        "count ','": lambda b, nd: b.count(b","),
        "startswith": lambda b, nd: b.startswith(b"2026-09-29T12:0"),
        "lengths": lambda b, nd: b.lengths(),
        "lower": lambda b, nd: b.lower(),
        # a result buffer allocated once and reused (see run_ops)
        "lower (reused out)": lambda b, nd: b.lower(out=b._bench_out),
        "hash": lambda b, nd: b.hash(),
        "filter contains": lambda b, nd: b.filter(b.contains(nd)),
    }


def py_ops():
    return {
        "contains": lambda ls, nd: [nd in s for s in ls],
        "find": lambda ls, nd: [s.find(nd) for s in ls],
        "count ','": lambda ls, nd: [s.count(b",") for s in ls],
        "startswith": lambda ls, nd: [s.startswith(b"2026-09-29T12:0") for s in ls],
        "lengths": lambda ls, nd: [len(s) for s in ls],
        "lower": lambda ls, nd: [s.lower() for s in ls],
        "lower (reused out)": lambda ls, nd: [s.lower() for s in ls],
        # bytes objects cache their hash: see run_ops, which hashes fresh copies
        "hash": lambda ls, nd: [hash(s) for s in ls],
        "filter contains": lambda ls, nd: [s for s in ls if nd in s],
    }


def pa_ops():
    if pa is None:
        return {}
    return {
        "contains": lambda a, nd: pc.match_substring(a, nd.decode()),
        "find": lambda a, nd: pc.find_substring(a, nd.decode()),
        "count ','": lambda a, nd: pc.count_substring(a, ","),
        "startswith": lambda a, nd: pc.starts_with(a, "2026-09-29T12:0"),
        "lengths": lambda a, nd: pc.binary_length(a),
        "lower": lambda a, nd: pc.ascii_lower(a),
        "lower (reused out)": lambda a, nd: pc.ascii_lower(a),
        # pyarrow has no per-string hash kernel
        "filter contains": lambda a, nd: a.filter(pc.match_substring(a, nd.decode())),
    }


def comparable(op, impl, out):
    """Results as plain lists, to check the variants agree."""
    if out is None:
        return None
    if op in ("lower", "lower (reused out)", "filter contains"):
        if impl == "pa":
            return [v.encode() if isinstance(v, str) else v for v in out.to_pylist()]
        return list(out)
    if op == "hash":
        return None  # different hash functions
    if impl == "pa":
        return [int(v) for v in out.to_pylist()]
    return [int(v) for v in out]


# ---- 1. ops ----

def section_ops(args):
    print("\n== 1. Is a batch op fast? ==========================================")
    total = args.bytes
    rng = random.Random(1)
    rows_by_len = []
    for length in args.lengths:
        n = max(total // (length + 1), 10)
        lines = log_lines(n, length, 0.01, rng)
        run_ops(f"lines of {length} B ({n:,} lines, ~1% contain ERROR)", lines,
                b"ERROR", args, rows_by_len, length)
    # selectivity at a fixed length
    n = max(total // 129, 10)
    for rate in (0.01, 0.5):
        lines = log_lines(n, 128, rate, rng)
        run_ops(f"128 B lines, {rate:.0%} contain ERROR ({n:,} lines)", lines,
                b"ERROR", args, None, 128, ops=["contains", "find", "filter contains"])
    keys = short_keys(max(total // 16, 10), rng)
    run_ops(f"short keys, 8-24 B ({len(keys):,})", keys, b"order:", args, None, 16,
            ops=["contains", "startswith", "lengths", "hash", "lower"])
    utf = utf8_lines(max(total // 129, 10), 128, rng)
    run_ops(f"UTF-8 heavy text, 128 B ({len(utf):,})", utf, "données".encode(),
            args, None, 128, ops=["contains", "find", "count ','", "lower"])
    cold_lower(args)
    if rows_by_len:
        table("Summary: faf vs plain Python / pyarrow by line length (ns per string)",
              ["op"] + [f"{ln} B" for ln in args.lengths],
              summarize(rows_by_len, args.lengths))


def run_ops(title, lines, needle, args, collect, length, ops=None):
    data = b"\n".join(lines)
    batch = faf.Buffer.from_bytes(data).split(b"\n")
    batch._bench_out = bytearray(len(data))
    arr = pa.array(lines, pa.large_string()) if pa else None
    n = len(lines)
    fo, po, ao = faf_ops(), py_ops(), pa_ops()
    rows = []
    for op in ops or list(fo):
        tf, rf = best(lambda: fo[op](batch, needle), args.runs)
        if op == "hash":
            # a fresh copy of the list for each run, or Python only looks up
            # the hash it cached on the first run
            copies = [[bytes(bytearray(s)) for s in lines] for _ in range(args.py_runs)]
            tp, rp = best(lambda: po[op](copies.pop(), needle), args.py_runs)
        else:
            tp, rp = best(lambda: po[op](lines, needle), args.py_runs)
        ta, ra = best(lambda: ao[op](arr, needle), args.runs) if op in ao else (None, None)
        agree(f"{title} / {op}", comparable(op, "faf", rf), comparable(op, "py", rp),
              comparable(op, "pa", ra))
        ns = lambda t: t * 1e9 / n  # noqa: E731
        rows.append([op, fmt_ns(ns(tf)), fmt_ns(ns(tp)),
                     fmt_ns(ns(ta)) if ta else "n/a",
                     ratio(tp, tf), ratio(ta, tf) if ta else ""])
        if collect is not None:
            collect.append((length, op, ns(tf), ns(tp), ns(ta) if ta else None))
    # splitting the input, for reference
    ts, _ = best(lambda: faf.Buffer.from_bytes(data).split(b"\n"), args.runs)
    tps, _ = best(lambda: data.split(b"\n"), args.py_runs)
    tas, _ = best(lambda: pc.split_pattern(pa.array([data], pa.large_binary()), "\n"),
                  args.runs) if pa else (None, None)
    rows.append(["split into lines", fmt_ns(ts * 1e9 / n), fmt_ns(tps * 1e9 / n),
                 fmt_ns(tas * 1e9 / n) if tas else "n/a", ratio(tps, ts),
                 ratio(tas, ts) if tas else ""])
    table(title + " -- ns per string",
          ["op", "faf", "python", "pyarrow", "python/faf", "pyarrow/faf"], rows)


def summarize(collected, lengths):
    by_op = {}
    for length, op, tf, tp, ta in collected:
        by_op.setdefault(op, {})[length] = (tf, tp, ta)
    rows = []
    for op, per in by_op.items():
        cells = []
        for ln in lengths:
            tf, tp, ta = per[ln]
            cells.append(f"{fmt_ns(tf)} ({tp / tf:.1f}x / " +
                         (f"{ta / tf:.1f}x)" if ta else "n/a)"))
        rows.append([op] + cells)
    return rows


def cold_lower(args):
    """lower once, in a fresh process: no warm memory for anyone (faf's
    bytearray or pyarrow's pool), as in a script that runs once."""
    rows = []
    for length in (8, 128, 4096):
        cells = []
        for impl in ("faf", "pyarrow") if pa else ("faf",):
            out = subprocess.run([sys.executable, __file__, "--worker", f"cold-lower-{impl}",
                                  str(length), str(args.bytes)],
                                 capture_output=True, text=True, check=True)
            cells.append(json.loads(out.stdout.strip().splitlines()[-1])["ns"])
        rows.append([f"{length} B"] + [fmt_ns(c) for c in cells] +
                    ([ratio(cells[1], cells[0])] if len(cells) > 1 else []))
    table("lower, first call in a fresh process (ns per string)",
          ["lines", "faf", "pyarrow", "pyarrow/faf"][:len(rows[0])], rows)


def cold_lower_worker(impl, length, total):
    n = max(total // (length + 1), 10)
    lines = log_lines(n, length, 0.01, random.Random(5))
    if impl == "faf":
        batch = faf.Buffer.from_bytes(b"\n".join(lines)).split(b"\n")
        fn = batch.lower
    else:
        arr = pa.array(lines, pa.large_string())
        fn = lambda: pc.ascii_lower(arr)  # noqa: E731
        pc.ascii_lower(pa.array(["A"]))  # start the compute engine first
    del lines
    gc.collect()
    t0 = time.perf_counter()
    fn()
    print(json.dumps({"ns": (time.perf_counter() - t0) * 1e9 / n}))


# ---- 2. pipeline (time and peak memory) ----

NEEDLE = b"ERROR"


def pipeline(impl, path, out_path):
    """Keep lines containing ERROR, lower-cased, written to out_path.
    Returns the number of lines written."""
    if impl == "faf":
        lines = faf.Buffer.from_file(path).lines()
        kept = lines.filter(lines.contains(NEEDLE)).lower()
        with open(out_path, "wb") as f:
            f.write(kept.join(b"\n"))
        return len(kept)
    if impl == "python":
        with open(path, "rb") as f:
            lines = f.read().split(b"\n")
        if lines and lines[-1] == b"":
            lines.pop()
        kept = [s.lower() for s in lines if NEEDLE in s]
        with open(out_path, "wb") as f:
            f.write(b"\n".join(kept))
        return len(kept)
    if impl == "python streaming":
        n = 0
        with open(path, "rb") as f, open(out_path, "wb") as out:
            first = True
            for line in f:
                if NEEDLE in line:
                    out.write((b"" if first else b"\n") + line.rstrip(b"\n").lower())
                    first, n = False, n + 1
        return n
    if impl == "pyarrow":
        with pa.memory_map(path) as src:
            data = src.read_buffer()
        whole = pa.Array.from_buffers(pa.large_binary(), 1, [
            None, pa.py_buffer(bytes(16)[:8] + len(data).to_bytes(8, "little")), data])
        lines = pc.split_pattern(whole, "\n").flatten()
        if len(lines) and len(lines[-1].as_py()) == 0:
            lines = lines.slice(0, len(lines) - 1)
        kept = lines.filter(pc.match_substring(lines, NEEDLE.decode()))
        kept = pc.ascii_lower(kept.cast(pa.large_string()))
        joined = pc.binary_join(pa.ListArray.from_arrays(
            pa.array([0, len(kept)], pa.int32()), kept),
            pa.scalar("\n", pa.large_string()))[0]
        with open(out_path, "wb") as f:
            f.write(joined.as_buffer() if joined.is_valid else b"")
        return len(kept)
    raise ValueError(impl)


def worker(impl, path):
    """Run one pipeline in this process; print time and peak memory.

    Each variant first runs on a tiny file, so one-time costs (pyarrow starts
    its compute engine on first use) aren't counted as pipeline work; memory
    is the peak above what the process had after that."""
    import hashlib
    fd, out = tempfile.mkstemp()
    os.close(fd)
    fd, tiny = tempfile.mkstemp()
    os.write(fd, b"a ERROR b\nc\n")
    os.close(fd)
    try:
        pipeline(impl, tiny, out)
        gc.collect()
        base = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
        t0 = time.perf_counter()
        n = pipeline(impl, path, out)
        t = time.perf_counter() - t0
        peak = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
        with open(out, "rb") as f:
            digest = hashlib.sha1(f.read()).hexdigest()
    finally:
        os.unlink(out)
        os.unlink(tiny)
    scale = 1 if sys.platform == "darwin" else 1024  # bytes on macOS, KB on Linux
    print(json.dumps({"seconds": t, "lines": n, "digest": digest,
                      "base": base * scale, "peak": peak * scale}))


def run_worker(impl, path):
    out = subprocess.run([sys.executable, __file__, "--worker", impl, path],
                         capture_output=True, text=True, check=True)
    return json.loads(out.stdout.strip().splitlines()[-1])


def section_pipeline(args):
    print("\n== 2. Does staying in views pay off? ===============================")
    impls = ["faf", "python", "python streaming"] + (["pyarrow"] if pa else [])
    rng = random.Random(2)
    with tempfile.TemporaryDirectory() as tmp:
        files = []
        path = os.path.join(tmp, "app.log")
        with open(path, "wb") as f:
            for chunk in range(args.pipeline_lines // 100_000 or 1):
                n = min(100_000, args.pipeline_lines)
                f.write(b"\n".join(log_lines(n, 110, 0.02, rng)) + b"\n")
        files.append(("log lines, 110 B, 2% ERROR", path))
        big = os.path.join(ROOT, "random_strings.txt")
        if args.big_file and os.path.exists(big):
            files.append(("random_strings.txt (no ERROR lines)", big))
        for title, p in files:
            size = os.path.getsize(p)
            rows, digests = [], set()
            for impl in impls:
                results = [run_worker(impl, p) for _ in range(args.pipeline_runs)]
                r = min(results, key=lambda x: x["seconds"])
                digests.add(r["digest"])
                extra = max(x["peak"] - x["base"] for x in results)
                rows.append([impl, f"{r['seconds']:.3f}", f"{extra / 2**20:,.0f}",
                             f"{extra / size:.2f}x", f"{r['lines']:,}"])
            if len(digests) != 1:
                raise AssertionError(f"{title}: pipelines wrote different output")
            table(f"{title}: {size / 2**20:,.0f} MB -> keep ERROR lines, lower, "
                  f"write (best of {args.pipeline_runs}, fresh process each; "
                  f"memory: peak above the warmed-up process)",
                  ["variant", "seconds", "peak MB", "x file size", "lines out"], rows)


# ---- 3. crossover ----

def section_crossover(args):
    print("\n== 3. When is a batch too small? ==================================")
    rng = random.Random(3)
    rows = []
    for n in args.sizes:
        lines = log_lines(n, 80, 0.05, rng)
        batch = faf.Buffer.from_bytes(b"\n".join(lines)).split(b"\n")
        runs = max(3, min(1000, 200_000 // n))
        row = [f"{n:,}"]
        for op in ("contains", "lower", "lengths"):
            f, p = faf_ops()[op], py_ops()[op]
            reps = max(1, 20_000 // n)
            tf, _ = best(lambda: [f(batch, NEEDLE) for _ in range(reps)], runs)
            tp, _ = best(lambda: [p(lines, NEEDLE) for _ in range(reps)], runs)
            per = lambda t: t * 1e9 / (n * reps)  # noqa: E731
            row += [f"{fmt_ns(per(tf))} / {fmt_ns(per(tp))}", ratio(tp, tf)]
        rows.append(row)
    table("ns per string, faf / plain Python, 80 B lines (python/faf > 1: faf wins)",
          ["strings", "contains", "", "lower", "", "lengths", ""], rows)


# ---- 4. ingest ----

def section_ingest(args):
    print("\n== 4. What does ingest from Python objects cost? ==================")
    rng = random.Random(4)
    n = args.ingest_n
    rows = []
    for title, items in (("bytes, 80 B", log_lines(n, 80, 0.0, rng)),
                         ("str, 80 B", [s.decode() for s in log_lines(n, 80, 0.0, rng)]),
                         ("str, short keys", [s.decode() for s in short_keys(n, rng)])):
        tf, bf = best(lambda: faf.Batch.from_list(items), args.runs)
        ta = None
        if pa:
            ta, _ = best(lambda: pa.array(items, pa.large_binary() if isinstance(
                items[0], bytes) else pa.large_string()), args.runs)
        tc, _ = best(lambda: bf.lower(), args.runs)  # for scale: one op
        rows.append([title, fmt_ns(tf * 1e9 / n), fmt_ns(ta * 1e9 / n) if ta else "n/a",
                     fmt_ns(tc * 1e9 / n)])
    table(f"ns per string, {n:,} strings from a Python list",
          ["input", "faf from_list", "pyarrow array", "faf lower (for scale)"], rows)


# ---- main ----

def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("sections", nargs="*",
                    default=["ops", "pipeline", "crossover", "ingest"])
    ap.add_argument("--quick", action="store_true", help="small inputs, fewer runs")
    ap.add_argument("--no-big-file", dest="big_file", action="store_false",
                    help="skip random_strings.txt in the pipeline section")
    ap.add_argument("--worker", nargs="+", help=argparse.SUPPRESS)
    args = ap.parse_args()
    if args.worker and args.worker[0].startswith("cold-lower-"):
        cold_lower_worker(args.worker[0][len("cold-lower-"):], int(args.worker[1]),
                          int(args.worker[2]))
        return
    if args.worker:
        worker(*args.worker)
        return

    q = args.quick
    args.runs, args.py_runs = (3, 2) if q else (5, 3)
    args.bytes = 4 << 20 if q else 32 << 20
    args.lengths = [8, 32, 128, 512, 4096]
    args.pipeline_lines = 200_000 if q else 5_000_000
    args.pipeline_runs = 1 if q else 3
    args.sizes = [10, 100, 1000, 10_000, 100_000] + ([] if q else [1_000_000])
    args.ingest_n = 100_000 if q else 1_000_000

    commit = subprocess.run(["git", "-C", ROOT, "rev-parse", "--short=9", "HEAD"],
                            capture_output=True, text=True).stdout.strip()
    print(f"faf Python example benchmarks, commit {commit}"
          f"{' (quick)' if q else ''}")
    print(f"{platform.platform()}, {platform.processor() or platform.machine()}, "
          f"Python {platform.python_version()} ({platform.python_implementation()}), "
          f"pyarrow {pa.__version__ if pa else 'not installed'}")
    for s in args.sections:
        {"ops": section_ops, "pipeline": section_pipeline,
         "crossover": section_crossover, "ingest": section_ingest}[s](args)


if __name__ == "__main__":
    main()
