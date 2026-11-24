#!/usr/bin/env python3
"""Build the board's game-data image: a trimmed pak plus a baked model image.

The display CPU's data region (platform/qog_flashmap.h, 0x10100000) holds

    offset 0          the trimmed pak (tools/mkpak.py), files 16-byte aligned
    next 64 KB        qogbake.bin, the baked maps, alias models and sprites
                      (engine/model.c, qog_bake), built for this address

port/rp2040/sys_rp2040.c serves the first as id1/pak0.pak and finds the
second at the first 64 KB boundary after the pak that holds "QOGB".

The PC build maps a pak at 0x10100000 too and lays it out exactly the way
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

--stub-maps (engine flag QOG_BAKE_STUBS) moves every map into the baked
image: qog_bake -bakestubs copies what a map's block points at in the pak
(light, vis, entities, vertexes, edges, surface edges, clip nodes, mark
surfaces, texture pixels, each distinct texture once for all maps), and the
board's pak keeps a 132-byte stub per map (engine/model.h, qogbake_stub_t).
The pak is built with the maps last, so every other file has the same
address in the full pak the bake reads and in the board's pak with stubs,
and the image goes right after the smaller pak. The full pak is kept as
<out>/src.pak, which qog_bake check needs (-bakesrc <out>/src.pak).

--main (Agent T) also writes the main chip's server data image for the
split game from the same pak, in the same run, so the two copies cannot
drift. The main chip's region (platform/qog_flashmap.h) is 0x10200000 to
0x107FFFFF, between its app and its FAT volume:

    offset 0          the server's pak: progs.dat, every sound of the
                      display's pak for the mixer on main (Agent W, left out
                      with --main-no-sounds), then every map, alias model and
                      sprite of the display's pak as a stub (132 bytes for a
                      map, 16 for a model, engine/model.h)
    next 64 KB        the server's qogbake.bin (qog_bake -bakestubs
                      -bakeserver): planes, nodes, leafs, clip nodes and
                      hulls, submodels, entities and visibility of each map,
                      and each model's model_t, built for that address

Nothing in that image points into a pak, so the PC bakes it from the
display's full pak for the address -bakebase gives, then writes
<out>_main/id1/pak0.pak and qogbake.bin (the PC's server half runs them
with -basedir <out>_main -flashbase 0x10200000), <out>_main.bin and
<out>_main.uf2, a UF2 of that region only.

--display-no-sounds (Agent Y) leaves every sound out of the display's image:
the split game's client never opens one, the mixer on main plays them from
main's image, which keeps them all. --display-no-progs leaves progs.dat out
the same way (QuakeC runs on main). Both need --stub-maps. The files left
out go after every kept file and before the maps in the pak the bake reads,
so the kept files have the same address in it and in the display's pak.

--boot-map NAME (Agent AG) names the level the board starts in: mkboard
writes it as qogboot.txt into the display's pak and main's (the board
client runs qog_boot, the server half goes there when a level exit names
a level the image does not hold, engine/host_cmd.c). The default is start
when --maps has it, else the first of --maps, so one app starts any
episode's image. A board app finds e1m1 when the image has no such file.

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
QOGB_ENTRY = "<40s6I"     # name, model, size, fileaddr, filelen, filehash (+ pad)
QOGB_STUB_IDENT = 0x53474F51   # "QOGS", engine/model.h qogbake_stub_t
BSPVERSION = 29
LUMP_PLANES = 1
# the main chip's server data region, platform/qog_flashmap.h (Agent T),
# read from the header by mkdata (Agent AC)
MAIN_DATA_ADDR = mkdata.XIP_BASE + mkdata.MAIN_DATA_OFFSET   # QOG_MAIN_DATA_OFFSET
MAIN_DATA_MAX = mkdata.MAIN_DATA_MAX                          # up to QOG_MAIN_FAT_OFFSET
# what the server opens besides its models: QuakeC, and gfx/pop.lmp, by
# which COM_CheckRegistered sets the "registered" cvar that QuakeC reads
SERVER_FILES = ["progs.dat", "gfx/pop.lmp"]
# the level the data image starts in (Agent AG, engine/common.c COM_BootMap)
BOOT_FILE = "qogboot.txt"
QOGB_SERVER_LAYOUT = 0x53525652                  # engine/model.h


def boot_map_name(maps, boot):
    """The boot level: --boot-map, else start when the image keeps it, else
    the first level of --maps, or start when every manifest is kept."""
    if boot:
        return boot
    if not maps:
        return "start"
    return "start" if "start" in maps else maps[0]


def with_boot_file(pak, name):
    """The same pak with qogboot.txt naming the boot level, put before the
    first map so the files before it keep their addresses and the maps stay
    last (--stub-maps)."""
    ents = [(n, d) for n, _, d in pak_entries(pak) if n != BOOT_FILE]
    if "maps/%s.bsp" % name not in [n for n, _ in ents]:
        sys.exit("mkboard: the boot level %s is not in the pak (--maps, --boot-map)" % name)
    at = next((i for i, (n, _) in enumerate(ents) if mkpak.is_map(n)), len(ents))
    return mkpak.build_pak(ents[:at] + [(BOOT_FILE, (name + "\n").encode("ascii"))] + ents[at:])


def is_model(name):
    """A file qog_bake bakes: maps/*.bsp, progs/*.mdl, progs/*.spr."""
    return mkpak.is_map(name) or (name.startswith("progs/") and name[-4:] in (".mdl", ".spr"))


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


def fnv(data, h=0x811C9DC5):
    """FNV-1a, as Mod_Fnv in engine/model.c."""
    for c in data:
        h = ((h ^ c) * 0x01000193) & 0xFFFFFFFF
    return h


def file_hash(f):
    """Mod_FileHash: a map's header and plane lump, or a file's first 4 KB."""
    if len(f) < 124 or struct.unpack_from("<i", f, 0)[0] != BSPVERSION:
        return fnv(f[:4096])
    ofs, ln = struct.unpack_from("<ii", f, 4 + 8 * LUMP_PLANES)
    return fnv(f[ofs:ofs + ln], fnv(f[:124]))


def make_stub(f):
    """The qogbake_stub_t that stands for map f in the board's pak (Bake_MakeStub)."""
    return struct.pack("<I", QOGB_STUB_IDENT) + f[4:124] + struct.pack("<II", len(f), file_hash(f))


def make_mstub(f):
    """The qogbake_mstub_t that stands for alias model or sprite f in the
    server's pak (Bake_Model with -bakeserver writes its hash)."""
    return struct.pack("<IiII", QOGB_STUB_IDENT, struct.unpack_from("<i", f, 0)[0], len(f), file_hash(f))


def pak_entries(pak):
    """[(name, filepos, data)] of a pak, in directory order."""
    _, dofs, dlen = struct.unpack_from("<4sii", pak, 0)
    out = []
    for i in range(dlen // 64):
        n, o, ln = struct.unpack_from("<56sii", pak, dofs + 64 * i)
        out.append((n.split(b"\0")[0].decode("latin-1"), o, pak[o:o + ln]))
    return out


def stub_pak(pak, drop=()):
    """The board's pak: every map replaced by its stub, the files in drop
    left out (Agent Y). Checks that no other file moves, so the image's
    pointers into the pak stay right."""
    ents = [e for e in pak_entries(pak) if e[0] not in drop]
    new = mkpak.build_pak([(n, make_stub(d) if mkpak.is_map(n) else d) for n, _, d in ents])
    for (n, o, _), (_, o2, _) in zip(ents, pak_entries(new)):
        if not mkpak.is_map(n) and o != o2:
            sys.exit("mkboard: %s moves when the maps become stubs (build the pak with the maps last)" % n)
    return new


def display_drop(names, sounds, progs):
    """The files the display's image leaves out (Agent Y): the sounds, which
    the mixer on main plays, and progs.dat, which QuakeC on main runs."""
    return {n for n in names if (sounds and is_sound(n)) or (progs and n == "progs.dat")}


def drop_last(pak, drop):
    """The same pak with the files in drop moved after every other file but
    before the maps, each group in its order, so the kept files keep their
    addresses when the dropped ones and the maps go."""
    ents = pak_entries(pak)
    rest = [(n, d) for n, _, d in ents if n not in drop and not mkpak.is_map(n)]
    out = [(n, d) for n, _, d in ents if n in drop]
    maps = [(n, d) for n, _, d in ents if mkpak.is_map(n) and n not in drop]
    return mkpak.build_pak(rest + out + maps)


def bake_dir(img):
    """[(name, fileaddr, filelen, filehash)] of a baked image."""
    h = bake_header(img)
    return [(e[0].split(b"\0")[0].decode("latin-1"), e[3], e[4], e[5])
            for e in (struct.unpack_from(QOGB_ENTRY, img, h[6] + 64 * i) for i in range(h[5]))]


def bake_header(img):
    if len(img) < 64:
        return None
    h = struct.unpack_from(QOGB_HEADER, img, 0)
    return h if h[0] == QOGB_MAGIC else None


def run_bake(exe, basedir, base, timeout, stubs=False, extra="", flashbase=DATA_ADDR):
    """qog_bake on the PC: hidden, silent, through tools/pctest.ps1. The PC
    maps the packs at flashbase, the region's address on the board (Agent
    AC: given every time, so a PC build with another default still bakes
    for the board's flash map)."""
    args = "-nosound -nobake -flashbase 0x%08X -basedir %s" % (flashbase, basedir.replace("\\", "/"))
    if stubs:
        args += " -bakestubs"
    if extra:
        args += " " + extra
    if base:
        args += " -bakebase 0x%08X" % base
    args += " +qog_bake +qog_quit"
    log = os.path.join(basedir, "bake.txt")
    cmd = ["powershell", "-NoProfile", "-File", os.path.join(ROOT, "tools", "pctest.ps1"),
           "-Exe", exe, "-GameArgs", args, "-Out", log, "-TimeoutSec", str(timeout)]
    subprocess.run(cmd, check=True, cwd=ROOT)
    tail = open(log, errors="replace").read().strip().splitlines()[-1:] if os.path.exists(log) else []
    print("bake: %s" % (tail[0] if tail else "(no log)"))


def is_sound(name):
    return name.startswith("sound/") and name.endswith(".wav")


def build_main(full, out, exe, timeout, no_bake, layout, sounds=True):
    """The main chip's server data image (--main) from the display's full
    pak (every file before stubs): returns 0 when it fits, 2 when not.
    With sounds (Agent W), every sound of the display's pak too, for the
    mixer that runs on main (QOG_SND_HOST): the same files, already in the
    11025 Hz 8-bit mono form mkpak converts to, so they play in place."""
    mout = out + "_main"
    if len(mout) > 80:
        sys.exit("mkboard: use a shorter output path (%s is %d characters, at most 80)" % (mout, len(mout)))
    gamedir = os.path.join(mout, "id1")
    os.makedirs(gamedir, exist_ok=True)
    ents = pak_entries(full)
    names = [n for n, _, _ in ents]
    missing = [n for n in SERVER_FILES[:1] if n not in names]   # pop.lmp: registered data only
    if missing:
        sys.exit("mkboard: the pak has no %s for the server" % " ".join(missing))
    # the server's files, the sounds (Agent W), then a stub for each model
    keep = [(n, d) for n, _, d in ents if n in SERVER_FILES or n == BOOT_FILE]
    snd = [(n, d) for n, _, d in ents if sounds and is_sound(n)]
    for n, d in snd:
        w = mkpak.wav_info(d)
        if not w or w[:3] != (1, mkpak.MIX_RATE, 8):
            sys.exit("mkboard: %s is not 11025 Hz 8-bit mono in the display's pak" % n)
    keep += snd + [(n, d) for n, _, d in ents if is_model(n)]
    pak = mkpak.build_pak([(n, make_stub(d) if mkpak.is_map(n) else make_mstub(d) if is_model(n) else d)
                           for n, d in keep])
    if pc_layout(pak) != len(pak):
        sys.exit("mkboard: the server's pak is not laid out the way the PC maps it")
    base = MAIN_DATA_ADDR + align(len(pak), 0x10000)

    # Nothing in the server's image points into a pak (maps detached, models
    # are a model_t alone), so the PC bakes it from the display's full pak,
    # which also holds what the PC needs to start, for the address -bakebase
    # gives. The models are the same files, so their stubs match.
    pakpath = os.path.join(gamedir, "pak0.pak")
    bakepath = os.path.join(gamedir, "qogbake.bin")
    if not no_bake:
        with open(pakpath, "wb") as f:
            f.write(full)
        if os.path.exists(bakepath):
            os.remove(bakepath)
        run_bake(exe, mout, base, timeout, True, "-bakeserver", MAIN_DATA_ADDR)
    if not os.path.exists(bakepath):
        sys.exit("mkboard: %s was not written" % bakepath)
    img = open(bakepath, "rb").read()
    h = bake_header(img)
    if not h or h[4] != len(img):
        sys.exit("mkboard: %s is not a baked image" % bakepath)
    if h[3] != base:
        sys.exit("mkboard: the server's image is for 0x%08X, the data image puts it at 0x%08X" % (h[3], base))
    if h[2] != layout ^ QOGB_SERVER_LAYOUT:
        sys.exit("mkboard: %s is not the server's image of the build that baked the display's" % bakepath)
    baked = {n: (addr, ln, hs) for n, addr, ln, hs in bake_dir(img)}
    for n, _, d in pak_entries(pak):
        if is_model(n) and baked.get(n) != (0, len(d), fnv(d)):
            sys.exit("mkboard: %s is not in the server's image as a stub (see %s)" % (n, os.path.join(mout, "bake.txt")))
    with open(pakpath, "wb") as f:
        f.write(pak)          # the server's pak, the PC runs it with -basedir <out>_main

    ofs = base - MAIN_DATA_ADDR
    data = pak + b"\xff" * (ofs - len(pak)) + img
    data += b"\xff" * (-len(data) % 4096)    # whole 4 KB sectors
    with open(mout + ".bin", "wb") as f:
        f.write(data)
    with open(mout + ".uf2", "wb") as f:
        f.write(mkdata.to_uf2(data, MAIN_DATA_ADDR))
    models = sum(1 for n in names if is_model(n))
    print("main chip server data region at 0x%08X, %.2f MB:" % (MAIN_DATA_ADDR, MAIN_DATA_MAX / 2**20))
    print("  0x%08X  pak        %9d bytes  %6.2f MB, %s and %d model stubs"
          % (MAIN_DATA_ADDR, len(pak), len(pak) / 2**20, " ".join(n for n in SERVER_FILES if n in names), models))
    print("            (the full models took %d bytes)" % sum(len(d) for n, d in keep if is_model(n)))
    print("            %d sounds, %d bytes, for the mixer on main" % (len(snd), sum(len(d) for _, d in snd)))
    if BOOT_FILE in names:
        print("            %s: %s" % (BOOT_FILE, dict((n, d) for n, _, d in ents)[BOOT_FILE].decode("ascii").strip()))
    print("  0x%08X  qogbake    %9d bytes  %6.2f MB, %d models, the server's image"
          % (base, len(img), len(img) / 2**20, h[5]))
    print("  0x%08X  end        %9d bytes  %6.2f MB, %.1f%% of the region"
          % (MAIN_DATA_ADDR + len(data), len(data), len(data) / 2**20, 100.0 * len(data) / MAIN_DATA_MAX))
    if len(data) > MAIN_DATA_MAX:
        print("THE SERVER IMAGE DOES NOT FIT: %d bytes over." % (len(data) - MAIN_DATA_MAX))
        return 2
    print("wrote %s.bin and %s.uf2 (0x%08X to 0x%08X, run on the PC with -basedir %s -flashbase 0x%08X)"
          % (mout, mout, MAIN_DATA_ADDR, MAIN_DATA_ADDR + len(data) - 1, mout, MAIN_DATA_ADDR))
    return 0


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
    ap.add_argument("--stub-maps", action="store_true",
                    help="move the maps into the baked image, a stub for each in the pak (QOG_BAKE_STUBS)")
    ap.add_argument("--main", action="store_true",
                    help="also write the main chip's server data image <out>_main (the split game)")
    ap.add_argument("--main-no-sounds", action="store_true",
                    help="leave the sounds out of the main chip's image (a build with QOG_SOUND off)")
    ap.add_argument("--display-no-sounds", action="store_true",
                    help="leave the sounds out of the display's image (the split game, main keeps them)")
    ap.add_argument("--display-no-progs", action="store_true",
                    help="leave progs.dat out of the display's image (the split game, main keeps it)")
    ap.add_argument("--boot-map", help="level the board starts in, written as qogboot.txt "
                    "(default: start when --maps has it, else the first of --maps)")
    ap.add_argument("-o", "--out", required=True, help="output path without extension, also the PC basedir")
    a = ap.parse_args(argv)
    if (a.display_no_sounds or a.display_no_progs) and not a.stub_maps:
        sys.exit("mkboard: --display-no-sounds and --display-no-progs need --stub-maps")

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
    if a.stub_maps:
        mk += ["--maps-last"]
    mkpak.main(mk)
    pak = open(pakpath + ".pak", "rb").read()
    boot = boot_map_name(a.maps, a.boot_map)
    pak = with_boot_file(pak, boot)    # Agent AG: the level the board starts in
    with open(pakpath + ".pak", "wb") as f:
        f.write(pak)
    if pc_layout(pak) != len(pak):
        sys.exit("mkboard: the pak is not laid out the way the PC maps it")
    drop = display_drop([n for n, _, _ in pak_entries(pak)], a.display_no_sounds, a.display_no_progs)
    if drop:
        # Agent Y: the files the display leaves out go before the maps
        pak = drop_last(pak, drop)
        with open(pakpath + ".pak", "wb") as f:
            f.write(pak)
        if pc_layout(pak) != len(pak):
            sys.exit("mkboard: the pak is not laid out the way the PC maps it")
    full = pak            # every file whole, for the main chip's image (--main)
    srcpath = os.path.join(out, "src.pak")
    if a.stub_maps:
        # the bake reads the full pak, the board gets the one with stubs
        src = pak
        with open(srcpath, "wb") as f:
            f.write(src)
        maps_bytes = sum(len(d) for n, _, d in pak_entries(src) if mkpak.is_map(n))
        pak = stub_pak(src, drop)
        if pc_layout(pak) != len(pak):
            sys.exit("mkboard: the pak with stubs is not laid out the way the PC maps it")

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
        run_bake(os.path.abspath(a.exe), out, base if a.stub_maps else a.bake_base, a.timeout, a.stub_maps)
    if not os.path.exists(bakepath):
        sys.exit("mkboard: %s was not written" % bakepath)
    img = open(bakepath, "rb").read()
    h = bake_header(img)
    if not h or h[4] != len(img):
        sys.exit("mkboard: %s is not a baked image" % bakepath)
    if h[3] != base:
        sys.exit("mkboard: the image is for 0x%08X, the data image puts it at 0x%08X" % (h[3], base))
    if a.stub_maps:
        # every map in the pak must be in the image, baked against its stub
        baked = {n: (addr, ln, hs) for n, addr, ln, hs in bake_dir(img)}
        for n, _, d in pak_entries(pak):
            if mkpak.is_map(n) and baked.get(n) != (0, len(d), fnv(d)):
                sys.exit("mkboard: %s is not in the image as a detached map (see bake.txt)" % n)
        with open(pakpath + ".pak", "wb") as f:
            f.write(pak)          # the board's pak, the PC runs it with -basedir <out>

    # 3. the data region image
    ofs = base - DATA_ADDR
    data = pak + b"\xff" * (ofs - len(pak)) + img
    room = mkdata.DATA_MAX
    with open(out + ".bin", "wb") as f:
        f.write(data)
    with open(out + ".uf2", "wb") as f:
        f.write(mkdata.to_uf2(data, DATA_ADDR))

    print("data region at 0x%08X, %.2f MB:" % (DATA_ADDR, room / 2**20))
    print("  0x%08X  pak        %9d bytes  %6.2f MB, starts in %s (%s)" % (DATA_ADDR, len(pak), len(pak) / 2**20, boot, BOOT_FILE))
    if a.stub_maps:
        print("            maps are stubs (the full pak's maps took %d bytes, %.2f MB, kept in %s)"
              % (maps_bytes, maps_bytes / 2**20, srcpath))
    if drop:
        print("            left out for main: %d files, %d bytes (%s)"
              % (len(drop), sum(len(d) for n, _, d in pak_entries(full) if n in drop),
                 ", ".join(w for w, on in (("sounds", a.display_no_sounds), ("progs.dat", a.display_no_progs)) if on)))
    print("  0x%08X  padding    %9d bytes" % (DATA_ADDR + len(pak), ofs - len(pak)))
    print("  0x%08X  qogbake    %9d bytes  %6.2f MB, %d models" % (base, len(img), len(img) / 2**20, h[5]))
    print("  0x%08X  end        %9d bytes  %6.2f MB, %.1f%% of the region"
          % (DATA_ADDR + len(data), len(data), len(data) / 2**20, 100.0 * len(data) / room))
    rc = 0
    if len(data) > room:
        print("DOES NOT FIT: %d bytes over. Keep fewer maps (--maps)." % (len(data) - room))
        rc = 2
    else:
        print("wrote %s.bin and %s.uf2 (run on the PC with -basedir %s)" % (out, out, out))

    # 4. the main chip's server image from the same files (Agent T)
    if a.main:
        if not a.exe and not a.no_bake:
            sys.exit("mkboard: --exe is needed to bake (or --no-bake)")
        rc = max(rc, build_main(full, out, os.path.abspath(a.exe) if a.exe else None,
                                a.timeout, a.no_bake, h[2], not a.main_no_sounds))
    return rc


if __name__ == "__main__":
    sys.exit(main())
