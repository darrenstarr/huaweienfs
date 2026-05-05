#!/usr/bin/env python3
"""Aggregate fio JSON outputs from perf-seq-bench.sh / perf-parallel-bench.sh
into a markdown comparison table.

Usage:
    perf-bench-report.py LABEL DIR [DIR ...]

For each LABEL_<op>_<bs>.json or LABEL_<W|R>_<bs>_n<N>.json found,
prints aggregate throughput (MB/s) and IOPS in markdown tables
suitable for pasting into docs/internals/12-perf-tuning.md.
"""
import json, os, re, sys, glob

def last_json(path):
    """fio --status-interval writes multiple top-level JSON objects
    into one file; take the last."""
    text = open(path).read()
    dec = json.JSONDecoder()
    last = None
    i = 0
    while i < len(text):
        try:
            obj, end = dec.raw_decode(text[i:])
            last = obj
            i += end
            while i < len(text) and text[i] in ' \n\r\t':
                i += 1
        except json.JSONDecodeError:
            i += 1
    return last

def bs_order(bs):
    units = {'k': 1024, 'm': 1024 * 1024}
    n = int(re.match(r'(\d+)', bs).group(1))
    return n * units[bs[-1].lower()]

def parse(directory):
    """Returns dict (op, bs, nstreams) -> (bw_MBs, iops, lat_us_mean)."""
    out = {}
    for f in sorted(glob.glob(os.path.join(directory, '*.json'))):
        name = os.path.basename(f).replace('.json', '')
        # try parallel first: LABEL_W|R_BS_nN
        m = re.match(r'\w+?_([WR])_(\w+)_n(\d+)$', name)
        if m:
            op_letter, bs, nstr = m.groups()
            op = 'write' if op_letter == 'W' else 'read'
            N = int(nstr)
        else:
            # sequential: LABEL_op_BS
            m = re.match(r'\w+?_(write|read)_(\w+)$', name)
            if not m:
                continue
            op, bs = m.groups()
            N = 1
        try:
            d = last_json(f)
            j = d['jobs'][0]
            sec = j[op]
            out[(op, bs, N)] = (sec['bw_bytes'] / 1e6, sec['iops'],
                                sec['lat_ns']['mean'] / 1000)
        except Exception as e:
            sys.stderr.write(f"  parse fail {f}: {e}\n")
    return out

if __name__ == '__main__':
    if len(sys.argv) < 3:
        sys.stderr.write(f"usage: {sys.argv[0]} TITLE DIR [DIR ...]\n")
        sys.exit(2)
    title = sys.argv[1]
    print(f"\n## {title}\n")

    columns = []
    for d in sys.argv[2:]:
        label = os.path.basename(os.path.normpath(d))
        columns.append((label, parse(d)))

    # Find all (op, bs, N) tuples present in any column
    keys = set()
    for _, data in columns:
        keys |= set(data.keys())
    if not keys:
        print("(no data)")
        sys.exit(0)
    by_op = {}
    for op, bs, N in keys:
        by_op.setdefault(op, set()).add((bs, N))

    for op in sorted(by_op):
        print(f"### {op} (MB/s)\n")
        sizes_n = sorted(by_op[op], key=lambda x: (bs_order(x[0]), x[1]))
        header = "| bs | streams | " + " | ".join(c[0] for c in columns) + " |"
        sep    = "|---|---|" + "|".join(["---"] * len(columns)) + "|"
        print(header)
        print(sep)
        for bs, N in sizes_n:
            row = f"| {bs} | {N} | "
            for _, data in columns:
                v = data.get((op, bs, N))
                row += (f"{v[0]:.1f}" if v else "n/a") + " | "
            print(row.rstrip())
        print()
