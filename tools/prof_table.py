#!/usr/bin/env python3
"""QuakeOG (Agent AN): write the board profiler's function table into a linked app.

The app reserves a fixed size array, qog_prof_blob, in its flash image
(port/rp2040/prof_table_rp2040.c) holding only the placeholder magic "QPR0".
After the link this script reads the ELF's function symbols (STT_FUNC, any
section, so the RAM resident functions are in it too) and writes the table
into the same bytes of the .elf, the .bin and the .uf2. Nothing moves: the
array keeps its size.

Table layout (little-endian 32-bit words, offsets in bytes from the start):
  0  magic "QPRF"            6  offset of the end addresses
  1  functions               7  offset of the name offsets
  2  page shift              8  offset of the page index (uint16, pages + 1)
  3  page base address       9  offset of the name pool
  4  pages                  10  functions in flash (the RAM ones follow)
  5  offset of the starts   11  bytes used
Page p holds the index of the last flash function starting at or before
base + (p << shift).

Usage: prof_table.py --elf app.elf [--bin app.bin] [--uf2 app.uf2] [--list]
"""
import argparse
import struct
import sys

BLOB_SYM = "qog_prof_blob"
MAGIC_EMPTY = 0x30525051   # "QPR0"
MAGIC_DONE = 0x46525051    # "QPRF"
MAX_PAGES = 512            # PROF_PAGES in prof_rp2040.c
UF2_MAGIC = (0x0A324655, 0x9E5D5157)


