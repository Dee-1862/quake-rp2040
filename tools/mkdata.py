#!/usr/bin/env python3
"""Build a QOGD game-data image and a UF2 that writes it to the display CPU.

Standard library only. The image format is documented in platform/qog_data.h;
keep the two in step.

    # pack your own files (name defaults to the file's basename)
    python tools/mkdata.py -o build/qogdata file1 name2=path/to/file2 ...

    # a synthetic test image for the Phase 2 platform test (no game data)
    python tools/mkdata.py -o build/qogdata --test-pattern

Writes <out>.bin (the raw image) and <out>.uf2 (that image placed at the data
region). Copy the .uf2 to the DISPLAY CPU's RPI-RP2 drive (README, "Writing
the data region"). It never touches the app or its metadata sector.

Never commit or share an image built from id Software's data.
"""
import argparse
import os
import re
import struct
import sys
import zlib

QOGD_MAGIC = 0x44474F51          # "QOGD"
QOGD_VERSION = 1
HEADER_SIZE = 64
ENTRY_SIZE = 32
NAME_LEN = 24

FLASHMAP = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "platform", "qog_flashmap.h")


def flashmap_define(name, path=FLASHMAP):
    """A hex or decimal #define of platform/qog_flashmap.h, so the tools and
    the firmware read one flash map (Agent AC)."""
    with open(path, encoding="utf-8") as f:
        m = re.search(r"#define\s+%s\s+(0x[0-9A-Fa-f]+|\d+)u?\b" % name, f.read())
    if not m:
        raise SystemExit("mkdata: %s not found in %s" % (name, path))
    return int(m.group(1), 0)


XIP_BASE = flashmap_define("QOG_XIP_BASE")
DATA_OFFSET = flashmap_define("QOG_DATA_OFFSET")             # display data region, 0x100000
DATA_MAX = flashmap_define("QOG_SAVE_OFFSET") - DATA_OFFSET  # up to the save slots
MAIN_DATA_OFFSET = flashmap_define("QOG_MAIN_DATA_OFFSET")   # main's server data region, 0x200000
MAIN_DATA_MAX = flashmap_define("QOG_MAIN_FAT_OFFSET") - MAIN_DATA_OFFSET   # up to main's FAT volume

UF2_MAGIC0, UF2_MAGIC1, UF2_MAGIC_END = 0x0A324655, 0x9E5D5157, 0x0AB16F30
UF2_FLAG_FAMILY = 0x00002000
RP2040_FAMILY = 0xE48BFF56
UF2_PAYLOAD = 256


def align_up(n, a):
    return (n + a - 1) & ~(a - 1)


def build_image(entries, label="", align=16):
    """entries: list of (name, bytes). Returns the image bytes."""
    names = set()
    for name, _ in entries:
        raw = name.encode("ascii")
        if not raw or len(raw) > NAME_LEN:
            raise ValueError("lump name %r must be 1..%d ASCII bytes" % (name, NAME_LEN))
        if name in names:
            raise ValueError("duplicate lump name %r" % name)
        names.add(name)

    dir_offset = HEADER_SIZE
    pos = align_up(dir_offset + ENTRY_SIZE * len(entries), align)
    directory = bytearray()
    body = bytearray()
    for name, data in entries:
        directory += struct.pack("<%dsII" % NAME_LEN, name.encode("ascii"), pos, len(data))
        body += data
        pad = align_up(pos + len(data), align) - (pos + len(data))
        body += b"\xff" * pad
        pos += len(data) + pad

    after_header = bytearray(directory)
    after_header += b"\xff" * (align_up(dir_offset + len(directory), align) - (dir_offset + len(directory)))
    after_header += body
    total = HEADER_SIZE + len(after_header)
    if total > DATA_MAX:
        raise ValueError("image is %d bytes; the data region holds %d" % (total, DATA_MAX))

    lab = label.encode("ascii", "replace")[:27]
    head = struct.pack("<8I28s", QOGD_MAGIC, QOGD_VERSION, HEADER_SIZE, len(entries),
                       dir_offset, total, zlib.crc32(after_header) & 0xFFFFFFFF, align, lab)
    head += struct.pack("<I", zlib.crc32(head) & 0xFFFFFFFF)
    assert len(head) == HEADER_SIZE
    return bytes(head + after_header)


def to_uf2(data, base_addr):
    blocks = [data[i:i + UF2_PAYLOAD] for i in range(0, len(data), UF2_PAYLOAD)]
    out = bytearray()
    for n, chunk in enumerate(blocks):
        chunk = chunk + b"\x00" * (UF2_PAYLOAD - len(chunk))
        hdr = struct.pack("<8I", UF2_MAGIC0, UF2_MAGIC1, UF2_FLAG_FAMILY,
                          base_addr + n * UF2_PAYLOAD, UF2_PAYLOAD, n, len(blocks), RP2040_FAMILY)
        block = hdr + chunk + b"\x00" * (476 - UF2_PAYLOAD) + struct.pack("<I", UF2_MAGIC_END)
        assert len(block) == 512
        out += block
    return bytes(out)


def test_pattern(w=320, h=200):
    """A still image plus palette, so the platform test can show something
    read in place from flash without any game data."""
    pal = bytearray()
    for i in range(256):
        pal += bytes(((i * 7) & 0xFF, (i * 3) & 0xFF, 255 - i))
    img = bytearray(w * h)
    for y in range(h):
        for x in range(w):
            checker = ((x // 20) + (y // 20)) & 1
            img[y * w + x] = ((x * 255) // (w - 1)) if checker else ((y * 255) // (h - 1))
    return [("testimg", bytes(img)), ("testpal", bytes(pal))]


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-o", "--out", required=True, help="output path without extension")
    ap.add_argument("--label", default="", help="free text stored in the header")
    ap.add_argument("--align", type=int, default=16, help="lump alignment, power of two")
    ap.add_argument("--test-pattern", action="store_true", help="add testimg/testpal lumps")
    ap.add_argument("--size", default="320x200", help="test pattern size, WxH")
    ap.add_argument("files", nargs="*", help="[name=]path")
    a = ap.parse_args(argv)
    if a.align <= 0 or a.align & (a.align - 1):
        ap.error("--align must be a power of two")

    entries = []
    if a.test_pattern:
        w, h = (int(v) for v in a.size.lower().split("x"))
        entries += test_pattern(w, h)
    for spec in a.files:
        name, _, path = spec.rpartition("=")
        path = path or spec
        name = name or os.path.basename(path)
        with open(path, "rb") as f:
            entries.append((name, f.read()))
    if not entries:
        ap.error("nothing to pack")

    img = build_image(entries, a.label or ("test pattern" if a.test_pattern else ""), a.align)
    os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
    with open(a.out + ".bin", "wb") as f:
        f.write(img)
    with open(a.out + ".uf2", "wb") as f:
        f.write(to_uf2(img, XIP_BASE + DATA_OFFSET))
    print("qogd: %d lumps, %d bytes (%.1f%% of the data region) -> %s.bin, %s.uf2 at 0x%08X"
          % (len(entries), len(img), 100.0 * len(img) / DATA_MAX, a.out, a.out, XIP_BASE + DATA_OFFSET))
    return 0


if __name__ == "__main__":
    sys.exit(main())
