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
- `-bakefile <path>` reads (and `qog_bake` writes) another image instead of `<gamedir>\qogbake.bin`, for example a test image that must not replace the shared one. `-bakebase <address>` builds the image for a later 64 KB aligned address (the PC then maps it there).
- `-bakestubs` (with `QOG_BAKE_STUBS`) copies everything a map points at in the pack into the image, each distinct texture's pixels once, and records the map by its 132-byte stub (see "Maps in the image, stubs in the pak" below). `qog_bake check -bakesrc <pak>` then reads the full maps from that pak.
- At run time the image is used when it is present and matches. A model whose file changed (address, length or hash) loads the normal way and says so. `-nobake` turns the image off. The image needs 8-bit pixels (`r_pixbytes` 1).

Measured with the registered paks: the 32 single-player maps take 12.1 MB of image (E1M1 392 KB, the largest E4M7 497 KB, episode 1 with `start` 3.05 MB). All 51 maps take 13.2 MB, the alias models and sprites 0.2 MB, the whole image 13,448,304 bytes.

With `QOG_MOD_FLASH` (on in both builds since Agent I) each map's block also holds its inline models, so the whole image is 14,425,024 bytes (E1M1 414 KB, `start` with episode 1 3.32 MB). The `model_t` of a baked model is then used in place in the image: the model table in RAM keeps only a name and a pointer per model of the current level, and a map's per-frame arrays are found through `model_t.frameslot` (0 for a world map, 1 to 15 for the item boxes `b_*.bsp`, numbered by `qog_bake`).

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

