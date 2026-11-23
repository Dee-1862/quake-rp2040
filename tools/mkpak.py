#!/usr/bin/env python3
"""Build the board's trimmed, aligned game-data pak from asset manifests.

The PC build writes one manifest per map (port/pc/manifest_pc.c,
`qog_manifest`): the models and sounds the map precaches plus every pack file
opened while it ran. This tool keeps only those files (plus the common set),
converts sounds the mixer cannot play in place (anything but 11025 Hz 8-bit
mono) to that format, aligns every file to 16 bytes so the RP2040 can read it
in place, and checks the result against the flash data region.

    python tools/mkpak.py --pak data/id1/pak0.pak \
        --manifests build-pc-dev/manifests -o build/qogpak
    python tools/mkpak.py ... --maps start e1m1        # a subset of maps
    python tools/mkpak.py --pak data/id1/pak0.pak --pak data/id1/pak1.pak ...
                                                       # registered: later paks win, like Quake's search order

Writes <out>.pak (for the PC: put it in a game dir) and <out>.uf2 (the same
bytes at the board's data region). Never commit or share the output: it is
id Software's data.

Standard library only.
"""
import argparse
import fnmatch
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mkdata  # noqa: E402  (UF2 writer and the flash map constants)

ALIGN = 16
MIX_RATE = 11025
# Read by the engine at startup or by name from code, not via precache lists.
ALWAYS = ["gfx.wad", "gfx/*.lmp", "progs.dat", "quake.rc", "default.cfg"]


