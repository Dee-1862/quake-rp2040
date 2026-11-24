"""Deepest node path of each hull of a map's world model, the recursion bound
of SV_RecursiveHullCheck (Agent AC, docs/lessons.md L75). Standard library only.

    python tools/hull_depth.py data/id1/pak0.pak,data/id1/pak1.pak e1m3 e1m4

The depth counts the root, the recursion enters one more frame for the leaf.
"""
import struct
import sys

LUMP_NODES, LUMP_CLIPNODES, LUMP_MODELS = 5, 9, 14


def pak_files(path):
    d = open(path, "rb").read()
    _, dofs, dlen = struct.unpack_from("<4sii", d, 0)
    for i in range(dlen // 64):
        n, o, ln = struct.unpack_from("<56sii", d, dofs + 64 * i)
        yield n.split(b"\0")[0].decode(), d[o:o + ln]


def depth_of(children, root):
    # iterative, children(n) -> list of child node numbers (>= 0), leaves negative
    best = 0
    stack = [(root, 1)]
    while stack:
        n, dep = stack.pop()
        best = max(best, dep)
        for c in children(n):
            if c >= 0:
                stack.append((c, dep + 1))
    return best


def main():
    want = set(sys.argv[2:])
    for paks in sys.argv[1].split(","):
        for name, f in pak_files(paks):
            m = name[5:-4] if name.startswith("maps/") and name.endswith(".bsp") else None
            if not m or (want and m not in want):
                continue
            lumps = [struct.unpack_from("<ii", f, 4 + 8 * i) for i in range(15)]
            no, nl = lumps[LUMP_NODES]
            nodes = [struct.unpack_from("<i8h2H", f, no + 24 * i) for i in range(nl // 24)]
            co, cl = lumps[LUMP_CLIPNODES]
            clip = [struct.unpack_from("<i2h", f, co + 8 * i) for i in range(cl // 8)]
            mo, _ = lumps[LUMP_MODELS]
            headnode = struct.unpack_from("<9f4i", f, mo)[9:13]
            d0 = depth_of(lambda n: [nodes[n][1], nodes[n][2]], headnode[0])
            d1 = depth_of(lambda n: [clip[n][1], clip[n][2]], headnode[1])
            d2 = depth_of(lambda n: [clip[n][1], clip[n][2]], headnode[2])
            print("%-6s hull0 %3d  hull1 %3d  hull2 %3d" % (m, d0, d1, d2))


if __name__ == "__main__":
    main()
