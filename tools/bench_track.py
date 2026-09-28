#!/usr/bin/env python3

# Benchmark tracking: record runs, compare them, summarize them.
#
#   record LOG     parse a benchmark log into bench/results/<machine>/*.json,
#                  with what's needed to reproduce it (commit, backend, pool
#                  configuration, CPU or chip, compiler, command)
#   compare A B    two recorded runs side by side; changes beyond the noise
#                  threshold are flagged. `compare --machine esp32` compares
#                  that machine's latest two runs.
#   report         regenerate bench/RESULTS.md from every recorded run
#
# Usually run through make: bench_record, esp32_bench_record, bench_report.

import argparse
import datetime
import glob
import json
import os
import platform
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RESULTS = os.path.join(ROOT, "bench", "results")
REPORT = os.path.join(ROOT, "bench", "RESULTS.md")

# Rows summarized in RESULTS.md: (short name, section, group, label). Kernel
# rows labeled with the backend's name are stored as "faf", so they compare
# across backends.
OPERATIONS = [  # ns per op, lower is better
    ("record, region", "Record processing", "", "region: acquire ... release"),
    ("record, malloc", "Record processing", "", "malloc: strdup/strsep/malloc ... free"),
    ("split", "Splitting a line", "", "split (array of views)"),
    ("next_token", "Splitting a line", "", "next_token iterator (no allocation)"),
    ("faf_tokens", "Splitting a line", "", "faf_tokens iterator (batched)"),
    ("split_owned", "Splitting a line", "", "split_owned (copy + NUL terminated)"),
    ("churn 256", "Churn", "256 live strings", "region, compact when full"),
    ("append 1 B", "Growth", "1-byte appends", "faf_builder"),
    ("append 16 B", "Growth", "16-byte appends", "faf_builder"),
    ("ring push", "Ring", "", "faf_ring_push"),
]
KERNELS = [  # MB/s, higher is better
    ("find_byte", "Kernel throughput", "find_byte (absent)", "faf"),
    ("count_byte", "Kernel throughput", "count_byte", "faf"),
    ("strlen", "Kernel throughput", "strlen", "faf"),
    ("mismatch", "Kernel throughput", "mismatch (equal)", "faf"),
    ("find_set", "Kernel throughput", "find_set whitespace", "faf"),
    ("to_lower", "Kernel throughput", "ascii_case lower", "faf"),
]

NOISE = 3.0  # percent: smaller changes are reported as "~"


def git(*args):
    return subprocess.run(["git", "-C", ROOT, *args], capture_output=True,
                          text=True).stdout.strip()


def parse_log(path):
    meta, rows = {}, []
    section = group = ""
    backend = None
    for raw in open(path, errors="replace"):
        line = re.sub(r"\x1b\[[0-9;]*m", "", raw.rstrip("\n"))
        m = re.match(r"FAF_BENCH_META (.*)", line)
        if m:
            for kv in m.group(1).split():
                k, _, v = kv.partition("=")
                meta[k] = v
            continue
        m = re.match(r"backend (\w+), best of (\d+) runs", line.strip())
        if m:
            backend = m.group(1)
            meta["backend"], meta["runs"] = backend, int(m.group(2))
            continue
        m = re.match(r"(\d+) pools x (\d+) slots of (\d+) bytes, (\d+) input lines",
                     line.strip())
        if m:
            meta.update(pools=int(m.group(1)), slots=int(m.group(2)),
                        slot_bytes=int(m.group(3)), lines=int(m.group(4)))
            continue
        if line.startswith("== "):
            section, group = line.strip("= ").strip(), ""
            continue
        m = re.match(r"^\s+(\S.*?)\s{2,}([0-9.]+) (ns/op|GB/s|MB/s)(.*)$", line)
        if m:
            label, value, unit, rest = m.groups()
            value = float(value)
            if unit == "GB/s":
                value, unit = value * 1000, "MB/s"
            note = re.sub(r"^\s*(fastest|~same|FAILED|[0-9.]+x slower)\s*", "",
                          rest).strip()
            rows.append({"section": section, "group": group,
                         "label": "faf" if label == backend else label,
                         "value": value, "unit": unit,
                         "failed": "FAILED" in rest, "note": note})
        elif re.match(r"^  \S", line):
            group = line.strip()
    return meta, rows