After the header, one 16-byte aligned block per model, its `model_t` first. With `QOG_MOD_FLASH` a map's block starts with one `model_t` per submodel instead: the world finished the way id's `Mod_LoadBrushModel` leaves it (submodel 0's bounds and hulls, `numframes` 2), then `*1`, `*2` and so on, and the directory points at the first. A map's block then holds its planes, texture pointer list, `texture_t` headers, texinfo, surfaces, leafs, nodes, submodels and hull 0 clip nodes. An alias model's holds the block id's loader puts in the cache (header-relative offsets, `flashbase` pointing into the pack), and `model_t.cache.data` points at it. A sprite's holds its `msprite_t` and frames, with `cache.data` pointing at the `msprite_t` as id's sprite loader does. The directory follows the blocks, 64 bytes per model: the model name (40 bytes), the address of its `model_t`, the block size, and the address, length and FNV-1a hash of the file it was baked from (a map's header and plane lump, an alias model's or sprite's first 4 KB).

**Pointers are absolute.** Pointers into the block are resolved for `base`. Pointers into the packs (texture pixels, light, visibility, entities, vertexes, edges, surface edges, clip nodes, mark surfaces) are kept as they are, so an image only fits the packs it was baked against, mapped at the same addresses. The image goes at the first 64 KB boundary after the last pack (`Sys_FlashNext`): `0x13620000` on the PC with registered `pak0` and `pak1`. The engine maps it there (`Sys_FlashMap`) and refuses it if it lands anywhere else.

**On the board** the same rule holds with the board's own pack. `tools/mkboard.py` does it in one go: it builds the trimmed pak from the manifests (`tools/mkpak.py`, several `--pak` allowed, later ones win as in Quake, so `gfx/pop.lmp` comes from registered `pak1`), runs `qog_bake` on the PC hidden and silent with that pak as the only pak (the PC maps it at `0x10100000` (`QOG_DATA_XIP_ADDR`, Agent AC, 0x10400000 before) exactly as mkpak lays it out, so the addresses are the board's), and writes the data region image:

| Address | Content |
| --- | --- |
| `0x10100000` | trimmed pak, files 16-byte aligned |
| next 64 KB boundary (or `--bake-base`) | `qogbake.bin`, built for that address |

```powershell
python tools\mkboard.py --pak data\id1\pak0.pak --pak data\id1\pak1.pak `
    --manifests build-pc-dev\manifests --maps start e1m1 e1m2 `
    --exe build-pc-dev\Release\quakeog_pc.exe -o build-board\qogboard
```

It writes `<out>\id1\pak0.pak`, `<out>\id1\qogbake.bin`, `<out>.bin` and `<out>.uf2` (the UF2 writes the data region only). Keep the output path short (the engine keeps paths in 128-byte buffers). The PC runs the result exactly as laid out for the board with `-basedir <out>`. `port/rp2040/sys_rp2040.c` serves the pak as `id1/pak0.pak` and the image as `id1/qogbake.bin`, found at the first 64 KB boundary after the pak that holds `QOGB`.

Measured without stubs (registered data, manifests from `qogtest_manifest.cfg`, Agent I):

| Maps | Pak | Image | Data region use |
| --- | ---: | ---: | ---: |
| `start e1m1 e1m2` | 9.03 MB | 1.30 MB (57 models) | 10.36 MB of 11.94, fits |
| `start e1m1 e1m2 e1m3` | 10.59 MB | about 1.75 MB | about 12.4 MB, does not fit |
| `start` and all of episode 1 | 16.51 MB | 3.31 MB (81 models) | 19.87 MB, does not fit |

### Maps in the image, stubs in the pak (`--stub-maps`, Agent J)

Once a map is baked, the engine reads only some of its file through the image's pointers (light, visibility, entities, vertexes, edges, surface edges, clip nodes, mark surfaces, texture pixels) and none of the rest (faces, nodes, leafs, planes, texinfo, models). And each map carries its own copy of textures that other maps share. With `QOG_BAKE_STUBS` (both builds) and `--stub-maps`:

- `qog_bake -bakestubs` copies everything a map's block points at in the pack into the image, so the map points at nothing in the pack. Texture pixels are stored once per distinct content for the whole image (same bytes, any name), so a texture used by several maps is in flash once. Alias models and sprites are baked as before (alias frames and skins stay in the pack).
- Each map file in the board's pak becomes a 132-byte stub (`qogbake_stub_t` in `engine/model.h`): `QOGS` where the BSP version is, then the map's 15 lumps from its header unchanged (the leaf count is read from there), then the length and `Mod_FileHash` of the map it replaces. Its directory entry has `fileaddr` 0 (any address), and `filelen` and `filehash` are those of the stub. Without a matching image a stub stops with "is a stub, its map is in qogbake.bin".
- `mkpak --maps-last` puts the maps after every other file. The bake runs on that full pak, and the stubs only change the end of it, so every other file keeps its address. `-bakebase` with `-bakestubs` may then point inside the full pak, right after where the pak with stubs ends. Any pointer into the pack that would lie past that address makes the model unbakeable.
- `mkboard` keeps the full pak as `<out>\src.pak`. `qog_bake check` compares a stubbed map with the full map read from `-bakesrc <out>/src.pak`, and checks that its light, vis, entities, geometry and texture pixels are in the image.
- The full paks and `-nobake` are unchanged: without `-bakestubs` the bake writes what it wrote before.

```powershell
python tools\mkboard.py --pak data\id1\pak0.pak --pak data\id1\pak1.pak `
    --manifests build-pc-dev\manifests --maps start e1m1 e1m2 e1m3 e1m4 `
    --exe build-pc-dev\Release\quakeog_pc.exe --stub-maps -o build-board\qogboard
```

Also for the board's pak (mkpak, with or without stubs): the sounds the client loads by name are always kept (the ambient loops, the temp entity sounds, the menu sounds and the chat beep, which the manifests miss because they are recorded with sound off), and the three pics no engine code draws (`gfx/sell.lmp`, `gfx/dim_mult.lmp`, `gfx/vidmodes.lmp`) are left out. Demos stay out unless `--demos`: with `QOG_DEMOS_OPTIONAL` the attract loop plays only the demos present, so without them the game opens on the console instead of stopping with a missing file.

Measured with stubs (registered data, the same manifests):

| Maps | Pak | Image | Data region use |
| --- | ---: | ---: | ---: |
| `start e1m1 e1m2 e1m3` | 5.44 MB | 5.19 MB (66 models) | 10.69 MB, fits |
| `start` to `e1m4` | 5,735,168 B (5.47 MB) | 6,704,736 B (6.39 MB, 68 models) | 12,471,904 B (11.89 MB), fits with 45,472 B to spare |
| `start` to `e1m5` | 5.74 MB | 7.62 MB (73 models) | 13.37 MB, 1,500,112 B over |
| `start` to `e1m6` | 5.82 MB | 8.59 MB (76 models) | 14.46 MB, 2,649,584 B over |
| `start` and all of episode 1 | 6,577,520 B (6.27 MB) | 10,226,672 B (9.75 MB, 81 models) | 16,845,808 B (16.07 MB), 4,328,432 B over |

The episode's maps take 10.27 MB in a pak and 9.61 MB in the image (3.16 MB of baked structures, 2.28 MB of distinct texture pixels, and the light, vis, entity and geometry lumps). Without sharing, the texture pixels would take 1,794,520 bytes (1.71 MB) more. Per map in the image: `start` 1.56 MB, `e1m1` 1.27, `e1m2` 1.35, `e1m3` 1.07, `e1m4` 1.26, `e1m5` 1.27, `e1m6` 1.02, `e1m7` 0.41, `e1m8` 0.80 (a shared texture counts for the first map that uses it). What is left of the pak is alias models 2.65 MB, sounds 2.47 MB, pics 0.61 MB, `progs.dat` 0.39 MB, `gfx.wad` 0.11 MB and 19 stubs of 132 bytes.

The whole episode still needs a decision by the user (none of these is implemented):

| Option | Gain | Cost |
| --- | ---: | --- |
| `QOG_DATA_OFFSET` 0x400000 to 0x200000 (`platform/qog_flashmap.h` and `tools/mkdata.py`) | 2,097,152 B | app area 1.87 MB (the app is about 1 MB, 1.5 MB with compiled QuakeC). A board layout change: the data must be written again |
| `QOG_DATA_OFFSET` to 0x180000 | 2,621,440 B | app area 1.37 MB, too small for compiled QuakeC |
| 4-bit ADPCM sounds | about 1,284,085 B (half of 2,568,170 B of samples) | changes the sound slightly, decoding costs CPU in the mixer |
| Alias model stubs (header, skin vertices and triangles, already copied into the image) | up to 182,596 B | a stub format for `.mdl` like the maps' |
| `progs.dat` without its statements when QuakeC is compiled | 167,520 B | no interpreter fallback |

All five together give 4,255,641 B with the 0x180000 offset or 3,731,353 B with 0x200000, both short of the 4,328,432 B needed. With 0x200000 alone, `start` to `e1m5` fits (13.37 MB of 13.94, 597,040 B to spare).

### The main chip's server image (`--main`, Agent T)

In the split game the server runs on the main RP2040, which reads its own copy of what the server needs from main's flash: the region 0x10200000 to 0x107FFFFF (`platform/qog_flashmap.h`, 0x10300000 before Agent AC), between main's app and its FAT volume. `mkboard.py --main` writes it in the same run as the display image, from the same files, so the two cannot drift:

| Address | Content |
| --- | --- |
| `0x10200000` | the server's pak: `progs.dat`, `gfx/pop.lmp` (the `registered` cvar QuakeC reads), and a stub for every map (132 bytes) and every alias model and sprite (16 bytes, `qogbake_mstub_t` in `engine/model.h`) of the display's pak |
| next 64 KB boundary | the server's `qogbake.bin`, built for that address by `qog_bake -bakestubs -bakeserver` |

The server's image keeps, for each map, the planes, nodes, leafs, clip nodes and hulls, submodels, entities and visibility, and leaves out textures, texinfo, surfaces, edges, vertexes, mark surfaces and light. An alias model or sprite is its `model_t` alone (type, bounds, frame count, flags). Nothing in it points into a pak, so the PC bakes it from the display's full pak for the address `-bakebase` gives. Its layout word is the build's layout XOR `QOGB_SERVER_LAYOUT`, so only a server half (`QOG_SPLIT_SERVER`) takes it. It writes `<out>_main\id1\pak0.pak`, `<out>_main\id1\qogbake.bin`, `<out>_main.bin` and `<out>_main.uf2` (a UF2 of the region only, padded to whole 4 KB sectors).

```powershell
python tools\mkboard.py --pak data\id1\pak0.pak --pak data\id1\pak1.pak `
    --manifests build-pc-dev\manifests --maps start e1m1 e1m2 e1m3 e1m4 `
    --exe build-pc-dev\Release\quakeog_pc.exe --stub-maps --main -o build-board\qb
```

The PC runs the pair from the two images: the server with `-basedir build-board\qb_main -flashbase 0x10200000` (the PC maps its packs at that address), the client with `-basedir build-board\qb`. The server's saves go to its gamedir, which stands for main's FAT volume.

| Maps | Server pak | Server image | Region use |
| --- | ---: | ---: | ---: |
| `start` to `e1m4` | 420,880 B | 1,955,424 B (68 models) | 2,416,640 B (2.30 MB), 46.1% of 5 MB |
| `start` and all of episode 1 | 422,432 B | 2,948,000 B (81 models) | 3,407,872 B (3.25 MB), 65.0% of 5 MB |
