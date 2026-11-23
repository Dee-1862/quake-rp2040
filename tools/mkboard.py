#!/usr/bin/env python3
"""Build the board's game-data image: a trimmed pak plus a baked model image.

The display CPU's data region (platform/qog_flashmap.h, 0x10400000) holds

    offset 0          the trimmed pak (tools/mkpak.py), files 16-byte aligned
    next 64 KB        qogbake.bin, the baked maps, alias models and sprites
                      (engine/model.c, qog_bake), built for this address

port/rp2040/sys_rp2040.c serves the first as id1/pak0.pak and finds the
second at the first 64 KB boundary after the pak that holds "QOGB".

The PC build maps a pak at 0x10400000 too and lays it out exactly the way
mkpak writes it, so running qog_bake on the PC with the trimmed pak as its
only pak builds the image for the board's address. This tool does the three
steps:

    python tools/mkboard.py --pak data/id1/pak0.pak --pak data/id1/pak1.pak \\
        --manifests build-pc-dev/manifests --maps start e1m1 e1m2 e1m3 \\
        --exe build-pc-dev/Release/quakeog_pc.exe -o build/qogboard

  1. pak    <out>/id1/pak0.pak from the manifests (as mkpak.py)
  2. bake   runs the PC build hidden and silent through tools/pctest.ps1:
            -nobake -basedir <out> +qog_bake  ->  <out>/id1/qogbake.bin
            (--bake-base ADDR builds it for a later 64 KB boundary)
  3. image  <out>.bin and <out>.uf2: the pak, 0xFF padding, the baked image

--no-bake skips step 2 and uses an existing <out>/id1/qogbake.bin. The PC
can run the result as it will run on the board: -basedir <out>.

Never commit or share the output: it is converted id Software data.
Standard library only.
"""
import argparse
import os
import struct
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mkdata  # noqa: E402  (flash map constants, UF2 writer)
import mkpak   # noqa: E402  (trimmed pak)

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
DATA_ADDR = mkdata.XIP_BASE + mkdata.DATA_OFFSET
QOGB_MAGIC = 0x42474F51   # engine/model.h
QOGB_HEADER = "<7I"       # magic, version, layout, base, size, nummaps, dirofs


def align(n, a):
    return (n + a - 1) & ~(a - 1)