def read_elf(data):
    if data[:4] != b"\x7fELF" or data[4] != 1 or data[5] != 1:
        sys.exit("prof_table: not a 32-bit little-endian ELF")
    shoff, = struct.unpack_from("<I", data, 0x20)
    shentsize, shnum, shstrndx = struct.unpack_from("<HHH", data, 0x2E)
    secs = []
    for i in range(shnum):
        name, typ, flags, addr, off, size, link, info, align, entsize = \
            struct.unpack_from("<IIIIIIIIII", data, shoff + i * shentsize)
        secs.append(dict(name=name, type=typ, flags=flags, addr=addr, off=off,
                         size=size, link=link, entsize=entsize))
    syms = []
    for s in secs:
        if s["type"] != 2:  # SHT_SYMTAB
            continue
        strtab = secs[s["link"]]
        for j in range(s["size"] // 16):
            st_name, value, size, info, other, shndx = struct.unpack_from("<IIIBBH", data, s["off"] + j * 16)
            end = data.index(b"\0", strtab["off"] + st_name)
            name = data[strtab["off"] + st_name:end].decode("ascii", "replace")
            syms.append((name, value, size, info & 0xF, info >> 4, shndx))
    return secs, syms


def file_offset(secs, addr, length):
    for s in secs:
        # SHF_ALLOC with file contents (not SHT_NOBITS)
        if (s["flags"] & 2) and s["type"] != 8 and s["addr"] <= addr and addr + length <= s["addr"] + s["size"]:
            return s["off"] + addr - s["addr"]
    sys.exit("prof_table: no section holds 0x%08x" % addr)


def build(syms, size):
    funcs = {}
    for name, value, fsize, typ, bind, shndx in syms:
        if typ != 2 or fsize == 0 or shndx == 0:   # STT_FUNC, defined, sized
            continue
        start = value & ~1
        if not (0x10000000 <= start < 0x11000000 or 0x20000000 <= start < 0x20042000):
            continue
        old = funcs.get(start)
        # one name per address: a global over a local, then the longer size
        if old is None or (bind == 1 and old[2] != 1) or (bind == old[2] and fsize > old[1]):
            funcs[start] = (name, fsize, bind)
    starts = sorted(funcs)
    flash = [a for a in starts if a < 0x20000000]
    if not flash:
        sys.exit("prof_table: no flash functions")
    base = flash[0]
    top = max(a + funcs[a][1] for a in flash)
    shift = 8
    while ((top - base + (1 << shift) - 1) >> shift) > MAX_PAGES:
        shift += 1
    pages = (top - base + (1 << shift) - 1) >> shift
    page_idx = []
    j = 0
    for p in range(pages + 1):
        at = base + (p << shift)
        while j + 1 < len(flash) and flash[j + 1] <= at:
            j += 1
        page_idx.append(j)
    if len(starts) >= 0x7FFE:
        sys.exit("prof_table: too many functions (%d)" % len(starts))

    pool = bytearray()
    name_offs = []
    for a in starts:
        name_offs.append(len(pool))
        pool += funcs[a][0].encode("ascii", "replace")[:79] + b"\0"
    n = len(starts)
    hdr = 12 * 4
    o_starts = hdr
    o_ends = o_starts + 4 * n
    o_names = o_ends + 4 * n
    o_pages = o_names + 4 * n
    o_pool = (o_pages + 2 * (pages + 1) + 3) & ~3
    used = o_pool + len(pool)
    if used > size:
        sys.exit("prof_table: the table needs %d bytes, the image reserves %d (raise QOG_PROF_TABLE_KB)" % (used, size))
    blob = bytearray(size)
    struct.pack_into("<12I", blob, 0, MAGIC_DONE, n, shift, base, pages,
                     o_starts, o_ends, o_names, o_pages, o_pool, len(flash), used)
    for i, a in enumerate(starts):
        struct.pack_into("<I", blob, o_starts + 4 * i, a)
        struct.pack_into("<I", blob, o_ends + 4 * i, a + funcs[a][1])
        struct.pack_into("<I", blob, o_names + 4 * i, name_offs[i])
    for p, v in enumerate(page_idx):
        struct.pack_into("<H", blob, o_pages + 2 * p, v)
    blob[o_pool:o_pool + len(pool)] = pool
    return bytes(blob), n, len(flash), shift, pages, used, funcs, starts


def check_placeholder(buf, off, what):
    magic, = struct.unpack_from("<I", buf, off)
    if magic not in (MAGIC_EMPTY, MAGIC_DONE):
        sys.exit("prof_table: %s has 0x%08x where the table should start" % (what, magic))


def patch_uf2(path, addr, blob):
    data = bytearray(open(path, "rb").read())
    hit = 0
    for b in range(0, len(data), 512):
        m0, m1, flags, target, nbytes = struct.unpack_from("<IIIII", data, b)
        if (m0, m1) != UF2_MAGIC:
            continue
        lo = max(target, addr)
        hi = min(target + nbytes, addr + len(blob))
        if lo < hi:
            if target <= addr < target + nbytes:
                check_placeholder(data, b + 32 + addr - target, path)
            data[b + 32 + lo - target:b + 32 + hi - target] = blob[lo - addr:hi - addr]
            hit += hi - lo
    if hit != len(blob):
        sys.exit("prof_table: %s holds %d of the table's %d bytes" % (path, hit, len(blob)))
    open(path, "wb").write(data)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--elf", required=True)
    ap.add_argument("--bin")
    ap.add_argument("--uf2")
    ap.add_argument("--list", action="store_true", help="print the table instead of patching")
    a = ap.parse_args()

    elf = bytearray(open(a.elf, "rb").read())
    secs, syms = read_elf(elf)
    blob_sym = [s for s in syms if s[0] == BLOB_SYM]
    if not blob_sym:
        sys.exit("prof_table: %s has no %s (built without QOG_PROF?)" % (a.elf, BLOB_SYM))
    _, addr, size, _, _, _ = blob_sym[0]
    flash_start = [s for s in syms if s[0] == "__flash_binary_start"]
    blob, n, nflash, shift, pages, used, funcs, starts = build(syms, size)
    if a.list:
        for i, s in enumerate(starts):
            print("%5d 0x%08x 0x%08x %s" % (i, s, s + funcs[s][1], funcs[s][0]))
        return

    off = file_offset(secs, addr, size)
    check_placeholder(elf, off, a.elf)
    elf[off:off + size] = blob
    open(a.elf, "wb").write(elf)
    if a.bin:
        if not flash_start:
            sys.exit("prof_table: no __flash_binary_start in the ELF")
        binf = bytearray(open(a.bin, "rb").read())
        boff = addr - (flash_start[0][1] & ~1)
        if boff < 0 or boff + size > len(binf):
            sys.exit("prof_table: the table is outside %s" % a.bin)
        check_placeholder(binf, boff, a.bin)
        binf[boff:boff + size] = blob
        open(a.bin, "wb").write(binf)
    if a.uf2:
        patch_uf2(a.uf2, addr, blob)
    print("prof_table: %d functions (%d in RAM), %d pages of %d bytes, %d of %d bytes at 0x%08x"
          % (n, n - nflash, pages, 1 << shift, used, size, addr))


if __name__ == "__main__":
    main()
