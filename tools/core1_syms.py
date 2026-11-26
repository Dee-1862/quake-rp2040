#!/usr/bin/env python3
"""What core 1's surface drawing can touch (Agent AM, QOG_DRAW_CORE1).

engine/d_core1.c, d_core1s.c and d_core1e.c compile the lit surface drawing
a second time with core 1's own names for what it writes (d_core1.h). A
name that file misses would be shared with core 0 silently: the board link
allows multiple definitions. This walks the board objects' relocations from
core 1's job (D_QogC1Worker) through every function it can call, and lists
each global or static variable reached that is neither core 1's own (c1_
names, the statics of the d_core1 objects) nor on the list below of values
nothing writes while the view's surfaces are drawn. It exits 1 if there is
any. Indirect calls are not followed (the drawing makes none).

    python tools/core1_syms.py --build build

Standard library only.
"""
import argparse
import glob
import os
import re
import subprocess
import sys
from collections import defaultdict

BIN = os.path.join(os.path.expanduser("~"), ".pico-sdk", "toolchain", "15_2_Rel1", "bin")
EXE = ".exe" if os.name == "nt" else ""

# read by both cores and written by neither while R_ScanEdges runs (the
# frame's view, texture, light and buffer values), or the pipeline's own
# shared state (d_edge.c), each checked by hand
SHARED = {
    # frame buffer, z-buffer, span links, set before the scan
    "d_viewbuffer", "screenwidth", "d_pzbuffer", "d_zwidth", "r_spanbase", "surfaces", "surface_p",
    # the view's projection and mip scale, set in R_SetupFrame / D_SetupFrame
    "xscaleinv", "yscaleinv", "xcenter", "ycenter", "scale_for_mip", "d_scalemip", "d_minmip",
    "r_refdef", "vid", "r_qogfast", "r_fullbright", "r_framecount",
    # map, entities, lights, set by the client before the view
    "cl", "cl_entities", "cl_visedicts", "cl_dlights", "d_lightstylevalue",
    # r_light.c's dynamic light hash, filled before the scan (D_QogPipeBegin asks R_DlightLit)
    "dl_surf", "dl_bits", "dl_lost", "dl_pending", "dl_used", "dl_visonly", "dl_pendframe",
    "r_dlightframecount", "mod_frames", "mod_curframe", "r_origin",
    # reached only through R_DlightRunPending, which cannot run while
    # dl_pending is false (D_QogPipeBegin ran it), and Sys_Error and the
    # power button poll it calls (a stop, never in a running view)
    "s_next_draw", "s_ship", "s_dma_ctrl", "s_dma_data", "s_on", "s_pio", "s_sm",
    # the pipeline (d_edge.c, under its lock), the core 1 counters (own indices)
    "pipe_ring", "pipe_head", "pipe_tail", "pipe_cursor", "pipe_end", "pipe_drawn",
    "d_qogc1_drawing", "s_pipe_lock", "qog_c1_stat",
    # the SDK's float and divide helpers' tables, the timer
    "timer_hw",
    # Agent AR: the span assembly's start-up check result (d_lit_q.h), written
    # once by D_QogLitQCheck on core 0 at D_Init, before core 1 ever draws
    "litq_asm_ok",
    "litq_wrap_ok",  # the same for the span loop (D_QogLitWrapCheck)
}


def run(tool, *args):
    return subprocess.run([os.path.join(BIN, "arm-none-eabi-" + tool + EXE)] + list(args),
                          capture_output=True, text=True, errors="replace").stdout


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--build", default="build")
    ap.add_argument("--root", default="D_QogC1Worker")
    a = ap.parse_args()
    base = os.path.join(a.build, "apps", "quake", "CMakeFiles")
    objs = glob.glob(os.path.join(base, "qog_engine_cl.dir", "**", "*.obj"), recursive=True)
    objs += glob.glob(os.path.join(base, "quake_display.dir", "**", "*.obj"), recursive=True)
    if not objs:
        sys.exit("no objects under " + base)
    local = {}                  # obj -> {name: type} of its local symbols
    gtype = {}                  # global name -> type letter
    for o in objs:
        local[o] = {}
        for line in run("nm", o).splitlines():
            p = line.split()
            if len(p) == 3:
                t, n = p[1], p[2]
                if t.islower():
                    local[o][n] = t
                elif t != "U":
                    gtype.setdefault(n, t)
    # the linked app's types for what the objects leave undefined (library
    # functions, the SDK's)
    elf = os.path.join(a.build, "apps", "quake", "quake_display.elf")
    for line in run("nm", elf).splitlines():
        p = line.split()
        if len(p) == 3 and p[1].isupper():
            gtype.setdefault(p[2], p[1])
    edges = defaultdict(set)    # node -> nodes (functions) or ("data", name, obj)
    sec_re = re.compile(r"RELOCATION RECORDS FOR \[(\.[^\]]+)\]")
    for o in objs:
        cur = None
        for line in run("objdump", "-r", o).splitlines():
            m = sec_re.match(line)
            if m:
                s = m.group(1)
                cur = None
                for pre in (".text.", ".time_critical."):
                    if s.startswith(pre):
                        n = s[len(pre):]
                        cur = (o, n) if n in local[o] else n
                continue
            p = line.split()
            if cur is None or len(p) < 3 or not p[0][0].isdigit():
                continue
            sym = p[2].split("+")[0].split("-")[0]
            for pre in (".bss.", ".data.", ".rodata.", ".sdata.", ".text.", ".time_critical."):
                if sym.startswith(pre):
                    sym = sym[len(pre):]
                    break
            if sym in local[o]:
                edges[cur].add((o, sym, local[o][sym]))
            else:
                edges[cur].add((None, sym, gtype.get(sym, "U")))
    c1objs = {o for o in objs if re.search(r"d_core1[se]?\.c\.obj$", o)}
    seen, todo, data = set(), [a.root], defaultdict(set)
    shared_funcs = set()
    while todo:
        f = todo.pop()
        if f in seen:
            continue
        seen.add(f)
        for (o, sym, t) in edges.get(f, ()):
            if t.upper() in "TW":
                nxt = (o, sym) if o else sym
                todo.append(nxt)
                if not o and not sym.startswith("c1_"):
                    shared_funcs.add(sym)
            elif t.upper() in "BDCSG":
                if sym.startswith("c1_") or (o in c1objs):
                    continue
                data[sym].add(f if isinstance(f, str) else f[1])
    bad = sorted(s for s in data if s not in SHARED)
    print("core 1 reaches %d functions; shared (not c1_) ones:" % len(seen))
    print("  " + " ".join(sorted(shared_funcs)))
    print("shared variables on the list:")
    for s in sorted(data):
        if s in SHARED:
            print("  %s (%s)" % (s, ", ".join(sorted(data[s]))))
    if bad:
        print("NOT on the list, check each:")
        for s in bad:
            print("  %s (%s)" % (s, ", ".join(sorted(data[s]))))
        sys.exit(1)
    print("every variable core 1 reaches is its own or on the list")


if __name__ == "__main__":
    main()
