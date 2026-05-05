#!/usr/bin/env python3
"""Render a side-by-side tier comparison table from multiple
per-tier result directories.

Usage:
    perf-tier-report.py op bs LABEL1=DIR1 LABEL2=DIR2 ...

Example:
    perf-tier-report.py write 1m baseline=/tmp/perf-baseline \
        slot64=/tmp/perf-slot64 slot128=/tmp/perf-slot128
"""
import json, os, sys, glob, re
from collections import OrderedDict

def last_json(path):
    text = open(path).read()
    dec = json.JSONDecoder()
    last = None; i = 0
    while i < len(text):
        try:
            obj, end = dec.raw_decode(text[i:])
            last = obj; i += end
            while i < len(text) and text[i] in ' \n\r\t': i += 1
        except json.JSONDecodeError:
            i += 1
    return last

def parse(directory, op_filter=None, bs_filter=None):
    """Returns (op, bs, N) -> bw_MBs, only for files we can parse."""
    out = {}
    for f in sorted(glob.glob(os.path.join(directory, '*.json'))):
        name = os.path.basename(f).replace('.json', '')
        m = re.match(r'\w+?_([WR])_(\w+)_n(\d+)$', name)
        if m:
            op_letter, bs, nstr = m.groups()
            op = 'write' if op_letter == 'W' else 'read'
            N = int(nstr)
        else:
            m = re.match(r'\w+?_(write|read)_(\w+)$', name)
            if not m: continue
            op, bs = m.groups()
            N = 1
        if op_filter and op != op_filter: continue
        if bs_filter and bs != bs_filter: continue
        try:
            d = last_json(f)
            j = d['jobs'][0]
            out[(op, bs, N)] = j[op]['bw_bytes'] / 1e6
        except Exception:
            pass
    return out

if __name__ == '__main__':
    if len(sys.argv) < 4:
        sys.stderr.write(f"usage: {sys.argv[0]} op bs LABEL1=DIR1 ...\n")
        sys.exit(2)
    op, bs = sys.argv[1], sys.argv[2]
    cols = OrderedDict()
    for spec in sys.argv[3:]:
        label, _, d = spec.partition('=')
        cols[label] = parse(d, op_filter=op, bs_filter=bs)

    # Use first column as N-baseline ordering
    ns = sorted({n for data in cols.values() for (_,_,n) in data})

    print(f"\n### {op} {bs} (MB/s)\n")
    header = "| streams | " + " | ".join(cols.keys()) + " |"
    sep    = "|---|" + "|".join(["---"] * len(cols)) + "|"
    print(header)
    print(sep)
    for n in ns:
        row = f"| {n} | "
        for label, data in cols.items():
            v = data.get((op, bs, n))
            row += (f"{v:.0f}" if v else "n/a") + " | "
        print(row.rstrip())
    print()
