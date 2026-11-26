#!/usr/bin/env python3
"""QuakeOG: sum the board profiler's windows from a saved USB log.

The profiler (port/rp2040/prof_rp2040.c, build option QOG_PROF) prints every
5 s, per core, a header and the top functions of that window:

  [prof] core 0: 20012 samples in 5.0 s, 412 slots of 512 used, 0 lost
  [prof] c0  1  21.3%  4263 D_DrawSpans8 0x1004cde4
  [prof] c0 rom   9.8%  1961 from R_AliasPreparePoints 0x10051234

This script adds the windows of one or more logs (COM8 for the display, COM7
for main) and prints each core's functions over the whole run. A window's
functions below its top 20 are not in the log, so the sums are lower bounds
for rarely sampled functions and exact for the ones that stay in the top.

  python tools/prof_resolve.py display.log [--top 30] [--from 3] [--to 20]
  python tools/prof_resolve.py --elf build/apps/quake/quake_display.elf 0x1004ce10 0x20000412

With --elf, addresses given on the command line are resolved to functions
(through tools/prof_table.py's reader), for PCs from a fault record.
"""
import argparse
import os
import re
import sys
from collections import defaultdict

HEAD = re.compile(r"\[prof\] core (\d): (\d+) samples in ([\d.]+) s")
FUNC = re.compile(r"\[prof\] c(\d)\s+\d+\s+([\d.]+)%\s+(\d+) (\S+) 0x([0-9a-fA-F]+)")
ROM = re.compile(r"\[prof\] c(\d) rom\s+([\d.]+)%\s+(\d+) from (\S+) 0x([0-9a-fA-F]+)")


def resolve(elf_path, addrs):
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import prof_table
    data = open(elf_path, "rb").read()
    _, syms = prof_table.read_elf(data)
    funcs = sorted((v & ~1, s, n) for n, v, s, t, b, sh in syms if t == 2 and s and sh)
    for a in addrs:
        x = int(a, 0)
        hit = [f for f in funcs if f[0] <= x < f[0] + f[1]]
        if hit:
            f = hit[-1]
            print("0x%08x %s+0x%x" % (x, f[2], x - f[0]))
        elif x < 0x4000:
            print("0x%08x [rom]" % x)
        else:
            print("0x%08x [unknown]" % x)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("inputs", nargs="*", help="saved logs, or addresses with --elf")
    ap.add_argument("--elf", help="resolve the given addresses in this ELF")
    ap.add_argument("--top", type=int, default=30)
    ap.add_argument("--from", dest="first", type=int, default=1, help="first window to count (1 based)")
    ap.add_argument("--to", dest="last", type=int, default=0, help="last window to count, 0 for all")
    a = ap.parse_args()
    if a.elf:
        resolve(a.elf, a.inputs)
        return

    total = defaultdict(int)
    secs = defaultdict(float)
    funcs = defaultdict(lambda: defaultdict(int))
    roms = defaultdict(lambda: defaultdict(int))
    windows = defaultdict(int)
    for path in a.inputs:
        counting = {}
        for line in open(path, errors="replace"):
            m = HEAD.search(line)
            if m:
                core = int(m.group(1))
                windows[core] += 1
                w = windows[core]
                counting[core] = w >= a.first and (a.last == 0 or w <= a.last)
                if counting[core]:
                    total[core] += int(m.group(2))
                    secs[core] += float(m.group(3))
                continue
            m = FUNC.search(line)
            if m and counting.get(int(m.group(1))):
                funcs[int(m.group(1))][m.group(4)] += int(m.group(3))
                continue
            m = ROM.search(line)
            if m and counting.get(int(m.group(1))):
                roms[int(m.group(1))][m.group(4)] += int(m.group(3))
    if not total:
        sys.exit("prof_resolve: no [prof] windows in the log")
    for core in sorted(total):
        n = total[core]
        print("core %d: %d samples in %.1f s (%d windows counted)" % (core, n, secs[core], windows[core]))
        for i, (name, c) in enumerate(sorted(funcs[core].items(), key=lambda x: -x[1])[:a.top]):
            print("  %3d %6.2f%% %8d %s" % (i + 1, 100.0 * c / n, c, name))
        if roms[core]:
            print("  boot ROM (float routines) by caller")
            for name, c in sorted(roms[core].items(), key=lambda x: -x[1])[:10]:
                print("      %6.2f%% %8d %s" % (100.0 * c / n, c, name))


if __name__ == "__main__":
    main()
