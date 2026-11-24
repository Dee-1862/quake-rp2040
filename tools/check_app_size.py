#!/usr/bin/env python3
"""Fail the build if the display app would overlap the game data region.

Usage: check_app_size.py <app.bin> <qog_flashmap.h> [main]
Reads QOG_APP_OFFSET and QOG_DATA_OFFSET from the header so there is one
source of truth (tools/mkdata.py reads the same header). Also prints how
much flash headroom is left. With "main" the app is the main CPU's (the split game's quake_main, Agent R), which must
stay below the server data region at QOG_MAIN_DATA_OFFSET.
"""
import os
import re
import sys


def read_define(text, name):
    m = re.search(r"#define\s+%s\s+(0x[0-9A-Fa-f]+|\d+)u?" % name, text)
    if not m:
        sys.exit("check_app_size: %s not found in flash map" % name)
    return int(m.group(1), 0)


def main():
    if len(sys.argv) not in (3, 4):
        sys.exit(__doc__)
    bin_path, map_path = sys.argv[1], sys.argv[2]
    with open(map_path, encoding="utf-8") as f:
        text = f.read()
    main = len(sys.argv) == 4 and sys.argv[3] == "main"
    if main:
        limit = read_define(text, "QOG_MAIN_DATA_OFFSET")
        name = "QOG_MAIN_DATA_OFFSET"
    else:
        limit = read_define(text, "QOG_DATA_OFFSET") - read_define(text, "QOG_APP_OFFSET")
        name = "QOG_DATA_OFFSET"
    size = os.path.getsize(bin_path)
    pct = 100.0 * size / limit
    print("qog: app %s is %d bytes, %.1f%% of the %d-byte app slot, %d bytes spare"
          % (os.path.basename(bin_path), size, pct, limit, limit - size))
    if size > limit:
        sys.exit("qog: app %s is %d bytes over its slot and would overwrite the game data region; "
                 "move %s in platform/qog_flashmap.h (the tools read it from there) and rebuild "
                 "the data images, or shrink the app" % (os.path.basename(bin_path), size - limit, name))


if __name__ == "__main__":
    main()
