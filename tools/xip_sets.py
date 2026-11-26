#!/usr/bin/env python3
"""QuakeOG (Agent AQ): count how the hot render code shares the XIP cache sets.

The RP2040's XIP cache has 1024 sets of two 8-byte lines, a line's set is
bits 3 to 12 of its address. Code that runs at the same time and puts three
or more lines in one set evicts itself whatever the total size. This reads
the hot function groups of port/rp2040/ld_hottext/default_text_excludes.incl
(QOG_BOARD_HOTTEXT) and a link map, finds each group's functions in the map,
and prints for each group that names the groups running beside it ("with"):
  lines     8-byte lines of code in the group and the ones beside it
  over      lines in sets that hold more than two of them (conflict lines)
  sets3     sets with more than two lines
  excess    lines beyond two in each set, lines that cannot stay cached
            together with the rest
  least     the least excess any layout can give (lines - 2048), so a layout
            with excess at least has no avoidable conflicts
A map linked without QOG_BOARD_HOTTEXT gives the same counts for today's
layout, so two maps compare the layouts. Code only, no data: the BSP,
texels and lightmaps compete for the same sets on the board as well.

  python tools/xip_sets.py build/apps/quake/quake_display.elf.map [more maps]
"""
import os
import re
import sys
from collections import defaultdict

INCL = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "port", "rp2040",
                    "ld_hottext", "default_text_excludes.incl")
SETS = 1024


def read_groups(path):
    groups, order, cur, beside = {}, [], None, {}
    for line in open(path):
        m = re.match(r"/\* group (\S+)(.*)\*/", line)
        if m:
            cur = m.group(1)
            if cur != "end":
                groups[cur] = []
                order.append(cur)
                w = re.search(r"with ([\w ]+)", m.group(2))
                if w:
                    beside[cur] = w.group(1).split()
            continue
        m = re.match(r"\*(?:libqog_engine_cl\.a:(\S+?))?\((\.text\.\S+)\)\s*$", line.strip())
        if m and cur in groups:
            groups[cur].append((m.group(1), m.group(2)))
    return groups, order, beside


def read_map(path):
    """(section, object, address, size) of every flash .text input section"""
    out, cur = [], None
    for line in open(path):
        m = re.match(r"^ (\.text\.\S+)\s*$", line)
        if m:
            cur = m.group(1)
            continue
        m = re.match(r"^ (\.text\.\S+)\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S+)", line)
        if m:
            sec, a, z, o = m.group(1), int(m.group(2), 16), int(m.group(3), 16), m.group(4)
            cur = None
        elif cur:
            m = re.match(r"^\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S+)", line)
            sec, cur = cur, None
            if not m:
                continue
            a, z, o = int(m.group(1), 16), int(m.group(2), 16), m.group(3)
        else:
            continue
        if 0x10000000 <= a < 0x20000000 and z:
            mo = re.search(r"\(([^)]+)\)$", o)
            out.append((sec, mo.group(1) if mo else os.path.basename(o), a, z))
    return out


def group_lines(secs, members):
    lines, found, size = set(), 0, 0
    for obj, name in members:
        for sec, o, a, z in secs:
            if sec == name and (obj is None or o == obj):
                lines.update(range(a >> 3, (a + z + 7) >> 3))
                found += 1
                size += z
    return lines, found, size


def report(mappath, groups, order, beside):
    secs = read_map(mappath)
    gl = {g: group_lines(secs, groups[g]) for g in order}
    print(mappath)
    for g in order:
        lines, found, size = gl[g]
        lo = min(lines) << 3 if lines else 0
        hi = (max(lines) + 1) << 3 if lines else 0
        print("  %-8s %2d of %2d functions %6d B in %6d B of flash" % (g, found, len(groups[g]), size, hi - lo))
    for g in order:
        if g not in beside:
            continue
        both = set(gl[g][0])
        for h in beside[g]:
            both |= gl[h][0]
        per = defaultdict(int)
        for ln in both:
            per[ln & (SETS - 1)] += 1
        over = sum(n for n in per.values() if n > 2)
        sets3 = sum(1 for n in per.values() if n > 2)
        excess = sum(n - 2 for n in per.values() if n > 2)
        alone = defaultdict(int)
        for ln in gl[g][0]:
            alone[ln & (SETS - 1)] += 1
        self_over = sum(n for n in alone.values() if n > 2)
        print("  %-8s with %-28s lines %5d over %5d sets3 %4d excess %4d least %4d (alone: over %d)"
              % (g, " ".join(beside[g]), len(both), over, sets3, excess, max(0, len(both) - 2 * SETS), self_over))


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    groups, order, beside = read_groups(INCL)
    for path in sys.argv[1:]:
        report(path, groups, order, beside)
    return 0


if __name__ == "__main__":
    sys.exit(main())
