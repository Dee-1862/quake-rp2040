#!/usr/bin/env python3
"""Deepest stack chain of a board build, from -fstack-usage and call edges.

Reads every .su file under the build folder (one line per function, its own
frame in bytes) and the call edges of the objects that make the app, taken
from `arm-none-eabi-objdump -dr` (R_ARM_THM_CALL and R_ARM_THM_JUMP24
relocations, one function per section as -ffunction-sections builds them).
It then walks the call graph from a root (main by default) and prints the
deepest chain with every frame on it.

Indirect calls (`blx rN`, function pointers) cannot be seen in the code. Each
function with one gets an edge to every function whose address is taken
somewhere (an R_ARM_ABS32 relocation that names it), which is an upper bound,
unless --indirect gives a narrower list for that caller. Recursion is cut
where it closes (a function already on the chain contributes nothing), and
the cycles found are listed: their depth has to be bounded by hand, as the
QuakeC call depth (--qc-depth) is.

Dynamic frames (alloca, variable length arrays) are flagged with their
static part only. Functions without a .su line (libgcc, newlib, the SDK's
assembly) count --unknown bytes each and are listed.

    python tools/stack_chain.py --build build --app quake_display
    python tools/stack_chain.py --root R_RenderView --top 3

Standard library only.
"""
import argparse
import glob
import os
import re
import subprocess
import sys
from collections import defaultdict

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
BIN = os.path.join(os.path.expanduser("~"), ".pico-sdk", "toolchain", "15_2_Rel1", "bin")
EXE = ".exe" if os.name == "nt" else ""

# Indirect calls in this engine, by caller: which functions the pointer can
# hold (from the sources: the command table, the QuakeC builtins and
# compiled functions, the network driver table, the clippers, span and block
# drawers, the menus, printf's output hooks). Callers not listed reach the
# byte swappers (LittleLong and friends), which is what the rest use.
# --indirect adds or replaces rules.
RULES = [
    (r"PR_ExecuteProgram|PR_AOT_Call", r"PF_.*|QC"),
    (r"Cmd_ExecuteString", r".*_f|IN_.*|Cmd_ForwardToServer|cmd_qog_.*|S_Play.*|S_SoundList|"
                           r"S_StopAllSoundsC|Sbar_.*Scores|V_StartPitchDrift|Cache_Flush|Mod_Print|"
                           r"ED_PrintEdicts|ED_Count|MaxPlayers_f"),
    (r"NET_.*|Slist_.*|CL_GetMessage|CL_WriteDemoMessage", r"Loop_.*|VCR_.*|Slist_.*|.*Swap"),
    (r"R_AliasClip", r"R_Alias_clip_.*"),
    (r"R_DrawSurface", r"R_DrawSurfaceBlock.*"),
    (r"R_QogScanRows|R_ScanEdges", r"R_GenerateSpans.*"),
    (r"M_.*", r"M_.*|vid_menu_.*"),
    (r"Host_Status_f", r"Con_Printf|SV_ClientPrintf"),
    (r"COM_ForEachPackFile", r"Bake_.*"),
    (r"_vsnprintf|_etoa|_out_.*|weak_raw_.*|stdio_.*|__wrap_.*", r"_out_.*|stdio_.*"),
    (r"runtime_.*|rom_.*|.*_irq_handler|flash_safe_.*|platform_entry|ph_.*|fatan2_shim|fix642float_shim|__aeabi_i2f_main|__revll", r"$^"),
    (r".*", r".*Swap"),
]

# newlib and libgcc frames that matter, measured from their disassembly
# (push and sub sp) in the 15.2 toolchain. Everything else unknown counts
# --unknown bytes.
KNOWN_LIB = {
    "vsnprintf": 120, "_svfprintf_r": 228, "_vfprintf_r": 228, "_dtoa_r": 112,
    "snprintf": 120, "sprintf": 120, "printf": 24, "vprintf": 24,
    "memcpy": 16, "memset": 16, "memmove": 16, "strlen": 8, "strcmp": 16,
}


