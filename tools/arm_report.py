#!/usr/bin/env python3
"""Report what the engine costs on the RP2040: code size, static RAM, and the
biggest stack frames.

Reads the target build (build/, ARM toolchain): the qog_engine library for
code and data sizes, and the .su files -fstack-usage writes next to each
object for per-function stack frames. Run after:

    cmake --build build --target qog_engine
    python tools/arm_report.py [--top 25]

The board's core 0 stack is 4 KB in scratch memory by default (Pico SDK), so
any single frame above a few KB is a crash waiting to happen there.
Standard library only.
"""
import argparse
import glob
import os
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
BIN = os.path.join(os.path.expanduser("~"), ".pico-sdk", "toolchain", "15_2_Rel1", "bin")
EXE = ".exe" if os.name == "nt" else ""


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--top", type=int, default=25)
    ap.add_argument("--build", default=os.path.join(ROOT, "build"))
    a = ap.parse_args(argv)

    lib = os.path.join(a.build, "apps", "quake", "libqog_engine.a")
    if not os.path.exists(lib):
        sys.exit("build the engine first: cmake --build build --target qog_engine")
    out = subprocess.run([os.path.join(BIN, "arm-none-eabi-size" + EXE), "-t", lib],
                         capture_output=True, text=True, check=True).stdout.strip().splitlines()
    text, data, bss = (int(x) for x in out[-1].split()[:3])
    print("engine on Cortex-M0+: code+consts %.1f KB (flash), data %.1f KB, bss %.1f KB (static RAM %.1f KB)"
          % (text / 1024, data / 1024, bss / 1024, (data + bss) / 1024))

    frames = []
    for su in glob.glob(os.path.join(a.build, "apps", "quake", "**", "*.su"), recursive=True):
        for line in open(su, encoding="utf-8", errors="replace"):
            parts = line.rstrip("\n").split("\t")
            if len(parts) >= 3 and parts[1].isdigit():
                where = parts[0].split(":")
                func = where[-1] if where else parts[0]
                src = os.path.basename(where[0]) if len(where) > 1 else ""
                frames.append((int(parts[1]), func, src, parts[2]))
    frames.sort(reverse=True)
    print("\nbiggest stack frames (bytes, function, file, kind):")
    for size, func, src, kind in frames[:a.top]:
        print("  %7d  %-32s %-14s %s" % (size, func, src, kind))
    print("\n%d functions measured; %d use more than 1 KB, %d more than 4 KB"
          % (len(frames), sum(1 for f in frames if f[0] > 1024), sum(1 for f in frames if f[0] > 4096)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