def pc_layout(pak):
    """The size the PC's realign_pack (port/pc/sys_pc.c) gives this pak, or
    None if it would move any file: then the PC's addresses would not be
    the board's."""
    magic, dofs, dlen = struct.unpack_from("<4sii", pak, 0)
    if magic != b"PACK" or dlen % 64:
        return None
    pos = 16
    for i in range(dlen // 64):
        fpos, flen = struct.unpack_from("<ii", pak, dofs + 64 * i + 56)
        if fpos != pos:
            return None
        pos = align(pos + flen, 16)
    return pos + dlen if pos == dofs else None


def bake_header(img):
    if len(img) < 64:
        return None
    h = struct.unpack_from(QOGB_HEADER, img, 0)
    return h if h[0] == QOGB_MAGIC else None


def run_bake(exe, basedir, base, timeout):
    """qog_bake on the PC: hidden, silent, through tools/pctest.ps1."""
    args = "-nosound -nobake -basedir %s" % basedir.replace("\\", "/")
    if base:
        args += " -bakebase 0x%08X" % base
    args += " +qog_bake +qog_quit"
    log = os.path.join(basedir, "bake.txt")
    cmd = ["powershell", "-NoProfile", "-File", os.path.join(ROOT, "tools", "pctest.ps1"),
           "-Exe", exe, "-GameArgs", args, "-Out", log, "-TimeoutSec", str(timeout)]
    subprocess.run(cmd, check=True, cwd=ROOT)
    tail = open(log, errors="replace").read().strip().splitlines()[-1:] if os.path.exists(log) else []
    print("bake: %s" % (tail[0] if tail else "(no log)"))


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--pak", required=True, action="append", help="source pak, repeat (later paks win)")
    ap.add_argument("--manifests", required=True, help="directory of qog_manifest outputs")
    ap.add_argument("--maps", nargs="*", help="maps to keep (default: every manifest)")
    ap.add_argument("--demos", action="store_true", help="keep demo*.dem for the attract loop")
    ap.add_argument("--exe", help="PC build (quakeog_pc.exe) that runs qog_bake")
    ap.add_argument("--no-bake", action="store_true", help="use an existing <out>/id1/qogbake.bin")
    ap.add_argument("--bake-base", type=lambda s: int(s, 0), default=0,
                    help="flash address for the baked image (default: first 64 KB boundary after the pak)")
    ap.add_argument("--timeout", type=int, default=900, help="seconds for the bake run")
    ap.add_argument("-o", "--out", required=True, help="output path without extension, also the PC basedir")
    a = ap.parse_args(argv)

    out = os.path.abspath(a.out)
    if len(out) > 80:   # the engine keeps paths in MAX_OSPATH (128) buffers
        sys.exit("mkboard: use a shorter output path (%d characters, at most 80)" % len(out))
    gamedir = os.path.join(out, "id1")
    os.makedirs(gamedir, exist_ok=True)
    pakpath = os.path.join(gamedir, "pak0")

    # 1. the trimmed pak, the only pak of the PC run that bakes
    mk = sum((["--pak", p] for p in a.pak), []) + ["--manifests", a.manifests, "-o", pakpath, "--no-uf2"]
    if a.maps:
        mk += ["--maps"] + a.maps
    if a.demos:
        mk += ["--demos"]
    mkpak.main(mk)
    pak = open(pakpath + ".pak", "rb").read()
    if pc_layout(pak) != len(pak):
        sys.exit("mkboard: the pak is not laid out the way the PC maps it")

    first = align(len(pak), 0x10000)
    base = a.bake_base or (DATA_ADDR + first)
    if base < DATA_ADDR + first or base & 0xFFFF:
        sys.exit("mkboard: --bake-base 0x%08X must be 64 KB aligned and at or after 0x%08X" % (base, DATA_ADDR + first))

    # 2. the baked image, for that address
    bakepath = os.path.join(gamedir, "qogbake.bin")
    if not a.no_bake:
        if not a.exe:
            sys.exit("mkboard: --exe is needed to bake (or --no-bake)")
        if os.path.exists(bakepath):
            os.remove(bakepath)   # never map a stale image while baking
        run_bake(os.path.abspath(a.exe), out, a.bake_base, a.timeout)
    if not os.path.exists(bakepath):
        sys.exit("mkboard: %s was not written" % bakepath)
    img = open(bakepath, "rb").read()
    h = bake_header(img)
    if not h or h[4] != len(img):
        sys.exit("mkboard: %s is not a baked image" % bakepath)
    if h[3] != base:
        sys.exit("mkboard: the image is for 0x%08X, the data image puts it at 0x%08X" % (h[3], base))

    # 3. the data region image
    ofs = base - DATA_ADDR
    data = pak + b"\xff" * (ofs - len(pak)) + img
    room = mkdata.DATA_MAX
    with open(out + ".bin", "wb") as f:
        f.write(data)
    with open(out + ".uf2", "wb") as f:
        f.write(mkdata.to_uf2(data, DATA_ADDR))

    print("data region at 0x%08X, %.2f MB:" % (DATA_ADDR, room / 2**20))
    print("  0x%08X  pak        %9d bytes  %6.2f MB" % (DATA_ADDR, len(pak), len(pak) / 2**20))
    print("  0x%08X  padding    %9d bytes" % (DATA_ADDR + len(pak), ofs - len(pak)))
    print("  0x%08X  qogbake    %9d bytes  %6.2f MB, %d models" % (base, len(img), len(img) / 2**20, h[5]))
    print("  0x%08X  end        %9d bytes  %6.2f MB, %.1f%% of the region"
          % (DATA_ADDR + len(data), len(data), len(data) / 2**20, 100.0 * len(data) / room))
    if len(data) > room:
        print("DOES NOT FIT: %d bytes over. Keep fewer maps (--maps)." % (len(data) - room))
        return 2
    print("wrote %s.bin and %s.uf2 (run on the PC with -basedir %s)" % (out, out, out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