def read_pak(path):
    d = open(path, "rb").read()
    magic, dofs, dlen = struct.unpack_from("<4sii", d, 0)
    if magic != b"PACK":
        sys.exit("%s is not a pak file" % path)
    files = {}
    order = []
    for i in range(dlen // 64):
        n, o, l = struct.unpack_from("<56sii", d, dofs + 64 * i)
        name = n.split(b"\0")[0].decode("latin-1")
        files[name] = d[o:o + l]
        order.append(name)
    return files, order


def read_paks(paths):
    """Several paks merged the way Quake searches them: a file in a later
    pak (pak1) wins over the same name in an earlier one (pak0). The order
    is the first pak's, then the names only later paks have."""
    files, order = {}, []
    for path in paths:
        f, o = read_pak(path)
        for name in o:
            if name not in files:
                order.append(name)
            files[name] = f[name]
    return files, order


def read_manifest(path):
    out = {"map": None, "files": set()}
    for line in open(path, encoding="latin-1"):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        kind, _, name = line.partition(" ")
        if kind == "map":
            out["map"] = name
        elif kind in ("model", "sound", "file"):
            out["files"].add(name)
    return out


def wav_info(data):
    """(channels, rate, bits, data_ofs, data_len, loopstart) or None."""
    if data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        return None
    p, fmt, dat, loop = 12, None, None, -1
    while p + 8 <= len(data):
        cid, size = data[p:p + 4], struct.unpack_from("<I", data, p + 4)[0]
        body = p + 8
        if cid == b"fmt ":
            _, ch, rate = struct.unpack_from("<HHI", data, body)
            bits = struct.unpack_from("<H", data, body + 14)[0]
            fmt = (ch, rate, bits)
        elif cid == b"data":
            dat = (body, size)
        elif cid == b"cue ":
            # first cue point's sample offset (same field id's GetWavinfo reads)
            loop = struct.unpack_from("<I", data, body + 24)[0]
        p = body + size + (size & 1)
    if not fmt or not dat:
        return None
    return fmt + dat + (loop,)


def convert_wav(data):
    """Resample to 11025 Hz 8-bit mono, keeping the loop point. id's
    ResampleSfx does the same at load time with nearest-sample stepping."""
    ch, rate, bits, dofs, dlen, loop = wav_info(data)
    width = bits // 8
    n_in = dlen // (width * ch)
    step = rate / MIX_RATE
    n_out = int(n_in / step)
    out = bytearray(n_out)
    for i in range(n_out):
        s = int(i * step)
        if width == 2:
            v = struct.unpack_from("<h", data, dofs + s * 2 * ch)[0] >> 8
            out[i] = (v + 128) & 0xFF
        else:
            out[i] = data[dofs + s * ch]
    chunks = struct.pack("<4sIHHIIHH", b"fmt ", 16, 1, 1, MIX_RATE, MIX_RATE, 1, 8)
    chunks += struct.pack("<4sI", b"data", n_out) + bytes(out) + (b"\0" if n_out & 1 else b"")
    if loop >= 0:
        lp = int(loop / step)
        chunks += struct.pack("<4sII", b"cue ", 28, 1) + struct.pack("<IIIIII", 0, 0, 0x61746164, 0, 0, lp)
    return b"RIFF" + struct.pack("<I", 4 + len(chunks)) + b"WAVE" + chunks


def build_pak(entries):
    """entries: list of (name, bytes) -> aligned pak bytes."""
    body = bytearray(b"\0" * ALIGN)
    directory = bytearray()
    for name, data in entries:
        pos = len(body)
        body += data
        body += b"\0" * ((-len(body)) % ALIGN)
        directory += struct.pack("<56sii", name.encode("latin-1"), pos, len(data))
    dirofs = len(body)
    body += directory
    struct.pack_into("<4sii", body, 0, b"PACK", dirofs, len(directory))
    return bytes(body)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--pak", required=True, action="append",
                    help="source pak, repeat for more (a later pak overrides an earlier one, as in Quake)")
    ap.add_argument("--manifests", required=True, help="directory of qog_manifest outputs")
    ap.add_argument("--maps", nargs="*", help="maps to keep (default: every manifest)")
    ap.add_argument("--demos", action="store_true", help="keep demo*.dem for the attract loop")
    ap.add_argument("--no-uf2", action="store_true", help="write only the .pak (tools/mkboard.py)")
    ap.add_argument("-o", "--out", required=True, help="output path without extension")
    a = ap.parse_args(argv)

    files, order = read_paks(a.pak)
    mans = {}
    common = set()
    for fn in sorted(os.listdir(a.manifests)):
        if not fn.endswith(".txt"):
            continue
        m = read_manifest(os.path.join(a.manifests, fn))
        if m["map"]:
            mans[m["map"]] = m["files"]
        else:
            common |= m["files"]
    maps = a.maps or sorted(mans)
    missing = [m for m in maps if m not in mans]
    if missing:
        sys.exit("no manifest for: %s" % " ".join(missing))

    keep = set(common)
    for pat in ALWAYS + (["demo*.dem"] if a.demos else []):
        keep |= {n for n in files if fnmatch.fnmatch(n, pat)}
    for m in maps:
        keep |= mans[m]
    unknown = sorted(n for n in keep if n not in files)
    keep = {n for n in keep if n in files}

    entries, converted = [], []
    for name in order:            # keep id's order: related files stay together
        if name not in keep:
            continue
        data = files[name]
        if name.endswith(".wav"):
            info = wav_info(data)
            if info and (info[0], info[1], info[2]) != (1, MIX_RATE, 8):
                new = convert_wav(data)
                converted.append((name, len(data), len(new)))
                data = new
        entries.append((name, data))

    pak = build_pak(entries)
    budget = mkdata.DATA_MAX
    os.makedirs(os.path.dirname(os.path.abspath(a.out)) or ".", exist_ok=True)
    with open(a.out + ".pak", "wb") as f:
        f.write(pak)
    if not a.no_uf2:
        with open(a.out + ".uf2", "wb") as f:
            f.write(mkdata.to_uf2(pak, mkdata.XIP_BASE + mkdata.DATA_OFFSET))

    total_in = sum(len(v) for v in files.values())
    print("maps: %s" % " ".join(maps))
    print("kept %d of %d files: %.2f MB of %.2f MB" % (len(entries), len(files), len(pak) / 2**20, total_in / 2**20))
    for name, before, after in converted:
        print("  converted %s to 11025 Hz 8-bit: %d -> %d bytes" % (name, before, after))
    if unknown:
        print("  listed but not in the pak (ignored): %s" % ", ".join(unknown[:8]))
    by_kind = {}
    for name, data in entries:
        kind = name.split("/")[0] if "/" in name else name.rsplit(".", 1)[-1]
        by_kind[kind] = by_kind.get(kind, 0) + len(data)
    print("  by folder: " + ", ".join("%s %.2f MB" % (k, v / 2**20) for k, v in sorted(by_kind.items(), key=lambda x: -x[1])))
    fits = len(pak) <= budget
    print("flash data region %.2f MB: %s (%.1f%% used)" % (budget / 2**20, "FITS" if fits else "DOES NOT FIT", 100.0 * len(pak) / budget))
    print("wrote %s.pak%s" % (a.out, "" if a.no_uf2 else " and %s.uf2" % a.out))
    return 0 if fits else 2


if __name__ == "__main__":
    sys.exit(main())