def host_info():
    info = {"os": f"{platform.system()} {platform.release()}"}
    cpu = ""
    if platform.system() == "Darwin":
        cpu = subprocess.run(["sysctl", "-n", "machdep.cpu.brand_string"],
                             capture_output=True, text=True).stdout.strip()
    elif os.path.exists("/proc/cpuinfo"):
        for line in open("/proc/cpuinfo"):
            if line.startswith("model name"):
                cpu = line.split(":", 1)[1].strip()
                break
    info["cpu"] = cpu or platform.machine()
    cc = os.environ.get("CC", "cc")
    out = subprocess.run([cc, "--version"], capture_output=True, text=True).stdout
    info["compiler"] = out.splitlines()[0] if out else cc
    return info


def slug(text):
    return re.sub(r"[^a-z0-9]+", "-", text.lower()).strip("-")


def cmd_record(args):
    meta, rows = parse_log(args.log)
    if not rows:
        sys.exit(f"bench_track: no benchmark rows in {args.log}")
    if "chip" not in meta:  # host run
        meta.update(host_info())
    commit = git("rev-parse", "--short=12", "HEAD")
    dirty = bool(git("status", "--porcelain", "--", ".",
                     ":!bench/results", ":!bench/RESULTS.md"))
    now = datetime.datetime.now()
    meta.update(date=now.isoformat(timespec="seconds"), commit=commit,
                subject=git("log", "-1", "--format=%s"), dirty=dirty,
                command=args.cmd or "", note=args.note or "")
    machine = args.machine or meta.get("chip") or slug(meta.get("cpu", "host"))
    meta["machine"] = machine
    out_dir = os.path.join(RESULTS, machine)
    os.makedirs(out_dir, exist_ok=True)
    name = f"{now:%Y%m%d-%H%M}-{commit}{'-dirty' if dirty else ''}.json"
    path = os.path.join(out_dir, name)
    with open(path, "w") as f:
        json.dump({"meta": meta, "rows": rows}, f, indent=1)
        f.write("\n")
    print(f"bench_track: recorded {len(rows)} rows -> {os.path.relpath(path, ROOT)}"
          + ("  (uncommitted changes: commit the code first for a clean record)"
             if dirty else ""))


def runs(machine):
    return sorted(glob.glob(os.path.join(RESULTS, machine, "*.json")))


def load(path):
    with open(path) as f:
        return json.load(f)


def key(row):
    return (row["section"], row["group"], row["label"])


def change(a, b, unit):
    """Percent improvement from a to b (positive = faster)."""
    if unit == "ns/op":
        return (a - b) / a * 100 if a else 0.0
    return (b - a) / a * 100 if a else 0.0


def cmd_compare(args):
    if args.machine:
        paths = runs(args.machine)
        if len(paths) < 2:
            sys.exit(f"bench_track: fewer than two runs for {args.machine}")
        a_path, b_path = paths[-2], paths[-1]
    elif len(args.runs) == 2:
        a_path, b_path = args.runs
    else:
        sys.exit("bench_track: compare needs two result files or --machine")
    a, b = load(a_path), load(b_path)
    for label, run in (("A", a), ("B", b)):
        m = run["meta"]
        print(f"{label}: {m['machine']} {m['commit']}{' (dirty)' if m['dirty'] else ''}"
              f"  {m['date']}  backend {m.get('backend')}  {m['subject']}")
    brows = {key(r): r for r in b["rows"]}
    counts = {"faster": 0, "slower": 0, "~": 0}
    section = None
    for r in a["rows"]:
        other = brows.get(key(r))
        if not other or r["unit"] != other["unit"]:
            continue
        if r["section"] != section:
            section = r["section"]
            print(f"\n{section}")
        pct = change(r["value"], other["value"], r["unit"])
        verdict = ("~" if abs(pct) < args.threshold
                   else "faster" if pct > 0 else "slower")
        counts[verdict] += 1
        if args.changed and verdict == "~":
            continue
        name = (r["group"] + " | " if r["group"] else "") + r["label"]
        print(f"  {name[:52]:52} {r['value']:12.1f} {other['value']:12.1f} "
              f"{r['unit']:5}  {pct:+6.1f}%  {verdict}")
    new = [k for k in brows if k not in {key(r) for r in a["rows"]}]
    print(f"\n{counts['faster']} faster, {counts['slower']} slower, {counts['~']} "
          f"within {args.threshold:g}%" + (f", {len(new)} new rows" if new else ""))


