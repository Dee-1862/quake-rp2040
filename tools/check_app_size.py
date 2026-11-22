#!/usr/bin/env python3
"""Fail the build if the display app would overlap the game data region.

Usage: check_app_size.py <app.bin> <qog_flashmap.h>
Reads QOG_APP_OFFSET and QOG_DATA_OFFSET from the header so there is one
source of truth. Also prints how much flash headroom is left.
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
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    bin_path, map_path = sys.argv[1], sys.argv[2]
    with open(map_path, encoding="utf-8") as f:
        text = f.read()
    limit = read_define(text, "QOG_DATA_OFFSET") - read_define(text, "QOG_APP_OFFSET")
    size = os.path.getsize(bin_path)
    pct = 100.0 * size / limit
    print("qog: app %s is %d bytes, %.1f%% of the %d-byte app slot"
          % (os.path.basename(bin_path), size, pct, limit))
    if size > limit:
        sys.exit("qog: app overlaps the game data region; move QOG_DATA_OFFSET "
                 "in platform/qog_flashmap.h and tools/mkdata.py, or shrink the app")


if __name__ == "__main__":
    main()
