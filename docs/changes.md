# Changes log

Newest first. Each entry says what changed, why, and whether it ran on hardware.

## 2026-11-23: Per-level map arrays (Agent L, PC and ARM build, registered data)

The map's hunk block (the per-frame arrays `Mod_FinishBrushModel` allocates next to the edge cache) on E1M1 92,128 → 34,720 bytes, on e3m5 (the worst map) 116,112 → 43,024, on e4m7 115,136 → 42,528. Static RAM +6,164 bytes (the light hash). Net saving 51.2 KB on E1M1, 65.4 KB on e3m5. The edge cache (27,024 bytes on E1M1, Agent K's) is untouched and still allocated in the same place. Every change keeps the picture byte-identical and is on in both flag lists. Nothing ran on hardware.

| Array (E1M1 / e3m5 bytes, hunk block with header) | Before | After | Flag |
| --- | ---: | ---: | --- |
| Surface visframe stamps | 11,056 / 14,192 | 720 / 912 | `QOG_FRAMEBITS` |
| Node visframe stamps | 5,536 / 7,024 | 368 / 464 | `QOG_FRAMEBITS` |
| Leaf visframe stamps | 3,088 / 3,712 | 224 / 256 | `QOG_FRAMEBITS` |
| Surface dlightframe stamps | 11,056 / 14,192 | 720 / 912 | `QOG_DLIGHT_SPARSE` |
| Surface dlight bits | 22,080 / 28,336 | 0 (+6,164 static) | `QOG_DLIGHT_SPARSE` |
| Leaf efrag list heads | 6,144 / 7,392 | 2,576 / 2,896 | `QOG_LEAF16` |
| Leaf keys | 6,144 / 7,392 | 3,088 / 3,712 | `QOG_LEAF16` |
| Edge cache (not changed) | 27,024 / 33,872 | 27,024 / 33,872 | |

