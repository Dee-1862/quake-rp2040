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

## Baked models: `qogbake.bin` (registered data, local only)

A map's BSP structures (planes, textures, texinfo, surfaces, leafs, nodes, clip hulls, submodels) are built by id's loader at every map load and then only read during play. So are an alias model's header, frame and skin descriptors and texture coordinates (kept in the cache) and a sprite's frames (kept on the hunk). With `QOG_BAKE` the PC builds them once and writes them into an image that is read in place, like the packs. A map load then copies one `model_t` and allocates only the per-frame arrays (visframe stamps, dynamic light bits, efrag lists, the edge cache). An alias model or sprite load copies one `model_t` whose data pointer is in the image, and nothing goes into the cache or the hunk.

**It is a converted copy of id's data.** Make it only from registered or retail paks, keep it under `data\` (git-ignored), and never commit or share it.

Build it with the PC build, once per set of paks and again after any change to the BSP structures (the engine refuses an image from another layout and says so):

```powershell
powershell -NoProfile -File tools\pctest.ps1 -Exe build-pc-dev\Release\quakeog_pc.exe `
    -GameArgs "-nosound -nobake -basedir C:/path/to/data +qog_bake +qog_quit" -Out $env:TEMP\bake.txt
```

- `qog_bake` with no arguments bakes every `maps/*.bsp`, `progs/*.mdl` and `progs/*.spr` in the packs (133 for the registered game: 51 maps including the item boxes `b_*.bsp` and `dm1` to `dm6`, 79 alias models, 3 sprites). `qog_bake start e1m1 progs/player.mdl` bakes only those. The output is `<gamedir>\qogbake.bin`.
- `-nobake` keeps an existing image from being mapped, which `qog_bake` needs. Run it before any map is loaded.
- `qog_bake check` (without `-nobake`) loads every baked model again with id's loader and compares the two field by field (an alias model byte for byte, except the 16 bytes past its last hunk block that id's loader copies along unused). It must print `0 differences`.
- At run time the image is used when it is present and matches. A model whose file changed (address, length or hash) loads the normal way and says so. `-nobake` turns the image off. The image needs 8-bit pixels (`r_pixbytes` 1).

Measured with the registered paks: the 32 single-player maps take 12.1 MB of image (E1M1 392 KB, the largest E4M7 497 KB, episode 1 with `start` 3.05 MB). All 51 maps take 13.2 MB, the alias models and sprites 0.2 MB, the whole image 13,448,304 bytes.

### Image layout

Little-endian, 32-bit, defined in `engine/model.h` (`qogbake_header_t`, `qogbake_entry_t`).

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 4 | magic `QOGB` |
| 4 | 4 | version (2) |
| 8 | 4 | layout: FNV-1a hash of the structure sizes, offsets and `QOG_` layout flags of the build that wrote it |
| 12 | 4 | base: the flash address the image is built for |
| 16 | 4 | size in bytes, header included |
| 20 | 4 | number of models |
| 24 | 4 | directory offset from base |
| 28 | 36 | zero |

After the header, one 16-byte aligned block per model, its `model_t` first. A map's block then holds its planes, texture pointer list, `texture_t` headers, texinfo, surfaces, leafs, nodes, submodels and hull 0 clip nodes. An alias model's holds the block id's loader puts in the cache (header-relative offsets, `flashbase` pointing into the pack), and `model_t.cache.data` points at it. A sprite's holds its `msprite_t` and frames, with `cache.data` pointing at the `msprite_t` as id's sprite loader does. The directory follows the blocks, 64 bytes per model: the model name (40 bytes), the address of its `model_t`, the block size, and the address, length and FNV-1a hash of the file it was baked from (a map's header and plane lump, an alias model's or sprite's first 4 KB).

**Pointers are absolute.** Pointers into the block are resolved for `base`. Pointers into the packs (texture pixels, light, visibility, entities, vertexes, edges, surface edges, clip nodes, mark surfaces) are kept as they are, so an image only fits the packs it was baked against, mapped at the same addresses. The image goes at the first 64 KB boundary after the last pack (`Sys_FlashNext`): `0x13620000` on the PC with registered `pak0` and `pak1`. The engine maps it there (`Sys_FlashMap`) and refuses it if it lands anywhere else.

**On the board** the same rule holds with the board's own pack: bake with the PC build pointed at the trimmed pak that will be flashed (`tools/mkpak.py` lays it out exactly as the PC maps it), and write the image right after that pak in the data region. The RP2040 port does not map the image yet (it only serves `id1/pak0.pak`), so the board build falls back to the normal load. All 51 maps do not fit the 12 MB region next to a pak; one episode does (3.05 MB for `start` and E1).
