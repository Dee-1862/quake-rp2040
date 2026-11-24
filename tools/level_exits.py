#!/usr/bin/env python3
"""Level exits of each map: the "map" of every trigger_changelevel in its
entity lump (Agent AG). Shows which levels an episode's image needs and
where its exits lead outside it (e2m6 and e3m6 lead to start).

    python tools/level_exits.py data/id1/pak0.pak,data/id1/pak1.pak e2m1 e2m6 start

Standard library only."""
import re
import struct
import sys

paks = sys.argv[1].split(",")
files = {}
for p in paks:
    d = open(p, "rb").read()
    _, dofs, dlen = struct.unpack_from("<4sii", d, 0)
    for i in range(dlen // 64):
        n, o, ln = struct.unpack_from("<56sii", d, dofs + 64 * i)
        files[n.split(b"\0")[0].decode()] = d[o:o + ln]
for m in sys.argv[2:]:
    f = files["maps/%s.bsp" % m]
    ofs, ln = struct.unpack_from("<ii", f, 4)
    ents = f[ofs:ofs + ln].decode("latin-1")
    out = []
    for blk in re.findall(r"\{([^}]*)\}", ents):
        kv = dict(re.findall(r'"([^"]*)"\s+"([^"]*)"', blk))
        if kv.get("classname") == "trigger_changelevel":
            out.append(kv.get("map", "?"))
    print(m, " ".join(out))
