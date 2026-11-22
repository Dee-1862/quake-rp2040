# Getting the game data (pak0.pak)

The engine is GPL, but Quake's **game data** (maps, textures, sounds, models, the compiled game logic `progs.dat`) belongs to id Software. Episode 1 was released as **shareware** that anyone may download and use for free, but not redistribute modified. So:

- **Never commit `pak0.pak` or anything made from it.** `.gitignore` already blocks `*.pak`, `data/` and `qogdata*`.
- Each person who builds QuakeOG supplies their own copy. Our tools convert it on their PC.

## What you need

One file, `pak0.pak`, the Episode 1 data. It's the same in the shareware release and in the full game (the full game adds `pak1.pak` for episodes 2 to 4, which we don't need for Tier 1).

| | |
| --- | --- |
| File | `pak0.pak` |
| Expected size | 18,689,235 bytes (version 1.06) |
| Expected MD5 | `5906e5998fc3d896ddaf5e6a62e03abb` (version 1.06) |

Both values are the commonly published ones for v1.06. Check yours with the commands below. A different size means a different version, which might still work, but say so before we build on it.

## Three legitimate ways to get it

**A. You own Quake on Steam or GOG (easiest).** Open the install folder and copy `id1\PAK0.PAK`.
- Steam: right-click Quake → Manage → Browse local files. The original file is in `id1\`. **Use that one, not the one in `rerelease\`**, which is the 2021 enhanced edition with different data.
- GOG: the same `id1\` folder inside the install directory.

**B. The official shareware release, `quake106.zip`.** id distributed it freely in 1996, and it's mirrored widely (for example on archive.org; search for "quake106.zip"). Inside the zip, the data sits in an archive called `resource.1`:
1. Unzip `quake106.zip`.
2. Open `resource.1` with **7-Zip** (it's an LHA archive and 7-Zip reads it), and extract `id1\pak0.pak`.

**C. An old Quake CD.** Copy `id1\pak0.pak` from it.

## Where to put it

```
quake-2040\
  data\
    id1\
      pak0.pak      <- here (data\ is git-ignored)
```

Check it in PowerShell:

```powershell
Get-Item data\id1\pak0.pak | Select-Object Length
Get-FileHash -Algorithm MD5 data\id1\pak0.pak
```

## Run it on the PC

```powershell
cmake -S pc -B build-pc -G "Visual Studio 17 2022" -A Win32
cmake --build build-pc --config Release
build-pc\Release\quakeog_pc.exe                 # attract-mode demos
build-pc\Release\quakeog_pc.exe +map e1m1       # straight into the first level
```

Keys: the normal Quake keys (arrows, Ctrl fire, Space jump, Esc menu, the backtick key for the console). **F12** switches to board mode, where keys 1–5 are the five buttons (gray, yellow, green, blue, red) and follow the board's control map.

## Later: getting it onto the board

`pak0.pak` is 18.7 MB and the board's data region holds 11.9 MB. The converter planned for Phase 3–6 (`tools/`) will strip what Episode 1 doesn't need, downsample sounds and restructure maps for reading in place from flash, the way MG24Quake's converter does. Then `tools/mkdata.py` turns the result into a UF2. Until then the data only lives on the PC.