- `QOG_FRAMEBITS`: id's stamps are only compared for equality with a counter that never goes back, so an array is kept as one bit per entry plus the counter the bits belong to. A write with another counter clears the bits first. A fresh array has tag 0 and every bit set, like id's zeroed stamps. The clear costs numsurfaces/8 bytes of memset once per frame (690 bytes on E1M1) and the node and leaf bits once per `R_MarkLeaves`. A leaf's visframe was not recomputed from the PVS row instead: nodes need their own bits anyway, and the bits are smaller than the PVS row of a large map.
- `QOG_DLIGHT_SPARSE`: the dlightframe stamp is one bit per surface (as above) and the bits of the lit surfaces sit in a 1,024 entry open hash (`QOG_DLIGHT_SURFS`, 2-byte surface number and 4-byte bits, emptied with the bits). `R_PushDlights` only notes the world pass. It runs at the first question about a lit surface in the frame (`R_DlightLit`, behind `SURF_DLIGHTFRAME_IS`), after `R_RenderWorld` has stamped the visible surfaces, and marks only world surfaces visible in this frame: no other world surface is drawn (`R_RecursiveWorldNode` tests the same stamp). The brush model pass in `R_DrawBEntitiesOnList` marks as before. A second world pass for the same counter before the first ran (only when no view was drawn in between) runs the first one in full, as id's marks add up. If the hash fills, the lost lights are remembered and `R_DlightBits` finds their bits by walking the world tree and the drawn brush model's tree with `R_MarkLights`' arithmetic.
- Peaks over the full-game fight tour (`qogtest_h_fight1` to `4`, rockets, explosions, muzzle flashes): at most 573 lit visible surfaces in one frame (e1m6), so the hash is at most 56% full. Marking every surface in reach, as id does, gave 1,918 on e3m6, which is why the world pass is deferred.
- `QOG_LEAF16`: leaf keys are 16-bit (a frame's keys count nodes, leafs and drawn surfaces, `LEAF_KEY_SET` stops with an error rather than cut one). A leaf's efrag list head is one of 256 world heads selected by a byte per leaf, so `LEAF_EFRAGS` stays a pointer lvalue (`R_NewMap` in r_main.c is unchanged). Once the heads run out further leafs share the last one and `R_StoreLeafEfrags` skips the efrags of other leafs in it, which keeps id's order per leaf. Most heads in use over the fight tour: 191 including the NULL head 0 (e1m4).
- Verification, hidden and silent, against a build of c97750d (`build-pc-base`): state hashes identical in `qogtest_hash.cfg`, `qogtest_f_hash.cfg` (32), `qogtest_e_shots*.cfg` (base and new build run side by side, as the shared data folder made base's own hashes drift between runs). 177 frames of `qogtest_d_shots.cfg` and `qogtest_e_shots*.cfg` and the 8 `qogtest_f_hash` frames byte-identical. With `-nobake` `qogtest_hash` and the 113 demo frames identical. `qog_bake check` 0 differences, "Baked models: 133". Per-frame hashes over the demos and the four fight tours (`qogtest_l_demos.cfg`, `qogtest_l_fight1..4.cfg`, copies of Agent H's scripts) were compared by dumping every frame of fight tours 1 and 4 from both builds: of 2,847 frames of tour 4, 2,801 are identical, 40 differ only in the notify rows 0 to 31 and 6 are the startup console with the build time. Of 5,913 frames of tour 1, 5,800 are identical, 71 differ only in rows 0 to 31, and the other 42 differ only in console text (build time, "7.7 megabyte data cache" for 7.6, the server time of the `qog_mem` report at each map change). The other differing hashes are notify lines in rows 0 to 31 that show for a real-time span and differ between two runs of the base build too (45 of 2,845 frames of fight tour 4 between two base runs).
- Fallbacks checked with a build using `QOG_DLIGHT_SURFS=4` and `QOG_EFRAG_HEADS=4` (the hash overflows in nearly every lit frame, most leafs share a head): the same per-frame hashes as the normal build over the demos and all four fight tours, apart from the same noise. Builds with `QOG_FRAMEBITS` alone and with `QOG_DLIGHT_SPARSE` added ran the demos and fight tours with the same kind of differences only (not dumped frame by frame), and the first gave `qogtest_hash` state hashes equal to base run next to it.
- Speed on the PC (timedemo demo1, two runs each, noisy machine): base 18.2 to 19.1 s for 970 frames over five runs, new build 17.5 to 20.1 s, no difference above the noise. Per frame the new work is one memset of the surface bits, a hash probe per lit drawn surface and the deferred world pass, which visits the same nodes as before.
- Edits outside Agent L's files: `engine/r_bsp.c`, three lines in `R_RecursiveWorldNode` (the surface visframe write through `SURF_VISFRAME_SETNUM`, the efrag store through `R_StoreLeafEfrags`, the key through `LEAF_KEY_SET`). One new block each at the end of the lists in `pc/CMakeLists.txt`, `apps/quake/CMakeLists.txt` and `tools/ram_meter.py`.
- The bake layout hash did not change (`model_t` and `framestamp_t` keep their sizes, `modframe_t` is RAM only), so `data/id1/qogbake.bin` needs no rebuild.
- Board (`quake_display`, 64 KB placeholder hunk): static RAM overflow 65,308 bytes with these flags. The light hash adds 6,164 bytes of static RAM (`tools/ram_meter.py`), while each level's hunk needs 57 to 73 KB less. The deferred world pass runs from inside `D_DrawSurfaces`, so `R_MarkLights`' recursion (80 bytes a level on the ARM build, `R_RecursiveWorldNode` takes 88) now starts about 330 bytes deeper in the stack than `R_RecursiveWorldNode` does. Not measured on the board.

## 2026-11-23: Flash budget for the board's data image (Agent J, PC and ARM build, registered data)

Start plus episode 1 (`start`, `e1m1` to `e1m8`) in the board's data region: 20,833,408 → 16,845,808 bytes (19.87 → 16.07 MB of 11.94). It still does not fit, by 4,328,432 bytes. `start` to `e1m4` now fits (11.89 MB, 45,472 bytes to spare), where before only `start e1m1 e1m2` did. Engine static RAM on the ARM meter unchanged (118.2 KB). Nothing ran on hardware.

| Change | Flag | Data region |
| --- | --- | --- |
| Maps moved into the baked image. `qog_bake -bakestubs` copies what a map points at in the pack (light, vis, entities, vertexes, edges, surface edges, clip nodes, mark surfaces, texture pixels) into the image, each distinct texture's pixels once for all maps (1,794,520 bytes not stored twice). The pak keeps a 132-byte stub per map, matched by length and hash at any address. `mkboard --stub-maps` | `QOG_BAKE_STUBS` | −4,053,136 bytes (19.87 → 16.00 MB) |
| Sounds the client loads by name kept in the board pak (ambient loops, temp entity, menu and chat sounds). Six were missing, since the manifests are recorded with sound off | none (mkpak) | +110,512 bytes |
| The demo loop plays only the demos present. The board pak has none, and the attract loop stopped with a missing file error. It now opens on the console | `QOG_DEMOS_OPTIONAL` | 0 (demos were already left out, 0.52 MB) |
| The three pics no engine code draws (`gfx/sell.lmp`, `gfx/dim_mult.lmp`, `gfx/vidmodes.lmp`) left out of the board pak | none (mkpak) | −72,552 bytes |

- Layout for start plus episode 1: pak with stubs 6,577,520 bytes at `0x10400000`, 41,616 bytes of padding, `qogbake.bin` 10,226,672 bytes at `0x10A50000` (81 models), end at `0x11410BF0`.
- How it stays exact: `mkpak --maps-last` puts the maps after every other file, the bake runs on that full pak for the address after the pak with stubs (`-bakebase` may lie inside the full pak with `-bakestubs`, and any pack pointer past it makes the model unbakeable), and `mkboard` checks that no other file moves and that every map is in the image against its stub. The full pak is kept as `<out>\src.pak` for `qog_bake check -bakesrc`.
- Verification, hidden and silent, against a build of c92533c with the full paks: the new `qogtest_j_ep1.cfg` (then `_ep1b`, `_ep1c`) visits `start` and `e1m1` to `e1m8` with `host_framerate 0.05`, 2 frames and a state hash per map. All 9 hashes and 18 frames identical on the board layout and on the full paks with the new build. `qogtest_hash` identical on all three and with `-nobake`. `qog_bake check -bakesrc` 0 differences over 81 models. The same tour with the mixer on (`-silentsound +volume 0`, no audio device opened) and the new `qogtest_j_menu.cfg` (every menu) show no load error on the board layout and the same 9 hashes as the full paks. `start` to `e1m4` checked the same way.
- What is left needs the user. Exact sizes in docs/game-data.md: `QOG_DATA_OFFSET` to 0x200000 gives 2,097,152 bytes (app area 1.87 MB), to 0x180000 2,621,440 bytes (app area 1.37 MB, too small for compiled QuakeC), 4-bit ADPCM about 1,284,085 bytes, alias model stubs up to 182,596 bytes, `progs.dat` without statements 167,520 bytes. All five give at most 4,255,641 bytes, still short of 4,328,432. With the offset at 0x200000 alone `start` to `e1m5` fits (13.37 MB of 13.94).
- Edits outside Agent J's files: `engine/host_cmd.c` (the demo loop, behind `QOG_DEMOS_OPTIONAL`), one declaration in `engine/common.h` (`COM_HasFile`), and one new block each in `pc/CMakeLists.txt`, `apps/quake/CMakeLists.txt` and `tools/ram_meter.py`.
- The shared `data/id1/qogbake.bin` needs no rebuild: the layout hash did not change, and without `-bakestubs` the bake writes what it wrote before.

## 2026-11-23: Video memory and renderer RAM (Agent H, PC and ARM build, registered data)

Board static RAM overflow 186,540 → 145,688 bytes (−40,852). Video memory 158,720 → 145,920 bytes. The deepest render stack chain is about 3.4 KB shorter. Every part A change keeps the picture byte-identical and is on in both flag lists. The part B options change the picture and are off. Nothing ran on hardware.

| Change | Flag | Saved (measured on the ARM build) |
| --- | --- | ---: |
| sintable and intsintable precomputed in `engine/r_turbtab.h`, `r_avertexnormals`, the alias box edges and the polyset edge table `const` | `QOG_CONST_RTABLES` | 5,952 B static |
| Row offsets of the screen, the z-buffer and the alias skin computed with one multiply, not read from `d_scantable`, `zspantable` and `skintable` | `QOG_NO_ROWTABLES` | 3,516 B static |
| Sky composed per drawn pixel from the texture (front layer if non-zero, else back), no 16 KB `newsky` | `QOG_SKY_DIRECT` | 16,376 B static |
| Status bar rows 152 to 199 stored in the first 15 KB of the z-buffer, which is idle during the 2D overlay and the scanout | `QOG_HUD_IN_ZBUFFER` | 15,360 B video (15,032 B net) |
| R_ScanEdges spans and the brush clip buffers in the free end of the edge table, with 4,608 B of room past the unchanged edge limit | `QOG_SPANS_IN_EDGES` | R_ScanEdges 8,072 → 88 B, brush clip frame 4,664 → 64 B, R_EdgeDrawing 80,160 → 84,768 B |

- `QOG_HUD_IN_ZBUFFER`: id redraws the status bar only when something changed and otherwise relies on the frame buffer keeping it. After each 3D view R_RenderView repaints the rows with the tile background a full update leaves there, and when Sbar_Draw is going to skip the frame it draws the status bar again from the client state it was last drawn with (saved by `R_HudFrameEnd`, which both VID_Update ports call). A first version that simply redrew the status bar every frame was not exact: after damage that left the stats unchanged it showed the pain face where id still shows the old face (`qogtest_f_hash.cfg` end frame). The next 3D view may not start before the scanout ends, which the single frame buffer already requires.
- `QOG_SKY_DIRECT`: R_MakeSky only records the texture and shift it would have composited, and keeps them until the shift changes, so a map change with the same shift shows what id showed.
- `QOG_SPANS_IN_EDGES`: where spans are flushed does not change the picture (each pixel is drawn once). The span room is the free part of the edge table plus the 4,608 B reserve, at least 576 spans and up to the 8,191 the 16-bit links reach. On spawn views of e3m6, e1m1, e4m7 and demo1 it was 4,611 to 6,573 spans, and one frame used at most 2,977. The reserve holds the brush clip arrays (128 vertices, 256 edges) before R_ScanEdges runs. `qog_rpeaks` prints these figures on the PC.
- Not changed, with the reason: the z-buffer itself. Alias models, sprites and particles test and write it, and a reader can cover the whole view (a monster close up, an explosion), so storing z only where readers draw saves nothing in the worst case. World z also depends on where each span starts, so it cannot be rebuilt from surface planes without keeping every span of the frame. No exact scheme with a smaller worst case was found. The z-buffer is idle outside the 3D view, which `QOG_HUD_IN_ZBUFFER` uses and other short-lived buffers could use too.
- Not changed: the edge and surface tables (2,000 × 24 B and 800 × 40 B). They are live while D_DrawSurfaces writes z mid-scan, so they cannot share the z-buffer without holding every span of the frame, whose number has no bound.

Part B, off by default and not in the shipped flag lists (built with extra `-D` flags, 177 frames of `qogtest_d_shots.cfg` and `qogtest_e_shots*.cfg` compared with the part A build):

| Option | Video memory (with the part A flags) | Picture |
| --- | ---: | --- |
| `QOG_RENDER_SCALE=75`: 3D view drawn at 240×114 in the top left of its rectangle and scaled up in place before the 2D overlay, z-buffer only as large as the view (`QOG_ZBUFFER_VIEW`) | 103,360 B (−42,560) | Blockier 3D, 39% of view pixels differ, status bar and text unchanged, 56% of the 3D pixels to draw |
| `QOG_ZBUFFER8`: one byte of depth, id's 16-bit value shifted right by 3 and clamped to 255 | 97,280 B (−48,640) | 24 of 177 frames identical, 0.39% of view pixels differ: objects nearer than about 16 units tie, so the weapon covers its own muzzle flash, and models close to walls show coarser depth steps |
| Both | 76,000 B (−69,920) | As both above |

Verification (hidden and silent, against a build of 2c6c10f with the same test tooling): `qogtest_d_shots.cfg` (113 frames, demos 1 to 3 including the underwater demo2, every map's spawn view), `qogtest_e_shots*.cfg` (64), `qogtest_sky.cfg`, `qogtest_demo.cfg`, `qogtest_f_hash.cfg` all byte-identical, state hashes identical in `qogtest_hash.cfg`, `qogtest_e_shots*.cfg` and `qogtest_f_hash.cfg`, no Short messages. New per-frame hashes (`qog_framehash`, new `qogtest_h_*.cfg` scripts) over the three demos, `qog_tour_fight` on every map of the full game (the e4m5 run stops at a QuakeC teleport error in both builds), `qog_tour e1m1 e3m6 e4m7` with edge and surface reports, and a status bar script (console, menu, pause, viewsize 80, scoreboard, death, intermission): 45,525 frames. Every frame equals a base run except the console frames at startup and at each map change, which show the build time or the `qog_mem` video figure. Those were dumped from both builds and differ only in those text cells (checked for every such frame in the first 4,000 of each run, and they fall in the same places later). Two runs of the same build differ on a few frames around map loads, so each frame is compared against two base runs.

## 2026-11-23: Model table in flash, server tables, board data image (Agent I, PC and ARM build, registered data)

Engine static RAM on the ARM meter 202.8 → 124.5 KB. Board link overflow (`quake_display`, 64 KB placeholder hunk) 186,540 → 106,428 bytes. Nothing ran on hardware.

| Change | Flag | Static RAM |
| --- | --- | --- |
| A baked model's `model_t` is used in place in the image. The table keeps a name, a pointer and a load state per model of the current level (2.2 KB), a map's per-frame arrays hang off `mod_frames` through `model_t.frameslot` (1 KB). A model loaded the normal way gets its `model_t` on the level's hunk | `QOG_MOD_FLASH` | `mod_known` 78.8 → 2.2 KB, +1 KB `mod_frames`, total 202.8 → 127.0 KB |
| Server precache lists sized from full-game peaks (188 models on e3m4, 154 sounds), no copy of the map's model name | `QOG_SV_TABLES`, `QOG_SV_MAX_MODELS=208`, `QOG_SV_MAX_SOUNDS=176` | `sv` 11.8 → 11.1 KB |
| The all-visible PVS row is a constant in flash | `QOG_CONST_NOVIS` | −1 KB |
| Server PVS rows sized for 4,096 leafs (largest full-game map 2,156), checked at map load | `QOG_PVS_LEAFS=4096` | −1 KB |

- Image format: with `QOG_MOD_FLASH` a map's block holds the finished world and its inline models `*1` to `*n` (layout hash bit 32). The registered image grew from 13,448,304 to 14,425,024 bytes. `qog_bake check` also compares every inline model and the frame slot: 0 differences. Hunk use is unchanged in the baked path (the same per-frame arrays are allocated).
- `-bakefile <path>` reads and writes another image, `-bakebase <address>` bakes for a later address, and the PC maps such an image there.
- The most models one level used was 187 (e3m4), inside the 192-entry table. The table is emptied at every level change. In the normal path (`-nobake`) an alias model is now loaded again on each level, as its `model_t` is freed with the hunk.
- `tools/mkboard.py` builds the board's data image: a trimmed pak (mkpak now takes several paks, later ones winning), `qog_bake` run on the PC with that pak alone so the addresses are the board's, and the image at the next 64 KB boundary. `start e1m1 e1m2` is 9.03 MB of pak plus 1.30 MB of image, 10.36 MB of the 11.94 MB region. A whole episode does not fit: its trimmed pak alone is 16.5 MB. The PC ran that layout (`-basedir build-board\qogboard`) with the same state hashes as the full paks and `qog_bake check` 0 differences, also with the image moved to `0x10E00000` by `--bake-base`.
- `port/rp2040/sys_rp2040.c` serves the image as `id1/qogbake.bin` from the first 64 KB boundary after the pak. The board CMake has `-DQOG_AOT=ON` (with `QOG_AOT_PAK`, off by default) and a `QOG_HUNK_KB` cache value. Compiled QuakeC links on the board: 500 KB of flash, no change to static RAM. With a realistic hunk (about 393 KB for E1M1) the overflow would be about 435 KB.
- Verification against a build of 4e5dc15, hidden and silent: the same state hashes in `qogtest_hash`, `qogtest_f_hash` (8 maps, fight, save and load) and `qogtest_m_save`, 113 `qogtest_d_shots` frames, 64 `qogtest_e_shots` frames and 8 `qogtest_f_hash` frames byte-identical, the 32-map `qogtest_reg` and the `e1m1 e3m6 e4m7` tour logs equal apart from the model counts and times. With `-nobake` the hashes and the 113 demo frames are identical too.
- Edits outside Agent I's files: one line each in `engine/r_bsp.c` and `engine/r_draw.c` (the per-frame arrays through `MODFRAME`), two loop bounds and `checkpvs` in `engine/pr_cmds.c`, and the model and precache lines in `port/common/qog_mem.c`.
- Main must regenerate `data/id1/qogbake.bin` after the merge (the layout hash changed, the old image is refused and models load the normal way until then).

## 2026-11-23: Client memory and 2D pics (Agent G, PC and ARM build, registered data)

E1M1 (`qog_mem`, PC): hunk low 392.8 → 364.0 KB, cache 62.6 → 0 KB, total 613.8 → 522.5 KB. Engine static RAM on the meter 202.8 → 196.5 KB. Board overflow 187,500 (Agent F's figure for main) → 180,112 bytes. Nothing ran on hardware.

| Flag | Change | Saved |
| --- | --- | --- |
| `QOG_CONBACK_FLASH` | gfx/conback.lmp read in place from the mapped pack. The version number goes into a copy of each of the 8 rows it covers as that row is drawn | cache 62.6 KB |
| `QOG_ENTITY_PACKED` | client `entity_t` 184 → 104 bytes. Network origins and angles kept as the protocol's shorts and signed bytes (`ENT_COORD` and `ENT_ANGLE` give back the exact floats), `msgtime` a float (only ever set from a float read from the message), baseline as bytes, `update_type`, `dlightframe`, `dlightbits` gone. Renderer fields unchanged | E1M1 cl_ents 58,352 → 32,992 hunk, `cl_temp_entities` 11.5 → 6.5 KB static |
| `QOG_PARTICLE_PACKED` | `particle_t` 44 → 36 bytes: colour and type as bytes, list link a 16-bit index (d_iface.h, r_part.c) | hunk 4,096 |
| `QOG_PARTICLE_AVEL8` | entity particle rates as bytes, scaled to the same floats on use | static 1.4 KB |
| `QOG_CACHEPIC_FLASH` | `Draw_CachePic` returns menu, intermission, finale and plaque pics in place (little endian only, conback excluded since id writes into it) | cache 30.6 KB at an intermission, 222.9 KB with every menu open |

- Peaks: the fight tour (`qog_tour_fight e1m1 e3m6 e4m7`) fills all 512 particles, so `QOG_MAX_PARTICLES` stays. MG24Quake's 16-byte particle stores positions and velocities with less precision and would change the picture, so it was not used.
- Verified hidden and silent against the untouched worktree after each change: identical state hashes (`qogtest_hash`, the 8-map fight with save and load, `qogtest_m_save`), 203 frames byte-identical (demo 3, sky, the 113 D shots, the 64 E map shots, and the new `qogtest_g_con.cfg` console frames), the fight tour logs equal apart from memory lines. The new `qogtest_g_pics.cfg` (every menu and the pause pic) matched too. The only pixel differences are the blinking text cursor of the console input line and the menu name fields, which follow `realtime` and differ between two runs of the untouched build as well. Logs lose the `PackFile:` lines of the pics now read in place.
- Looked at and left: `scores` (16 KB) is already one entry in single player, it is that player's translation table, which the renderer indexes as a full colormap when chase view draws the player. `sizebuf` is the 8 KB command buffer, the 6.5 KB `net_message` and the 1 KB client message (all sized earlier). `cl_lightstyle` and the temp entity count stay at id's sizes.
- Found, not changed (server code): the "unknown" hunk blocks on E1M1 are 520 QuakeC strings from the entity lump (`ED_NewString`, pr_edict.c), 4,400 bytes of text in 17,472 bytes of blocks because each gets a 16-byte hunk header and is rounded to 16. One string arena per level would save about 13 KB. The rest (about 5 KB) is console commands at 32 bytes each.
- Not measured: speed on the board. The board build must map the pack (the pics are read as `qpic_t` with int fields, so their image positions must stay 4-byte aligned).

## 2026-11-23: Speed benchmark on the board (hardware)

`apps/qogbench` times the engine's world-drawing inner loop on the display CPU (200 MHz Cortex-M0+), drawing a perspective-textured plane over the whole 320×152 view. The span code is a copy of `engine/d_scanlit.c`. The texture is a 320×200 picture read from the flash data region. Flashed with `fw flash qogbench --uf2 build/apps/qogbench/qogbench_main.uf2`, results read from the display CPU's USB console (COM8). Steady over 10+ rounds:

| Measurement | Time | Per pixel or per item |
| --- | ---: | ---: |
| Lit spans as the game does it now (texture in flash, code in flash) | 52.5 ms | 1.08 µs, about 216 cycles |
| Same code placed in RAM | 52.3 ms | the XIP cache already holds the loop |
| Lit spans with a power-of-two texture (masks, not modulo) | 41.9 ms | 20% faster |
| id's surface-cache span drawer (no per-pixel light) | 24.0 ms | for comparison, its cache needs RAM we do not have |
| Z spans (16-bit depth for every view pixel) | 1.8 ms | |
| One float divide (perspective step, once per 8 pixels) | 870 ns | about 5.3 ms per full view |
| 64 KB sequential read from flash | 7.7 ms | about 8.5 MB/s |
| Random single-byte flash reads | 538 ns each | an XIP cache miss |

What it means:
- World drawing alone costs about 52 ms per frame today (about 19 fps for the world, before edges, models, particles and game logic). Core 1's 22 ms LCD scanout runs alongside.
- Code placement does not matter for this loop. The per-pixel arithmetic does: two modulos and a 4-sample light blend per pixel.
- Clear next steps for speed: wrap texture coordinates without modulo (per span or with power-of-two masks, worth about 10 ms), and step the light linearly along each 8-pixel run instead of blending 4 samples per pixel.

## 2026-11-23: Sparse edicts, baseline pool, compiled QuakeC globals (Agent F, PC and ARM build, registered data)

Edicts on E1M1 went from 277,712 to 84,992 bytes of hunk (edicts, field arena, baselines, field map), and on e4m3, the largest array, from 447,664 to 139,888. With compiled QuakeC the globals also went from 45,904 to 7,280 bytes. Hunk low: E1M1 995.5 → 807.3 KB interpreted, 769.6 KB compiled. e4m7 1,338.0 → 1,040.2 / 1,002.5 KB. Nothing ran on hardware.

| Step | Flag | E1M1 hunk | e4m3 hunk |
| --- | --- | ---: | ---: |
| Core fields in place, the rest as (field, value) pairs in a compacting arena, 12 words per slot | `QOG_EDICT_SPARSE` | 277,712 → 105,504 | 447,664 → 169,920 |
| Baselines only for edicts that get one at spawn, 16-bit index | `QOG_BASELINE_POOL` | → 95,136 | → 156,240 |
| avelocity, ltime, blocked, effects, skin, colormap out of the core (38 words, slot 208 B) | `QOG_EDICT_SPARSE` | → 84,992 | → 139,888 |
| Compiled QuakeC: constants as literals, most temporaries as C locals, packed RAM globals (1,813 of 11,471 words) | `QOG_AOT_GLOBALS` | progglobals 45,904 → 7,280 | same |

- Measured with `qog_tour` (32 maps, noclip, notarget) and the new `qog_tour_fight` (same tour, monsters awake, rocket launcher firing). Most edicts in use: 389 high water on e4m3 in the fight tour, 365 on e4m5 and e4m7 in the plain tour. The high water never went more than 35 slots over `SV_EdictsForMap`'s estimate, and every map kept at least 92 spare slots, so `QOG_EDICT_MARGIN` 128 stays. The arena peaked at 9.2 words per slot (12 allocated), and it compacts when full.
- Real play can go further than the tours: nails live up to 6 s, gibs 10 to 20 s, backpacks 120 s. A long fight with many gibs and packs could pass the margin. Overflow is still id's `Sys_Error`.
- Field access: engine code uses `EV_` macros for non-core entvars fields, QuakeC goes through `ED_FieldGet` and `ED_FieldSet`, and `OP_ADDRESS` returns the edict offset times 256 plus the field number. qc2c inlines the core words and the engine checks its map at load, falling back to the interpreter if they differ. The PC build regenerates the compiled code when `progs.h` changes.
- Compiled QuakeC (`-DQOG_AOT=ON`, registered `progs.dat` from pak0) is 496 KB of Cortex-M0+ flash with 27 KB of tables, and its largest generated stack frame is 64 bytes. On the PC, server frame time over the bench script was 0.84 to 0.96 s compiled, 0.96 to 1.04 s interpreted with sparse edicts, and 0.89 to 0.98 s interpreted with id's edicts (noisy, PC only).
- Board static RAM: overflow 187,264 → 187,500 bytes (+236, the arena bookkeeping and a vector ring). Engine flash +3 KB.
- Verification: identical `qog_statehash` to the untouched build on E1M1 (`qogtest_hash.cfg`) and on eight registered maps with combat, save and load (`qogtest_f_hash.cfg`). Checked interpreted, compiled, compiled with id's edict layout, and `-noaot`. Demo 3, sky and map frames were byte-identical. `qogtest_reg.cfg` (all 32 maps) logs matched apart from the memory lines, and hunk low was 148 to 301 KB smaller on every map.
- Not done: `leafnums` (36 bytes per slot) stays inline. The estimate still counts entities that skill flags inhibit (15 to 75 per map). `SV_PushMove` keeps its 9.7 KB stack frame (two `MAX_EDICTS` arrays).

## 2026-11-23: Maps, alias models and sprites baked into a flash image (Agent E, PC and ARM build, registered data)

- New PC command `qog_bake` runs id's own loaders on every map, alias model and sprite in the packs and writes what they build into `id1/qogbake.bin` (git-ignored, a converted copy of the registered data). Pointers are resolved for the flash address the image is mapped at: the first 64 KB boundary after the packs, `0x13620000` with registered `pak0` and `pak1`. The PC maps it read only there. Format and use in `docs/game-data.md`.
- `QOG_BAKE` (PC and ARM): a map load copies the baked `model_t` and allocates only the per-frame arrays. An alias model or sprite load puts nothing in the cache or on the hunk. A layout hash refuses an image from a build with other structures, and a model whose file changed loads the normal way. `QOG_BAKE_WRITER` (PC only) adds the command. `-nobake` turns the image off.
- `qog_bake check` reloads all 133 models with id's loader and compares them field by field with the image: 0 differences.
- Map hunk blocks, E1M1: 473.6 → 90.0 KB. Worst of the 32 single-player maps: 598.0 KB (E4M7) → 113.4 KB (E3M5). What is left is the per-frame state (visframe and dlight stamps, dlight bits, efrag lists, the edge cache).
- E1M1 total hunk blocks 995.5 → 580.9 KB. Cache 107.1 → 62.6 KB on E1M1 and 152.5 → 62.6 KB on E4M7 (only the console background is left). Hunk high peak 18.5 KB → 0. `qog_mem` total on E1M1 1,261 → 802 KB, on E4M7 1,649 → 1,035 KB.
- Image size: 13,448,304 bytes for all 133 models. The 32 single-player maps take 12.1 MB (E1M1 392 KB, largest E4M7 497 KB), `start` with episode 1 3.05 MB, alias models and sprites 0.2 MB. The board's 12 MB region fits one episode next to its pak, not all four.
- Fix found on the way: with `QOG_MAX_MOD_KNOWN` a reused model slot kept the previous model's `synctype`, so a map loaded into an alias model's old slot got `ST_RAND` and the client drew a random syncbase, which shifted the random sequence from the 20th level of a long run on. Reused slots now start empty.
- Checked hidden and silent against the build before (with the slot fix): same state hashes, the same logs apart from hunk and cache figures and the `PackFile` lines of sprites (no longer loaded from the pack directory), and 75 frames byte-identical (demo 3, the 9 spawn shots, a save and load, and a spawn and a turn view on all 32 maps). The 32-map regression run and the level tour on E1M1, E3M5 and E4M7 match too.
- Board: static RAM overflow 187,256 → 187,268 bytes (three statics). The RP2040 port does not map the image yet, so the board build still loads models the normal way.
- Not changed: the tour stops on `end` with "MOVETYPE_PUSH with a non bsp model" in the build before as well, and long test scripts can run the 24 KB zone out (a 32-map script failed in `Z_Malloc` in both builds, so the new tests chain short scripts).

## 2026-11-23: Render tables packed, renderer stack cut (Agent D, PC and ARM build)

Frames from `tools/arm_report.py` with the worktree's 1,600 edges, 640 surfaces, 1,000 spans and 384 alias vertices. Each flag defaults to id's code.

| Flag | Entry size | Frame before → after |
| --- | --- | --- |
| `QOG_EDGE_T_PACKED`: links are 16-bit byte offsets into the edge buffer, the four sentinels sit at its front, `owner` is the world edge index (the only kind the edge cache compares), `newedges`/`removeedges` 16-bit | `edge_t` 32 → 24 B | R_EdgeDrawing 84,592 → 71,856 |
| `QOG_ESPAN_T_PACKED`: u, v, count as shorts, link and `surf_t.spans` as 16-bit offsets into R_ScanEdges' buffer | `espan_t` 16 → 8 B | R_ScanEdges 16,112 → 8,072 |
| `QOG_SURF_T_SMALL` (needs the two above and `QOG_SURF_T_PACKED`): stack links are surface indices, last_u and spanstate 16-bit, insubmodel and flags bytes | `surf_t` 52 → 40 B | R_EdgeDrawing 71,856 → 64,160 |
| `QOG_FINALVERT_PACKED` (no assembly padding) and `QOG_ALIAS_NO_AUXVERTS` (the triangle clipper recomputes a near-clipped vertex with the same shared transform instead of keeping a 12-byte view-space entry per vertex) | `finalvert_t` 32 → 28 B, 12 B per vertex gone | R_AliasDrawModel 16,944 → 10,824 |

- Deepest render chain is about 29 KB shorter. With main's 2,000 edges and 800 surfaces R_EdgeDrawing should be about 2,000 × 24 + 800 × 40 + 96 ≈ 80 KB instead of 105,712.
- Static RAM about −0.9 KB (16-bit edge lists, no static sentinels). Board overflow 187,256 → 186,296 bytes.
- Limits checked at R_NewMap: at most 2,725 edges for 16-bit links, fewer than 65,535 map edges (largest registered map e3m5 has 16,923), fewer than 65,536 surfaces. Spans are checked at compile time (8,191).
- Verified hidden and silent against a build of 89a587a after each change: 113 shots (demo1 to demo3 every 40 frames, and the spawn view of every registered map) byte-identical, the same state hashes, and the same `qog_tour e1m1 e2m6 e3m6 e4m7` peaks. The near-plane alias clip path ran about 95,000 times in the shot run.
- The largest registered alias model is boss.mdl with 325 vertices, so `QOG_MAXALIASVERTS` 328 would save another 56 × 28 B on R_AliasDrawModel's stack. Not changed here (shared build line).
- Not measured: speed on the board. Each link now costs an add (edges, spans) or a multiply (surfaces) to decode.

## 2026-11-23: Limits checked against the full game (PC build, registered data)

- Registered PAK1 added locally in data/id1 (git-ignored). PAK0 from Steam is byte-identical to the shareware one.
- All 32 single-player maps load with no errors. Worst signon 5,052 of 6,500 bytes (e2m6). At least 94 spare edicts on every map. Zone peak 7.7 of 24 KB.
- New PC test command `qog_tour` (port/pc/tour_pc.c) visits every entity spot of a map in noclip and looks four ways. Spots with the eye inside solid are skipped, because from inside solid the whole map counts as visible.
- Tour peaks: edges about 1,737 and surfaces 668 on e3m6, which overflowed the old 1,600 and 640. Brush clip peaks 48 vertices and 148 edges on e3m3, inside 128 and 256.
- `QOG_NUMSTACKEDGES` 2000 and `QOG_NUMSTACKSURFACES` 800. R_EdgeDrawing's frame is 105,712 bytes on the board as a result.

## 2026-11-23: Agent B and C merged, renderer stack cut (PC and ARM build)

- Agent B (packed BSP structures) and Agent C (message buffers and client tables) are merged into main. Each merge was checked hidden and silent against the build before it: same state hashes, same all-level manifest output, and byte-identical demo and spawn frames.
- E1M1 hunk after both merges is 1,018,640 bytes in total blocks, down from 1,123,216 after B alone. The map's own blocks went from 657,312 to 484,960 with B.
- Board static RAM overflow: 229,744 → 187,256 bytes. The engine's static RAM on the meter is 203.6 KB of the 245 KB budget.
- `QOG_SURF_T_PACKED`: `surf_t` 64 → 52 bytes. The padding was for id's x86 assembly, which neither build compiles (`id386` needs `__i386__`). spanstate and insubmodel share a word. R_EdgeDrawing's frame went from 92,280 to 84,592 bytes.
- `QOG_MAX_BMODEL_VERTS` 128 and `QOG_MAX_BMODEL_EDGES` 256 (id 500 and 1000). qog_mem now prints these high-water marks: 22 vertices and 74 edges over all three demos. The buffers are reset for every brush polygon, and overflow drops only that fragment. R_DrawSolidClippedSubmodelPolygons' frame went from 18,056 to 4,664 bytes.
- Recheck both bmodel peaks and the edge and surface peaks (1,037 and 435 on shareware, limits 1,600 and 640) once registered data is in place.

## 2026-11-22: Message buffers and client/host tables sized from measured peaks (PC and ARM meter)

Peaks were measured with temporary counters over every shareware level, every menu, a save and load, and demos 1 to 3. Each limit is a `QOG_` define that defaults to id's value. Overflow still takes id's path (Sys_Error, a dropped client or a dropped datagram).

| Change | Measured peak | Before → after |
| --- | --- | --- |
| Loopback sockets: no send buffers, one receive buffer per direction (`QOG_LOOP_CLIENT_RX` 7532, `QOG_LOOP_SERVER_RX` 2048) | 3,844 B to the client, 40 B to the server | hunk 32.4 KB → 0.4 KB, static +9.4 KB |
| `QOG_MAX_MSGLEN` 6500 (client message, net_message, demo limit, like MG24Quake's retail build), `QOG_SIGNON_SIZE` 6500 | 3,838 B per message, 3,836 B signon | hunk −3.1 KB, static −1.7 KB |
| Mapped pack looked up in its flash directory, disk positions kept only where they differ (PC) | | hunk 25.2 KB → 1.3 KB (0 on the board) |
| Colormap read in place from flash (never written; the PC pages are read only) | | hunk −16 KB |
| QuakeC locals stack sized per progs: largest locals × 31 nestable calls (`QOG_LOCALSTACK_FROM_PROGS`) | 21 of 2,048 ints used | static 8 KB → 0, hunk +2 KB |
| Menu pic cache: 56 entries with 24-byte names (`QOG_MAX_CACHED_PICS`, `QOG_CACHEPIC_NAME`); the engine can name only 51, the longest 16 characters | 28 | static 8.5 KB → 1.5 KB |
| Static entities and efrags from the hunk per level (`QOG_STATICS_ON_HUNK`), still capped at 128 and 640 | 44 entities, 197 efrags, 16 per entity | static −33 KB, hunk up to +13 KB |
| gfx.wad lump directory used in place | | hunk −5.1 KB |
| Zone 24 KB (`QOG_ZONE_KB`) | 7.4 KB, plus up to 8 KB while a script is inserted | hunk −24 KB |
| Inline model names as a constant table (`QOG_CONST_LOCALMODELS`) | | static −1.25 KB |
| Spawn command and keepalive copy off the load-path stack | | stack −8 KB each on the ARM build |

Totals: E1M1 hunk low 1,268.6 → 1,166.5 KB. Peak hunk low over all levels 1,521.1 → 1,429.8 KB. Engine static RAM (ARM meter) 265.4 → 223.9 KB. State hashes, all 10 manifests and 24 demo frames (rows 16–199) are identical to the reference build.

Left as is: `cl_temp_entities` (lightning beams can legitimately use all 64), the 16 KB player translation table (needed for chase view and the renderer indexes it as a full colormap), the 8 KB command buffer, and `MAX_DATAGRAM` (a smaller one drops entities from the frame).

## 2026-11-22: Engine-only RAM cuts (PC, measured, all verified hidden and silent)

| Step | Change | Saved (E1M1) |
| --- | --- | ---: |
| A | Single player: one client slot, two loopback sockets (`QOG_SINGLE_PLAYER`) | 107 KB hunk |
| B | Edict array sized per map from its entity lump, minus what QuakeC frees at spawn, plus a margin of 128 (`QOG_EDICT_MARGIN`): 247–480 instead of 600 | 242 KB hunk |
| C | Client entity table sized the same way, on the hunk | 100 KB static |
| D | Mixer volume table replaced by an equivalent multiply | 32 KB static |
| E | `gfx.wad` read in place; only its 5 KB directory is copied | 105 KB hunk |
| F | Model table 192 instead of 256 (`QOG_MAX_MOD_KNOWN`); the most one level references is 160 | 27 KB static |
| G | Sky composited straight from its texture (front if non-zero, else back): no masked copies, 128×128 output. Equivalent on both sky textures at all 128 scroll positions | 49 KB static |
| H | Particle pool 512 instead of 2,048 (`QOG_MAX_PARTICLES`), id's own minimum, as MG24Quake and RP2350-Quake use. **Visual trade-off** | 66 KB hunk |

E1M1 now: hunk low 1,639 KB, total dynamic 1,953 KB, static 271 KB (interpreter, since QuakeC translation waits for registered data). State hashes are unchanged throughout. Demo frames are identical to the reference build, status bar included. All nine levels and every menu run. Sound was verified by level only (peak 19,193, RMS 6,395), never played aloud.

Testing changes: `-hidden` now also mutes the speakers and drives playback by game time. New `tools/pctest.ps1` and `qog_soundlevel`.

## 2026-11-22: Asset manifests, pak builder, licence decision, BSP state split (PC)

- **Item 2, asset manifests:** a new engine hook `com_fileopened` reports every pack file opened. On the PC, `qog_manifest` writes a map's precached models and sounds plus every file opened, and `tools/mkpak.py` builds a trimmed pak aligned to 16 bytes, plus a UF2, from those lists. It also converts the two 22 kHz sounds. **Start + E1M1 is 7.08 MB and fits. The whole shareware episode is 16.6 MB and does not fit 11.94 MB**: deduplicating textures (−1.72 MB) and a smaller app area would both be needed.
- **Licence finding:** `SLICNSE.TXT` §3 forbids modifying, translating or making derivative versions of the shareware data, and id's engine refuses a trimmed shareware pak. **User decision: engine-only work for now.** The shareware data is used only by the unmodified PC build. The QuakeC translation is now opt-in (`-DQOG_AOT=ON`, registered data only), and the derived files generated earlier were deleted. Baking data into flash images waits for registered or retail data.
- **Item 1, engine part:** the per-frame state id kept inside the BSP structures (surface `visframe`, `dlightframe`, `dlightbits`; node and leaf `visframe`; leaf `efrags`, `key`; edge `cachededgeoffset`) moved to per-model RAM arrays behind accessor macros. **The map structures are now never written during play**, so they can be baked into flash once registered data is available. Edges are already read in place: −139 KB of hunk for E1M1. Six demo frames across three demos are byte-identical to the previous code, and the state hashes are unchanged.
- The PC port's Options > Video menu no longer crashes (it was a NULL hook).
- RP2350-Quake (zenodante) studied: it bakes immutable BSP and model data into XIP at fixed addresses, the same idea as our next step. Its shareware image is 15.9 MB, and it targets the RP2350's 520 KB of SRAM.

## 2026-11-22: First Quake data on the board (hardware)

- Quake's console background (`gfx/conback.lmp`, 320×200) and palette (`gfx/palette.lmp`) were extracted from the user's `pak0.pak`, packed with `tools/mkdata.py`, and written to the display chip's data region at 0x10400000. The steps: `fw bootsel --cpu display`, copy the UF2 to the RPI-RP2 drive, then `fw flash qog` so the main chip sends RUN (no button press needed).
- **The data-region write path is verified on hardware.** The console reports `data=ok`, and the app and its metadata survived. Lessons L8 is confirmed.
- **Measured flash read speed:** a 64,000-byte `memcpy` from XIP flash takes **7.55 ms, about 8.5 MB/s** sequential (clk_sys 200 MHz, flash at 50 MHz). The frame rate is 34 fps.
- Quake itself does not run on the board yet: E1M1 still needs about 2.8 MB of RAM against 264 KB, and the game data (about 17 MB) must be trimmed to about 12 MB.

## 2026-11-22: Models and sounds from flash (PC, measured)

- **Alias models** (−1,312 KB): frame vertices, skin pixels and triangles stay in flash. The cache keeps only the header, the descriptors and the 16.16 texture coordinates. A new absolute `flashbase` pointer in `aliashdr_t` stays valid when the cache moves the header, and `ALIAS_BULK()` resolves offsets. The player model went from 190 KB to 7 KB. Demo 1 frames are byte-identical with and without flash.
- **Sound switched on in the PC build**: `port/pc/snd_pc.c` (a waveOut ring at 11,025 Hz) plus id's real mixer. Before the in-place change, sounds cost about 1,306 KB of cache for E1M1.
- **Sounds from flash** (−1,235 KB): 8-bit mono sounds at the mixing rate keep their samples in flash, and the mixer applies the sign flip (`^ 0x80`), which equals id's `u - 128` conversion for every byte. 188 of 190 shareware sounds qualify.
- **E1M1 with sound: about 2,759 KB in total**, against about 7,270 KB for unmodified id code with sound.

## 2026-11-22: The three big RAM cuts (PC, measured)

All changes are commits against id's baseline in `engine/`, each verified before the next.

1. **Level data from flash** (−795 KB). New `Sys_FlashMap` port call and `COM_FlashFile` lookup. Brush models load from a read-only image. Lighting, visibility, entities, texture pixels, and (when aligned) vertices, surface edges and clip nodes are used in place. `texture_t` holds mip pointers instead of self-relative offsets. The PC maps the pak into read-only pages at 0x10400000 (the board's data-region address), so any stray write faults immediately.
2. **No surface cache** (−599 KB). New `engine/d_scanlit.c` lights each pixel from the texture and the 18×18 light grid as it draws. The E1M1 start frame matches id's render on 96.3% of pixels. `qog_shot` saves frames for comparisons like this.
3. **QuakeC compiled to C** (−569 KB together with flash alignment). `tools/qc2c.py` translates all 2,091 functions of `progs.dat` into C at build time, from the player's own pak (never committed). `progs.dat` stays in flash and only its 45 KB of globals sit in RAM. **E1M1 state hashes match id's interpreter exactly** at 6.25 s and 21.25 s with monsters patrolling, with and without flash.

Supporting work:
- A shared `qog_rand` (MSVC's generator) on every port, so PC and board make the same random choices.
- Repeatable PC runs: fixed memory layout, and `Host_Frame` called only when a frame is due.
- A semantic `qog_statehash` that hashes by QuakeC type, so layout changes don't affect it.
- The PC flash image realigns pack files to 16 bytes, the way the board converter will.

**E1M1 total: about 5,886 KB down to about 3,923 KB** (see `ram-meter.md`).

## 2026-11-22: E1M1 runs on the PC build, RAM baseline measured

- Shareware data downloaded and checked (`quake106.zip` MD5 matches the published value). `pak0.pak` extracted as data with a pure-Python LHA decoder, MD5 `5906e599…3abb`, placed in the git-ignored `data/id1/`.
- The user turned off Windows Smart App Control, which had been blocking each fresh unsigned build.
- **id's unmodified engine loads E1M1 on our Windows port** ("the Slipgate Complex, player entered the game").
- Meter fixes: id never sets the zone's size field, and the zone lives inside hunk low. Video buffers are now counted.
- **Baseline: about 5.9 MB in total for E1M1 against 245 KB** (map 1.77 MB, cache 1.42 MB, surface cache 600 KB, edicts 513 KB, progs 404 KB, static 435 KB). Full table and shrink plan in `ram-meter.md`.

## 2026-11-22: Clean engine base from id's source, PC build and RAM meter

**Direction change (user decision):** build from id's clean GPL source instead of forking a port. MG24Quake and RP2350-Quake become references to borrow from deliberately, with provenance recorded.

- `engine/`: id's WinQuake software-renderer files copied **unmodified** from id-Software/Quake `bf4ac42` as the baseline commit (`engine/PROVENANCE.md`). Every later change is a git diff against it. A `.gitattributes` keeps the CRLF bytes so diffs stay clean.
- First engine change: `_WIN32` renamed to `ID_WIN32` (41 lines). id used the compiler macro to mean "id's Win32 port", which on MSVC turned on the x86 assembly and the SciTech video library.
- `port/pc/` and `pc/CMakeLists.txt`: a 32-bit MSVC Windows build with a Win32 window and 8-bit blit, keyboard mode, and a board mode (F12, keys 1–5) driven by the firmware's own `qog_input.c`. **It builds and starts**, and stops at "couldn't load gfx.wad" until `pak0.pak` is supplied (`game-data.md`).
- `port/common/qog_mem.c`: runtime RAM meter (hunk low and high, cache, zone, with peaks) in the title bar plus a `qog_mem` console command. It reads id's allocator globals, so `zone.c` stays unmodified.
- `tools/ram_meter.py`: static RAM meter that compiles the engine for Cortex-M0+ and sizes every global. **First reading: 435.3 KB static** against a 245 KB budget. The biggest items are `cl_entities` (108 KB) and `mod_known` (97 KB).
- Commits now follow the user's message rules: a one-line title, a bullet body, no em dashes, semicolons or colons, and no tool attribution.

## 2026-11-22: Resolution measurements (hardware)

Output is always 320×200 on the panel, with single buffering. Set the test size with `cmake --preset target -DQOG_TEST_W=… -DQOG_TEST_H=…`.

| Source | Frame rate | Test render | Scanout | Static RAM (BSS) | Frame buffer |
| --- | --- | --- | --- | --- | --- |
| 320×200 | 32 fps | 9.6 ms | 22.4 ms | 72.5 KB | 62.5 KB |
| 240×150 | 36 fps | 5.4 ms | 22.4 ms | 44.6 KB | 35.2 KB |
| 160×100 | 41 fps | 2.4 ms | 22.4 ms | 24.6 KB | 15.6 KB |

What these numbers say:

- **Scanout costs 22.4 ms whatever the source size.** It's bound by the SPI link (64,000 output pixels at 50 MHz), and the scaling path costs nothing extra.
- **With one buffer, render and scanout run back to back**, so the frame time is the sum of the two (9.6 + 22.4 ms gives 32 ms, 31 fps). Single buffering puts a 22 ms tax on every frame.
- **Removing that tax without a second buffer:** only the span-drawing stage of Quake writes the frame buffer. Order the frame as present → game logic, physics and BSP/edge setup for the next frame → wait for the scanout → draw spans. The scanout then overlaps the work that doesn't touch the buffer, at no RAM cost.
- Other levers if needed: show 240×150 unscaled (11.5 ms scanout, smaller picture), or a faster SPI rate. The PL022 divider is even, so the next step up from 50 MHz is 100 MHz, beyond the ST7789's rating. Leave this alone.
- Platform overhead stays at about 8.5 KB of static RAM at all three sizes.

**Resolution recommendation:** develop at **240×150** (scaled 4:3 to 320×200), with 160×100 as the compile-time fallback. Combining the MG24 study with our measured budget leaves about 18 KB spare at 240×150 and about 42 KB at 160×100. 320×200 doesn't fit.

## 2026-11-22: Control map v2, red is tap-only (flashed)

- The plan's control table put "move back" on red, and the board showed that holding red starts the 6 s power-off. The new map (`platform/qog_input.c`):

  | Input | Alone | With green held |
  | --- | --- | --- |
  | Gray | Forward | Back |
  | Yellow | Turn left | Jump |
  | Blue | Turn right | Crouch / swim down |
  | Red | Next weapon (tap) | Previous weapon (tap) |
  | Green | Fire | Modifier |
  | Yellow + blue | Menu | |

- This replaces the plan's controls table. The test app now shows every action: weapon taps cycle the box colour, fire turns it red, menu turns it grey, jump hops it, crouch shrinks it.
- Blue was re-checked by the user and moves right, as intended.

## 2026-11-22: First run on the board (hardware)

- The board came with BSP-built firmware ("FWOG main/display wilirehab 002"), so the display bootloader was already installed. `fw flash qog` replaced wilirehab on both chips, and both now enumerate as `FWOG … qog 001`.
- **Bug found on hardware:** the first flash hung core 0 with a black screen. Frames were handed to core 1 through the SIO FIFO, and `multicore_lockout_victim_init()` installs a core-1 IRQ that eats FIFO words. I replaced the FIFO with a volatile pointer plus `__sev`/`__wfe` (lessons.md, L14).
- **Bug found on hardware:** the tilt zero was never set, because the first LIS3DH read came before a sample was ready. It now averages 8 samples with a 300 ms timeout.
- Also fixed: if video init failed, the test app printed nothing. It now reports.
- **Measured** at 320×200 with output 320×200 and single buffering:

  | Measurement | Value | Note |
  | --- | --- | --- |
  | Frame rate | **32 fps** | |
  | Scanout | **22.4 ms** per frame | Theory was 20.5 ms; the extra is setup plus a DMA re-arm per line |
  | Test-pattern render | 9.6 ms | Uses a `%` per pixel, so this measures the test pattern, not the pipeline |
  | Core-0 wait on scanout | about 0 µs | Core 1 finishes before core 0 needs the buffer again |

- The data region reads `bad magic`: there's old non-QOGD content at 0x400000, probably left by earlier firmware. The test data UF2 hasn't been written yet.
- Buttons and the action map respond. The 6 s red-hold LED countdown works.
- **Third fix: backlight.** The screen was blank because the app never called `board_backlight(255)` (lessons.md, L18). After the fix, the user's photo shows the scrolling bands, the box and the fps counter: **the video pipeline is verified on the panel.**
- **User button test:** gray moves the box up, red down, yellow left; green turns the box red (palette index 0, as intended). Blue was reported as moving *left* too, where right is expected; re-checking. Red also starts the power-off countdown (L6 confirmed).
- **Orientation:** the LCD's 320-pixel axis runs along the board's long side, so with the board upright the 320×200 frame shows rotated 90°. Proposed: play holding the board sideways (landscape), with no rotation in code. Remap the buttons once the user confirms which edge they sit on.

## 2026-11-22: Phase 1 research landed

- `phase1-mg24quake-notes.md`: MG24Quake uses 275.9 KiB of RAM, with a 320×152 3D view plus a 48-row status bar. The biggest buffers are the z-buffer (95 KB, which also holds the edge tables and network buffers), the frame buffer (48.5 KB) and the zone (47 KB). Its estimate against 232 KB: 320×200 doesn't fit (45 KB over), 240×150 fits barely (5 KB spare), 160×100 fits comfortably (29 KB spare). Our measured budget is about 245 KB, which adds about 13 KB to each margin.
- `research-open-source-quake-bases.md`: no public Quake runs on an FPU-less MCU without PSRAM. The best bases are **zenodante/RP2350-Quake** (an MG24Quake fork on pico-sdk, flash-resident data, a core-1 LCD path and an SDL host build) and MG24Quake itself for its memory tricks. The combined work must be **GPL-3.0**.

## 2026-11-22: Project skeleton and Phase 2 platform layer (built, not run on hardware)

**Setup**
- The Pico VS Code extension already provides SDK 2.3.0, GCC 15.2 (`15_2_Rel1`), picotool 2.3.0, ninja and cmake under `~/.pico-sdk`. That's exactly what the BSP expects.
- Cloned `wiliOGbsp` and `MG24Quake` into `third_party/`. They are git-ignored and not vendored.
- Top-level `CMakeLists.txt` consumes the BSP via `add_subdirectory(third_party/wiliOGbsp/bsp)`, as its AGENTS.md documents for outside projects.
- `CMakePresets.json` points `picotool_DIR` and `pioasm_DIR` at the extension's prebuilt tools. Without this the build tries to compile picotool from source and fails (lessons.md, L1).

**Code**
- `platform/qog_flashmap.h`: display flash map. App ≤ 3.87 MB at 0x021000, game data at 0x400000 (up to 11.9 MB), saves at 0xFF0000 (64 KB).
- `platform/qog_data.[ch]`: QOGD image format and an in-place reader. The header has its own CRC, an optional full-body CRC, and name lookup returns XIP pointers.
- `platform/qog_video.[ch]`: an 8-bit frame buffer goes through the palette, gets scaled to 320×out_h, and is DMA'd to the ST7789 with 16-bit SPI frames, all on core 1 with two line buffers. Palette changes are latched per frame. There is a core-1 hook for sound later.
- `platform/qog_input.[ch]`: buttons map to game actions per the plan's control table (green as fire/modifier, yellow+blue for menu). Tilt comes from the LIS3DH with zero calibration and a dead zone.
- `apps/qog/`: display test app (pattern, button-driven box, fps, still image from flash) and a minimal main companion.
- `tools/mkdata.py`: packs files into a QOGD image and a UF2 at 0x10400000, with a `--test-pattern` mode. `tools/tests/test_mkdata.py`: 4 host tests, passing.
- `tools/check_app_size.py`: the build fails if the app would overlap the data region.

**Measured (from the build, not the board)**
- `qog_display`: 46 KB of flash, 72.5 KB of static RAM, of which 64 KB is the 320×200 test frame buffer. So the BSP plus the platform layer take about **8.5 KB of static RAM**.

**Plan deltas**
- The plan's "232 KB for Quake" assumed a 32 KB bare display app. Measured static use is about 8.5 KB, and stacks live in the separate 2×4 KB scratch banks. The working figure is now **about 245 KB, pending a runtime heap measurement**.
- The "main CPU runs stock firmware" wording changed: with this BSP the main CPU runs *our* 30-line companion (`apps/qog/main/main.c`), built from the template. It's easier to disclose because anyone can read all of it. Note that the BSP's main `board_init()` also loads the stock FPGA bitstream.