def cmd_report(args):
    machines = sorted(d for d in os.listdir(RESULTS)
                      if os.path.isdir(os.path.join(RESULTS, d))) \
        if os.path.isdir(RESULTS) else []
    out = ["# Benchmark results", "",
           "Generated by `tools/bench_track.py report` from the runs in "
           "`bench/results/` (don't edit by hand).", "",
           "Each run is recorded against a commit of the code, on one machine. The "
           "tables show a few key rows per run; the JSON files hold every row, "
           "plus the machine, compiler, backend and pool configuration. A run "
           "marked *dirty* was made with uncommitted changes.", "",
           "Numbers are the best of several runs. Operations are ns per "
           "operation (lower is better); kernels are MB/s over a large buffer "
           "(higher is better). Machines differ in input sizes (the ESP32 apps "
           "use 400 lines and 8 KB buffers), so compare runs within a machine, "
           "not across machines.", "",
           "Noise: the ESP32 boards run the library and benchmarks from IRAM and "
           "repeat within 0.1%. The Mac is noisier: most rows repeat within 3%, "
           "but rows that read files or call malloc/realloc can move about 5% "
           "between runs of the same code.", "",
           "## Reproducing", "",
           "```sh",
           "git checkout <commit>",
           "make bench_record                                  # this computer",
           "make esp32_bench_record                            # original ESP32",
           "make esp32_bench_record IDF_TARGET=esp32s3 ESPPORT=/dev/cu.usbmodem101",
           "python3 tools/bench_track.py compare --machine esp32   # latest two runs",
           "make bench_report                                  # regenerate this file",
           "```", ""]
    for machine in machines:
        recs = [load(p) for p in runs(machine)]
        if not recs:
            continue
        last = recs[-1]["meta"]
        what = (f"{last['chip']} rev {last.get('revision')}, {last.get('mhz')} MHz, "
                f"ESP-IDF {last.get('idf')}" if "chip" in last else
                f"{last.get('cpu')}, {last.get('os')}, {last.get('compiler')}")
        out += [f"## {machine}", "", what, ""]
        for title, metrics, unit in (
                ("Operations, ns (lower is better)", OPERATIONS, "ns/op"),
                ("Kernels, MB/s (higher is better)", KERNELS, "MB/s")):
            present = [m for m in metrics
                       if any(key_of(r, m) is not None for r in recs)]
            if not present:
                continue
            out += [f"**{title}**", "",
                    "| run | backend | " + " | ".join(m[0] for m in present) + " |",
                    "|---|---|" + "---:|" * len(present)]
            for r in recs:
                m = r["meta"]
                run = (f"{m['date'][:10]} `{m['commit'][:9]}`"
                       f"{' dirty' if m['dirty'] else ''} {m['subject'][:60]}")
                vals = []
                for metric in present:
                    v = key_of(r, metric)
                    vals.append("" if v is None else f"{v:,.1f}" if v < 100
                                else f"{v:,.0f}")
                out.append(f"| {run} | {m.get('backend', '')} | " + " | ".join(vals) + " |")
            out.append("")
    with open(REPORT, "w") as f:
        f.write("\n".join(out))
    print(f"bench_track: wrote {os.path.relpath(REPORT, ROOT)} "
          f"({sum(len(runs(m)) for m in machines)} runs, {len(machines)} machines)")


def key_of(run, metric):
    _, section, group, label = metric
    for r in run["rows"]:
        if (r["section"], r["group"], r["label"]) == (section, group, label):
            return None if r["failed"] else r["value"]
    return None


def main():
    parser = argparse.ArgumentParser(description="benchmark tracking")
    sub = parser.add_subparsers(dest="cmd_name", required=True)
    p = sub.add_parser("record", help="record a benchmark log")
    p.add_argument("log")
    p.add_argument("--machine", help="default: the chip, or the CPU name")
    p.add_argument("--cmd", help="command that produced the log")
    p.add_argument("--note", help="free text stored with the run")
    p.set_defaults(func=cmd_record)
    p = sub.add_parser("compare", help="compare two recorded runs")
    p.add_argument("runs", nargs="*")
    p.add_argument("--machine", help="compare this machine's latest two runs")
    p.add_argument("--threshold", type=float, default=NOISE,
                   help=f"percent treated as noise (default {NOISE:g})")
    p.add_argument("--changed", action="store_true", help="only rows that changed")
    p.set_defaults(func=cmd_compare)
    p = sub.add_parser("report", help="regenerate bench/RESULTS.md")
    p.set_defaults(func=cmd_report)
    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