def frames_from_su(build):
    """name -> (bytes, kind). Static functions keep their bare name; when two
    files share a name the bigger frame wins (an upper bound)."""
    fr = {}
    for su in glob.glob(os.path.join(build, "**", "*.su"), recursive=True):
        for line in open(su, encoding="utf-8", errors="replace"):
            parts = line.rstrip("\n").split("\t")
            if len(parts) < 3 or not parts[1].isdigit():
                continue
            name = parts[0].split(":")[-1]
            n = int(parts[1])
            if name not in fr or fr[name][0] < n:
                fr[name] = (n, parts[2])
    return fr


def objdump(obj, flag):
    """objdump output, cached next to the object while it is unchanged."""
    cache = obj + (".dis" if flag == "-dr" else ".rel")
    if os.path.exists(cache) and os.path.getmtime(cache) >= os.path.getmtime(obj):
        return open(cache, encoding="utf-8", errors="replace").read()
    out = subprocess.run([os.path.join(BIN, "arm-none-eabi-objdump" + EXE), flag, obj],
                         capture_output=True, text=True, errors="replace").stdout
    with open(cache, "w", encoding="utf-8") as fh:
        fh.write(out)
    return out


def edges_from_objects(objs):
    """caller -> set of callees, the set of callers with indirect calls, and
    the address-taken functions."""
    calls = defaultdict(set)
    indirect = set()
    taken = set()
    funcs = set()
    sec_re = re.compile(r"^[0-9a-f]+ <([^>]+)>:")
    for obj in objs:
        out = objdump(obj, "-dr")
        cur = None
        for line in out.splitlines():
            m = sec_re.match(line)
            if m:
                cur = m.group(1)
                funcs.add(cur)
                continue
            if cur is None:
                continue
            if "R_ARM_THM_CALL" in line or "R_ARM_THM_JUMP24" in line or "R_ARM_THM_JUMP" in line:
                tgt = line.split()[-1]
                tgt = tgt.split("+")[0]
                if tgt != cur:
                    calls[cur].add(tgt)
            elif re.search(r"\tblx\s+r\d+", line):
                indirect.add(cur)
            elif "R_ARM_ABS32" in line:
                taken.add(line.split()[-1].split("+")[0])
        # address-taken functions referenced from data sections
        out = objdump(obj, "-r")
        for line in out.splitlines():
            if "R_ARM_ABS32" in line:
                taken.add(line.split()[-1].split("+")[0])
    taken &= funcs
    return calls, indirect, taken, funcs


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--build", default=os.path.join(ROOT, "build"))
    ap.add_argument("--app", default="quake_display", help="app target whose objects are added to the engine's")
    ap.add_argument("--lib", default=os.path.join("apps", "quake", "libqog_engine.a"))
    ap.add_argument("--root", default="main")
    ap.add_argument("--unknown", type=int, default=64, help="bytes for a function without a .su line")
    ap.add_argument("--indirect", action="append", default=[],
                    help="CALLER=REGEX: the indirect calls of CALLER only reach functions matching REGEX")
    ap.add_argument("--exclude", action="append", default=[],
                    help="REGEX: functions never reached (for example code a --wrap replaces)")
    ap.add_argument("--cut", action="append", default=[],
                    help="CALLER=CALLEE: a call that cannot happen at the depth that matters (regexes)")
    ap.add_argument("--qc-depth", type=int, default=1,
                    help="QuakeC calls nested on the C stack (compiled QuakeC), the chain holds one")
    ap.add_argument("--top", type=int, default=1, help="print the deepest chain under this many children of the root")
    a = ap.parse_args(argv)

    objs = [os.path.join(a.build, a.lib)]
    objs += glob.glob(os.path.join(a.build, "**", a.app + ".dir", "**", "*.obj"), recursive=True)
    fr = frames_from_su(a.build)
    calls, indirect, taken, funcs = edges_from_objects(objs)
    rules = [(re.compile(c), re.compile(t)) for c, t in RULES]
    for spec in reversed(a.indirect):
        caller, rx = spec.split("=", 1)
        rules.insert(0, (re.compile(caller), re.compile(rx)))

    # compiled QuakeC (tools/qc2c.py): every qc_N becomes one node QC with
    # the biggest frame and every callee, so QuakeC recursion is one cycle
    qc = re.compile(r"qc_\d+")
    qcf = [f for f in funcs if qc.fullmatch(f)]
    if qcf:
        big = max(fr.get(f, (0, "static"))[0] for f in qcf)
        fr["QC"] = (big, "static")
        calls["QC"] = set().union(*(calls.get(f, set()) for f in qcf))
        for f in list(calls):
            calls[f] = {"QC" if qc.fullmatch(t) else t for t in calls[f]}
        if any(f in indirect for f in qcf):
            indirect.add("QC")
        funcs.add("QC")
        taken = {t for t in taken if not qc.fullmatch(t)} | {"QC"}
        print("compiled QuakeC: %d functions as one node QC, biggest frame %d bytes" % (len(qcf), big))

    def targets(f):
        for c, t in rules:
            if c.fullmatch(f):
                return t
        return None
    excl = [re.compile(x) for x in a.exclude]

    def excluded(f):
        return any(x.fullmatch(f) for x in excl)

    unknown = set()
    dynamic = set()

    def frame(f):
        if f in fr:
            if fr[f][1] != "static":
                dynamic.add(f)
            return fr[f][0]
        if f in KNOWN_LIB:
            return KNOWN_LIB[f]
        unknown.add(f)
        return a.unknown

    cuts = [(re.compile(x.split("=", 1)[0]), re.compile(x.split("=", 1)[1])) for x in a.cut]

    def children(f):
        ch = set(calls.get(f, ()))
        if f in indirect:
            rx = targets(f)
            ch |= {t for t in taken if rx is None or rx.fullmatch(t)}
        ch.discard(f)
        return [c for c in ch if not excluded(c)
                and not any(x.fullmatch(f) and y.fullmatch(c) for x, y in cuts)]

    memo = {}
    cycles = set()
    sys.setrecursionlimit(100000)

    # Depth first, each function at most once on a chain: a call back to a
    # function already on the chain is recursion, cut and listed. Results
    # are memoised whatever the chain was, so a second trip around a cycle
    # is never counted; recursion depth is bounded separately (--qc-depth).
    def deepest(f, onpath):
        if f in memo:
            return memo[f]
        onpath.add(f)
        best, bestc = 0, []
        for c in children(f):
            if c in onpath:
                cycles.add((f, c))
                continue
            d, ch = deepest(c, onpath)
            if d > best:
                best, bestc = d, ch
        onpath.discard(f)
        memo[f] = (frame(f) + best, [f] + bestc)
        return memo[f]

    if a.root not in funcs and a.root not in calls:
        sys.exit("root %s not found" % a.root)
    total, chain = deepest(a.root, set())
    print("deepest chain from %s: %d bytes (%d frames)" % (a.root, total, len(chain)))
    run = 0
    for f in chain:
        n = frame(f)
        run += n
        kind = fr[f][1] if f in fr else ("lib" if f in KNOWN_LIB else "no .su")
        print("  %7d %7d  %-40s %s" % (n, run, f, kind))
    if a.top > 1:
        print("\ndeepest chain under each child of %s:" % a.root)
        res = []
        for c in children(a.root):
            d, ch = deepest(c, {a.root})
            res.append((d, c, ch))
        res.sort(reverse=True)
        for d, c, ch in res[:a.top]:
            print("  %7d  %s -> ... -> %s" % (d, c, " > ".join(ch[-4:])))
    if "QC" in fr:
        level = frame("QC") + frame("PR_AOT_Call") + frame("PR_EnterFunction")
        print("\none more level of compiled QuakeC (QC + PR_AOT_Call + PR_EnterFunction): %d bytes, "
              "%d levels beyond the chain's one add %d bytes, total %d"
              % (level, a.qc_depth - 1, level * (a.qc_depth - 1), total + level * (a.qc_depth - 1)))
    big = sorted(((frame(f), f) for f in chain if f in dynamic), reverse=True)
    if big:
        print("\ndynamic frames on the chain (static part counted): " + ", ".join("%s %d" % (f, n) for n, f in big))
    if cycles:
        print("\nrecursion cut at %d call edges, for example: %s" % (len(cycles),
              ", ".join("%s->%s" % c for c in sorted(cycles)[:12])))
    print("\nfunctions with indirect calls: %d, address-taken functions: %d, no .su: %d (%d bytes each)"
          % (len(indirect), len(taken), len(unknown), a.unknown))
    return 0


if __name__ == "__main__":
    sys.exit(main())
