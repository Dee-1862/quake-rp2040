# Changes log

Newest first. Each entry says what changed, why, and whether it ran on hardware.

## 2026-11-26, one copy of the lit surface drawing for both cores, exact (PC and ARM build, registered data, not run on hardware)

Both cores draw lit surfaces during the edge scan, and each ran its own copy of the code, so the same hot functions sat at two flash addresses and competed for the 16 KB XIP cache.

- `QOG_BOARD_SHAREDDRAW` (on, define `QOG_DRAW_SHARED`), the compilation in d_core1.c, d_core1s.c and d_core1e.c is the one both cores run. What the drawing writes is in a context per core (engine/d_drawctx.h), picked by SIO CPUID. The values core 0's brush entity surfaces change (the entity and the view vectors) and the light grids are core 0's own or core 1's copies, picked the same way. OFF builds the two copies as before.
- Core 0's turbulent surfaces take their gradients from the same code and copy them to the globals `Turbulent8` reads.
- The span loop and the pixel loops (`litq_wrap_asm`, `litq_span_asm`, `litq_zruns`) already took their values through pointers and are unchanged.
- port/rp2040/ld_hottext now also lists `litq_wrap_asm`, which it had missed.
- tools/xip_sets.py, scan phase with the code beside it (the new list in both builds), 3,108 lines and 1,060 excess with two copies, 2,481 lines and 433 excess with one. 433 is the least any layout gives.
- The shared functions are bigger, since they find their context first. The drawing group is 9.0 KB against 7.6 KB for one of the two old copies. This costs an estimated 0.1 ms a view.
- Estimate of the gain, 0.5 to 2 ms a frame in demo3 Fast. It is not measured. boardbench does not see the cache, and a `QOG_BOARD_XIPSTATS` build on the board with the option on and off would measure it.
- RAM (README build line), quake_display `end` 0x2003f710 to 0x2003f77c (108 B more, 132 B spare beside the 2 KB heap reserve), quake_main unchanged at 0x2003f5b4.
- tools/core1_syms.py knows the shared build and passes, also on the XIPSTATS and FPCOUNT builds.
- The PC's measurement sums are not in the shared build, so `QOG_PC_BOARDMODEL` keeps two copies.
- Checks, qogtest_hash `ce86f894f9de` (hidden, `-nosound`). 113 shots (`con_notifytime -1`, then `qogtest_d_shots`) in r_fast 0, 1 and 2 are identical to main on the default PC build, and on a build with the board's render flags that draws on core 1 (`QOG_PC_EXTRA_DEFS` with `QOG_DRAW_CORE1`, `QOG_DRAW_SHARED` and the flags beside them in pc/CMakeLists.txt), on its second thread and with `-core1first` and `-core1last`. The split pair was not run. With the option OFF the board build has main's function sizes and `end`.

Files, engine/d_drawctx.h (new), d_core1.h, d_core1.c, d_edge.c, d_scanlit.c, d_lit_q.h, r_surf.c, apps/quake/CMakeLists.txt, port/rp2040/ld_hottext/default_text_excludes.incl, tools/core1_syms.py.

## 2026-11-25, The last two scanout waits, the row race and the edge cache tail, exact (PC and ARM build, registered data, not run on hardware)

In run10_8 (f4dfb75) timedemo demo3 still waits for the LCD. Turbo windows show up to 4.5 ms of `scanwait` a frame, mostly 0 to 3 (the wait for the end of the scan before R_ScanEdges). Fast windows show 3 to 6 ms of `race` (row waits for the edge cache in the first rows of the frame).

- `QOG_BOARD_ROWRACE` (on), R_ScanEdges runs beside the last scanout. It first waits for the z-buffer region and the view's z-buffer, then each span flush and each surface sent to core 1 waits only for the rows down to the scan line reached (r_edge.c R_QogRowFree). The wait for the end of the scan moves after R_ScanEdges, before the models, particles and warp. Core 1 draws nothing until its scanout ends, so core 0 draws more surfaces itself at the start of the edge scan, and the gain is smaller than the wait it removes.
- `QOG_BOARD_ECTAIL` (on, not in Turbo views), the world's edge cache goes to the end of the z-buffer region, which no scanout reads, while R_RenderWorld runs, and edge_max stops the edge table below it. A view whose world edges reach it moves the cache to the frame rows after the vertex cache (R_QogEdgeCacheOut), and the table gets its whole room back, so no view gets fewer edges. demo3 never moves it. `QOG_ECTAIL_TEST=300` forces the move for tests.
- boardbench (300 MHz, PIO 60 MHz), main 5dc4518 to this, Fast 21.9 fps (21.6 to 21.8) to 24.0 (23.7 to 24.0), Turbo 26.6 (25.9 to 27.0) to 27.5 (26.7 to 27.9). The board measured 22.0 and 26.0 on f4dfb75. tools/boardmodel.py now keeps core 1 busy with the rest of the scanout in its pipeline (without that it overstates the row race).
- Checks, qogtest_hash `ce86f894f9de`. Against builds of main 5dc4518, 121 shots each in Exact, Fast and Turbo are identical in the single process build, and in the split pair with the board flags in Exact and Fast, also with `QOG_ECTAIL_TEST=300` (the cache moved in more than 1,200 views a run). The Turbo split pair differs in 2 shots in status bar rows only (y 154 and below), which the 100 ms status bar cap sends by wall clock time. scanrace 0 writes before the scanout passed, lockcheck 0 changed while scanned. tools/core1_syms.py passes.
- RAM (board build with Turbo at 300 MHz), quake_display `end` 0x2003f704 (252 B spare beside the 2 KB heap reserve), quake_main 0x2003f5b4.
- What the board log should show in the demo3 windows, Fast `race` from 3 to 6 ms down to about 1 or less, Turbo `scanwait` near 0. Part of the wait moves into core 0's time in the edge scan (more `pipe` work on core 0, fewer `c1` surfaces), so judge by the Benchmark line.

Files, engine/r_edge.c, r_main.c, r_draw.c, r_local.h, vid.h, apps/quake/CMakeLists.txt, pc/CMakeLists.txt, port/pc/vid_pc.c, boardmodel_pc.c, tools/boardmodel.py.

## 2026-11-25, alias span pixels and the lit span loop in Thumb, exact (not run on hardware)

- `QOG_BOARD_ASPANASM` (on) draws id's alias spans (models and the view weapon) with hand written Thumb from flash (port/rp2040/d_aspan_rp2040.S). The light and the depth step on the interpolators. In the emulator the span drawer takes 0.65 of the C's cycles on random span lists and 0.73 to 0.83 on model meshes (67.4 to 49.3 cycles a pixel on 100 pixel meshes, 110.7 to 91.9 on 24 pixel ones). Player colours still take the C.
- The loop over a lit surface's spans in the Fast and Turbo drawer (the span start, the z choice, the calls) is also Thumb from flash (`litq_wrap_asm`, port/rp2040/d_litq_rp2040.S), on both cores. An emulator replay of demo3 surfaces puts the loop at about 150 cycles a span against about 185 before (about 1% of the lit spans). Output is byte identical.
- Neither names a C global. Their values come through a pointer (lessons L245).
- Each is checked once at start-up against its C (`D_QogAspanCheck`, `D_QogLitWrapCheck`) and the C is used if they differ, with a console line.
- RAM on the display grows 12 B, 256 B stay spare beside the heap reserve. No code was added to RAM.
- PC checks with r_fast 0, 1 and 2 against main give identical shots (121 frames), also with the board's render flags (one frame, d_demo1_12 in Fast, differs between two runs of main itself). qogtest_hash is ce86f894f9de. tools/core1_syms.py passes.
- boardbench predicts demo3 Fast 22.2 fps (main 21.9) and Turbo 26.9 (main 26.6) at 300 MHz. The span loop is not in the model.

## 2026-11-25, XIP measurement build and the code placement cure, fifth round, exact (ARM build, registered data)

The board's Turbo run (26.0 fps, run10_8) shows world 4.4 to 7.5 ms, bmodels 2.2 to 5.6 and edgescan 5.0 to 8.1 per view between windows, more than their work counts move, and the world front end's compute is about 1 M cycles against about 3.3 M measured. This round makes the build that measures the XIP cache per phase and prepares the cures that need no RAM. Nothing ran on hardware.

Measurement build (for one flash, with the user's approval).

```
cmake --preset target -B build-xip -DQOG_SPLIT=ON -DQOG_AOT=ON -DQOG_AOT_PAK=<pak0.pak> -DQOG_SPLIT_MAIN_EMBED=quake_display -DQOG_SOUND=ON -DQOG_CL_HUNK_KB=75 -DQOG_CL_STACK_KB=31 -DQOG_SV_HUNK_KB=174 -DQOG_SV_STACK_KB=21 -DQOG_SYS_KHZ=300000 -DQOG_SYS_MV=1250 -DQOG_FLASH_FAST=ON -DQOG_FAT_FORMAT_IF_MISSING=ON -DQOG_BOARD_TURBO=ON -DQOG_BOARD_XIPSTATS=ON
cmake --build build-xip --target quake_main
```

- Beside each `[time cl] views` line it prints `[time cl] xip acc/miss` (XIP cache accesses and misses per view in each phase, both cores, edgescan as in the views line) and `[time cl] work per view` (nodes, leafs, faces, clipedge, emitedge, cached, project, clipdot, rotate, rottrig).
- RAM, with the client stack at 32 KB the display did not link (144 B over the heap reserve). The float call arrays of the FPCOUNT build (112 B) are now kept only in that build (port/common/qog_phase.c, engine/qog_phase.h). That brought `end` to exactly 0x2003f800 with nothing spare, so the build takes 1 KB of the client stack, as the FPCOUNT build already does, quake_display `end` 0x2003f400 (1,024 B spare beside the 2 KB heap reserve). In run10_8 the least client stack free is 5,400 B of 32 KB, so 31 KB keeps about 4.4 KB in demo3. Watch the `stack free` value of the `link wait` lines.
- The normal build is unchanged, quake_display `end` 0x2003f6f4, quake_main 0x2003f5b4.
- How to read it, the world front end touches about 4,900 lines of BSP data per view (below). World misses far above that are code misses, which `QOG_BOARD_HOTTEXT` addresses. Misses near it are the data.

`QOG_BOARD_HOTTEXT` (off, exact, no RAM), code placement.

- The XIP cache has 1,024 sets of two 8-byte lines, so flash 8 KB apart shares a set. The hot render code was spread over about 180 KB of flash text (world over 15.8 KB, the edge scan over 107 KB, bmodels over 135 KB, core 1's surface code over 25 KB), so code that runs together put three or more lines in a set and evicted itself.
- port/rp2040/ld_hottext/default_text_excludes.incl replaces the SDK's file of that name for quake_display only (`pico_add_linker_script_override_path`). It lists 150 functions by section in ten groups and keeps the SDK's catch-all line last, so everything else follows in the usual order. There are no source changes. The same code sits at other addresses, all 1,595 function sizes match the normal build, RAM `end` is the same, and text is 32 B smaller (padding).
- Groups, helpers (float and divide wrappers, 616 B), c1loop (core 1's scanout and job loop, 1,752 B), world (9,376 B), bmodels (6,572 B), c1alias (1,288 B), models (12,368 B), scan (10,456 B), spans0 (core 0's lit surface drawing, 3,976 B), c1spans (core 1's copy, 7,596 B), core1x (8,772 B). Each group's comment names the groups that run beside it on the other core. world and bmodels together are about 16 KB, so c1alias and models start in the sets right after c1loop, as world does.
- tools/xip_sets.py (new) reads the groups and a link map. For each group with the groups beside it, it counts the code lines beyond two in a set (lines that cannot stay cached together) and the least any layout can give.

| Phase, with the code beside it | Lines | Excess today | Excess HOTTEXT | Least possible |
|---|---|---|---|---|
| world, helpers, c1loop | 1,472 | 277 | 0 | 0 |
| bmodels, helpers, c1loop | 1,123 | 48 | 0 | 0 |
| models, c1alias, helpers, c1loop | 2,007 | 391 | 55 | 0 |
| scan, spans0, c1spans, helpers, c1loop | 3,060 | 1,179 | 1,003 | 1,003 |

- Estimate, a miss costs about 0.2 us at flash 150 MHz (about 30 flash clocks a line). Suppose a fifth of the world's 277 excess lines are on the per-face path (197 faces a view). That is about 11,000 misses, or about 2 ms a view. Lines that run per node or per edge count for more and lines that run once per view for less, so the range is about 0.3 to 2 ms for the world, up to about 0.5 for bmodels and up to 1 for models. The scan phase keeps its excess in any layout, so its gain is small.
- The XIPSTATS build can take it too. `-DQOG_BOARD_HOTTEXT=ON` beside `-DQOG_BOARD_XIPSTATS=ON` links with `end` 0x2003f400 and the same zero excess for world and bmodels (24 for models, 990 for the scan, its least). Two flashes, with and without, measure the cure directly. The group lists are a judgement from the call paths, not a profile. If the counts confirm the problem, a `QOG_PROF` run (the board PC-sampling profiler) gives the heat to trim each group to what really runs.

A finding for the scan phase (not changed here, the area).

- During the edge scan both cores draw lit surfaces (QOG_DRAW_CORE1) and each runs its own copy of the drawing code (d_core1.c compiles d_scanlit.c, r_surf.c and d_edge.c again under c1_ names). About 6.3 KB of identical functions are then hot twice, D_CalcGradients, D_QogGradFast, D_QogGradRows, D_QogDrawLitBody, D_SetupLitSurface, R_BuildLightMap, D_LitFixSetup, litq_start64, D_DrawSpans8LitQ and the rest. With the edge scan code, that is about 24 KB of code against a 16 KB cache in any layout. One copy that reads its state through a per-core pointer would take the excess from about 1,000 lines to about 200.

BSP data, estimated from demo3's counts per view (nodes 271, faces 197, clipedge 982, about 480 unique vertices) and the board's structures (mnode_t 36 B, msurface_t about 32 B, medge_t 4 B, mvertex_t 12 B, surfedges 4 B, mplane_t 20 B), with nothing kept from the last view, about 1,220 lines of nodes, 790 of surfaces, 450 of surfedges, 900 of edges, 960 of vertices and 600 of planes. That is about 4,900 lines, or 1 ms a view. Ways to cut it without RAM (none built).

- The baker (QOG_BAKE_WRITER) could renumber edges in surfedge order and vertices in first-use order, so a face's edges share lines and its surfedges read in order. That saves about 700 lines, or 0.14 ms a view, and it is exact (indices only), but it needs a new data image.
- Not worth it, the XIP no-allocate alias for BSP data (each field read becomes its own flash read, 5 to 12 reads for a node against 5 line fills), a DMA read as a prefetch (XIP has one bus port, so the DMA's fill blocks core 0's own code fetches unless that code runs from RAM), and the XIP stream (it needs a RAM target, and only the lightmaps are sequential, 2,392 cells a view, about 300 lines, 0.06 ms).

Moving hot code out of flash, RAM has 268 B spare, so only a stack trade could pay for it. A 1 KB cut of the client stack (demo3 leaves 5,400 B free) would hold about 860 B of the hottest per-node and per-row code (R_QogNodeSide, R_QogCullNodeInt, R_InsertNewEdges, R_StepActiveU). It is not prepared, because it needs RAM and the user's decision on the stack margin.

Checks, qogtest_hash `ce86f894f9de` (PC build, hidden, `-nosound`). The PC engine code is unchanged (the only source change is the FPCOUNT guard), so the shots were not rerun. tools/core1_syms.py passes on the HOTTEXT and XIPSTATS builds. boardbench does not see code layout, so it has no before and after here.

Files, port/rp2040/ld_hottext/default_text_excludes.incl and tools/xip_sets.py (new), apps/quake/CMakeLists.txt (`QOG_BOARD_HOTTEXT`), port/common/qog_phase.c and engine/qog_phase.h (the FPCOUNT arrays only in that build), docs/lessons.md (L310 to L313).

## 2026-11-25, The board cost model recalibrated on the board's run of d31520e, the Turbo status bar cap what-if (PC only, registered data)

The board measured main d31520e at 20.0 fps in Fast and 22.3 in Turbo (300 MHz, flash 150, LCD by PIO at 60 MHz), against boardbench's 26.5 and 30.4 from the b585365 calibration. Nothing ran on hardware here, the numbers come from the lead's log.

- tools/boardmodel.py `calibrate-board` (new), from the b585365 calibration, each phase's residual set so that the Fast log of d31520e gives the board's phases (`BOARD_F7`, frame weighted means of the demo's `[time cl]` lines), the surface work scaled to core 0's share plus core 1's `pipe`, core 1's speed fitted to core 0's share (0.98), the time around the view taken from the board (2.6 ms before, 0.5 after) and one scale on the waits fitted to the frame (1.05, so the waits the code implies were right and the three reconciliations are gone). Every sample of the range is recalibrated the same way.
- Check on the run not used to calibrate, Turbo 43.9 ms (22.8 fps) against the board's 44.8 (22.3), phases within 0.4 ms except vmodel (2.0 against 2.4).
- The wait table splits like the board's counters, row waits, the whole scanout in status bar frames (the edge table's wait at those rows, which the board counts as scanwait), after the world and in notify frames.
- `hudcap:N` what-if, in Turbo the status bar rows go out at most N times a second of board time, a change waits for the next allowed frame (the earlier cap). Turbo 0.9 ms alone (1.2 at 5 a second), 4.2 with the notify clear, 6.7 with the whole race package.
- New files, tools/boardmodel_f7d9450_fast.csv.gz and _turbo.csv.gz (the PC logs of d31520e), tools/boardmodel_calib.json is now the board calibration, the b585365 one is tools/boardmodel_calib_de58316.json.
- docs/fps-board.md, the board row, the phase comparison, the gap to 35 fps in Turbo (16.3 ms on the board, about 8.7 ms of compute after the scanwait fix) and the re-ranked attacks. Lessons L260 to L262.

## 2026-11-25, The pipeline with Turbo checked, particle physics on both cores, and why the world front end does not split yet (PC and ARM build, registered data)

Nothing ran on hardware.

### The two-core pipeline together with Turbo, the PIO scanout and Turbo text, the cuts (main 9b0fef4)

Hidden, `-nosound`, own build folders and data folder, link port 27961. The references are builds of the same tree without `QOG_DRAW_CORE1`, so they are main.

| Run | Result |
| --- | --- |
| quakeog_pc with and without the pipeline, r_fast 0, 1 and 2 (thread), r_fast 2 with `-core1first` and `-core1last`, the 121 frames (`qogtest_k_quiet` + `qogtest_d_shots`, which play demo1, demo2 and demo3, and the 8 fhash frames) | 121 of 121 identical in every run, `qogtest_f_hash` e92bc9e9650d in every run |
| the board-flag pair (the earlier pcb flags, one-byte z, 32-byte surfaces, interpolators, scan race, status bar skip, vertex cache) with and without `QOG_DRAW_CORE1`, r_fast 0, 1 and 2, and r_fast 2 with `-core1last` | 121 of 121 identical, f_hash e92bc9e9650d, `qog_scanrace` 0 writes before the scanout passed (4,409 and 3,512 checks in Turbo), `qog_lockcheck` 0 changed of 3,550 and 3,512 |
| `qogtest_ao_switch.cfg` (Exact, Turbo, Fast, Turbo with notify text, the menu, e1m3, e2m1, e1m6) on the pair and on quakeog_pc | no error, scan race 0 of 521, lock check 0 of 317. 15 of 16 frames identical. sw_notify differs, but two runs of the reference also differ from each other there (the notify text times out in real time) |

Core 1's copies and Turbo, core 1 keeps no copy of the view's size. The 160-wide rows (screenwidth, d_viewbuffer), the z-buffer Turbo moves into the frame buffer (d_pzbuffer, d_zwidth) and the doubled aspect (xscale, xscaleinv, xcenter) are values both cores read and nothing writes during the scan (tools/core1_syms.py lists them as shared), and core 1's copies of the view vectors, modelorg and the Fast gradient rows are made from the Turbo view's own values every view. The edge table is in the z-buffer memory and the Turbo z-buffer in the frame buffer, so every surface may go to core 1 early. the three statics in d_edge.c belong to the brush entity path and D_DrawSurfaces, which only core 0 runs, and the view planes are restored after the flush has waited for core 1. The board build with `-DQOG_SYS_KHZ=300000 -DQOG_SYS_MV=1250 -DQOG_BOARD_TURBO=ON` links, display `end` 0x2003f588 on 9b0fef4.

### Particle physics on both cores (`QOG_PARTS_CORE1`, board option `QOG_BOARD_PARTSCORE1`, on)

id's R_DrawParticles draws a particle and then moves it. The drawing stays on core 0 in id's order, since particles can cover each other's pixels. The moving reads and writes only its own particle, so core 0 moves two of every five particles and core 1 the other three, each only after core 0 has drawn it and unlinked its dead successors (a shared count). A moved particle is not read again in the frame (the kill test only looks at particles not yet drawn), so the result is the same in any order. 32 B of display RAM.

Exact, quakeog_pc with it against main, r_fast 0, 1 and 2 (thread), r_fast 1 and 0 with `-core1last`, r_fast 2 with `-core1first`, 121 of 121 frames identical each, `qogtest_hash` ce86f894f9de, `qogtest_f_hash` e92bc9e9650d. The board-flag pair with it, r_fast 1 and 2, 121 of 121 identical, scan race and lock check 0.

boardbench (300 MHz, PIO 60 MHz), parts 0.8 to 0.5 ms, Fast 26.5 to 26.7 fps (37.7 to 37.4 ms), Turbo 30.4 to 30.7 fps (32.9 to 32.6 ms). The model counts the moving at half its old cost, core 1's three fifths of it (about 108,000 cycles) is longer than core 0's drawing and two fifths (about 90,000).

Display `end` 0x2003f6ac on main d31520e, 0x2003f6cc with this (308 B spare beside the 2 KB heap reserve). quake_main is unchanged.

### Why the world front end does not split onto core 1 yet (attack #2)

- The work, per demo3 view the world walks 265 nodes and 33 leafs and renders 193 faces (960 edge clips, 450 edges emitted, 487 points projected, 1,309 plane distances). With boardmodel's weights the walk (nodes and leafs) is about 7% of the world's cycles and R_RenderFace about 93%. A walk on core 1 saves at most 0.6 ms.
- R_RenderFace's emission is sequential. Edges and surfaces are appended in walk order, the edge cache decides between reuse and emission from what was emitted before, and the scan sorts ties by that order, so two cores cannot emit at once without changing pixels. Moving all the emission to core 1 only helps if core 0 has other work meanwhile, and bmodels must follow the world's edges.
- Core 1 is not free during the world, the next view starts while the last frame goes to the LCD, and the PIO scanout (152 rows of about 86 us, 13 ms) covers the whole world front end in Turbo and the part after the race wait in Fast. The scanout itself is mostly waiting (a row's conversion is about 3 us of its 86).
- So the first step is an interrupt-driven scanout, the DMA's end-of-row interrupt on core 1 converts the next row and starts it, and core1_main runs jobs in between. With it, core 1 can fill the world's vertex cache ahead of core 0 (projection and plane distances, about 30% of the world's cycles, the same values whoever computes them, an entry written data first and tag last and read tag, data, tag), which is exact and gives about the model's world x 0.7 (2 ms Fast, 1.6 Turbo). Splitting the edge scan as frank-quake2 does would take core 1 from the surfaces it already draws during the scan.
- Not done here, the interrupt scanout changes the earlier PIO code and can only be proven on the board.

### The alias front end on core 1 (attack #5, the rest)

Pipelining entity N+1's front end (bbox test, lighting, vertex transforms) on core 1 while core 0 draws entity N's triangles needs a second finalvert buffer (up to 328 vertices of 28 bytes, about 9 KB). The display has 308 B spare and core 1's stack is 2 KB, so it is not done. Splitting the triangle rows between the cores (exact, each pixel still gets its triangles in order) needs core 1's own span package array (about 6 KB in id's D_PolysetDraw). The earlier vertex halves stay as they are.

Files, engine/r_part.c, apps/quake/CMakeLists.txt, pc/CMakeLists.txt and port/pc/boardmodel_pc.c (the boardbench build), tools/boardmodel.py (the particle weight).

## 2026-11-25, World front end per call, fourth round, exact (PC and ARM build, registered data)

The board's run (log run9_8.txt) puts world at 9 to 11 ms a view while standing (8.8 to 14.8 in the demo), against about 1 M cycles of work the agents count, so much of it is likely flash reads of the BSP data and code, with core 1's span texels through the same 16 KB XIP cache. That cannot be measured on the PC, and no RAM is left for code or data copies (about 340 B), so this round cut the per call work that can be counted. Two flags, exact, r_fast 0, 1 and 2 draw the same bits as main d31520e. Nothing ran on hardware.

### Boardbench (300 MHz, LCD by PIO at 60 MHz)

| | Fast fps | Turbo fps | world ms (Fast / Turbo) |
| --- | ---: | ---: | ---: |
| main d31520e | 26.5 (24.6 to 28.0) | 30.4 (27.7 to 31.9) | 8.2 / 7.9 |
| this branch | 26.7 (24.8 to 28.1) | 30.6 (27.7 to 33.2) | 8.0 / 7.6 |

### What the model priced (Turbo, main, world, ms per view)

points projected 1.84 (506 a view), faces 1.78 (193), view plane distances 1.51 (1,373), clipped edges 1.19 (960), emitted edges 0.82 (450), nodes 0.54 (265). A vertex cache of 1,024 entries instead of 128 in Fast and Turbo only took the projections from 506 to 486 and the distances from 1,337 to 1,270 (PC, demo3, Turbo), so the misses are first uses and a bigger cache in the frame buffer is not worth its rows.

### Changes

| Change | Flag | What it does |
| --- | --- | --- |
| A vertex's four view plane distances at its first need | `QOG_CLIPDIST_FILL4` (r_draw.c R_QogClipDistFx) | Fast and Turbo, when R_ClipEdge first asks a world vertex for one plane's distance, its position is already in 1/32 units, so the other unknown slots of its cache entry are filled with the same statement (three multiplies each). A vertex was asked about 2.8 times a view, so the distances computed in a demo3 view went from 1,337 to 843 (the rest of the old count are brush entity vertices, which have no cache entry) plus 437 fills. The same values, so the same pictures. New count `wc_clipfill` (50 cycles) |
| Node side and facing by the float's bits | `QOG_NODESIDE_BITS` (r_bsp.c R_QogNodeSide) | R_RecursiveWorldNode's fdot >= 0, fdot < -0.01f and fdot > 0.01f by the bits of the finite float difference (both zeros are >= 0, and the magnitude's bits order like the values), in a helper that is not inlined so the recursion's frame stays 48 B. One float subtraction a node, no compares. Priced at 0.25 of the node's float calls |

### Exact proof (hidden, `-nosound`, main d31520e, which has the same tree as the third round's tested branch, with the board's world flags)

| Test | Result |
| --- | --- |
| `qogtest_hash`, default quakeog_pc and with the world flags | ce86f894f9de |
| 121 shot frames, all 200 rows, r_fast 0, 1 and 2 | 121 of 121 byte identical in each |
| check build (`QOG_NODESIDE_CHECK`), timedemo demo1 r_fast 0 and demo3 r_fast 1 and 2 | 131,072 node tests in each run counted to the last print, 0 differ |
| tools/core1_syms.py | passes |

### RAM (full configure line with `-DQOG_SYS_KHZ=300000 -DQOG_SYS_MV=1250 -DQOG_BOARD_TURBO=ON`)

quake_display `end` 0x2003f6ac, the same as main, quake_main 0x2003f5b4. Stack, R_RecursiveWorldNode 48 B a level as before, R_QogClipEdgeFast 48 B as before, R_QogClipDistFx 32 B (40 before), R_QogNodeSide a 24 B leaf.

### Not done

- Flash, the world's hot code (R_RenderFace about 1.4 KB, the Fast projection 0.9 KB, the clip and emit paths) and the BSP data are read through the XIP cache that core 1's texels also use. A board run with `QOG_BOARD_XIPSTATS` (the `[time cl] xip` line) during demo3 would tell how much of world is misses. If it is most of it, the lever is code placement or RAM, not arithmetic.
- Holding a world vertex's 1/1024 position in its cache entry would also let the projection skip its three conversions, but every entry grows by 12 bytes in the frame buffer rows the scanout race watches.

Files, engine/r_draw.c, engine/r_bsp.c, engine/qog_phase.h (one count), port/pc/boardmodel_pc.c, tools/boardmodel.py, pc/CMakeLists.txt, apps/quake/CMakeLists.txt.

## 2026-11-25, Alias triangles, third round, all exact (PC and ARM build, registered data)

Models (4.0 ms in Fast, 3.3 in Turbo by the model) and the view model (2.7 and 2.0). Three flags in d_polyse.c, on the rasterizer side only (r_alias.c and r_main.c, where earlier work moves the front end to core 1, are not touched). All exact, r_fast 0, 1 and 2 draw the same bits as main 2c4ba79. Nothing ran on hardware.

### Boardbench (300 MHz, LCD by PIO at 60 MHz)

| | Fast fps | Turbo fps | models ms (Fast / Turbo) | vmodel ms (Fast / Turbo) |
| --- | ---: | ---: | ---: | ---: |
| main 2c4ba79 | 26.1 (24.3 to 27.7) | 30.0 (27.1 to 31.9) | 4.2 / 3.5 | 2.8 / 2.1 |
| this branch | 26.5 (24.6 to 28.0) | 30.4 (27.6 to 31.9) | 3.7 / 3.1 | 2.7 / 2.0 |

New counts per view (Fast / Turbo), 21.3 / 2.3 small triangles of 124.6 / 64.6 that used to take gradients, 259 / 249 subdivision leaves tested inline of 494 / 477 calls. The coordinator's main figure was 27.0 / 30.9 on another run, this machine gave 26.1 / 30.0 for the same commit, so compare the two rows above.

### What the model priced (Fast, main, ms per view)

models, recursive subdivision calls 0.86 (495 a view drawing 29 points), gradient triangles 0.81 (125 a view, 19 with no pixel and 61 with 1 to 4), vertices 0.55, span pixels 0.51, triangles looked at 0.42, span rows 0.38. vmodel, span pixels 1.30 (2,777 a view), span rows 0.62, clipped triangles 0.42, gradient triangles 0.24.

### Changes

| Change | Flag | What it does |
| --- | --- | --- |
| Small triangles without gradients | `QOG_ALIAS_SMALLTRI` | a triangle of at most two scan lines runs only the integer edge steps first (D_PolysetSetUpForLineScan's quotients, the left edge's counts as D_PolysetScanLeftEdge makes them, the right edge's as D_PolysetDrawSpans8 steps them). With no pixel nothing else happens (id's path would only set globals the next triangle sets again). With one pixel at the start of the first line, that pixel takes the top left corner's own s, t, light and 1/z, since id adds the steps only after it, so its texel, colour and depth need no gradient. Anything else takes id's path. the measured 2,250 cycles a gradient triangle at one pixel become the integer edge steps |
| Subdivision leaves inline | `QOG_SUBDIV_LEAF` | a call of D_PolysetRecursiveTriangle whose three edges are each at most one pixel long returns at once, so the caller tests that inline and leaves the call out. The same points in the same order |
| Left edge in locals | `QOG_LEFTEDGE_LOCALS` | D_PolysetScanLeftEdge with the edge's state in locals, written back at the end (the engine's -fno-strict-aliasing made every span package store reload every d_ global, L220) |

New counts for the model. `am_subleaf` and `am_smalltri` (and `vm_smalltri`), weights 25 and 150 integer cycles from the Thumb code. A small triangle is counted as `smalltri`, not `smooth`. `QOG_LEFTEDGE_LOCALS` scales the span row weight by 0.85.

### Exact proof (hidden, `-nosound`, an untouched build of 2c4ba79 with the board's world flags)

| Test | Result |
| --- | --- |
| `qogtest_hash`, default quakeog_pc and with the world flags | ce86f894f9de |
| 121 shot frames, all 200 rows, r_fast 0, 1 and 2 | 121 of 121 byte identical in each (so 0% of the Fast and Turbo pixels changed) |
| check build (`QOG_ALIAS_SMALLTRI_CHECK` with `QOG_ALIAS_STATS`), each small triangle's prediction (pixels, colour, depth, or the old bytes when the depth test fails) against what id's path then drew, timedemo demo1 r_fast 0 and demo3 r_fast 1 and 2 | over 32,000 small triangles (counted to the last 16,384 printed), 0 differ |
| tools/core1_syms.py | passes |

### RAM (full configure line with `-DQOG_SYS_KHZ=300000 -DQOG_SYS_MV=1250 -DQOG_BOARD_TURBO=ON`)

quake_display `end` 0x2003f6ac, the same as main, no static RAM. quake_main 0x2003f5b4. Stack, D_PolysetScanLeftEdge 144 B (120 before) under D_RasterizeAliasPolySmooth's 152 (unchanged, the small triangle test is inlined), D_PolysetRecursiveTriangle 64 B a level as before.

Not done, the view model's span pixel loop (1.3 ms) keeps 14 values live through each pixel on a core with 8 low registers, so GCC spills, and only an assembly loop would change that. The gradient triangles with two or more pixels keep id's path.

Files, engine/d_polyse.c, engine/r_qogalias.h, port/pc/boardmodel_pc.c, tools/boardmodel.py, pc/CMakeLists.txt, apps/quake/CMakeLists.txt.

## 2026-11-25, The exact race package, demo3's scanout waits cut by 4 to 7 ms a frame (PC and ARM build, registered data)

The earlier board model ranks the scanout waits first (docs/fps-board.md). This branch removes most of them exactly, so the panel shows the same pixels. The biggest wait was not where the ranking put it. Nothing ran on hardware. The board numbers below come from two PC models, the boardbench, and the pair's `-scanmodel`.

### Where a demo3 frame waited (main ff5bd24, PC pair with the board client's flags)

The pair now records, for every timedemo frame, how far into the scanout the next frame waited in each part of its work. `VID_QogRaceMark` marks the parts, before the view, the world's front end, after R_RenderWorld, after the brush entities. Each wait site was then tagged once by hand.

| Wait | demo3 frames (of 1,094) | Why |
| --- | ---: | --- |
| SCR_SetUpToDrawConsole clearing the notify rows (`clearnotify`) | 898 | Every frame after one with notify text clears rows 0 to 31, and the clear waited for the end of the scanout. The 3D view then draws over those rows. In Turbo the clear also spread the packed view first (Draw_TileClear), and the earlier text list made that happen in more frames |
| the edge table at the end of the z-buffer, in frames that send the status bar | about 40% | The table (1,800 edges of 24 B) starts 5,344 B into the z-buffer, inside the status bar rows the scanout sends last, so it waited for the whole scan |
| after R_RenderWorld | the rest | R_DrawBEntitiesOnList waited for the end too |

### The changes, all exact

| Change | Where | Flag |
| --- | --- | --- |
| A notify clear or centre print erase is skipped when the coming 3D view covers its rows over the whole width. V_RenderView runs after SCR_SetUpToDrawConsole and returns early only with con_forcedup. In Turbo this applies only to rows the packed pixels cover, the first half of the view. Any other clear keeps its old wait | engine/screen.c SCR_QogViewCovers | QOG_VID_SCANRACE |
| The brush entities' front end runs before the wait for the end of the scanout. It writes only the edge table (released before the world) and the brush clip buffers at the start of the z-buffer. The clip buffers get their own row wait (`VID_QogScanFree` of BMODEL_CLIP_BYTES). The wait for the end moves to just before R_ScanEdges | engine/r_main.c R_EdgeDrawing | QOG_VID_SCANRACE |
| A scanout that sends the status bar rows sends them first, rows 152 to 199 in a window of their own, then rows 0 to 151. The scan position the game waits on counts rows in that order (`qog_video_set_first`, `VID_QogScanNeed` in send order). The edge table and clip buffers then wait for 48 rows (4.1 ms at 60 MHz) instead of 200 (17.1 ms) | platform/qog_video.c, port/rp2040/vid_rp2040.c, port/pc/vid_pc.c | `QOG_VID_HUDFIRST` (`QOG_BOARD_HUDFIRST`, on) |

### boardbench (main 2c4ba79 against this branch merged with it, 300 MHz, LCD by PIO at 60 MHz)

The board model did not know these code paths, so it is taught them here.
- The logger names `QOG_NOTIFY_COVERED`, `QOG_BMODELS_EARLY` and `QOG_VID_HUDFIRST` as features of the build.
- boardmodel.py drops the whole-scan wait for notify frames the view covers, but keeps it for centre prints in Turbo.
- The brush clip buffers wait after the world, and the brush entities run before the wait for the end.
- Scan positions follow the send order.
- boardbench builds with QOG_VID_HUDFIRST, as the board does.

| | main Fast | branch Fast | main Turbo | branch Turbo |
| --- | ---: | ---: | ---: | ---: |
| race wait before the world | 2.8 | 5.9 | 0.8 | 1.0 |
| race wait after the world | 0.3 | 0.6 | 1.3 | 3.3 |
| whole scanout waits (notify text) | 7.7 | 0.0 | 8.5 | 0.5 |
| frame ms | 38.3 (35.9 to 41.0) | 36.0 (34.2 to 37.7) | 33.4 (31.1 to 36.7) | 28.8 (26.5 to 31.2) |
| **fps** | **26.1** (24.3 to 27.7) | **27.8** (26.4 to 28.9) | **30.0** (27.1 to 31.9) | **34.7** (32.0 to 36.3) |

The notify waits go and part of them comes back as ordinary race waits, in Fast before the world (the edge cache rows at the top of the frame, 48 rows later when the status bar goes first), in Turbo after the brush entities (the wait for the end of a 13.4 ms packed scan).

### The pair's own model (`-scanmodel`, demo3 frames, board ms at 60 MHz, world 8.6 and brush entities 5.7, pre 5 ms with 3 and 7 in brackets, main ff5bd24 against the branch before the merge)

| Mode | main | branch |
| --- | ---: | ---: |
| Fast | 7.0 (5.1 to 9.0) | 2.9 (1.1 to 4.9) |
| Turbo, packed frames (1,041 of 1,094) | 7.6 (5.9 to 9.5) | 0.3 (0.2 to 0.5) |

This model has no calibration and a fixed world time, so where it disagrees with boardbench, boardbench is the number to use.

### #3, two packed Turbo views, not done

- What it would win now, boardbench puts Turbo's remaining race wait at 4.3 ms a frame, 3.3 of it the wait for the end of the scanout before R_ScanEdges.
- The RAM.
  - A packed 160x152 view is 24,320 B, and its one-byte z-buffer is another 24,320 B.
  - In a Turbo frame, the view's 48,640 bytes of frame rows hold the packed view and its z-buffer.
  - The 48,640-byte z-buffer region holds the status bar rows (15,360 B at its start, read by the scanout) and the edge table, sized for 1,800 edges (43,296 B from byte 5,344).
  - A second view can take the first view's z-buffer half. Its own z-buffer then needs 24,320 B that the scanout in flight does not read. After R_ScanEdges moves the used edges to the end of the region, that room exists only when at most (48,640 - 15,360 - 24,320) / 24 - 4 = 369 edges are used.
  - In demo3 the world emits a median of 425 edges a view (42% of views at 369 or fewer), before the brush entities add theirs.
  - The display has 332 B of RAM spare (`end` 0x2003f6b4).
- So it fits in under half the frames, as a second path through the z-buffer layout, the edge fallback and the earlier two-core pipeline, for about 1.4 ms on average. That is not worth its risk now.
- A cheaper attack on the same 3.3 ms, in a frame whose last scanout was packed, R_ScanEdges could wait per row instead of for the end, at each span flush and pipeline push, for the scan line it has reached. The edge scan runs top to bottom like the scanout. That is the next step for this wait.

### What the board's first demo3 runs of main d31520e say (log run9_8.txt, read from the saved log)

- Fast, 20.0 fps. Turbo, 22.3 fps.
- During the demo `scanwait` (the waits for a whole scanout, VID_LockBuffer and VID_QogScanDone) is 5 to 16 ms a frame and `race` (row waits) 0 to 4 ms.
- In windows with `status bar rows sent in 17 of 17 frames, last scan 17145 us for 200 rows`, scanwait is about 15 ms. In windows with `0 of 25 frames` it is near 0.
- The PIO rate is confirmed, 152 rows in 13.05 ms, 200 in 17.15 ms.
- That is the three whole-scan waits this branch removes.
  - the notify clear, in every frame after a pickup message.
  - the edge table in frames that send the status bar.
  - the wait after R_RenderWorld.
- Why the status bar goes out so often (PC pair, demo3 frames, counted once by hand in R_RenderView's status bar block), of 1,091 views, 941 left the rows alone, 119 had a changed status bar (ammo when firing, health, armour, the face), 62 a palette change (the pickup and damage flashes fade over several frames, and each step changes the palette), and 31 something else over the rows (the console at the demo's start, a centre print).
- On the board each demo frame takes 2 to 3 times longer, so a status bar or palette change touches more consecutive frames in real time, and combat windows send the rows in every frame.

### The Turbo status bar cap (`QOG_HUD_CAP`, `QOG_BOARD_HUDCAP` on, cvar `r_hudcap` 100 ms, approved by the user)

- In Turbo (r_fast 2 only), the status bar rows go to the panel at most once every `r_hudcap` ms. The gap counts in real time from the last frame that sent them.
- A changed status bar in between still goes through Sbar_Draw, and R_HudFrameEnd keeps its state, but the rows are left out. The panel keeps showing the last status bar it got.
- The first frame after the gap repaints the newest state and sends it, so the last change always shows, a change just before a pause shows on the next frame after the gap.
- A palette change waits the same way.
- A console, menu, centre print or intermission over the rows sends them as before (SCR_QogHudQuiet).
- `r_hudcap 0` turns it off. Exact and Fast are untouched (R_QogHudCapHold checks r_fast 2).
- At the board's 22 fps a 100 ms gap lets at most every third frame send the rows, so combat windows should drop from `19 of 19 frames` to about a third or less, and the rest go out with 152 rows (13.0 ms) instead of 200 (17.2 ms).
- On the PC pair (real time, about 57 fps) the Turbo Benchmark sent the rows in 504 of 1,450 frames instead of 584.
- RAM, the cvar and the time of the last send, 32 B. quake_display `end` 0x2003f6d4.
- The board log should show, in Turbo during combat, `status bar rows sent in` about a third of the frames or fewer. In Fast it should be unchanged.

### Checks (hidden, `-nosound`, own build folders, own git-ignored data folder)

| Check | Result |
| --- | --- |
| `qogtest_hash`, quakeog_pc | ce86f894f9de |
| the 121 shot frames (`qogtest_k_quiet` + `qogtest_d_shots` and the fhash frames), quakeog_pc r_fast 0, 1 and 2, against untouched builds of ff5bd24 (before the merge) and of 2c4ba79 (after it) | 121 of 121 byte identical in each mode, `qogtest_f_hash` e92bc9e9650d. The race package is compiled only with QOG_VID_SCANRACE, which quakeog_pc does not have |
| the same 121 frames on the split pair with the board client's flags plus `QOG_VID_HUDFIRST` (the earlier list), r_fast 0, 1 and 2, against the pair of the same untouched trees | Fast and Turbo 121 of 121 identical, before and after the merge. Exact had one frame different in one run each time, d_demo3_24 before the merge, d_demo2_11 after it. Both differ only in the bottom view rows, 117 to 151 and 142 to 151, where the z-buffer meets the edge table. Three more runs of the branch before the merge gave 121 of 121. On the PC none of this branch's code changes a pixel below row 31, the waits do nothing there, and the skipped clear covers rows 0 to 31 at most. So these are the pair's run-to-run differences (the earlier core 1 thread, demo pipelining), but they are worth watching |
| `qog_scanrace` and `qog_lockcheck` in every pair run (shots, fhash, Fast and Turbo Benchmark) | 0 writes before the scanout passed, 0 scanouts changed |
| `tools/core1_syms.py`, board build | passes |

### RAM (board, the full configure line with 300 MHz, 1250 mV and Turbo)

| | quake_display `end` | quake_main `end` |
| --- | --- | --- |
| main 2c4ba79 | 0x2003f6ac | 0x2003f5b4 |
| this branch, race package | 0x2003f6b4 (8 B for the scan order) | 0x2003f5b4 |
| this branch, with the Turbo status bar cap | 0x2003f6d4 (300 B spare beside the 2 KB heap) | 0x2003f5b4 |

`tools/core1_syms.py --build build-ap` passes. quake_display.bin is 437,744 B with the cap.

### What the board should show

- `[time cl]`, `race` and `scanwait` should drop. AS asks for both from the next demo3 log.
- The status bar diag line's `last scan` should stay about the same, the same rows, plus one more window command.
- A wrong status bar picture, or a split at row 152, would mean the panel does not take the second window. `-DQOG_BOARD_HUDFIRST=OFF` sends the rows top to bottom again.

Files, engine/screen.c, engine/r_main.c, engine/vid.h, platform/qog_video.c and .h, port/rp2040/vid_rp2040.c, port/pc/vid_pc.c, apps/quake/CMakeLists.txt, port/pc/boardmodel_pc.c, tools/boardmodel.py, pc/CMakeLists.txt. The PC pair needs `QOG_VID_HUDFIRST` in `QOG_SPLIT_CL_EXTRA` to model the board's order.

## 2026-11-25, Fast and Turbo spans in hand-written Thumb in RAM, 32-bit span starts, tiny Turbo model triangles, particle kill checks in integers, and a board cost model (PC and ARM build, registered data)

The board's spans phase was 16.9 ms of a demo3 view in Fast mode at 250 MHz, models 5.1, vmodel 3.4 and parts 1.2. Nothing ran on hardware. The savings below come from a cost model that runs the board's own compiled code (quake_display.elf) in an emulator on real surfaces from timedemo demo3, then a range up to the board's ratio to that model.

### The cost model (build-ar, git-ignored, rebuilt from these notes)

- A PC measurement build (`-DQOG_PC_EXTRA_DEFS=QOG_AR_DUMP;...`, `qog_ardump <file> <first view> <views>`, engine/d_scanlit.c) writes every Fast lit surface of the views it is given, gradients, the lit_ and litfix_ values, the light grid, the mip level's texels and the spans, after the colour map.
- `replay.py` loads quake_display.elf into Unicorn (pip package, a scratch virtual environment), emulates the SIO interpolators and divider through `mmio_map`, does the ROM float calls in Python at 60 cycles each, weights each M0+ instruction by its cycles (loads and stores 2, SIO 1, taken branches 2, bl 3, push and pop 1 + n), replays each surface through D_DrawSpans8LitFix and D_DrawSpans8LitQ (this one) and compares their pixels and z. `test_span.py` runs the assembly against the C on random spans, `bench_alias.py` runs D_PolysetDraw on made-up meshes.
- Views 600 to 603 of timedemo demo3, Fast, 53 surfaces, 1,194 spans and 44,800 pixels a view. The earlier drawer models at 3.69 M cycles a view (82.5 a pixel), 7.4 ms of one core at 250 MHz with the spans shared by both cores, against the board's 16.9 ms phase, so the board runs about 2.3 times the model (flash misses, the two cores sharing the XIP cache, core 1's join).
- PC counts (`QOG_AR_STATS`, `qog_arstat spans`, every 100 views), per view about 1,470 lit spans in Fast (48,200 pixels) and 1,430 in Turbo (24,100). Turbo's spans, 9.5% are 1 pixel, 7.7% 2, 12.7% 3 or 4, 18.2% 5 to 8. In every span of the demo the surface's nearest 1/z is at most 16 times the span's own, 99.4% within 4 times.

### Changes

| Change | Flag (board option) | Mode | What it does | Model, cycles a view (demo3) |
| --- | --- | --- | --- | --- |
| 32-bit span starts | `QOG_LITSPAN_Q` (`QOG_BOARD_LITQ`, on, needs FAST and INTERP) | Fast and Turbo | D_LitFixSetup also keeps the surface's 1/z * 2^k, Ns and Nt and their steps shifted down by 32, so D is 2^28 to 2^29 at the nearest point. A span's start is then three sums of two 32-bit products each, modulo 2^32 (every true value of the surface fits 32 bits, so the wrapped sums are the true ones, L241). The coefficients lose at most a unit each, so a start is off by at most \|du\| + \|dv\| + 1 units of D. A span whose D falls below 2^24 at either end takes the earlier 64-bit start (none in demo3). Its 1/z * 2^31 for the z is D shifted | part of the next rows |
| The z by the pixel loop | with the above | both | when every pixel of the span takes the one-byte z-buffer's plain z >> 3 (D_QogZSpanI's far case, the same test), the pixel loop writes the z, else D_QogZSpanI does as before | 0.42 M of z walks gone |
| One span in hand-written Thumb, its hottest loop in RAM | `QOG_LITQ_ASM` (same option) | both | litq_span_asm (port/rp2040/d_litq_rp2040.S, in flash, with its z run loop litq_zruns, 172 B, in .time_critical RAM), the earlier sub-spans (length from how fast 1/z changes, the end by one SIO divide whose 8 cycles the shifts of Ns and Nt fill, the steps by a shift or two SIO divides), then the runs in two passes. Pass 1 blends the light at each run's ends with the coordinates shifted by the mip level, so one blend serves every level (S << mip puts the light cell at bit 20), and writes each run's light start and step. Pass 2 sets interpolator 1 from them and draws, interpolator 0 gives the texel addresses and carries S and T from run to run, interpolator 1 the colour map rows, and a run's pixels are an unrolled sequence entered at slot 8 - k. A pixel is 8 cycles, 12 with its z. The C version (litq_span_c, d_lit_q.h) is the PC's and the board's fallback. The board checks the assembly against it at start-up on 800 made-up spans (D_QogLitQCheck from D_QogLitInterpCheck) and uses the C if any pixel, z or divisor shift differs. On the PC both ran 2,300 random spans and the replayed demo views with 0 differences | Fast 3.69 M to 2.49 M, Turbo 2.41 M to 1.52 M |
| Shared light ends | `QOG_LITQ_SHARELIGHT` (`QOG_BOARD_LITQSHARE`, on) | Fast and Turbo | each light run of 8 ends at the next run's first pixel, not at its own last one, so the next run starts from it, one blend a run instead of two, and the light is one continuous line along the span | Fast 2.49 M to 2.39 M, Turbo 1.52 M to 1.47 M |
| Tiny Turbo model triangles | `QOG_ALIAS_TURBOTINY` (`QOG_BOARD_TURBOTINY`, on with TURBO, also in quakeog_pc) | Turbo only | a front-facing alias triangle whose corners lie within 3 x 3 pixels is drawn from its three edge functions (on an edge, drawn for top and left edges), each covered pixel in the first corner's texel, light and depth with id's depth test (D_QogTinyTriangle, d_polyse.c, out of line so its frame sits beside the rasterizer's) | 25.4 of Turbo's 85 gradient triangles a view (demo3), about 2,250 to 750 cycles each, about 38 K |
| Particle kill checks in integers | `QOG_PART_DIEBITS` (engine flag, PC and board) | exact, all modes | id's `p->die < cl.time` (a float to double conversion and a double compare, twice a particle) is an integer compare of the float's bits against the bits of the smallest float at or above cl.time, made once a view (R_QogDieBits, r_part.c). For a float die that is the same answer | about 205 particles a view, 2 checks each |
| Measurement | `QOG_AR_STATS`, `QOG_AR_DUMP` | PC only | the counts above, the surface dump | |

### What else the model showed

- After these changes a Fast view's spans split into the pixel slots (about 0.62 M), pass 1's blends (about 0.75 M) and the sub-span logic with its divides (about 0.8 M). Fewer sub-spans (longer affine runs) would change the picture.
- A gradient alias triangle costs about 2,250 cycles even when it covers one pixel (D_RasterizeAliasPolySmooth 490, D_PolysetScanLeftEdge 430, D_PolysetDrawSpans8 400, gradients 370 in Fast mode), and id's recursive subdivision is cheaper only below a pixel (1,400) and much dearer above (15,000 for a 5 x 5 pixel triangle), so moving id's transition nearer does not pay (L243). Turbo already moves it to 0.71 of Fast's by id's own area rule.
- The vertex transforms, gradients and clip points of models are already integers in Fast mode. Cached transforms between frames would only help the view weapon, whose cost is its 2,772 pixels and clipping. Not done.

### Estimated board savings, ms a view of demo3 (the spans shared by both cores)

| Phase | Fast at 250 MHz | Fast at 300 MHz | Turbo at 300 MHz |
| --- | ---: | ---: | ---: |
| spans (model saving, both cores' work halved) | 2.6 to 6.0 | 2.2 to 5.0 | 1.6 to 3.6 |
| models (Turbo tiny triangles) | 0 | 0 | 0.13 to 0.3 |
| parts (kill checks) | about 0.1 | about 0.1 | about 0.1 |

The low end is the model's cycles at the clock, the high end assumes the board's 2.3 times the model holds for what was removed. Code that used to be fetched through the XIP cache now runs from RAM, so the high end is the more likely while the cores share the cache.

### After merging main 9b0fef4 (the earlier two-core surfaces, the PIO scanout, the float cuts, the board model)

- Core 1 draws whole surfaces with its own compile of d_scanlit.c (`QOG_DRAW_CORE1`, d_core1.c). The Q drawer's state is per call and in that file's statics, so each core has its own. The assembly now takes the surface's values through a pointer in the span struct, not by the name `litq_surf`, which in core 1's copy would have been core 0's. The one shared value is `litq_asm_ok`, the start-up check's result, written once on core 0 at D_Init (on tools/core1_syms.py's reviewed list). D_QogLitQCheck and the PC measurement code are left out of core 1's copy. `tools/core1_syms.py --build build-ar/arm` passes.
- RAM, main's display `end` is 0x2003f588 with 632 B spare, so the 1,268 B of assembly no longer fit in RAM. Scratch X holds the link rings and core 1's stack, scratch Y the command history and the interrupt stack. The routine now runs from flash, only the z run loop and its pixel slots (172 B) from RAM, and the 32-bit coefficients are read per call from the high words of the earlier 64-bit values instead of being kept (88 B less in both cores' copies). The cycle model does not see where code sits, so its savings stand. The XIP misses on the routine's code are what the RAM placement would have removed.
- D_DrawSpans8LitQ and litq_span_c are out of line, so core 1's copy keeps main's deepest span chain (c1_D_DrawSpans8Lit 8 + 424 B).
- The emulator replay through core 1's own entry (c1_D_DrawSpans8Lit, which runs its own D_LitFixSetup) gives the same pixels and z as core 0's drawer, with the assembly and with the C, in Fast and Turbo.
- the board model (tools/boardbench.ps1) now builds with `QOG_LITSPAN_Q QOG_LITQ_SHARELIGHT`, logs the new features and the Turbo tiny triangles (`am_tinytri`), and weights them (tools/boardmodel.py, the span counts at 0.64 of their cycles from the replay, a tiny triangle at a third of a smooth one, the particle kill checks as 4 of 16 float calls). Predicted demo3 at 300 MHz, LCD by PIO, main 9b0fef4 Fast 22.6 fps (44.3 ms), Turbo 26.5 (37.7 ms). This branch Fast **25.3 fps** (39.6 ms, 23.6 to 27.1), Turbo **29.0 fps** (34.5 ms, 26.4 to 31.3). Spans 25.0 to 16.3 ms in Fast and 16.3 to 10.7 in Turbo, parts 1.0 to 0.8.

Merged proof (hidden, `-nosound`, an untouched build of 9b0fef4 from `git archive`). `qogtest_hash` ce86f894f9de on quakeog_pc and on the board-flag build with `QOG_DRAW_CORE1` and the Q drawer, `qogtest_f_hash` e92bc9e9650d in every mode. The 121 frames, quakeog_pc r_fast 0 and 1 byte identical, r_fast 2 0.013% (the tiny triangles). The board-flag build with `QOG_DRAW_CORE1` against the same flags on 9b0fef4, r_fast 0 121 of 121 byte identical, Fast 0.258% (worst d_demo3_00 1.05%), Turbo 0.563% (worst d_demo3_03 2.56%).

### Exact and Fast proof before the merge (hidden, `-nosound`, own build-ar folders, untouched build of dd0f819 from `git archive` in build-ar/ref and build-ar/pcbref, own git-ignored data folder with its own qogbake.bin)

| Test | Result |
| --- | --- |
| `qogtest_hash`, quakeog_pc of this tree (with `QOG_PART_DIEBITS` and `QOG_ALIAS_TURBOTINY`) | ce86f894f9de |
| the same, the board-flag build with the Q drawer (`QOG_LITSPAN_INTERP QOG_VERTCACHE QOG_VERTCACHE_CLIPDIST QOG_WORLDNODE_FAST QOG_FRONT_FAST QOG_WORLDNODE_INT QOG_ZBUFFER8 QOG_ZBUFFER8_NEAR=7 QOG_NUMSTACKEDGES=1800 QOG_SPANS_CORE1 QOG_ALIAS_CORE1` plus `QOG_LITSPAN_Q QOG_LITQ_SHARELIGHT QOG_ALIAS_TURBOTINY`) | ce86f894f9de, and `qogtest_f_hash` e92bc9e9650d in every mode |
| 121 shot frames (`qogtest_k_quiet` + `qogtest_d_shots` 113 and the 8 fhash frames), r_fast 0, quakeog_pc against dd0f819 | 121 of 121 byte identical |
| the same, r_fast 1 (the PC has no INTERP, so this shows the particle change is exact) | 121 of 121 byte identical |
| the same, r_fast 2 (the Turbo tiny triangles) | 0.013% of the view pixels, worst d_demo1_03 0.18% |
| the board-flag build with the Q drawer against the same flags on dd0f819, r_fast 0 | 121 of 121 byte identical (a run without the stats build's notify lines, and with them all frames identical below the notify rows) |
| the same, r_fast 1 | 0.258% of the view pixels, worst d_demo3_00 1.05%, single pixels one colour map row apart (the 32-bit starts alone 0.023%) |
| the same, r_fast 2 | 0.564% of the panel's view pixels, worst d_demo3_03 2.56% (the 32-bit starts and shared light 0.551%, the tiny triangles 0.013%) |
| litq_span_asm against litq_span_c, the board's code in the emulator | 2,300 random spans and the replayed demo3 views, 0 differences, and D_QogLitQCheck run from quake_display.elf passes |

### RAM and stack (board, the full configure line with `-DQOG_SYS_KHZ=300000 -DQOG_SYS_MV=1250 -DQOG_BOARD_TURBO=ON`, own build folder build-ar/arm, merged with 9b0fef4)

| | main 9b0fef4 | merged |
| --- | --- | --- |
| quake_display `end` | 0x2003f588 (632 B spare beside the 2 KB heap reserve) | 0x2003f694 (+268 B, the z run loop 172 B in RAM, the surface values and the check flag), 364 B spare |
| quake_main `end` | 0x2003f5b4 | 0x2003f5b4 |
| quake_display.bin | | 435,216 B |

Stack, core 0's chain is D_DrawSpans8Lit 56 + D_DrawSpans8LitQ 240 + litq_span_asm 52, or + litq_span_c 184 on the C fallback, below the earlier 432 B drawer, which still runs for textures that are not a power of two. Core 1's is c1_D_DrawSpans8Lit 8 + 424, as on main. D_DrawNonSubdiv 72 B, D_QogTinyTriangle 120 B beside D_RasterizeAliasPolySmooth 152 B. D_QogLitQCheck's 736 B frame runs once from D_Init on core 0.

### For earlier work and the lead

- The drawers' state is per call (litq_span_t on the caller's stack) plus litq_surf, the coefficients and the earlier litfix_ values, all written on core 0 by D_SetupLitSurface and D_LitFixSetup before the spans are shared, so the core split by spans (QOG_SPANS_CORE1) is unchanged. Each core uses its own interpolators and divider.
- Files, engine/d_lit_q.h (new), port/rp2040/d_litq_rp2040.S (new), engine/d_scanlit.c, engine/d_polyse.c, engine/r_part.c, engine/r_alias.c and engine/r_qogalias.h (a stats counter), engine/d_init.c (the PC dump command), apps/quake/CMakeLists.txt, pc/CMakeLists.txt, and with the merge tools/core1_syms.py (one reviewed name), tools/boardmodel.py and port/pc/boardmodel_pc.c (the new counts and features). r_main.c is not touched.
- The PC game (quakeog_pc) has no INTERP, so its Fast mode draws with the earlier drawer. The board's drawer runs on the PC with `-DQOG_PC_EXTRA_DEFS="QOG_LITSPAN_INTERP;QOG_LITSPAN_Q;QOG_LITQ_SHARELIGHT"`.

Not done, for later.
- The per-span C of D_DrawSpans8LitQ (about 0.22 M cycles a Fast view) could join the assembly.
- An exact small-triangle rasterizer for Fast mode (id's coverage and gradients without the edge tables) would save most of the 1,500 cycles a tiny triangle costs in both modes.
- R_DrawParticles' physics stays float (about 10 float operations a particle), R_LightPoint's float trace runs per model (r_light.c).

## 2026-11-25, Brush models, the edge scan and the light grid build, second round, all exact (PC and ARM build, registered data)

Attacks 6, 7 and 8 of docs/fps-board.md. Six flags, each exact, r_fast 0, 1 and 2 give the same bits as main 9b0fef4. Nothing ran on hardware. The numbers below are the earlier model (`tools\boardbench.ps1`), with the new paths priced from Thumb instruction counts of the hot loops before and after.

### Boardbench (300 MHz, LCD by PIO at 60 MHz)

| | Fast fps | Turbo fps | bmodels ms | edgescan ms | lmap ms (Fast / Turbo) |
| --- | ---: | ---: | ---: | ---: | ---: |
| main 9b0fef4 | 22.6 (21.4 to 23.5) | 26.5 (24.5 to 27.8) | 2.7 | 4.8 | 3.2 / 2.9 |
| this branch | 23.2 (21.9 to 24.5) | 27.4 (25.4 to 28.7) | 2.0 | 4.0 | 3.0 / 2.7 |

### What the model priced (Turbo, main, ms per view)

edgescan, edge visits in R_GenerateSpans 2.69 (3,984 a view at 55 integer cycles), 1/z compares 0.87, scan lines 0.43. bmodels, brush clip edges 0.70, R_SplitEntityOnNode2 plane tests 0.41, brush clip calls 0.35, brush edges 0.55, box tests 0.22. lmap, integer dynamic light cells 0.74, the world's light marking 0.52, the base grid (cells and cell styles) 0.46 with its setup 0.26.

### Changes

| Change | Flag | What it does |
| --- | --- | --- |
| Span generation in one loop | `QOG_SCAN_FAST` (r_edge.c) | id's R_GenerateSpans, R_TrailingEdge and R_LeadingEdge as one loop with the same statements. The engine is built with -fno-strict-aliasing, so every store made GCC load r_edgebase, surfaces, r_spanbase, span_p and current_iv again, and each edge paid two calls. They are locals now, span_p and r_bmodelactive are written back before R_CleanupSpan. R_StepActiveU keeps r_edgebase and the sentinels in locals too. The surf half of a 1/z compare (its fu, a + b and exponent) is kept for the whole search of one leading edge, 62% of the compares come from that search and 16% of all reuse the half, 5 of 11 float calls. Priced at 0.7 of the integer cycles an edge |
| Brush clip sides inline | `QOG_BPOLY_SIDES` (r_bsp.c) | R_RecursiveClipBPoly's axial sides without two nested calls a point, and a point that ends or starts the edge before keeps its side (statics reset per call, no recursion inside one call's edge loop). Priced at 0.5 of the integer cycles an edge |
| Axial box tests in integers | `QOG_BMSPLIT_INT` (r_efrag.c) | R_SplitEntityOnNode2's two float compares on an axial plane as integer compares of the float bits in real order. Denormals and NaNs take the float compares. Counted as `bm_bmsplitint` (0 float calls, 40 cycles) |
| Brush entity box test in integers first | `QOG_BMODEL_BBOXINT` (r_main.c, r_draw.c) | the corners in 1/4 unit and the world's view planes in 1/16384 (now kept through the brush entities), with a margin of 14,400 + 0.53 times the corner size, about 0.3 units. Only 0.06 plane tests a view in demo3 fell back to floats |
| Fused light grid build | `QOG_LMAP_FUSE` (r_surf.c) | R_BuildLightMap's clear, per style passes and bound in one pass for surfaces of one or two styles without dynamic lights (the cell is an unsigned sum, the same in any order), two passes with dynamic lights. No new names, so core 1's copy (d_core1.h) needs none, and tools/core1_syms.py passes. Priced at 0.65 of the cell and cell style cycles |

### Exact proof (hidden, `-nosound`, an untouched build of 9b0fef4 with the board's world flags)

| Test | Result |
| --- | --- |
| `qogtest_hash`, default quakeog_pc and with the world flags | ce86f894f9de |
| `qogtest_f_hash` | e92bc9e9650d |
| 121 shot frames, all 200 rows, r_fast 0, 1 and 2 | 121 of 121 byte identical in each |
| check build (`QOG_LEADZ_CHECK QOG_BPOLY_CHECK QOG_BMODEL_BBOX_CHECK QOG_LMAP_FUSE_CHECK` and the earlier ones), timedemo demo1 r_fast 0, demo2 r_fast 1, demo3 r_fast 1 and 2 | about 590,000 1/z compares (with the cache), 2.75 M clip tests, 32,000 box tests, 160,000 light grids, 0 differ |
| tools/core1_syms.py on the board build | every variable core 1 reaches is its own or on the list |

### RAM (full configure line with `-DQOG_SYS_KHZ=300000 -DQOG_SYS_MV=1250 -DQOG_BOARD_TURBO=ON`)

quake_display `end` 0x2003f5a0 against 0x2003f588 on 9b0fef4 (24 B, the clip side statics), about 600 B spare. quake_main 0x2003f5b4, unchanged. R_GenerateSpans' frame grows to 64 B but it calls nothing deeper than before (R_QogZFrontC is a leaf), R_RecursiveClipBPoly stays 136 B a level.

### For earlier work

R_GenerateSpans is replaced under `QOG_SCAN_FAST` (the id version stays in the `#else`), the scan loop R_QogScanRows and the core 1 hooks are untouched, R_StepActiveU moved below R_GenerateSpans under the same flag.

Files, engine/r_edge.c, engine/r_bsp.c, engine/r_efrag.c, engine/r_main.c, engine/r_draw.c, engine/r_surf.c, engine/qog_bmodel.h, engine/qog_aqstats.h, port/pc/boardmodel_pc.c, tools/boardmodel.py, pc/CMakeLists.txt, apps/quake/CMakeLists.txt.

## 2026-11-25, The board cost model sees the two-core surfaces, the PIO scanout, Turbo text and the exact float cuts (PC only, registered data)

main ff5bd24 merged into an earlier branch, and the model extended so boardbench prices what they changed. Nothing ran on hardware.

### Prediction (demo3, board, 300 MHz, 10 to 90% range)

| LCD | Fast | Turbo |
| --- | ---: | ---: |
| PIO 60 MHz (default) | 22.6 fps (21.4 to 23.4), 44.3 ms | 26.5 fps (24.5 to 27.7), 37.7 ms |
| SPI peripheral 50 MHz (`-DQOG_LCD_PIO=OFF`) | 21.2 | 24.6 |

Worth by the model, the earlier pipeline 3.9 ms in Fast and 3.6 in Turbo, the earlier PIO scanout 2.8 and 3.0, the earlier cuts about 4 ms (bmodels 5.7 to 2.7, edgescan 6.0 to 4.8). docs/fps-board.md has the phases, the gap to 35 fps and the re-ranked attacks, Turbo reaches 37.0 fps in the model with the exact race package, the world front end halved, span pixels and alias models 30% cheaper.

### What changed in the model

- Both cores' counts, core 1's copy of the drawing (engine/d_core1.h) undefines QOG_PHASE_TIME, so in the board model builds it keeps its own sums (`c1_qog_ph_count`, `c1_qog_bm_count`, the `c1_qog_lms`) and the logger adds both cores (port/pc/boardmodel_pc.c, engine/qog_phase.h `QOG_C1_PHCOUNT`, the lit span counts in d_scanlit.c and the span pixel helper in d_edge.c also compiled for it). Without that the logged lit pixels were core 0's 21,764 of 48,150.
- The pipeline (`QOG_DRAW_CORE1` in the model build), pipelined world surfaces and pixels, those handed on early (the `qog_c1_stat`), and a per frame model of core 0's clock, core 1 draws early surfaces while core 0 scans (at most the scan's length times core 1's speed, 0.9, sampled 0.8 to 1.0), core 0 draws the sky, water and brush surfaces, both share the rest, and core 0 waits half of a surface at the end.
- The PIO scanout. `--lcd pio --spi 60` (85.7 us a row, earlier work), the default of boardbench (`-Lcd pl022 -Spi 50` for the old path).
- Turbo text, a notify frame still waits for the whole scanout (SCR_SetUpToDrawConsole's clear) and spreads the last view, but the view stays packed, so the next view does not race a full width scanout.
- the earlier paths, QOG_AQ_STATS counts in the log (`aq_` columns), the 1/z compares of brush surfaces and the facing tests as their own work, and each of those flags makes its counts cheaper by the cycle reads (`FEATURE_SCALE`). The brush entity rotate and the brush surface weights were raised to the measured costs.
- New reference logs from the merged tree with the six, the eight flags and `QOG_DRAW_CORE1` taken out (every count equals the first reference within 1%), recalibrated, two factors x 1.52 for float calls and x 4.10 for integer cycles, 17% rms over the nine phases. E1M1 standing 53.6 ms against the board's 55.2.
- `-WhatIf` takes several what-ifs separated by `;`, plus `nopipe`.

### Checks

- `qogtest_hash` ce86f894f9de on the merged normal PC build (the md5 of the two qog_statehash lines with LF endings).
- Board build with the full configure line, `-DQOG_SYS_KHZ=300000 -DQOG_SYS_MV=1250 -DQOG_BOARD_TURBO=ON`, quake_display.bin 425,760 B, quake_main.bin 1,370,620 B. `tools/core1_syms.py --build build-arm`, every variable core 1 reaches is its own or on the list. The board build does not define `QOG_BOARDMODEL`, so none of the counting is in it.

Files, engine/d_core1.h, engine/qog_phase.h, engine/d_scanlit.c, engine/d_edge.c (one guard each, inside the cost model's flags), port/pc/boardmodel_pc.c, pc/CMakeLists.txt, tools/boardmodel.py, tools/boardbench.ps1, tools/boardmodel_calib.json, tools/boardmodel_ref.csv.gz, tools/boardmodel_ref_e1m1.csv.gz, docs/fps-board.md, docs/handoff.md, docs/lessons.md (L200, L201).

## 2026-11-25, A board cost model and tools/boardbench.ps1, the demo3 scoreboard (PC only, registered data)

A way to measure board speed without the board. `powershell -File tools\boardbench.ps1` plays timedemo demo3 on the PC with the board client's render flags, counts the work of every code path in every frame, and predicts the board's ms per phase and fps for Fast and Turbo at any clock and LCD SPI rate. The scoreboard is docs/fps-board.md, the how-to docs/handoff.md. Nothing ran on hardware, nothing touched the board or a COM port.

### Prediction for main dd0f819 (demo3, board, 10 to 90% range)

| | 250 MHz, SPI 62.5 | 300 MHz, SPI 50 |
| --- | ---: | ---: |
| Fast | 16.2 fps (15.8 to 16.4) | 18.0 fps (17.2 to 18.9), 55.5 ms |
| Turbo | 18.9 fps (18.1 to 19.4) | 20.6 fps (19.2 to 22.2), 48.5 ms |

Per phase at 300 MHz, Fast, spans 14.1, world 8.9, edgescan 6.0, bmodels 5.7, models 4.3, lmap 3.1, vmodel 2.8, parts 1.0, grad 0.9. Turbo, spans 9.3, the rest as Fast less 0.7 in world and 1.4 in models and vmodel. Scanout waits, Fast 4.7 ms before the world, 0.9 after, 7.9 in notify frames. Turbo 1.5, 3.8 and 8.4 (the model's view, see the reconciliation below).

### How it works

- PC build option `QOG_PC_BOARDMODEL` (pc/CMakeLists.txt, measurement only), quakeog_pc with the board client's render flags (one-byte z-buffer, edge cache in the frame, 256 entry lit surface hash, scan race, status bar skip, both cores, the earlier and the world flags, interpolators), the phase times and every work count. `QOG_PC_REMOVE_DEFS` takes flags out again.
- `-bmlog <file>` (port/pc/boardmodel_pc.c, new) writes one line per timedemo frame, the rows and packed rows of the scanout the view raced, the byte ranges the view handed to VID_QogScanFree, console lines and centre prints, and the change of every count. `-bmall` logs every frame of a level instead.
- New counts (engine/qog_bmodel.h, new, empty macros unless `QOG_BOARDMODEL`), dynamic light pairs and cells on the integer and float paths, lit float cells, R_QogLightReachesWorld calls and steps (r_surf.c, r_light.c), gradients by path (d_edge.c), scan lines and active edge visits (r_edge.c), surfaces and brush surfaces drawn (d_edge.c), brush entity faces, edges, clips, light marking, box tests, world splits, R_RecursiveClipBPoly calls and edges (r_main.c, r_bsp.c, r_efrag.c). QOG_ALIAS_STATS keeps its counts off the console in these builds (r_alias.c), because console lines are notify text (L180).
- tools/boardmodel.py (new), float helper calls and integer cycles per unit of each path (a priori, from earlier Thumb reads), two factors fitted to the board's nine b585365 phases (kf 1.61 for float calls, ki 3.87 for integer cycles, 20% rms), a residual per phase so the calibration run is exact, E1M1 standing (lmap 2.0) to split lmap into base grids and dynamic lights, and a frame by frame simulation in board time of the scanout (91.5 us a row at 62.5 MHz, 112 at 50), the waits at VID_QogScanFree, after R_RenderWorld and in notify frames, and Turbo's packed frames. Ranges come from refitting with every weight scaled at random and the time before the view and the clock scaling drawn at random. `--whatif` asks what a change would give.
- Calibration data, tools/boardmodel_calib.json, the demo3 log tools/boardmodel_ref.csv.gz (main with the earlier six flags removed, whose per view counts equal an untouched b585365 build's within 0.3%, lit pixels 48,113 against 48,245, spans 1,448 against 1,452, nodes 265.2 against 266) and the E1M1 log tools/boardmodel_ref_e1m1.csv.gz. tools/boardbench_config.cfg is the fixed test config.

### How well it reproduces the board

- b585365 phases, exact by construction. Under it the two factor fit is within 12% on six phases, bmodels 33% under, parts 25% over, edgescan 18% over.
- E1M1 standing, b585365, 56.3 ms against the board's 55.2, row race 4.9 ms plus the vertex cache wait against the board's race 6.0.
- demo3 frame, the waits the code implies give 79.6 ms against the measured 73.3. Notify text shows in 34% of demo3's frames in board time and makes the whole view wait for the scanout, and frames after a status bar change wait for it at the edge table, while the board's own view total leaves only about 1 ms outside the view. The model keeps three reconciliations (a constant offset, all waits x 0.31, all phases x 0.90) and averages them. The `race` and `scanwait` fields of the `[time cl]` line during demo3 would settle it.

### Findings for the next attacks (docs/fps-board.md has the ranked list)

- Scanout waits are the largest item left, removing them all is worth 11.4 ms in Fast and 10.7 in Turbo at 300 MHz. An exact package (notify rows cleared after a ScanFree of those rows only, the edge table's status bar wait made a ScanFree as edges reach those rows, brush entities before the wait for the scanout's end) is worth 3.6 and 6.2 ms.
- In Fast at 50 MHz the world hides behind the scanout, halving it saves 2.8 ms of frame, not 4.5 (L184).
- Turbo's race wait moved behind R_RenderWorld rather than going away (L198).
- 35 fps needs the scanout waits gone as well as cheaper phases, with world and bmodels halved and spans, edgescan and models 30% cheaper, Turbo reaches 30.0 fps, and 39.9 only with no waits on top.

### Exact

The normal PC build (`build-pc`, QOG_AOT) gives `qogtest_hash` ce86f894f9de. Every engine hook is inside `QOG_BOARDMODEL`, which only the measurement build defines.

Files, engine/qog_bmodel.h, port/pc/boardmodel_pc.c, tools/boardmodel.py, tools/boardbench.ps1, tools/boardbench_config.cfg, tools/boardmodel_calib.json, tools/boardmodel_ref.csv.gz, tools/boardmodel_ref_e1m1.csv.gz, docs/fps-board.md (new), engine/r_surf.c, engine/r_light.c, engine/d_edge.c, engine/r_edge.c, engine/r_main.c, engine/r_bsp.c, engine/r_efrag.c, engine/r_alias.c, port/pc/vid_pc.c, pc/CMakeLists.txt, docs/handoff.md, docs/lessons.md (L180 to L184, L198, L199).
## 2026-11-25, Both cores draw the world's surfaces during the edge scan (PC and ARM build, registered data)

Until now the display's core 1 drew every second span of a lit surface and half of an alias model's vertices, and waited through everything else of the view, world, bmodels, edgescan, lmap and grad, about 40 ms of a demo3 view on core 0's clock. Now core 1 draws whole lit world surfaces (light grid, gradients, spans and z) while core 0 is still scanning edges, and at every flush both cores share the remaining surfaces. Exact in both render modes, the same pixels as main in Exact and in Fast. Nothing ran on hardware, every board number below is an estimate.

### The pipeline (`QOG_DRAW_CORE1`, board option `QOG_BOARD_DRAWCORE1`, on, needs `QOG_BOARD_CORE1`)

- Before the first scan line R_ScanEdges gives every world surface the last scan line of its edges (one pass over the removeedges lists, each edge is in the list of its last line). On the board's 32-byte surfaces the line is kept in `entref`, which only brush entity surfaces use, so it costs no RAM (other builds use a byte table).
- At the end of each scan line the lit world surfaces whose last line that was have their final span list (no later line can add a span), and if their spans' z rows lie above the edge table in the z-buffer they go to a 64-entry ring (surface index and span link). Core 1 takes them from the ring and draws them while core 0 goes on scanning.
- At a flush (D_DrawSurfaces, span buffer full, before the edge rows, or the end) core 0 first draws the surfaces only it draws (sky, background, water and other turbulent surfaces, brush entity surfaces, whose transform changes core 0's view vectors), then both cores take the rest of the ring and the flush's remaining lit world surfaces from a shared cursor under one SIO spinlock, and core 0 waits until core 1 has finished its last surface before the span buffer is reused. The edge fallbacks keep working, a fallback that draws without z first waits for core 1.
- Core 1 runs id's drawing code unchanged, compiled a second time (engine/d_core1.c, d_core1s.c and d_core1e.c include d_scanlit.c, r_surf.c and d_edge.c) under engine/d_core1.h, which gives every variable the drawing writes and every function those files define a c1_ name, its own light grid, r_drawsurf, gradients, mip level, Fast drawer state, interpolator setup, and copies of the world's view vectors and currententity. So the earlier light grid code runs on core 1 as it is, and any change AL makes to r_surf.c or D_CalcGradients is compiled into both copies.
- Core 1 waits for work in WFE, core 0 wakes it with SEV. The job starts at R_ScanEdges, after the race waits (the scanout is over by then), and returns at its end, before models, the view model and particles draw on the finished world z (their order is unchanged).
- the earlier span halving stays in the code for builds without the pipeline. With it, core 0 draws all spans of the surfaces it draws (core 1 is busy with surfaces of its own).
- D_DrawOneSurface's lit branch moved unchanged into D_QogDrawLitBody, which both copies use.

### Why it is exact

- Every pixel and z of the view belongs to exactly one span, so the world's surfaces can be drawn in any order and on either core.
- A surface handed on early has the very span list the flush would give it (same spans, same order, same head span), so the earlier Fast drawer takes its fixed point origin from the same span (D_LitFixSetup). Flush points are unchanged (an extra flush would split lists and change Fast pixels, lessons L150).
- Its z rows lie above the edge table, which only moves to higher addresses (R_QogMoveEdges) or out of the z-buffer, so no edge is overwritten.
- Each core draws with id's statements from values nothing writes while the scan runs, and its own copies of what the drawing writes. tools/core1_syms.py walks the board objects' relocations from core 1's job and fails on any variable it reaches that is neither core 1's own nor on a hand-checked list (frame and z-buffer pointers, the view's scale and centre, textures, light styles, dynamic lights, r_qogfast, the pipeline's locked state). It passes on the board build.
- r_light.c's world dynamic light pass runs at the first R_DlightLit question of a frame, which either core could now ask, so core 0 runs it with the earlier R_QogDlightSettle before core 1 starts, after the brush entities as the first drawn surface would (lessons L152).
- the earlier interpolator check runs on core 0 before core 1 starts, so a failed check turns Fast off for both copies.
- The PC checks at every flush that core 1's copies of the view vectors and the transformed model origin equal what D_DrawSurfaces draws with (Sys_Error otherwise, never fired).

### Exact proof (hidden, `-nosound`, own build folders in `build-am`, own git-ignored `data\id1` with a baked qogbake.bin, link port 27961, one run at a time)

References are untouched builds of b585365 with the same flags. Each line is `qogtest_hash`, `qogtest_f_hash` and the 121 shot frames (`qogtest_k_quiet` + `qogtest_d_shots` 113 frames and the 8 fhash frames) against the reference of the same build flags and the same r_fast.

References are untouched builds of main ede79b2 (after merging the earlier integer dynamic lights and the earlier profiler) with the same flags. Each row is `qogtest_hash`, `qogtest_f_hash` and the 121 shot frames (`qogtest_k_quiet` + `qogtest_d_shots` 113 frames and the 8 fhash frames) against the reference of the same flags and the same r_fast. The pipeline builds add `QOG_SPANS_CORE1 QOG_DRAW_CORE1`. Core 1 modes are the PC's second thread (thread), core 1's surfaces drawn on the game's thread as soon as they are handed on (`-core1first`) or at each flush after core 0's (`-core1last`).

| Build (extra flags) | r_fast | core 1 mode | hash | fhash | 121 frames |
| --- | --- | --- | --- | --- | --- |
| default | 0 and 1 | thread, first, last | ce86f894f9de | e92bc9e9650d | 121 of 121 identical in all six runs |
| board-like (`QOG_ZBUFFER8 QOG_ZBUFFER8_NEAR=7 QOG_NUMSTACKEDGES=1800 QOG_SURF_T_TINY QOG_LITSPAN_INTERP`, so the last lines in `entref` and the interpolators emulated) | 0 and 1 | thread both, first in 1, last in 0 | ce86f894f9de | e92bc9e9650d | 121 of 121 identical in all four runs |
| many flushes (`QOG_ZEDGE_TEST_SPANS=400`, 1,280 edge moves and 129 stack fallbacks in the shot run) | 0 and 1 | thread both, first in 1, last in 0 | ce86f894f9de | e92bc9e9650d | 121 of 121 identical in all four runs |
| the no-z fallback forced (`QOG_ZEDGE_TEST_SPANS=400 QOG_ZEDGE_FORCE_NOZ`, 129 flushes without z, 837 rows) | 0 and 1 | thread | ce86f894f9de | e92bc9e9650d | 121 of 121 identical in both runs |

The same default runs were also identical against b585365 before the merge. `tools/core1_syms.py --build build-am/arm` passes on the board build, and the PC's view check never fired.

How much is handed on during the scan (PC, `-DQOG_PHASE=ON`, timedemo demo3, r_fast 1, per view), 56 lit world surfaces go through the pipeline, 41 of them during the scan with 55% of their 43,200 pixels (in the first 100 views 68 of 81 surfaces, 57% of the pixels). So on the board core 1 has more early work than the 7 ms of edgescan can take, and stays busy through the scan. On the PC's thread core 1 drew 38 of the 56.

### Estimated board time, timedemo demo3, Fast, ms per view on core 0

Before is the lead's board line. After is an estimate, the drawing of a view's surfaces is about 47 ms of work for one core (lmap 13.9 and grad 3.1 on core 0, the spans phase 16.9 is core 0's half of about 31 ms with its wait), of which about 4 ms is sky, water and brush entity surfaces that stay on core 0. Core 1 draws about 7 ms of surfaces during the edgescan (more than half the pixels are ready by then, above), the remaining 40 ms are shared, about 20 ms each plus about 1 ms for the last surface core 0 waits on (c0wait). The range allows core 1 to run up to 20% slower than core 0 while both cores' drawers compete for the XIP cache.

| Phase | before | after (estimate) |
| --- | ---: | ---: |
| world | 10.4 | 10.4 |
| bmodels | 6.9 | 6.9 |
| edgescan (with c0wait) | 7.2 | 7.5 to 8.5 |
| lmap, grad, spans (core 0's share) | 33.9 | 20 to 23 |
| models, vmodel, parts | 9.7 | 9.7 |
| other (setup, sky, turb, zspans) | 4.1 | 4.1 |
| total view | 72.2 | 59 to 63 |
| timedemo frame (13.6 fps, 73.5 ms) | 73.5 | 61 to 64 (15.6 to 16.4 fps) |

Core 1's share of the frame, before about 15 ms of span halves, 1 to 2 ms of alias vertices and 14 to 18 ms of scanout in a 73.5 ms frame, about 45% busy. After about 27 ms of surfaces, 1 to 2 ms of alias vertices and the scanout in a frame of about 62 ms, about 70% busy. Core 1 still waits through the world front end after its scanout, bmodels, models, the view model and particles.

### What to read on the board

A new line on COM8 after the `[time cl]` lines every 2 s.

```
[time cl] core1 per frame ms scan <ms> jobs <ms> busy <ms> idle <ms>, per view pipe <ms> wait <ms> c0wait <ms> surfs <n> c1 <n> early <n>
```

- per frame. `scan` core 1's LCD scanouts, `jobs` every job core 0 gave it (the pipeline, the earlier alias vertices), `busy` both less the pipeline's waiting, `idle` the frame less busy. busy over the frame time is core 1's share.
- per view. `pipe` core 1's time drawing surfaces, `wait` its time in the pipeline waiting for one, `c0wait` core 0's wait for core 1's last surface at a flush (it lands in the phase line's edgescan, lessons L153), `surfs` lit world surfaces drawn through the pipeline by either core, `c1` those core 1 drew, `early` those handed on during the scan.
- The phase line's lmap, grad and spans are now core 0's share only. The sum to watch is edgescan + lmap + grad + spans (41.1 ms before).
- A/B with `-DQOG_BOARD_DRAWCORE1=OFF` on the configure line (the earlier span halving comes back).

### RAM and stack

| | `end` | free beside the 2 KB heap reserve |
| --- | --- | ---: |
| quake_display, main ede79b2 | 0x2003eab4 | 3,404 B |
| quake_display, this branch | 0x2003f2c0 | 1,344 B |
| quake_main, main ede79b2 | 0x2003f58c | 628 B |
| quake_main, this branch | 0x2003f5b4 | 588 B |

The display takes 2,060 B, core 1's light grid 1,296 B, the ring 256 B, core 1's r_drawsurf 48 B, its gradients, Fast drawer and surface state about 270 B, its copy of the earlier Fast gradient rows 76 B, the view copies 60 B, the counters and pipeline state about 60 B. The surfaces' last scan lines cost nothing on the board (entref). quake_main takes 40 B (the counters in the shared qog_phase.c). quake_display.bin is 420,120 B (396,000 on main ede79b2), the second copy of the drawing code is about 24 KB of flash. Core 1's stack (2 KB in scratch X), the deepest chain under its job is 632 B (tools/stack_chain.py, D_QogC1Worker to the exact drawer and its z span), against about 160 B for the earlier alias jobs.

### For the lead and earlier work

- the builder and Fast gradients run on core 1 unchanged (each copy keeps its own rows, from core 1's copy of modelorg and the view vectors). A new global that the r_surf.c or D_CalcGradients writes needs a c1_ name in engine/d_core1.h (a new function defined in those files too), else both cores share it. Run `python tools/core1_syms.py --build <board build>` after a merge, it fails on any variable core 1 reaches that is not its own or on its list.
- D_QogPipeBegin calls the R_QogDlightSettle before core 1 starts. The earlier profiler still starts its core 1 alarm through the qog_video job slot at start-up, which the pipeline does not change (the profiler then also samples core 1 inside the pipeline job).
- Risk on the board, both cores now run their own copy of the drawers out of the shared 16 KB XIP cache. If the xip line shows many more misses in the scan, the copies compete for it.
- Next steps from the research the coordinator forwarded, core 1 could run the alias front end and particle physics while core 0 draws its share of the surfaces, and the edge scan itself could be split (one core sorting and stepping edges, the other making spans a line behind, as frank-quake2 does).

Files, engine/d_core1.c, d_core1.h, d_core1s.c, d_core1e.c (new), engine/d_edge.c (D_QogDrawLitBody, the pipeline), engine/r_edge.c (last lines, hand-on, the no-z wait), engine/d_local.h, engine/d_scanlit.c (D_QogLitCheck, no halving while the pipeline runs), engine/qog_phase.h, port/common/qog_phase.c (the core1 line), port/rp2040/vid_rp2040.c and port/pc/vid_pc.c (lock, wake, PC modes), platform/qog_video.c and .h (core 1's scanout and job times), apps/quake/CMakeLists.txt, tools/core1_syms.py (new).

## 2026-11-25, The LCD pixels through PIO at 60 MHz, text over a Turbo view drawn by the scanout, and a race wait model on the PC (PC and ARM build, registered data)

At 300 MHz the PL022's even prescaler gives the LCD only 50 MHz (300 / 6), and a scanout takes about 22.4 ms (earlier work measured 22.4 at the same 50 MHz, against 20.5 ms of wire). The view that races the scanout waits for it. Two changes and a measurement. Nothing ran on hardware. Every board number below is an estimate.

### The panel and its limit

The controller is a Sitronix ST7789 (bsp/display_cpu/lcd/st7789.c, 320x240 branch, MADCTL 0x70). Its serial write limits depend on the part, the ST7789V datasheet (V1.3, 2014) gives a 66 ns write cycle with 15 ns high and low pulses, the ST7789V3 (V0.1) 16 ns with 7 ns high and low. The board's record (`FWOG_LCD_MAX_HZ` 62.5 MHz, docs/hardware/pinmap.md) and the board itself run 62.5 MHz (250 MHz builds, 8 ns pulses) without trouble, so the panel takes the V3 timing in practice. 62.5 MHz stays the cap.

### PIO scanout (`QOG_LCD_PIO`, on)

| | PL022 (before) | PIO (now) |
| --- | --- | --- |
| clock at 300 MHz | clk_peri / 6 = 50 MHz | clk_sys / 5 = 60 MHz, divider 1 (no fractional jitter), SCK high 2 and low 3 system clocks (6.7 and 10 ns) |
| gaps | about 1.5 bit times between 16-bit frames (mode 0 pulses its frame signal between words) | none, autopull of 16 bits from an 8 deep joined FIFO |
| full scan, 200 rows | 22.4 ms (measured at the same rate) | about 17.2 ms (5,120 bits a row at 60 MHz plus about 0.4 us a line) |
| status bar skipped, 152 rows | about 17.0 ms | about 13.0 ms |
| a row | 112 us | about 85.7 us |

- platform/qog_video.c builds a two instruction program at start-up (`out pins, 1 side 0 [lo - 1]`, `nop side 1 [hi - 1]`, side-set on SCK, out on MOSI, shift left, autopull 16) with n the fewest whole system clocks a bit that keep the rate at or under `QOG_LCD_PIO_KHZ` (`qog_video_pio_bit_cycles`, pure). 250 MHz gives 62.5 MHz (4 clocks), 200 MHz 50 MHz. A 16-bit DMA write to the TX FIFO lands in both halves of the word, so core 1's RGB565 line buffers and line DMA stay as they were, only the DREQ and the write address change.
- The state machine takes pio1 first (pio0 SM 0 holds the WS2812 bar, sys_rp2040.c, and the speaker asks pio0 first, snd_rp2040.c, so it lands on pio0 too), then pio0. No free state machine or instruction room keeps the PL022 and says so.
- The PL022 still sends CASET, RASET and RAMWR (st7789_set_window, the BSP's own fills and text). SCK and MOSI change GPIO function to the PIO for the pixel stream and back after it. Both sides idle with SCK low (SPI mode 0). The end of a scanout is the state machine stalling on its empty FIFO (FDEBUG TXSTALL, cleared first, set again in every cycle it waits).
- SCK and MOSI get fast slew and 8 mA drive for the shorter high pulse.
- The fallback is `-DQOG_LCD_PIO=OFF` (the PL022 as before). `-DQOG_LCD_PIO_KHZ=50000` keeps PIO with 3 and 3 clocks (inside every timing of the V3 sheet, 20.5 ms instead of 22.4). `-DQOG_LCD_PIO_KHZ=75000` gives 75 MHz at 300 MHz (20% over the panel's 62.5), only to try on the board with the user's approval.
- Core 1's line loop stays in flash, the compiler inlines convert_line and scanout into core1_main, which is in flash, in main as well (the "hot loops live in RAM" comment was not true of the board build). Keeping them in RAM with `__no_inline_not_in_flash_func` costs about 450 B of RAM and was left out.

### Text over a Turbo view drawn by the scanout (Turbo only)

Counting on the PC pair showed that the earlier Turbo frames were mostly not packed, in the earlier menu Benchmark with r_fast 2 only 153 of 1,449 presented frames went out packed. The rest had a character over the view (901 spreads from Draw_Character, the notify lines of demo3's pickups, 64 from the console background), which spreads the view to full width (0.3 ms on the board) and makes the next view race a full width scanout. Now Draw_Character over a packed view lists the character instead (`R_QogTurboText`, engine/r_qogturbo.c), runs of characters side by side on one row, 32 runs and 160 characters a frame, in drawing order. VID_Update hands the frame's list to the scanout (`qog_video_set_text`), and core 1 draws each glyph over the doubled RGB565 line as Draw_Character would draw it over the spread view (index 0 see-through). Two lists alternate, so the one in flight stays. A character that does not fit, or any other 2D drawing over the view, spreads it as before and first draws the listed characters into the frame in their order. The crosshair goes to the list when characters are already listed, so it stays above them. The PC builds the panel the same way, so shots and frame hashes show the panel.

- After it, 782 of the same run's 1,449 frames went out packed, 408 of them with characters drawn by the scanout (at most 152 characters in a frame).
- RAM, the two lists 592 B, the scanout's pointers 20 B.

### The race wait model (`-scanmodel`, PC pair)

port/pc/vid_pc.c records for every frame the rows the next view waited for before the end of the scanout and the rows the scanout sent, and with `-scanmodel` prints at exit the board's race wait at five LCD rates, the front end waits until the scanout has sent its rows less the work before the view (pre), and after the world's front end (world) the view waits for the end of the scanout. world is 8.6 ms (the board's demo3 world phase scaled to 300 MHz), pre is not measured, so 3, 5 and 7 ms are printed. The earlier menu Benchmark on the board-flag pair, all 1,449 frames (the demo plus the menu and e1m1 start).

| Mode | LCD | scan ms | race wait ms, pre 5 (pre 3 to 7) |
| --- | --- | ---: | ---: |
| Fast | PL022 50 MHz (300 MHz today) | 19.2 | 5.7 (3.7 to 7.7) |
| Fast | PL022 62.5 MHz (250 MHz before) | 15.7 | 2.4 (1.2 to 4.4) |
| Fast | PIO 60 MHz | 14.7 | 1.7 (0.7 to 3.4) |
| Fast | PIO 75 MHz (not the default) | 11.8 | 0.2 (0.1 to 1.2) |
| Turbo before (PL022 50 MHz, text spreads) | | 19.2 | 5.6 |
| Turbo, PIO 60 MHz and text by the scanout | | 14.5 | 1.5 |

So the PIO scanout is worth about 4 ms of every demo3 frame at 300 MHz in Fast and in Turbo, more than 300 MHz lost to the 50 MHz SPI (3.3 ms in this model). A Turbo view still waits for the end of the scanout before its spans (R_EdgeDrawing's VID_QogScanDone), which the counts of row waits did not include. Every ms that later work takes off the world's front end comes back as race wait unless the scanout is shorter, so the faster scanout also keeps those savings.

### What the board should print (COM8, display)

```
[lcd] spi 50000000 Hz (clk_peri 300000000 Hz)
[qog_video] lcd pio1 sm 0, 60000 kHz (clk_sys 300000 kHz / 5, high 2 low 3, limit 62500 kHz), commands on the PL022
[qog_video] 320x200 -> 320x200 at y=20, dma ch <n>
...
[quake_display] status bar rows sent in 25 of 300 frames, last scan 13050 us for 152 rows
```

The last scan is about 13.0 ms for 152 rows and 17.2 ms for 200 with PIO, about 17.0 and 22.4 ms with the PL022 (`-DQOG_LCD_PIO=OFF`). `[qog_video] lcd no free PIO state machine, staying on the PL022` means the fallback ran. A picture with wrong colours, shifted pixels or a black panel with PIO but not with OFF means the panel misses the 6.7 ns high pulse, and `-DQOG_LCD_PIO_KHZ=50000` is the next build to try.

### Checks (hidden, `-nosound`, own build folders, own git-ignored data folder)

| Check | Result |
| --- | --- |
| `qogtest_hash`, quakeog_pc | ce86f894f9de |
| the 121 shot frames (`qogtest_k_quiet` + `qogtest_d_shots` and the fhash frames), quakeog_pc r_fast 0, 1 and 2, against an untouched build of dd0f819 | 121 of 121 byte identical in each mode, `qogtest_f_hash` e92bc9e9650d |
| `qogtest_ap_text.cfg` (E1M1 Turbo, six short notify lines, a long one, the crosshair, then Fast and Exact), 39 frame hashes, quakeog_pc and the board-flag pair against dd0f819 | identical, the pair drew 35 frames' characters from the list |
| the same with the lists cut to 3 runs and 20 characters (`QOG_TURBO_RUNS`, `QOG_TURBO_CHARS`), every frame overflows | identical |
| menu Benchmark on the pair, r_fast 1 and r_fast 2 | `qog_scanrace` 0 writes before the scanout passed, `qog_lockcheck` 0 of 1,449 changed |

### RAM and size (board, the full configure line with 300 MHz and Turbo)

| Build | quake_display `end` | spare beside the 2 KB heap | quake_main `end` |
| --- | --- | --- | --- |
| main dd0f819 | 0x2003ead8 | 3,368 B | 0x2003f58c |
| PIO scanout only | 0x2003eafc | 3,332 B | 0x2003f58c |
| this branch | 0x2003ed68 | 2,712 B | 0x2003f58c |

quake_display.bin 400,016 B (397,840 on main), quake_main.bin 1,343,788 B. `-DQOG_LCD_PIO=OFF` builds and links (0x2003eafc before the text lists).

Files, platform/qog_video.c and .h, platform/CMakeLists.txt (hardware_pio), port/rp2040/vid_rp2040.c, port/rp2040/link_rp2040.c (the diag line), apps/quake/CMakeLists.txt (`QOG_LCD_PIO`, `QOG_LCD_PIO_KHZ`), engine/r_qogturbo.c, engine/vid.h, engine/draw.c (Draw_Character, one call), port/pc/vid_pc.c. Test scripts in the git-ignored data folder. `qogtest_ap_fast.cfg`, `qogtest_ap_turbo.cfg` (r_fast 1 or 2, then the earlier menu Benchmark), `qogtest_ap_text.cfg`.

Not done, for later.
- Measure pre on the board (the time from VID_Update to the view's first scan wait). The model's ranges come from guessing it.
- Text drawn by the scanout covers Draw_Character only. Draw_Pic and Draw_TransPic over the view (the pause and loading plaques, the menu) still spread it.

## 2026-11-25, Brush entities and the edge scan with fewer float calls, all exact (PC and ARM build, registered data)

The board's timedemo demo3 spends 6.9 ms a view in bmodels, 10.4 in world and 7.2 in edgescan (250 MHz, Fast), and none of it shrinks in Turbo mode. Counted on the PC (`QOG_AQ_STATS`, below), most of the float and double work left in these phases after earlier work sat in brush entity code, the 1/z order of two brush surfaces in the edge scan, the brush polygons' clip against the world BSP, and the view planes turned for every brush surface drawn. Eight changes, each behind its own flag, all exact, r_fast 0, 1 and 2 give the same bits as before. Nothing ran on hardware. Every board saving below is an estimate from the Thumb code (objdump of `build-aq/arm`, ROM float calls at about 55 cycles, fdiv 75, f2d 22, dadd 115, dmul 150) and the PC's work counts.

### Where the time went (PC, timedemo demo3, Fast, per view)

| Work | Per view | Cost before |
| --- | ---: | --- |
| brush entities in the view list / rejected by their box / drawn (clipped to the BSP, in one leaf) | 20.7 / 15.2 / 5.4 (3.0, 2.4) | box test about 950 cycles a plane (two float dot products, four f2d, two dsub, two dcmp) |
| brush surfaces tested for facing / facing | 65.0 / 34.2 | a float dot product, f2d and dcmp each |
| R_RecursiveClipBPoly calls / edges tested / edges split | 108.8 / 425 / 28 | tplane 21 float calls a call, two float dot products and two compares an edge (about 730 cycles) |
| planes of those calls that are axial | 99.9% | |
| 1/z compares of two brush surfaces with one key in R_LeadingEdge | 283 | five f2d, four or five dmul, two dadd, two float multiplies and adds, a dcmp, about 1,350 cycles |
| brush surfaces drawn by D_DrawSurfaces (of the same entity as the brush surface before) | 13.9 (11.6) | two R_TransformFrustum (80 float calls each) and a TransformVector each |
| R_RotateBmodel in the bmodels phase | 5.4 entities | R_TransformFrustum to turn and again to put back |
| Fast face gradients (world and brush) | 140 | 16 float calls each |
| world nodes / faces / clip edges / emitted edges / projected points | 271 / 197 / 982 / 467 / 506 | integer already |
| scan lines / edge visits in R_GenerateSpans | 152 / 4,152 | integer |

### Changes

| Change | Flag | Mode | What it does | Estimated board saving per view, demo3, 300 MHz |
| --- | --- | --- | --- | --- |
| 1/z order of brush surfaces in float | `QOG_LEADZ_FLOAT` (r_edge.c, R_QogZFront) | exact, all modes | id's newzi is a float part plus a product exact in double, scaled by the double 0.99 or 1.01. The float version is within 11.2 2^-24 of the sum of the four terms' sizes of id's difference, which is below 2^(e-17) for e the largest exponent, read from the bits. Outside that bound the float sign is id's, inside it id's double code runs (0.07% of the compares in demo3) | 283 x about 700 cycles, 0.66 ms of edgescan |
| Brush clip sides by integer compares | `QOG_BPOLY_AXIAL` (r_bsp.c, R_QogAxPlane, R_QogAxSide) | exact, all modes | for an entity that does not turn, tplane's normal is id's identity product (R_QogIdentityRotate), and on an axial plane id's DotProduct is exactly plus or minus one coordinate (or a signed zero by round to nearest), so its bits need no float call. dist > 0 is then an integer compare of the float bits in real order. A split edge gets id's two dists by one float subtraction each. Values not finite, denormal or below 2^-100 take id's arithmetic | tplane 21 float calls to 1, an edge about 730 cycles to about 90, 0.37 M cycles, 1.24 ms of bmodels |
| Brush surface facing without double | `QOG_BMODEL_FACING` (r_bsp.c, R_QogFacing) | exact | for a float x, x < -0.01 and x > 0.01 are x < -0.01f and x > 0.01f, and on an axial plane the dot product is the exact one above | 65 x about 250 cycles, 0.05 ms |
| Brush box test in float compares | `QOG_BMODEL_BBOXF` (r_main.c, R_BmodelCheckBBox) | exact | id's double d is a float dot product less a float dist, exact in sign, so d <= 0 is dot <= dist (the earlier argument for the world nodes). Turned entities keep id's code | about 330 cycles a plane, 0.06 ms |
| Brush view planes from the world's | `QOG_BMODEL_FRUSTUM` (r_main.c, R_QogFrustum) | exact | R_TransformFrustum's normals depend on the vectors alone. While view_clipplanes holds the normals of the world's vectors, an entity whose turned vectors have the same bits, and the planes put back after it, compute only the four dists. Anything else runs R_TransformFrustum. No copy of the planes is kept | 6.6 of 11 calls a view reuse the normals, about 3,300 cycles each, 0.07 ms of bmodels |
| Brush surfaces draw without turning the view planes | `QOG_DSURF_NOFRUSTUM` (d_edge.c D_QogSubmodelBegin, r_bsp.c R_QogRotateBmodelVecs) | exact | nothing that draws spans reads view_clipplanes, so D_DrawSurfaces turns only the vectors and modelorg and sets the planes back once at its end, from the same vectors and modelorg as id's last restore. The entity's transformed origin is kept for the entity drawn last | 13.9 x about 9,000 cycles, 0.42 ms of edgescan |
| Brush entity Fast setup made again only for its origin | `QOG_FX_REUSE` (r_draw.c R_QogFxSetup) | Fast and Turbo, the same values | when the vectors have the bits of the last complete setup (usually the world's), only org, the rounding constants cx, cy, cz and the view planes' origin terms are made again, by the same statements | about 7,500 cycles an entity, 0.14 ms of bmodels |
| Face gradients with fewer float calls | `QOG_FGRAD_AXIAL` (r_draw.c R_QogFaceGradFast) | Fast and Turbo, the same values | an axial plane's dot product as above, and since gk is a power of two, g (distinv gk) rounds as (g gk) distinv, with g gk made by adding to the float's exponent | 140 x about 300 cycles, 0.15 ms (most of it world) |
| Measurement | `QOG_AQ_STATS`, `QOG_LEADZ_CHECK`, `QOG_BPOLY_CHECK`, `QOG_BMODEL_FACING_CHECK`, `QOG_FGRAD_CHECK` | PC only | work counts every 100 views (`qog_aqstats`, engine/qog_aqstats.h), and both ways on every compare, side, dist and gradient with the mismatches counted | |

Together, estimates at 300 MHz, bmodels about 1.6 ms, edgescan about 1.1 ms, world about 0.15 ms, about 2.8 ms a view (3.4 ms at 250 MHz). The board has run about twice earlier estimates, so 2.8 to 5.6 ms is the range to expect. Turbo has the same counts (the brush and scan work does not depend on the width).

Not done, the world phase is now mostly integer work and flash reads of the BSP data (271 nodes, 197 faces, about 1,300 plane distances and 500 projections a view, all integer since earlier work), which these flags cannot touch. The Fast clipped points (138 a view, 13 float calls each, about 0.35 ms) and an integer box test for brush entities (about 0.1 ms) are left, the first changes Fast pictures. R_GenerateSpans calls R_LeadingEdge for every leading edge (4,152 edge visits a view), and taking its first test (spanstate) into the loop would save the call on most of them, about 0.2 ms, but that loop is in the earlier edge scan work. The "brush entity in no visible leaf" reject the lead suggested only applies to 0.16 entities a view, since the server sends only entities in the PVS and the box test rejects 15 of 21.

### Exact proof (hidden, `-nosound`, own `build-aq` folders, an untouched build of dd0f819 in `build-aq/ref`, own git-ignored data folder)

Both PC builds carry the board's world flags (`QOG_VERTCACHE QOG_VERTCACHE_CLIPDIST QOG_WORLDNODE_FAST QOG_FRONT_FAST QOG_WORLDNODE_INT`) so the Fast front end runs too.

| Test | Result |
| --- | --- |
| `qogtest_hash`, quakeog_pc as pc/CMakeLists.txt builds it and with the world flags | ce86f894f9de both |
| `qogtest_f_hash` | e92bc9e9650d |
| 121 shot frames (`qogtest_k_quiet` + `qogtest_d_shots` 113 and the 8 fhash frames), all 200 rows, against dd0f819 | r_fast 0 121 of 121 byte identical, r_fast 1 121 of 121, r_fast 2 (Turbo) 121 of 121 |
| check build, timedemo demo1 (r_fast 0), demo2 (r_fast 1), demo3 (r_fast 1 and 2) | 1/z compares, clip planes, sides and dists, facing tests and face gradients both ways, about 710,000 compares, 2.75 million plane, side and dist tests, 160,000 facing tests and 410,000 gradients (counted to the last full 65,536 printed), 0 differ |

### RAM and stack (board, the full configure line of docs/handoff.md with `-DQOG_SYS_KHZ=300000 -DQOG_SYS_MV=1250 -DQOG_BOARD_TURBO=ON`, own `build-aq/arm` and `build-aq/armref` of dd0f819)

| | `end` | dd0f819 |
| --- | --- | --- |
| quake_display | 0x2003eaec | 0x2003ead8 (20 B more, the last entity and its transformed origin, three byte flags) |
| quake_main | 0x2003f58c | 0x2003f58c |

quake_display.bin 399,480 B (397,872 for dd0f819), so the display keeps about 3,350 B beside the 2 KB heap reserve. Stack, R_RecursiveClipBPoly 136 B a level as before (the axis rides in tplane.type), R_DrawBEntitiesOnList 120 B as before, new leaves R_QogZFront 48, R_QogAxSide 32, R_QogFacing 32, R_QogFrustum 32, D_QogSubmodelBegin 40, so no chain gets deeper.

### For earlier work

- r_edge.c, R_QogZFront and R_QogZFrontDouble before R_LeadingEdge, and the two 1/z sites inside it. They read only the two surfaces, the edge and fv, so they run on either core.
- d_edge.c, D_QogSubmodelBegin and three statics (the last entity, its transformed origin, a flag) used inside one D_DrawSurfaces call, which must stay on one core at a time. The view planes are set back at the end of D_DrawSurfaces.
- r_main.c, R_QogFrustum before R_DrawBEntitiesOnList and its two calls, the box test's float compares, and the `qog_aqstats` print at the end of R_RenderView_ (PC measurement only).

Files, engine/r_bsp.c, engine/r_edge.c, engine/d_edge.c, engine/r_main.c, engine/r_draw.c, engine/r_efrag.c (a count), engine/qog_aqstats.h (new), pc/CMakeLists.txt, apps/quake/CMakeLists.txt. Test scripts (git-ignored data folder). `qogtest_aq_td<1..3>_<0..2>.cfg` (timedemo with r_fast set), `qogtest_ai_turbo.cfg`.

## 2026-11-25, 300 MHz at 1.25 V, a flash check over every RX delay, die temperature lines, and a board PC-sampling profiler (ARM build, not run on hardware)

The user approved 300 MHz. Both Quake apps now raise the core voltage to 1.25 V and then the clock to 300 MHz right after `board_init()`, extending the earlier run-time switch (port/rp2040/clock_rp2040.c). The BSP, its 200 MHz display bootloader and every other app are untouched. A new build option, `QOG_PROF`, adds a profiler that samples the interrupted PC on the board and prints each core's top 20 functions every 5 s. Nothing ran on hardware. Every board number below is an estimate.

### Clock and voltage

| Option | Default | Step back |
| --- | --- | --- |
| `QOG_SYS_KHZ` | 300000 (VCO 1500 MHz over 5 and 1) | `-DQOG_SYS_KHZ=250000 -DQOG_SYS_MV=1200` is the earlier tested setting, `0` keeps the BSP's 200 MHz |
| `QOG_SYS_MV` | 1250 | 1300 is the regulator's top step |
| `QOG_FLASH_FAST` | ON | OFF keeps divider 4 |
| `QOG_FLASH_MAX_KHZ` | 150000, so 300 MHz tries 150 MHz | 133000 keeps the W25Q128JV's rating (div 4, 75 MHz, at 300 MHz) |
| `QOG_TEMP_S` | 5 | 0 for no temperature lines |

Why 1.25 V, community reports put RP2040s at up to about 275 MHz on 1.20 V and up to about 350 MHz on 1.25 V, with 1.30 V the regulator's top step (rp2040-doom runs 270 MHz at 1.30 V). 300 MHz sits in the middle of the 1.25 V range, about 50 MHz below where parts start to fail at that voltage, while 1.20 V would leave no margin past about 275 MHz. 1.30 V buys more margin for about 8% more core power. If the board shows faults that a 250 MHz build does not, the first step is `-DQOG_SYS_MV=1300`, the second 250 MHz.

### Flash divider and RX delay

At 300 MHz the SSI divider can only be 2 (150 MHz, 13% above the part's 133 MHz) or 4 (75 MHz), because it must be even. The run-time switch keeps the earlier shape (RAM code, interrupts off, core 1 not started yet, the old setting back before an instruction is fetched from flash again). What changed.

- Divider 2 is tried with every RX sample delay from 1 to 3 (`QOG_FLASH_DLY_MAX`), not only until the first one passes, and the middle of the run of passing delays is kept (2 when all three pass, the later one when two pass), so the sample point sits away from both edges of the data eye. Delay 1 is what boot2 writes. On the board at 125 MHz main once needed delay 2.
- The check reads the whole app (up to `QOG_FLASH_CHECK_KB` 1024, so all 384 KB of the display app and the first 1 MB of main's) and 16 blocks of 4 KB spread over the data region (the display's at 0x10100000, main's at 0x10200000), in 16 KB chunks through the uncached alias. Each chunk is hashed at the old divider with interrupts on, then twice at every delay still in the running with interrupts off (about 2 ms at a time), and the old setting goes back after every chunk. The chosen setting is written only after the whole check. Boot takes about 0.2 s longer on the display and 0.4 s on main.
- If no delay passes, the divider stays 4.
- While the game runs, one 4 KB block of the app is read again every 5 s at the live setting and compared with its hash at the old divider. A miss is counted. Three in a row put the old divider back, with core 1 locked out on the display (core 1 has the SDK's lockout handler, main has no core 1).

Disassembly of both ELFs (`flash_hash`, `ssi_set`, `flash_try.constprop.0`, at 0x200000c0 to 0x2000016e on the display and 0x20000120 to 0x200001ce on main, all RAM), between `cpsid i` and `msr PRIMASK` there are only `bl` to the other two RAM functions, stores to the SSI at 0x18000000 (SSIENR +8, BAUDR +0x14, RX_SAMPLE_DLY +0xF0), loads through the pointer into the 0x13xxxxxx alias, and the FNV constant in RAM. No load or branch reaches 0x10xxxxxx. The final switch, `qog_flash_fast_reapply` and the recheck's fallback call `ssi_set` through its flash veneer with interrupts already off, as before, and `ssi_set` turns the SSI on again before it returns to flash.

The lines to read (COM8 display, COM7 main, about 6 s after boot).

```
[quake] clock 300000 kHz at 1250 mV (BSP 200000 kHz), clk_peri 300000 kHz
[quake] flash 150.0 MHz ok (div 2 from 4, rx delay 2, delays passing 1,2,3 of 1 to 3, 448 KB checked, reapplied 0)
[quake] temp 38.4 C, clock 300 MHz 1250 mV, flash 150 MHz div 2 dly 2, recheck ok 3 bad 0
```

A failed check prints `[quake] flash 75.0 MHz fallback (div 4, rx delay 1), read check FAILED at div 2 with every rx delay 1 to 3` and the game runs on at 75 MHz. A failed recheck prints `[quake] flash recheck FAILED on block N, back to div 4 rx delay 1`. `delays passing` shows how wide the eye is, a single passing delay means that chip is at the edge, and `-DQOG_FLASH_MAX_KHZ=133000` is the safe choice for it.

### Everything derived from a clock, at 300 MHz

| Item | At 300 MHz |
| --- | --- |
| Link UART, both apps | 300 MHz x 8 / 12.5 Mbaud = 192, IBRD 1 FBRD 32, 300 x 4 / 96 = exactly 12.5 Mbaud. The display bootloader stays at 200 MHz (IBRD 1 FBRD 0, also exact), so main's display update at boot, after main's boost, still talks to it |
| LCD SPI | clk_peri 300 MHz, prescale 2 x postdiv 3 = **50 MHz** (62.5 would need 300 / 4.8). A full 200 row scanout about 22.4 ms (the earlier figure at 200 MHz, the same SPI rate) instead of 18.3, 152 rows (HUD skip) about 17 ms instead of 13.9. The scanout race point moves on E1M1 (row 136) from 12.4 to about 15.2 ms after VID_Update, on demo3 (row 116) from 10.6 to about 13.0 ms |
| clk_peri from another clock | looked at, not done. On the RP2040 clk_peri has no divider and comes from clk_sys, PLL_SYS, PLL_USB, the oscillators or GPIN. USB needs PLL_USB at a multiple of 48 MHz, 62.5 MHz SPI needs clk_peri at 125 or 250 MHz, an exact 12.5 Mbaud needs 3.125 MHz x N, and no multiple of 48 MHz meets either. The best mix is PLL_USB at 240 MHz (USB over 5), SPI 60 MHz (scanout about 19 ms) but the UART at 12.47 Mbaud (0.26% slow against main's exact 12.5), and reprogramming PLL_USB at run time drops the display's USB, which `fw.py bootsel --cpu display` needs on a CPU with no BOOTSEL button. Not worth that risk |
| Speaker I2S PIO | `(clk * 4 / 11025) >> 8` = 425 (clk x 4 = 1.2e9 still fits 32 bits), 300 MHz / (64 x 425) = 11,029 Hz, 0.04% fast (11,035 at 250) |
| WS2812 PIO | float divider from clock_get_hz at `ws2812_init`, after the boost |
| I2C1, backlight PWM | set again after the switch (the SDK's I2C timing, PWM divider 117.2 for 10 kHz with wrap 255) |
| FPGA clock on main | divider 12 set before the switch (16.7 MHz at 200 MHz), 300 / 12 = exactly 25 MHz after |
| Timers, sleeps, hang detector, phase timing | the 1 MHz tick from clk_ref, unchanged |
| USB, ADC | PLL_USB 48 MHz, unchanged |
| Main's saves | the ROM's erase and program use their own slow SSI divider, then boot2 sets div 4 (75 MHz) and the FAT code reapplies the checked setting. Watch `reapplied` grow after a save |

### 300 MHz against 266 MHz and others (estimates, demo3 at 13.6 fps is 73.5 ms a frame)

| clk_sys | Flash | UART | LCD SPI | E1M1 race point | CPU time | Estimated demo3 |
| --- | --- | --- | --- | --- | --- | --- |
| 250 (today) | 125 MHz div 2 | 12.5 exact | 62.5 | 12.4 ms | 1.00 | 13.6 fps (measured) |
| 266 (VCO 1596 / 6) | 133 MHz div 2, at the rating | 12.518 (+0.14%) | 44.3 (266 / 6) | 17.5 ms | 0.94 | about 13.5 to 14 fps, 4 ms of CPU saved against 2 to 5 ms more race wait |
| 288 (VCO 1440 / 5) | 144 div 2 | 12.522 (+0.17%) | 48 | 16.2 ms | 0.87 | about 14.5 fps |
| **300** (VCO 1500 / 5) | 150 div 2, or 75 div 4 | 12.5 exact | 50 | 15.2 ms | 0.83 | **about 15 to 15.5 fps** at div 2, about 14.8 to 15.3 at div 4 (11 ms of CPU saved, 2 to 4 ms more race wait, div 4 about 0.5 ms more XIP misses) |

The panel's SPI prescaler decides the trade, not the flash, above 250 MHz the SPI drops from clk / 4 to clk / 6, so 266 MHz loses more to the scanout race than it gains in the CPU. The flash at exactly its rating is worth little, because the earlier XIP model puts the misses at about 0.5 ms a view at 125 MHz (150 against 133 MHz is about 0.05 ms). 275 MHz has no exact PLL setting. So 266 MHz is not recommended and is not an extra option. The lead chooses with `-DQOG_SYS_KHZ=300000 -DQOG_SYS_MV=1250` (default, 1300 for more margin) or `-DQOG_SYS_KHZ=250000 -DQOG_SYS_MV=1200`, and the flash at 150 MHz or `-DQOG_FLASH_MAX_KHZ=133000` (75 MHz at 300 MHz). The earlier Turbo mode removes the race, and with it 300 MHz keeps the whole 11 ms.

### Current draw and temperature (estimates)

Core dynamic power goes with f x V squared, 250 MHz at 1.20 V to 300 MHz at 1.25 V is 1.2 x 1.085 = +30%, the core current (f x V) +25%. Taking a busy RP2040 core at about 45 mA at 250 MHz and 1.20 V (scaled from about 25 mA at 125 MHz and 1.10 V), each chip draws about 57 mA, about 12 mA more per chip and 25 mA for both, from the 3.3 V rail through the on-chip linear regulator, about 0.08 W more, a few percent of the board's draw with the backlight on. Each chip then dissipates about 0.19 W (core and regulator drop), a few degrees above ambient. Each app prints `[quake] temp ...` every 5 s (ADC channel 4, 27 - (V - 0.706) / 0.001721, the average of 8 readings, uncalibrated so a few degrees either way). Expect 30 to 45 C at room temperature. A reading that keeps rising past 60 C, or faults that come as the board warms, are the signs to step back.

### Board PC-sampling profiler (`QOG_PROF`, off by default)

- Sampling, one hardware timer alarm per core, its TIMER IRQ enabled only in that core's NVIC, priority 0 (so it also samples the USB, DMA and link interrupts), `QOG_PROF_HZ` 4000 with the period jittered by up to 1/8 so it cannot lock onto a loop of the same period. The handler is in RAM. EXC_RETURN bit 2 picks the process stack (the game, `QOG_GAME_PSP`) or the main stack (inside another interrupt), and the stacked PC and LR are read. Core 1's interrupt is enabled once through the frame's core 1 job slot (`qog_video_core1_start`), so core 1's code is not changed (`QOG_PROF_CORE1`, on by default with `QOG_PROF`). `QOG_PROF_MAIN` samples main's core 0 too (main has no core 1).
- Function table. `tools/prof_table.py` runs after each link. It reads the ELF's STT_FUNC symbols (flash and RAM resident, one name per address), makes sorted starts and ends, a name pool and a page index (the last function at or before each 1 KB of flash text, 2 KB on main), and writes it into `qog_prof_blob`, a fixed 96 KB const array (`QOG_PROF_TABLE_KB`) in the same image, patching the .elf, .bin and .uf2 in place. The array's size does not change, so no code moves. Checked, objcopy of the patched ELF gives the patched .bin byte for byte on both apps, picotool's uf2 of the patched main ELF equals the patched .uf2, and main's embedded display image carries the patched table. The display table has 1,336 functions (45 in RAM) in 37.9 KB, main's 3,103 (46 in RAM) in 69.7 KB. The SDK's .hex and .dis files are not patched.
- Per sample, the page index is in RAM, so a flash PC costs one or two table reads and a RAM PC a binary search over the RAM functions. Counts go into an open addressed table per core (512 pairs on core 0, 256 on core 1), what does not fit is counted as `lost`. A PC in the boot ROM (the float routines) counts as `[rom]` and once more against the function in the stacked LR, so float work shows under its caller. About 150 cycles a sample, 0.2% of a core at 4 kHz.
- Report every 5 s, with the sampling paused, per core.

```
[prof] core 0: 20014 samples in 5.0 s, 233 slots of 512 used, 0 lost
[prof] c0  1  21.3%  4263 D_DrawSpans8 0x1004cde4
...
[prof] c0 rom   9.8%  1961 from R_AliasPreparePoints 0x10051234
```

  then the counts start again, so each window is the 5 s before it. Core 1's idle shows as `core1_main`. To add up a run (all of timedemo demo3) save the USB log and run `python tools/prof_resolve.py display.log` (`--from N --to M` picks windows). `python tools/prof_resolve.py --elf <elf> 0x...` names addresses, for example from a fault record.
- Build it in a folder of its own, the full configure line plus `-DQOG_PROF=ON` (and `-DQOG_PROF_MAIN=ON` for main), for example `cmake -B build-prof --preset target <the usual options> -DQOG_PROF=ON -DQOG_PROF_MAIN=ON`, then `cmake --build build-prof --target quake_main`. Configure prints `qog: profiler build, quake_display hunk 72 KB less 3 KB is 69 KB`, the build `prof_table: ... functions` for each app. Flash it like the normal game.
- RAM, 2 KB (core 0), 1 KB (core 1), up to 1 KB of page index. The profiler build takes 3 KB of the client hunk (69 KB, timedemo demo3's e1m6 needs about 66, `QOG_PROF_HUNK_KB`) and 4 KB of the server hunk (170 KB, e1m6 needs 138, but e1m3 163 and e2m7 172 do not fit, `QOG_PROF_SV_HUNK_KB`), so this build is for demo3 and the early levels.

### RAM and size (`end`, arm-none-eabi-nm)

| Build | quake_display `end` | spare beside the 2 KB heap | quake_main `end` | spare |
| --- | --- | --- | --- | --- |
| main 8511c77 (250 MHz) | 0x2003ea28 | 3,544 B | 0x2003f544 | 700 B |
| this branch (300 MHz, check, recheck, temperature) | 0x2003ea70 | 3,472 B | 0x2003f58c | 628 B |
| profiler (`QOG_PROF=ON`, `QOG_PROF_MAIN=ON`) | 0x2003f18c | 1,652 B (hunk 69 KB) | 0x2003f468 | 920 B (hunk 170 KB) |

quake_display.bin 392,784 B (493,896 with the profiler), quake_main.bin 1,336,556 B (1,538,492). The PC build compiles none of these files. It builds (build-pcan, quakeog_pc, quakeog_cl and quakeog_sv). The `qogtest_hash` run could not be made in this session (the test runner was not allowed to start), and with no PC source touched the hash cannot have changed.

### What to look for on the board

- Both chips, the clock line with `clk_peri 300000 kHz`, the flash line, `[lcd] spi 50000000 Hz (clk_peri 300000000 Hz)`, `[link] uart0 12500000 baud (requested 12500000), clk_peri 300000000`, and on main `[fpga] clk 25000000 Hz (clk_sys 300000000 / 12)`.
- The temperature line every 5 s, `recheck ok` growing and `bad 0`.
- Core voltage trouble, hard faults, `Sys_Error`s, hangs or a watchdog loop that a 250 MHz build does not show, more likely as the board warms. Try `-DQOG_SYS_MV=1300`, then 250 MHz.
- Flash trouble, the fallback line (the game runs at 75 MHz), `bad` above 0, crashes or garbage textures only with div 2. Try `-DQOG_FLASH_MAX_KHZ=133000`.
- A dark display or main reporting a failed display update would mean the link failed at boot. Both sides compute an exact 12.5 Mbaud, so it should not happen. The step back is 250 MHz.
- Speed, the bench line should read `clock 300 MHz, flash 150 MHz`, and `race` in the `[time cl]` line grows by 2 to 4 ms.

Files, port/rp2040/clock_rp2040.c, port/rp2040/qog_clock.h, port/rp2040/prof_rp2040.c, port/rp2040/prof_table_rp2040.c and port/rp2040/qog_prof.h (new), tools/prof_table.py and tools/prof_resolve.py (new), apps/quake/CMakeLists.txt (the clock and profiler block), README.md and docs/handoff.md (the configure line). sys_rp2040.c, vid_rp2040.c, qog_video.c and the engine are not touched, the temperature line, the recheck and the profiler run from `qog_clock_report_once`, which both main loops already call every frame.

## 2026-11-25, Dynamic lights in integers, the lit surface hash without tree searches, gradients from rows in Fast mode (PC and ARM build, registered data)

The board's timedemo demo3 spends 13.9 ms a view in lmap (2 ms in E1M1 play) and 3.1 ms in grad. Nothing ran on hardware. Savings below are estimates from the Thumb code (objdump of `build-al/arm`) and the PC's work counts, with ROM float calls taken at 55 to 65 cycles. The board has run about twice earlier estimates before.

### Where lmap went (PC, `QOG_LMAP_STATS`, timedemo demo3, per view)

| Work | Per view | How it cost before |
| --- | ---: | --- |
| light grids built (R_BuildLightMap) | 73.0 (65.9 distinct surfaces) | integer already, about 30 cycles a cell, 2,457 cells and 2,712 cell styles |
| dynamic lights alive | 1.45 | |
| light and surface pairs | 34.5, of which 34.1 past the minlight test | |
| cells of those pairs | 1,361, of which 292 lit | id's loop does about 5 float calls a cell and 5 more a lit cell, about 0.6 M cycles |
| R_MarkLights world pass (runs inside lmap, at the first lit surface question, L141) | 220 nodes, 391 surfaces looked at | about 8 float calls a node |
| R_LightReaches tree searches with the board's 256 entry hash | 1,613 nodes (0 with the PC's 1,024 entries) | about 8 float calls a node, about 0.8 M cycles, the biggest item (L140) |

### Changes

| Change | Flag | Mode | What it does | Estimated board saving per view, demo3 |
| --- | --- | --- | --- | --- |
| Dynamic light cells in integers | `QOG_DLIGHT_INT` (r_surf.c, R_QogDlightInt) | exact, both modes | per light and surface the floats are read once (floor and fraction of the light's position in the grid, ceil of minlight, rad times 2^24), then each cell is integer, the distance, the test, and the add with id's float rounding as a threshold constant per light (L142, L143). Rows the light cannot reach are skipped (id's distance is never below the row's). Positions beyond 3,800 texels, a fraction within 2^-11 of a whole number, rad below 1 and cells that would reach 2^23 take id's float code. 99.7% of the pairs and all but 10 of 292,000 lit cells take the integer path in demo3 | 0.6 M to about 0.1 M cycles, about 2 ms |
| Only surfaces facing the eye are marked | `QOG_DLIGHT_FACING` (r_light.c, R_QogNodeFacing) | exact | in the pass that marks the surfaces visible in the frame, a node's surfaces are marked only on the side R_RecursiveWorldNode draws, from the same float distance and the same tests. A surface left out is never drawn in that frame, so nobody asks for its bits. The hash fills about half as often | tree searches 1,613 to 780 nodes a view |
| A lost surface's node found, the light tested up the parents | `QOG_DLIGHT_REACHUP` (r_light.c, R_QogLightReachesWorld) | exact | qbsp writes nodes and faces in the same order, so a binary search over firstsurface finds the surface's node (checked by its run, else id's search). id's walk reaches the node exactly when every node above it lets the walk go down that side, which is about the tree's depth of tests instead of a search of everything the light reaches | with FACING, searches 1,613 to 0.5 nodes a view plus about 20 tests per lost question, about 0.8 M cycles, 3 ms |
| A light's distance to an axial plane with one subtraction | `QOG_DLIGHT_AXIAL` (r_light.c, R_QogLightDist) | exact | id's sum of products with a normal of one 1 and two 0 is the coordinate itself | up to 5 of 8 float calls on axial nodes of the marking pass, about 0.3 ms |
| D_CalcGradients without double arithmetic | `QOG_GRAD_EXACTF` (d_edge.c) | exact, both modes | the mip scale from a table of the same floats, id's `(fixed16_t)(x + 0.5)` with the double 0.5 done in float below 2^23, as `x` or `x + 1` from 2^23 to 2^31 (the float is a whole number there), double only beyond (the earlier note) | 2 f2d, 2 dadd, 2 d2iz and a divide a surface, about 0.05 ms |
| Fast gradients from rows made once a view | `QOG_GRAD_FAST` (d_edge.c, D_QogGradFast) | r_fast 1 only, world surfaces | the earlier idea for the face 1/z gradients applied to s/z and t/z, three rows (vright / xscale, -vup / yscale, vpn - xcenter row0 - ycenter row1) in 2^13 to 2^14 times a power of two, the texture axis in 1/16384, three integer dot products and three float multiplies per axis. sadjust and tadjust are the eye's texture coordinate, modelorg . v + v[3], in 64-bit integers (the view rotation drops out of id's dot product). Brush entities and axes longer than 2 keep id's code. the own rows live in frame buffer rows the spans overwrite, so these are separate (76 B) | about 78 float calls a surface to about 15, 73 surfaces a view, about 0.25 M cycles, 1 ms of grad |
| Two-core helper | `R_QogDlightSettle` (r_light.c, declared in model.h) | exact, not called yet | runs the waiting marking pass at once, for earlier work to call after R_RenderWorld and before two cores build light grids | |
| Measurement and check builds | `QOG_LMAP_STATS`, `QOG_DLIGHT_CHECK` | PC only | work counts every 100 views (`qog_lmstats`), and both ways on every light pair and every lost question (`qog_dlcheck`, `qog_rccheck`) | |

Together, estimates, lmap from 13.9 ms to about 3 to 5 ms on the board (about 1.4 M of about 1.75 M estimated cycles removed), grad from 3.1 ms to about 1 to 2 ms in Fast mode.

Not done, Fast mode shortcuts for the light grids themselves. After the exact changes a dynamic light costs about 40 cycles a cell and the base build about 30 cycles a cell, so coarser or skipped dynamic lights would save a few hundred microseconds at most for a visible change. A cache of built grids between frames needs RAM the display does not have (1,296 B a grid).

### Exact proof (hidden, `-nosound`, own `build-al/pc2` and an untouched build of b585365 in `build-al/ref`, own git-ignored data folder with a baked qogbake.bin)

## 2026-11-25, Turbo render mode, the 3D view at half width doubled by the LCD scanout (PC and ARM build, registered data)

The user approved a third render mode next to Exact and Fast. `r_fast 2` (Options, `Render  Exact / Fast / Turbo`, left and right or gray cycle it, archived) draws the 3D view at 160x152 with all of Fast mode's shortcuts, and the LCD scanout sends each of its pixels twice side by side, blocky like Doom's low detail. The status bar, menus, console, notify lines and centre prints stay at 320 and sharp. The Benchmark line and page say `render Turbo`. Exact and Fast are byte identical to main. Nothing ran on hardware, every board saving below is an estimate.

### Layout and scanout

| Frame buffer bytes (P = 152 view rows, W = 320) | In a Turbo frame |
| --- | --- |
| 0 to P x W/2 (24,320 B) | the view's pixels, row r at r x 160 (packed) |
| P x W/2 to P x W (24,320 B) | the one-byte z-buffer while the view is drawn, before that the edge cache and vertex cache |
| from P x W | the status bar rows and the z-buffer memory, as always |

- `qog_video_present_packed(fb, rows, packed)` (platform/qog_video.c), core 1 reads a packed row and its `convert_line_double` writes each palette entry twice into the line buffer (two pixels a word store, in RAM like convert_line). Core 0 spends nothing on the doubling. The line buffers are now 4-byte aligned for the word stores.
- 2D drawing into a packed row (Draw_Character, Draw_Pic, Draw_TransPic, Draw_TransPicTranslate, Draw_ConsoleBackground, Draw_TileClear, Draw_Fill, Draw_FadeScreen and the board menu's two direct drawers) first spreads the packed rows to full width in place (`R_QogTurboUnpack`, from the last row up, one word read and two written per four pixels, estimated 0.3 ms on the board), and the frame then goes out as a normal one. Frames with notify text or a menu pay that, plain play frames do not. The crosshair is drawn straight into the packed view (`R_QogTurboChar`), so it keeps the frame packed.
- `r_qog_turbopacked` is the number of packed rows the frame holds. VID_Update hands it to the scanout with the frame, so the scanout in flight keeps its own copy while the next view starts (the earlier race).
- The status bar skip (QOG_HUD_SKIP) is unchanged. Packed rows are view rows, the status bar rows are never packed.

### How the view is made half as wide

`R_QogTurboBegin` and `R_QogTurboEnd` (engine/r_qogturbo.c) wrap R_RenderView_ only (R_HudRepaint after it draws 2D at full width). For a Turbo view vid.width and vid.rowbytes become 160 and vid.aspect doubles, as id's own lcd_x mode does with rows, scr_vrect.width halves (for the in-place warp), and the one-byte z-buffer moves after the packed pixels. R_SetupFrame then calls R_ViewChanged and D_ViewChanged for a 160x152 view, xscale halves and pixelAspect doubles, so yscale and the field of view stay id's and the picture is not squashed. A change between Turbo and the other modes sets r_viewchanged, so every frame switch sets the view up again. Turbo needs the view full width from the top, which the board always has (QOG_EDGECACHE_FRAME keeps viewsize 100), else the frame is drawn as Fast.

| Part | What Turbo does |
| --- | --- |
| spans, z, alias models, particles, sprites, view weapon | see an ordinary 160 wide view (screenwidth and d_zwidth 160) |
| particles | d_y_aspect_shift becomes 1 (pixelAspect above 1.4, id's rule) and d_pix_shift one more, so a particle is half as many pixels wide and twice as many rows and keeps its size on the screen. The smallest particles are one Turbo pixel, 2x2 on the panel where Fast draws 1x1 |
| sky (d_sky.c) | placed by the screen position 2u and the screen width, else it was stretched twice vertically |
| underwater warp (in place) | D_SetupFrame and D_ViewChanged keep the 160-byte rows when warping, the R_RenderView_ check allows them |
| turbulent water, lava, slime | texture space, unchanged |
| mip levels | scale_for_mip is max(xscale, yscale), 133 not 160, so distant walls take the coarser mip a little sooner (fewer texels, faster) |

### The caches

The edge cache and the vertex cache (QOG_EDGECACHE_FRAME, R_QogVertCacheBegin) live in frame buffer bytes that the view draws over. In a Turbo view the view owns the first 48,640 bytes, half pixels and half z-buffer. `R_QogTurboCaches` places them each view, taking whichever place the scanout in flight lets go of soonest (`VID_QogScanNeed`, the race rule extended to packed rows).
- a. both in the second half (the z-buffer's bytes, which only the spans write later), on into the z-buffer memory up to the edge table for a big map. A Turbo frame's scanout never reads the second half, so after a Turbo frame the view starts at once with no race wait.
- b. the edge cache there and the smallest vertex cache (64 entries and the Fast header) at the frame's start, which a Turbo scanout sends in its first 15 rows (about 1.4 ms). For maps whose edge cache is too big for a.
- c. both at the frame's start as in Fast, when the frame in flight was a normal one (after a menu or notify text, or the first frame after a switch from Exact or Fast).

On the PC pair, E1M1, e1m3, e2m1 and e1m6 views after a Turbo frame waited 0 rows with a and 15 rows with b. The edge table in the z-buffer memory never meets a 160 wide z-buffer in the frame, so R_ScanEdges' waiting spans and edge moves do not happen in Turbo. R_EdgeDrawing's check that the view covers a frame buffer cache is kept for the other modes.

### Pictures (PC, as the panel shows them)

The PC builds the panel's picture in VID_Update (packed rows doubled) and shots and frame hashes come from it. Pairs, Turbo left and Fast right, 2x, are kept locally in a `turbo` folder, the E1M1 start, demo1 with particles, demo2 under water, demo3, spawn views, sky views, a notify line and the main menu over a Turbo view, and the board-flag pair's E1M1 and E1M6 (Turbo, Fast, Exact). Side by side the Turbo picture has the same framing and field of view, with walls and models twice as coarse across.

### Exact and Fast proof (hidden, `-nosound`, own `build-ao` folders, own git-ignored data folder with its own qogbake.bin, link port 27981)

| Test | Result |
| --- | --- |
| `qogtest_hash`, quakeog_pc | ce86f894f9de |
| `qogtest_f_hash`, quakeog_pc | e92bc9e9650d |
| 121 shot frames (`qogtest_k_quiet` + `qogtest_d_shots` 113 and the 8 fhash frames), r_fast 0, all 200 rows | 121 of 121 byte identical with b585365 |
| check build (`QOG_DLIGHT_CHECK`, hash cut to 64 entries so it overflows in most lit frames), timedemo demo1, demo2, demo3 | 45,056 light pairs and 52,224 lost questions, 0 differ |

### Fast pictures

The light changes are exact, so frames with rockets and explosions are the same in both modes. Only `QOG_GRAD_FAST` changes Fast pictures, against b585365 with r_fast 1, 0.45% of the view pixels of the 121 frames, worst d_demo2_18 1.38%, demo3 frames up to 1.04% (d_demo3_15). The changed pixels are single texels scattered over world surfaces, sub-texel shifts of the texture coordinate, and side by side the frames look the same. Pairs (Fast reference, Fast new, changed pixels red) and the per-frame list are kept locally in a `fast3/light` folder.

### RAM and stack (board, full configure line)

- quake_display `end` 0x2003ea6c (main 0x2003ea28), 68 B for the Fast gradient rows. quake_main `end` 0x2003f544, unchanged. quake_display.bin 394,032 B.
- Stack, R_QogDlightInt 104 B under R_AddDynamicLights 128 B, R_QogLightReachesWorld 48 B under R_DlightBits 48 B, R_QogNodeFacing 24 B beside R_MarkLights 48 B a recursion level, D_QogGradFast 200 B (with D_QogGradRows 112 B once a view) under D_CalcGradients 104 B. All of these return before the span drawers run, whose frames are larger, so the deepest chain does not grow.

### For earlier work, what the light grid builder touches

- Writes `blocklights` (1,296 B global), `r_drawsurf` (surf, model, texture, mip, lightadj, set by D_SetupLitSurface) and the `lit_*` statics of d_scanlit.c. D_CalcGradients writes the `d_sdivz*`, `d_tdivz*`, `sadjust`, `tadjust`, `bbextent*` globals and, once a view, the Fast rows `d_qoggr`.
- The first lit surface of a view runs the world's marking pass (R_DlightRunPending, the lit surface hash, the surfaces' dlightframe bits, `r_dlightframecount` for a moment). Call `R_QogDlightSettle ()` after R_RenderWorld, then the builders only read the hash and the dlight list.
- Reads only, the surface, its samples, styles and texinfo, `d_lightstylevalue`, `r_refdef.ambientlight`, `cl_dlights`, the nodes and planes.
- To build on core 1 the builder needs its own grid (1,296 B, of the 3.5 KB free) and its own `r_drawsurf` and `lit_*` values passed in. R_QogDlightInt and the base loops already work only on the grid and their arguments, so passing a grid pointer is a small change. Not done here because the RAM is yours to place.

Files, engine/r_surf.c, engine/r_light.c, engine/d_edge.c (D_CalcGradients only, inside `#ifdef` blocks), engine/model.h (one declaration), pc/CMakeLists.txt, apps/quake/CMakeLists.txt.

| `qogtest_f_hash`, quakeog_pc r_fast 0, 1 and 2, the board-flag pair r_fast 1 and 2 | e92bc9e9650d each (Turbo does not touch game state) |
| 121 shot frames (`qogtest_k_quiet` + `qogtest_d_shots` 113 and the 8 fhash frames), r_fast 0, against an untouched build of 8511c77 | 121 of 121 byte identical |
| the same, r_fast 1 | 121 of 121 byte identical |
| Turbo, quakeog_pc and the board-flag pair (the earlier pcb flags plus `QOG_SPANS_CORE1 QOG_ALIAS_CORE1 QOG_WORLDNODE_FAST QOG_FRONT_FAST QOG_WORLDNODE_INT QOG_LITSPAN_INTERP`), the 121 frames | all drawn, no error, `qog_scanrace` 0 writes before the scanout passed in 4,410 checks, `qog_lockcheck` 0 of 3,551 scanouts changed |
| `qogtest_ao_switch.cfg` on both (Exact, Turbo, Fast, Turbo with notify text and the menu, four maps) | no error, `qog_scanrace` 0 of 521 |
| the earlier Benchmark with r_fast 2 on the pair | `[bench] timedemo demo3 1090 frames 17.8 seconds 61.4 fps, render Turbo, PC, view 100, 320x200` (PC pacing, not a speed), 0 race waits in 1,449 frames, the result page says render Turbo |

The board numbers will come from the board. Before the crosshair went into the packed view, the PC test config's crosshair spread every Turbo frame and the same benchmark had 308 race waits.

### Speed estimate (board, timedemo demo3, ms per view, from the lead's board numbers)

| Phase | Now (Fast) | What scales with width | Turbo estimate |
| --- | ---: | --- | ---: |
| spans | 16.9 | the pixels (about half the phase) halve, span starts and surface setup do not, sub-spans cover more of the view | 10 to 12 |
| edgescan | 7.2 | works per scan line and per edge, not per pixel. The z-buffer edge waits and moves go | 6 to 7 |
| world | 10.4 | nothing (BSP walk, edge emission) | 10.4 |
| lmap | 13.9 | nothing (lightmaps per surface) | 13.9 |
| bmodels and grad | 10.0 | nothing | 10.0 |
| models | 5.1 | raster pixels halve, vertices and gradients do not | 4.3 to 4.7 |
| vmodel | 3.4 | its 2,772 pixels halve, clipping does not | 2.6 to 3.0 |
| parts | 1.2 | particle pixels | 1.0 |
| outside the view | about 8 | the race wait goes after a Turbo frame (E1M1 play showed race 6.0 ms), unpacking adds 0.3 ms in frames with text | 5 to 7 |
| frame | 80.9 | | about 66 to 72 (14 to 15 fps) |

So Turbo alone is worth about 9 to 15 ms of today's demo3 frame. The part that does not scale with width (lightmaps, world, brush models, gradients, about 44 ms) is what earlier work work on, and Turbo's share of the frame grows as they shrink it.

### RAM and code (board, the full configure line)

| | `end` | main 8511c77 |
| --- | --- | --- |
| quake_display | 0x2003ea4c | 0x2003ea28 (36 B more, the Turbo state and the scanout's packed rows) |
| quake_main | 0x2003f544 | 0x2003f544 |

quake_display.bin 392,664 B, quake_main.bin 1,335,004 B. The display keeps about 3.4 KB beside the 2 KB heap reserve. convert_line_double runs from RAM with the scanout.

### Files and lines for the lead

New, engine/r_qogturbo.c. Mine, engine/menu.c (Render line, Benchmark mode, two hooks), port/rp2040/vid_rp2040.c, port/pc/vid_pc.c, platform/qog_video.c and .h, engine/vid.h, engine/draw.c (one hook line per drawer), engine/view.c (crosshair), engine/d_sky.c, engine/d_init.c and engine/d_modech.c (warp rows, particle shift), apps/quake/CMakeLists.txt (`QOG_BOARD_TURBO`, on, needs FAST, adds `QOG_RTURBO`), pc/CMakeLists.txt (`QOG_RTURBO` for quakeog_pc and the pair). Shared, each in an `#ifdef QOG_RTURBO` block, for the merge with earlier work.
- engine/r_main.c R_EdgeDrawing. `R_QogTurboCaches ()` before the frame buffer cache check, and the check skipped in a Turbo view (8 lines before `if ((byte *)r_qog_edgecache == vid.buffer`)
- engine/r_main.c R_RenderView_: the in-place warp check allows a Turbo view's rows (3 lines)
- engine/r_main.c R_RenderView, R_QogTurboBegin and R_QogTurboEnd around `R_RenderView_ ()` (10 lines)
- engine/r_draw.c R_QogVertCacheBegin, the vertex cache's room from r_qog_turbocachelo and r_qog_turbocachehi when a Turbo view set them (11 lines before the QOG_EDGECACHE_FRAME branch)

Test scripts (git-ignored data folder). `qogtest_ao_turbo.cfg`, `qogtest_ao_exact.cfg`, `qogtest_ao_fast.cfg`, `qogtest_ao_switch.cfg` (E1M1 Exact, Turbo, Fast, Turbo, a notify line, the menu, then e1m3, e2m1 and e1m6 switching between the modes), `qogtest_ao_sky.cfg`, `qogtest_ao_bench.cfg` (r_fast 2 and the earlier menu Benchmark).

Not done, for later.
- Notify text over a Turbo view spreads the whole view in every frame it shows (0.3 ms), and the next view then waits for the scanout as in Fast. Drawing only the text rows at full width, with the scanout told which rows are full, would keep the rest packed.
- A map whose edge cache is bigger than about 29 KB takes way b with a 64-entry vertex cache, a little slower in its front end than way a.

## 2026-11-24, Board results of the speed round (lead, on hardware)

All four speed changes plus episodes 2 and 3 support, flashed with the episode 1 image.

- E1M1 play, 18.0 fps (frame 55.2 ms, link 1.1, view 53.2, race 6.0, hud 0, scanwait 0), was 12.0. main's txstall 0 ms per second.
- `[bench] timedemo demo3 1090 frames 79.9 seconds 13.6 fps, render Fast, clock 250 MHz, flash 125 MHz`, was 9.3.
- Phase averages during the demo, ms per view, spans 16.9, lmap 13.9, world 10.4, edgescan 7.2, bmodels 6.9, models 5.1, vmodel 3.4, grad 3.1, parts 1.2, total 72.2. Frame 80.9 average including the demo's loading.
- lmap is now the second biggest item in the demo (dynamic lights from rockets and explosions rebuild lightmaps every frame), against 2 ms in E1M1.
- No fault, hang or error line on either chip. Display stack free 6,160 B, main 7,928 B.

## 2026-11-24, Alias models and particles in integers in Fast mode, an exact faster alias span loop (PC and ARM build, registered data)

The board's timedemo demo3 spends 14.3 ms of a 75.7 ms view on models, 5.2 on the view weapon and 3.4 on particles. Counted on the PC (`QOG_ALIAS_STATS`, timedemo demo3, per view), 9.05 alias bounding box checks, 2.82 models drawn, 299 vertices, 502 triangles of which 123 rasterised with gradients (84 of them 4 pixels or fewer) and 55 by subdivision, 748 span pixels, and the view weapon with 71 vertices, 40 rasterised triangles, 27 clipped ones and 2,772 pixels, plus 205 particles (74 on the screen). Pixels are few, the time is float work, an M0+ has no FPU and every float is a call to the ROM (objdump, 56 helper calls per triangle gradient, about 44 per vertex, 3 double sines and 3 double cosines in every AngleVectors). So Fast mode replaces the float work with integers, the way MG24Quake's rasteriser is fixed point but further (MG24Quake keeps float vertices, it has an FPU). MG24Quake's other alias ideas (per-triangle skin layout and DMA reads from its SPI flash) suit its external flash, not our XIP cache, and need the pak converter and RAM, so they are not taken. Nothing ran on hardware. Savings are estimates.

| Change | Flag | Mode | What it does |
| --- | --- | --- | --- |
| Integer vertices | `QOG_ALIAS_FAST` (board option `QOG_BOARD_ALIASFAST`, on with `QOG_BOARD_FAST`) | Fast | each vertex is nine integer multiplies with the model to view transform in 16.16, 1/z one unsigned divide (the hardware divider), the screen position two divides in 28.4 (within 1/16 pixel of id's), the light three multiplies with id's normals in 2.14 (engine/anorms14.h). Both cores share them as the earlier exact vertices do (`QOG_ALIAS_CORE1`). A point too far off screen for 32 bits takes a float divide |
| Integer transform | same | Fast | R_QogAliasXform builds the transform from a 1,024 step sine table (engine/qogsin.h, linear between steps) and integer products, the floats read into fixed point by their bits (R_QogFix). No AngleVectors, no R_ConcatTransforms. The light vector is rotated in 2.14. Only a clipped model also gets id's float aliastransform (for the clipper's z clip) |
| Integer bounding box test | same | Fast | a bounding sphere test (boundingradius with margin) rejects models wholly behind or beside the view before any transform, the box corners go through the integer transform, id's near plane edge points are kept. Reject only when more than a pixel off, accept unclipped only when a pixel inside, which covers the projection's rounding. Models drawn per view 2.85 (id 2.82) |
| Integer triangle gradients | same | Fast | D_QogCalcGradients, the same numerators, quotient and remainder by the same denominator, values shifted so products fit 32 bits (s and t keep whole texels, 1/z 21 bits), light keeps id's ceil |
| Integer clip points | same (engine/r_aclip.c) | Fast | the left, right, top and bottom clip points of a clipped triangle (the view weapon at the bottom edge) are integer quotients rounded as id's + 0.5. Distances of 2^14 pixels or more keep id's float |
| Integer particles | same (d_part.c, r_part.c) | Fast | the position read into 24.8 by its bits, shifted with the eye's until below 2^15, products with the view vectors in 2.14, two divides for the screen position and one for 1/z. R_DrawParticles makes the view's constants once (64 B of its stack) |
| Particle depth first | none, always | exact | D_DrawParticle computes the depth product first and returns before the other two, the compare as `(float)8.0` (the same) |
| Alias span loop | `QOG_ALIASSPAN_FAST` (both lists) | exact, both modes | id's D_PolysetDrawSpans8 reloaded every step from memory at each pixel (a byte store may alias any global, L110). Steps in locals, the texel as skin[(T >> 16) * width + (S >> 16)] which is id's pointer and two carries added up, and with the one-byte z-buffer a span whose ends lie in the same linear piece of D_ZQ takes that piece's shift (1/z is linear along the span). Player colours as before |
| Static matrices | none | exact | id's static tmatrix and viewmatrix (zeros kept for their off-diagonals) are locals with the same zeros, 96 B static RAM less |

Estimated board saving per view of demo3 at 250 MHz (cycle counts from the objdump of the ARM build, ROM float calls taken at 55 to 75 cycles, the PC's work counts above, an estimate the lead measures).

| Item | Before (cycles each) | After | Per view | Estimate |
| --- | --- | --- | --- | --- |
| vertices (370 a view) | about 2,400 | about 200 (3 divides) | 0.8 M CPU, two cores | about 1.8 ms |
| triangle gradients (about 200 incl. clipped fans) | about 3,250 | about 450 | 0.56 M | about 2.2 ms |
| transform builds (9 checks, 3.8 draws) | about 18,000 (double trig, two 3x4 products) | sphere test about 2,600, transform about 1,500 | 0.2 M | about 0.8 ms |
| box corners (9 checks) | about 15,000 | about 1,500 | 0.12 M | about 0.5 ms |
| particles (205, 74 on screen) | about 1,150 to 2,000 | about 150 to 250 | 0.27 M | about 1.1 ms |
| weapon clip points (about 40 a view) | about 2,500 | about 300 | 0.09 M | about 0.35 ms |
| span pixels (3,520) | about 48 instructions a written pixel | about 30 | 0.06 M | about 0.25 ms, both modes |
| total | | | | about 7 ms of the 22.9 ms of models, vmodel and parts, more if the board's float calls cost more than their ROM cycle counts (the measured phases are about twice what these counts predict) |

Fast pictures (quakeog_pc, the 121 shot frames, view rows, against main with r_fast 1, so only this change), 0.315% of the pixels differ, 114 frames, worst d_demo1_12 1.26%. The fight shots (fhash) 0.29 to 0.59%. The changed pixels are on models and the view weapon, light steps of one colour map row and texels moved by one at triangle edges. Side by side the models look the same. All of Fast mode against Exact is now 3.19% (main's Fast spans alone 2.88%). On the board-flag split pair (one-byte z-buffer, both cores as threads, `QOG_ALIAS_CORE1`) the 94 frames it reaches differ by 0.47% from the same pair without the change. Pairs (Fast reference, Fast new, changed pixels red, 2x) are kept locally in a `fast2\models` folder.

Exact proof (hidden, `-nosound`, own data folder, link ports 27921 and 27922), quakeog_pc `qogtest_hash` ce86f894f9de, `qogtest_f_hash` e92bc9e9650d, the 121 shot frames byte identical (all 200 rows) with an untouched build of main, before and after each merge of main (the last full run on db9d0c9, the hashes again on a44b819). The split pair with the board client flags (`QOG_ZBUFFER8_NEAR`, `QOG_SPANS_CORE1`, `QOG_ALIAS_CORE1`, the earlier flags) against the same pair of dd17213, 94 frames (it stops loading e2m3 in both builds, L113), 93 byte identical and one differing only in rows 1 to 23 (the statehash notify text, which appears a frame apart from run to run). That pair is what exercises the one-byte z per span piece.

RAM and stack (board, merged with main a44b819), quake_display `end` 0x2003ea28, main's 0x2003ea84, so 92 B less (96 B of matrices freed, one 4 B pointer to the model's transform). quake_main `end` 0x2003f544, unchanged code. quake_display.bin 390,048 B. Stack, R_AliasDrawModel 9,256 to 9,392 B (the 112 B transform), D_PolysetDrawSpans8 96 to 104, so the deepest alias chain (R_AliasDrawModel, R_AliasPreparePoints, R_AliasClipTriangle, D_PolysetDraw, D_RasterizeAliasPolySmooth, D_PolysetDrawSpans8) grows by 144 B. Side branches stay below it, R_QogAliasCheckBBoxFast 520 B beside R_AliasCheckBBox 824 B (832 before), R_QogAliasXform 184 B, R_DrawParticles 80 to 152 B. Core 1's alias job about 100 B. The SIO interpolators are not used, so nothing collides with the earlier `QOG_LITSPAN_INTERP`.

`QOG_ALIAS_STATS` (PC measurement only, `-DQOG_PC_EXTRA_DEFS=QOG_ALIAS_STATS`) prints `qog_aliasstats ents ...` and `qog_aliasstats vm ...` every 100 views, the sums since the level started. Test scripts `qogtest_ai_fast.cfg`, `qogtest_ai_exact.cfg` and `qogtest_ai_td3.cfg` (timedemo demo3 with `qog_quitafter`) are in the branch's git-ignored data folder.

Files, engine/r_alias.c, engine/d_polyse.c, engine/d_part.c, engine/r_part.c, engine/r_aclip.c (not in my list, alias clipping, nobody else's), engine/r_qogalias.h, engine/anorms14.h and engine/qogsin.h (new), pc/CMakeLists.txt and apps/quake/CMakeLists.txt (flags, shared). r_main.c, r_sprite.c and d_sprite.c are not touched.

Not done, for the lead or the user.
- R_LightPoint (r_light.c) runs a float BSP trace for every drawn model, about 3.8 a view, and R_DrawEntitiesOnList measures every dynamic light's distance with a sqrt. Not my files.
- The particle physics in R_DrawParticles stays float (about 12 float ops a particle a view), its state is the particles' float positions.
- Tiny triangles (64 of 123 a view are 4 pixels or fewer) still get gradients, now cheap. Drawing them flat would show on near models.
- MG24Quake's per-triangle skin layout needs the pak converter and a RAM buffer per triangle, our skins are read through the XIP cache.

## 2026-11-24, World and brush model front end in integers for Fast mode, brush entities without trig, integer node culling (PC and ARM build, registered data)

The display's world front end (world 13.7 ms, bmodels 4.3 ms a view in the board's timedemo demo3) does most of its work in software float, about 55 cycles an operation on the RP2040, while a multiply takes one cycle and the SIO divider eight. Three changes, two exact in both render modes and one for Fast mode only. Nothing ran on hardware, every saving below is an estimate from the Thumb code (objdump of `build-aj/arm2`) and the PC's work counts per view of `timedemo demo3` (nodes 265, faces 193, edges emitted 450, points projected 468, plane distances 1,242, brush entity rotations 18).

| Change | Flag (board option) | Kind | Estimated board saving per view, 250 MHz, demo3 |
| --- | --- | --- | --- |
| R_RotateBmodel without the double sine and cosine for a zero angle (sin of +-0 is +-0, cos is 1), and with all three angles +0 without the matrix products, id's code builds the identity with +0 everywhere, so the rotated vectors are the inputs with id's sign of zero rule (a -0 component becomes +0 unless all three components are negative), not finite or denormal vectors keep id's dot products. It runs twice per brush entity drawn and twice more per brush surface in D_DrawSurfaces | `QOG_BMODEL_ZEROANGLE` (engine defs, PC and board) | exact | 18 calls of about 6 double trig calls (estimated 1,500 to 2,000 cycles each), 6 double multiplies and divides and about 100 float operations, about 0.3 M cycles, 1.2 ms, mostly in edgescan (D_DrawSurfaces) and bmodels |
| BSP node frustum tests in integers first, the box corners (shorts) against the view planes in 1/16384. The integer distance is within 0.5 (abs c0 + abs c1 + abs c2) + 0.5 of the real one scaled, id's float dot product within 0.003 of that sum, so beyond a margin of the sum plus 512 the sign is id's. Boxes within that margin of a plane (a fraction of a unit) take id's float test | `QOG_WORLDNODE_INT` (`QOG_BOARD_WORLDNODEINT`, needs FRONTFAST and WORLDNODE) | exact | about 25 cycles a plane instead of about 600, about 0.27 M cycles, 1.1 ms of world |
| Fast mode front end in fixed point, points projected (a position less the eye in 1/1024 units within 128 units, 1/32 out to 4096, times rows of xscale vright, yscale vup and vpn, z to 14 bits, u and v from two integer divides in 1/256 pixel), edges set up (u and u_step in 12.20 from integer divides), the view plane sides in R_ClipEdge from integer distances (the planes pass through the eye), and a face's 1/z gradients from its normal in 1/16384 against three rows made per view or entity. The nearest 1/z is kept as the smallest z and turned into id's float once per surface. Points at the eye, far off the view or out of range take id's float projection | `QOG_FRONT_FAST` (`QOG_BOARD_FRONTFAST`, needs FAST, VERTCACHE, CLIPDIST) | approximate, r_fast 1 only | projection about 280 cycles a point instead of 1,750 (0.68 M), edge setup about 120 instead of 700 (0.26 M), plane distances about 80 instead of 370 (0.36 M), face gradients 16 float operations instead of 30 (0.15 M), about 1.45 M cycles, 5.8 ms of world and bmodels |
| The vertex cache keeps 128 entries in Fast mode (512 on E1M1 before). A miss now costs a few hundred cycles, and the cache ends 38 frame buffer rows higher, which the earlier scanout race reaches sooner | `QOG_VERTCACHE_FASTMAX` (128, r_draw.c) | Fast only, same pixels | demo3 projects 20 more points and 69 more distances a view (about 10 k cycles). By the earlier figures (136 rows about 12.4 ms) the race wait ends about 3 ms sooner where the frame waits on it |

Together, estimates, Exact about 2.3 ms a view, Fast about 8 ms plus the shorter race wait, of the 18 ms of world and bmodels.

Fast picture (113 `qogtest_d_shots` frames and the 8 `qogtest_f_hash` shots, view rows, against a build of dd17213 with r_fast 1, so only this change is counted).

| Build | Pixels changed | Worst frame |
| --- | ---: | --- |
| integer front end with the exact spans (measurement build `QOG_FRONT_FAST_ALWAYS`) | 0.07% | d_spawn_dm6 0.33% |
| Fast mode, integer front end, against Fast mode of dd17213 | 0.21% | d_demo2_12 0.65% |
| Fast mode against Exact (the earlier spans and this) | 2.88% | d_demo3_15 9.4% (the earlier alone 2.75%, 9.2%) |

The changed pixels are edge and span ends one pixel over where id's value lies within about 0.02 pixel (the mean point error) of a pixel boundary, and single texels where the face gradients differ in their last bits. Side by side the frames look the same. Pairs and marked differences (`<shot>_a.png` dd17213 Fast, `_b.png` this, `_diff.png` changed pixels red, 2x) are kept locally in a `fast2/world` folder. A first version stepped the first scan line's u with 1/256 of its slope and changed 1.2% of the pixels, the PC check build (`QOG_FRONT_FAST_CHECK`, every point projected both ways, error statistics on the console) found it.

Exact proof (hidden, `-nosound`, `+_cl_color 0`, own folders `build-aj`, own git-ignored `data\id1`, one run at a time), the PC build with the board's world flags (`QOG_VERTCACHE QOG_VERTCACHE_CLIPDIST QOG_WORLDNODE_FAST QOG_FRONT_FAST QOG_WORLDNODE_INT`, plus `QOG_BMODEL_ZEROANGLE` from pc/CMakeLists.txt) against an untouched build of dd17213, r_fast 0. `qogtest_hash` ce86f894f9de, `qogtest_f_hash` e92bc9e9650d, the 121 shot frames byte identical. The Fast runs keep both hashes (rendering does not touch game state). After merging main with earlier work (f353858) the same holds, and Fast mode with the earlier fixed point spans changes 2.99% of the pixels against Exact (worst d_demo3_15 9.4%).

RAM and stack, no static RAM, quake_display `end` equals main's after each merge (0x2003ea84 with the earlier merge). The Fast constants (284 B) sit after the vertex cache entries in the frame buffer rows, inside the earlier scan wait. Stack, the Fast face chain is 56 B deeper at its end (R_QogClipEdgeFast 48 B a clip level, R_QogEmitEdgeFast 72, R_QogProjectFast 48, the float fallback 56), the node test adds R_QogCullNodeInt's 64 B once at the deepest node, R_QogFxSetup 128 B from R_RenderWorld and R_DrawBEntitiesOnList only.

Files, engine/r_draw.c, engine/r_bsp.c, engine/r_main.c (setup call per brush entity, the cache now ends after the brush entities in Fast builds), engine/r_local.h, engine/qog_phase.h and port/common/qog_phase.c (work counts `rotate` and `rottrig`), pc/CMakeLists.txt, apps/quake/CMakeLists.txt. r_draw.c's R_QogVertCacheBegin holds the earlier scan wait, extended over the Fast header. d_edge.c and tools are not touched. The earlier vertex cache entries hold the Fast ints in their float slots in Fast mode.

Not done, for later.
- D_CalcGradients (grad 2.4 ms, shared with earlier work) can take the same three rows as the face gradients in Fast mode, about 26 float operations a surface instead of about 70. Its two `+ 0.5` in double can be made exact in float (below 2^23 the float add is exact, above it the value is an integer and only the sign decides).
- D_DrawSurfaces rotates and restores a brush entity's frame for each of its surfaces, two R_TransformFrustum calls each, which consecutive surfaces of one entity could share (the earlier file).
- The world's vertex positions converted once per frame into the cache entries would save the three conversions of each repeated distance.
## 2026-11-24, Episodes 2 and 3 on the board, the boot level from the data image, missing levels, the load buffer off the server hunk (PC and ARM build, registered data)

One board app now runs any episode's data image, mkboard writes the level to start in into the image and the board client starts there. Episodes 2 and 3 with start fit both chips, a level exit or a save of a level the image does not hold no longer stops the game, and every level of episodes 1 to 3 runs in the server's 174 KB hunk with sound after one exact cut (the load's line buffer moved from the hunk to the stack). Nothing ran on hardware. Merged with main d8f2f9e before the last builds and hashes.

Boot level (`QOG_BOOT_ENTRY`, engine/common.c COM_BootMap, engine/host_cmd.c `qog_boot`, port/rp2040/sys_rp2040.c).
- `mkboard.py --boot-map NAME` writes `qogboot.txt` (the name and a newline) into the display's pak and main's, just before the maps. The default is start when `--maps` has it, else the first of `--maps`.
- With `QOG_BOARD_BOOT` (on by default) the board client's command line is `+qog_boot` instead of `+map e1m1`, at the same place in quake.rc's `stuffcmds`. `qog_boot` reads qogboot.txt and runs `map <name>`. Without the file, or when it names a level the packs do not hold, e1m1, then the first level in the packs. An image written before (episode 1 today) has no file and still starts in e1m1. `QOG_BOARD_MAP` is used only with `QOG_BOARD_BOOT` off.
- `qog_boot` gives the same state hash as `map start` on the episode 2 image (7eb8d446d9d4), as `map e2m1` on an episode 2 image without start (4e4a5499b252), and as `map e1m1` on the full paks, split pair and single process (d7cea7e1b394).

Missing levels (`QOG_MISSING_MAPS`, engine/host_cmd.c).
- Every episode exits to start, e1m7, e2m6 and e3m6 (`tools/level_exits.py`, new). e2m7 and e3m7 are the secret levels and lead back to e2m4 and e3m5. start's gates lead to e1m1, e2m1, e3m1, e4m1 and end.
- A changelevel to a level the image does not hold used to stop the game, SV_SpawnServer sends the reconnect, then finds no level, and the client waits under the loading plaque. Host_Changelevel_f now checks first. If the level is missing it goes to the boot level with a new player's parms (SetNewParms, what id's start does to a player who finished an episode), keeping serverflags. A level that exists changes nothing (`qogtest_ad_chg.cfg` 7195dbe28396 on the full paks and on the episode 1 image).
- Tested, on an episode 2 image without start, e2m6 then `changelevel start` goes to e2m1, and its globals hashes equal a fresh `map e2m1` after the same frames (4631f97e, c609267c). On the episode 2 image, start then `changelevel e1m1` goes back to start.
- A save of a level the image does not hold (main's save volume outlives the episode image) is not loaded, Host_Loadgame_f checks the level before anything is stopped, the skill stays, the client takes the loading plaque down (`qog_loadfailed`, a new client command) and shows "That save's level is not in this data image". The state hash equals the same run without the load (35177481434b).

Server hunk, the cut (`QOG_LOADGAME_STACKBUF`, board server list only, exact).
Host_Loadgame_f took its 8 KB line buffer (`QOG_LOADGAME_STRSIZE`) from the high hunk after the level had loaded, so every load needed the level plus 8 KB, and that was the server's peak on every level. The buffer now lives on the stack in a frame of its own (Host_LoadgameStack, 8,224 B), taken after SV_SpawnServer and its physics frames have returned. The parsing is id's code moved into Host_LoadgameEdicts, every other build keeps its buffer. Smallest `-heapsize` of the server half for `qogtest_y_mem_<map>.cfg` (fight, save, load) from the images with start, sound on for both halves (the split pair never opens an audio device), KB.

| Map | Before | After | | Map | Before | After | | Map | Before (AC) | After |
| --- | ---: | ---: | --- | --- | ---: | ---: | --- | --- | ---: | ---: |
| e2m1 | 146 | 138 | | e3m1 | 146 | 138 | | e1m1 | 117 | 111 |
| e2m2 | 125 | 118 | | e3m2 | 130 | 122 | | e1m2 | 144 | 137 |
| e2m3 | 137 | 129 | | e3m3 | 135 | 127 | | e1m3 | 171 | 163 |
| e2m4 | 156 | 148 | | e3m4 | 162 | 154 | | e1m4 | 161 | 153 |
| e2m5 | 145 | 137 | | e3m5 | 155 | 147 | | e1m5 | 160 | 152 |
| e2m6 | 167 | 159 | | e3m6 | 167 | 159 | | e1m6 | 146 | 138 |
| e2m7 | 179 | 172 | | e3m7 | 127 | 119 | | e1m7 | 93 | 90 |
| start | 96 | 92 | | | | | | e1m8 | 121 | 113 |

e2m7 (the secret level, 438 edicts of 208 bytes, 91 KB) is the worst at 172 KB, 2 KB below 174. At 174 KB for the server and 75 KB for the client, with sound, every script of episodes 1, 2 and 3 runs, and the whole fight tours of start to e2m7 and of start to e3m7 run to the end.

Server stack (QOG_SV_STACKPROBE, frames from this build's .su files, board bytes).

| Run | Deepest | Chain |
| --- | ---: | --- |
| scripts before the cut, episodes 2 and 3 | 6,920 to 10,216 (e2m2) | the pusher lists and the hull recursion |
| scripts after the cut, episodes 1 to 3 | 9,504 to 10,600 (e3m5) | Host_LoadgameStack 8,224 B, then SV_LinkEdict and 52 levels of SV_FindTouchedLeafs |
| fight tour start to e2m7 | 12,752 | SV_PushMove with e2m7's lists (7,008 B), 48 levels of SV_RecursiveHullCheck |
| fight tour start to e3m7 | 13,280 | SV_SetIdealPitch's trace, 111 levels of SV_RecursiveHullCheck (e3m5) |
| deepest that runs the mixer | 1,816 | unchanged |

21 KB is 21,504 B, 8.2 KB above the measured worst. Hull depths (`tools/hull_depth.py`), e3m5's hull 0 is 110 levels deep (e1m3 85, e2m2 87). the constructed worst, a point entity pushed by a plat on the deepest leaf, is 752 + 16 x max_edicts + (depth + 1) x 104 B, e3m5 (361 edicts) 18,072 B, 3.4 KB below 21 KB (e1m3 16,352, e2m7 14,520). The load chain's own bound, with FindTouchedLeafs as deep as the tree, is about 12.5 KB on e3m5. Neither construct ran in a test.

Images (`mkboard.py --pak data\id1\pak0.pak --pak data\id1\pak1.pak --manifests <manifests of every level> --maps start e<N>m1 ... e<N>m7 --stub-maps --main --display-no-sounds --display-no-progs`, start included, boot level start).

| Image | Display | Region | Main | Region |
| --- | ---: | --- | ---: | --- |
| episode 2 with start | 14,654,720 B (13.98 MB) | 93.6% of 14.94 MB | 6,262,784 B (5.97 MB) | 99.5% of 6 MB, 28 KB spare |
| episode 2 without start | 13,411,632 B (12.79 MB) | 85.6% | 5,914,624 B (5.64 MB) | 94.0% |
| episode 3 with start | 13,812,656 B (13.17 MB) | 88.2% | 6,246,400 B (5.96 MB) | 99.3%, 44 KB spare |
| episode 3 without start | 12,534,208 B (11.95 MB) | 80.0% | 5,902,336 B (5.63 MB) | 93.8% |

start costs 1.19 MB of display data and 0.33 MB of main data and fits both, so both images keep it, the player picks the skill in start's hall and walks into the episode's gate. start's other gates lead to levels the image does not hold and bring the player back to start. The 6-byte qogboot.txt moved nothing across a 64 KB boundary (the earlier sizes are the same to the byte).

Demos and the Benchmark (the earlier menu line runs `timedemo demo3`), demo1, demo2 and demo3 play e1m3, e1m4 and e1m6. With `--demos` the images still fit (episode 2 display 15,179,008 B, 14.48 MB, 96.9%, episode 3 14,336,944 B, 13.67 MB, 91.5%, main unchanged), but on the PC pair the Benchmark on such an image loads demo3, finds no e1m6 ("Model maps/e1m6.bsp not found") and the game ends. Without the demos the menu says "demo3.dem is not in the game data" and the game goes on, so the images for the board leave them out. e1m6 does not fit either image as well (main has 28 and 44 KB to spare).

The earlier stop at e2m3 (its L113), the board-flag split pair's server half crashes loading e2m3 when the data folder has no qogbake.bin. e2m3's texture 46 is missing from the BSP, id points such a texinfo at r_notexture_mip, and the server half's r_notexture_mip is a NULL stub (port/common/qog_split_sv.c), so Mod_LoadFaces reads `texture->name` through NULL (Q_strncmp, found with a /MAP build). With a baked image the server never parses faces, every pair run from the images or from a data folder with qogbake.bin loads e2m3 (the earlier own reference build gives all 113 qogtest_d_shots frames on this branch's data folder). The board always runs from its baked image, so the board is not affected. Bake the data folder (`qog_bake`) before running the pair, or give the server half a texture to point at (not done).

Tests (hidden, `+_cl_color 0`, own build folders `build-ag`, own git-ignored data folder, link ports 27661 to 27670, one run at a time).

| Test | Result |
| --- | --- |
| `qogtest_hash`, quakeog_pc, default pair, board-flag pair, `-nosound` | ce86f894f9de, ce86f894f9de, ce86f894f9de |
| `qogtest_f_hash` quakeog_pc, `qogtest_r_fhash` default and board-flag pairs | e92bc9e9650d, e92bc9e9650d, e92bc9e9650d |
| 121 shot frames (113 of `qogtest_d_shots` after `qogtest_k_quiet`, 8 of `qogtest_f_hash`) against an untouched build of main d8f2f9e | 121 of 121 byte identical |
| `qogtest_j_ep1.cfg` board-flag pair, full paks and the episode 1 image | 9 hashes d4361b206cac, the same |
| `qogtest_ag_j_ep2.cfg` (start, e2m1 to e2m7, a hash and shots per level), full paks and the episode 2 image | 8 hashes 202904569395, the same |
| `qogtest_ag_j_ep3.cfg`, full paks and the episode 3 image | 8 hashes 7b5ac000f4fe, the same |
| `qogtest_y_mem_<map>` start, e1m1 to e1m8, e2m1 to e2m7, e3m1 to e3m7, images, sound, 174 and 75 KB | all run |
| fight tours start to e2m7 and start to e3m7, images, sound, 174 and 75 KB | both run to the end (built before the last merge with main, which did not touch the server half) |
| boot level, episode end, gate, missing save | above |
| tools/tests | 15 tests pass |

Board link (`cmake -B build-ag/arm --preset target` with the full configure line of docs/handoff.md).

| | at main 6205974 plus this branch | after merging main d8f2f9e |
| --- | --- | --- |
| quake_display `end` | 0x2003f7f0, unchanged | 0x2003ea84 (main's own changes) |
| quake_main `end` | 0x2003f520, unchanged | 0x2003f544 |

quake_display.bin 375,360 B, quake_main.bin 1,317,732 B after the merge. The two client commands take 24 bytes of the client hunk, no static RAM.

Install. `tools/install_episode.ps1 -Episode 2` (new, never run here) does the lead's three steps for one episode with retries, and stops whenever two RPI-RP2 drives are mounted (docs/handoff.md).

Files, engine/common.c, engine/common.h, engine/host_cmd.c, port/rp2040/sys_rp2040.c (the argv), apps/quake/CMakeLists.txt, pc/CMakeLists.txt, tools/mkboard.py, tools/install_episode.ps1 (new), tools/level_exits.py (new). Not menu.c.

For the user.
- e2m7 has 2 KB of server hunk to spare at 174 KB. More would need either fewer spare edicts (`QOG_EDICT_MARGIN_BOARD` 64, 208 B each, a behaviour change) or edicts taken in blocks as the level uses them (exact, e2m7 never uses 85 of its 438, about 17 KB, not done). Moving a KB from the stack to the hunk would leave e3m5's constructed bound 2.4 KB of margin.
- The episode end without start, the player goes to the episode's first level with a new player's items (as id's start resets them). Keeping the items is a one-line change.
- Both images start in start. `--boot-map e2m1` makes an image start in the episode's first level instead.

## 2026-11-24, Fast spans without float work per span, interpolator pixels, a smaller console, and what the XIP cache really misses (PC and ARM build, registered data)

The user wants a basis of 30 fps and asked to replicate and improve MG24Quake's span strategy. I read their span path (d_edge.c sortSurfs, d_surf.c's 20 KB texture buffer with DMA prefetch, d_scan.c's 32-pixel sub-spans and bilinear light) and measured, on the PC, what of it would help here. The answer was not the texture and colour map buffers, MG24Quake has an FPU and must copy textures because its flash is not memory mapped, while this board's spans spend their time in software float routines. So the Fast drawer now does no float work per span and the pixel loop runs on the RP2040's interpolators. Nothing ran on hardware. Exact mode is byte identical. The board savings are estimates (cycle counts below), the lead measures them.

### What the XIP cache misses in the spans (PC model, `QOG_XIPSIM`)

A PC measurement build (`-DQOG_PC_EXTRA_DEFS=QOG_XIPSIM`, engine/d_xipsim.c, `qog_xipsim`) feeds every flash read of the span drawer (texels, colour map entries) and of R_BuildLightMap (lightmap samples) into a model of the XIP cache (16 KB, two ways, 8-byte lines, LRU), with four caches side by side, as today, colour map in RAM, texture in RAM, both. Code fetches and the rest of the frame are not modelled, so these are lower bounds. 300 views of each demo after 30 (`qogtest_ah_xip.cfg`), misses per view.

| Demo, render | as today (tex, cmap, lmap) | colour map in RAM | texture in RAM | both in RAM |
| --- | --- | --- | --- | --- |
| demo1 Exact | 2,162 (1,275, 551, 335) | 1,155 | 217 | 15 |
| demo2 Exact | 965 (606, 187, 171) | 491 | 45 | 3 |
| demo3 Exact | 2,110 (1,238, 532, 339) | 1,155 | 223 | 17 |
| demo3 Fast | 2,105 | 1,155 | 221 | 17 |
| demo3 Exact, surfaces sorted by texture (`QOG_SURF_SORT`) | 2,023 | 1,169 | 224 | 17 |

At about 0.26 us a miss (an 8-byte line at 125 MHz flash, 8 address and mode clocks, 4 dummy, 16 data, plus the SSI, from the 538 ns measured at 50 MHz on the board), 2,100 misses are about 0.55 ms a view. Texture and colour map in RAM together (MG24Quake's 20 KB plus 16 KB) would save at most that much, and sorting by texture saves 0.02 ms. The texels hit the cache well because neighbouring pixels share 8-byte lines and a surface's texels stay in the cache while it is drawn. So I did not spend RAM on texture or colour map buffers. The lead's XIPSTATS line will show whether code misses (not in the model) are many, if the spans phase shows about 2,000 to 5,000 misses a view, flash is not what the spans wait on.

The 2-point clock fit agrees, spans were 43.2 ms at 200 MHz with 50 MHz flash and 30.7 ms at 250 MHz with 125 MHz flash, which fits about 27 ms of CPU and about 4 ms of flash waiting at 250/125.

### What the spans do spend (Fast, demo3, per view)

PC counts (`QOG_RFAST_STATS`), 1,758 spans, 3,766 sub-spans, 48,477 pixels, about 6,900 light runs. The earlier Fast drawer per span, the start values (two int to float, six float multiplies and six adds), a reciprocal, two multiplies and two float to int conversions at the start and at each sub-span end, the sub-span length test (one or two float compares), and for the last sub-span an int to float and three multiplies. With the RP2040's ROM float routines at about 55 cycles each through the SDK wrappers, that is about 2,150 cycles a span, 3.8 M cycles a view, more than the pixels (about 32 cycles each, 1.55 M).

### Changes

| Change | Flag | Exact? | Pixels changed | Estimated saving, cycles a view (both cores) | RAM |
| --- | --- | --- | --- | --- | --- |
| Fast spans in integers. Per surface on core 0 (D_LitFixSetup, about 33 float operations, the scaling done on the float bits) the three linear functions of the screen position become 64-bit fixed point at the surface's first span, 1/z times 2^k (k puts the surface's nearest 1/z near 2^60), and s/z, t/z with sadjust and tadjust folded in, scaled so s/1/z is in [0, 2^w) with w from the surface's extent. Per span the start values are exact 64-bit sums (three 32-bit multiplies each, no multiply call), moved to 32 bits by one shift, and per sub-span end three adds, one hardware integer divide (2^32 / 1/z normalised to 17 bits) and two multiplies give s and t in 16.16. Error at most 2^(w - 16) texel plus 2^-15 of the coordinate, at most 1/64 texel. Sub-span lengths, light runs and clamps are the earlier. A surface whose values do not fit keeps the float drawer | `QOG_RFAST_FIX` (on with `QOG_RFAST`, `QOG_RFAST_FLOATSETUP` brings back the float drawer) | approximate (Fast only) | 2.88% of view pixels against Exact (the earlier Fast 2.75%), 0.25% against the earlier Fast, worst frame d_demo3_15 9.26% | about 2,150 to about 560 a span, 2.8 M, less 0.1 M of setup on core 0 | 100 B static, D_DrawSpans8Lit's frame 56 B (+16), the Fix drawer 432 B as the exact one, D_LitFixSetup 96 B never under it |
| The z of a span from the integer 1/z (`D_QogZSpanI`) | with the above | approximate | in the 2.88% | about 90 cycles a span | 0 |
| Fast pixel loop on the SIO interpolators. Interpolator 0 holds S and T2 with the texture in BASE2, so its FULL result is the texel's address and reading it steps both, interpolator 1 holds the light accumulator with the colour map in BASE2, so its FULL result is the light row. A pixel is two register reads, two byte loads and a store (8 instructions, about 11 cycles in place of about 32). Each core sets its own pair per surface. The PC emulates the registers with the hardware's rules, and the board checks them once at start-up from D_Init (D_QogLitInterpCheck, 64 steps against the C expressions, a Con_Printf is safe there and not inside the edge drawing), and if they differ every lit surface takes the exact drawer and a console line says why | `QOG_LITSPAN_INTERP` (`QOG_BOARD_INTERP`, on) | exact (the same pixels as Fast without it) | 0 against Fast without it, 121 of 121 frames | about 20 cycles a pixel, 0.97 M | 12 B static |
| Fused z spans with the one-byte z-buffer, when both ends of a span are 32 units or farther (z below 1024) and the step cannot wrap, every pixel's D_ZQ is z >> 3, so the loop skips D_ZQ's two tests | (on with `QOG_ZBUFFER8_NEAR`, `QOG_ZSPAN8_SLOW` turns it off) | exact | 0, see below | about 4 cycles a pixel on most pixels, 0.18 M | 0 |
| Console text 4 KB to 1 KB on the board client (26 lines of scrollback, the 4 notify lines, MG24Quake keeps 913 B). The hunk shrinks by the same 3 KB, so `-DQOG_CL_HUNK_KB=75` still means the earlier measured hunk and every level keeps its margin, and the 3 KB become static RAM | `QOG_BOARD_CONSMALL` (on) | the console holds fewer lines | 0 | | 3,072 B of static RAM freed |
| D_DrawSurfaces' loop body in a function of its own, and an optional draw order grouped by texture and mip (the key kept in last_u, which only one scan line's span making uses, so no RAM) | `QOG_SURF_SORT` (off, the model above says it saves 0.02 ms) | exact | 0 | | 0 |
| The XIP cache model | `QOG_XIPSIM` (PC measurement only) | | | | |

Total about 3.9 M cycles a view of span work, 15.5 ms of one core at 250 MHz. The spans phase is timed on core 0, which draws every second span, if the cores share evenly it falls by about 7.7 ms (23.6 to about 16 ms in timedemo demo3), and by more if core 1 helps less than half today (the clock fit above leaves room for that). An A/B with `-DQOG_BOARD_CORE1=OFF` would show how much core 1 takes today.

Frame estimate, timedemo demo3 took 107.5 ms a frame (1090 frames in 117.2 s). 7.7 ms less is about 10.0 fps, 15 ms less about 10.8 fps, before the other earlier work. The miss model assumes about 0.26 us a line at 125 MHz flash (0.1 to 0.5 us brackets it), the cycle counts about 55 cycles per ROM float call with its wrapper.

### Exact proof (hidden, `-nosound`, own `build-ah/pc` and `build-ah/pc-ref` (untouched dd17213), own git-ignored `data\id1`)

| Test | Result |
| --- | --- |
| `qogtest_hash`, quakeog_pc | ce86f894f9de |
| `qogtest_f_hash`, quakeog_pc, r_fast 0 and r_fast 1 | e92bc9e9650d, e92bc9e9650d |
| 121 shot frames (`qogtest_k_quiet` + `qogtest_d_shots` 113 and the 8 fhash frames), r_fast 0, against dd17213 | 121 of 121 byte identical |
| the same with `QOG_LITSPAN_INTERP` (emulated), r_fast 1, against r_fast 1 without it | 121 of 121 byte identical |
| one-byte z (`QOG_ZBUFFER8 QOG_ZBUFFER8_NEAR=7 QOG_NUMSTACKEDGES=1800`), the far-z loop against `QOG_ZSPAN8_SLOW`, r_fast 0 and 1 | 121 of 121 byte identical in each mode |

Fast pictures, Exact and Fast pairs of all 121 frames with the changed pixels in red are kept locally in a `fast2` folder (`pairs/*.png`, `percent.txt`).

### Board link

| | before (dd17213) | after |
| --- | --- | --- |
| quake_display `end` | 0x2003f7f0 at dd17213, 16 B free | 0x2003ea84 after merging main aec5274 (main alone 0x2003f610, 496 B free), 3,452 B free beside the 2 KB heap reserve |
| quake_main `end` | 0x2003f520 (0x2003f544 on main aec5274) | 0x2003f544, unchanged from main |
| quake_display.bin | 366,456 B | 374,600 B (main aec5274 plus about 4 KB, the integer drawer and the interpolator loop) |

The configure line is the lead's (`-DQOG_CL_HUNK_KB=75` and the rest), the display's hunk is then 72 KB.

### For the lead and the user

- RAM, 3,452 B of the display's static RAM is free after this (merged with main), for whoever needs it next. Other cuts I looked at and left, the command buffer (its measured peak is 2,158 B at boot, so 2 KB would drop default.cfg), the zone (peak 7,920 B of 8 KB), particles (already MG24Quake's 512), the client stack (4.2 to 5.6 KB free measured, and the z-edge fallback uses what is free).
- What the player sees, Fast pictures as before within 0.25% of the pixels (sub-texel steps at texture seams), the console scrolls back 26 lines instead of about 100.
- No frame-buffer or z-buffer rows are used by any of this, so the earlier scanout race (QOG_VID_SCANRACE) is not affected.
- Next, in my estimate the largest left in the spans, the per-surface work on core 0 while core 1 waits (lmap 1.5 ms, grad 2.4 ms and the new setup about 0.4 ms in the demo3 phase line). Drawing surface N's spans on core 1 while core 0 sets up surface N+1, with a second light grid (1,296 B of the free RAM) and spans taken from a shared counter, would hide about half of it, about 2 ms.
- Files, engine/d_litspan.h, engine/d_scanlit.c, engine/d_scan.c, engine/d_edge.c (mine), engine/d_xipsim.c (new, PC only), engine/r_surf.c (a PC measurement hook under `QOG_XIPSIM`), engine/d_init.c (that hook's command and the board's interpolator check), apps/quake/CMakeLists.txt (two board options and the hunk subtraction).

## 2026-11-24, Both chips truly in parallel, the 3D view racing the LCD scanout, the status bar sent only when it changes, demos pipelined (PC and ARM build, registered data)

Everything in the board frame outside the 3D view's drawing. Four board options, all on, all exact (same state hashes, same pictures on the panel). Nothing ran on hardware, so every saving below is an estimate from the board numbers of the entries below.

### What the 11 to 12 ms link wait was

Main's server frame did not overlap the display's drawing, although the earlier pipeline sends the tick before the screen update. The UART has only its 32-byte FIFOs and both sides read it only while they pump the link. Main answers sound operations (each queued `S_ExtraUpdate` and the `S_Update` of the pipelined update) with samples. Main's `qog_link_hw_write` then sat byte by byte in the hardware flow control for the whole 70 ms the display drew, so the client's next tick sat unread in main's ring, and main ran its 10 ms server frame only once the display came back to wait for it. The client's link wait was the server frame plus the transfer (11 to 12 ms on the board, about equal to main's 10 ms frame). On the PC the TCP socket never blocks, so the pipeline looked fine there.

### Changes

| Change | Flag (CMake option) | Why it is exact | Estimated saving, E1M1 play | RAM |
| --- | --- | --- | --- | --- |
| Main sends every link frame by DMA from a frame buffer (UART DREQ, held by the flow control while the display draws) and never waits in a write. It reads the tick and runs its frame while the display draws. Replies wait for the frame on the wire, the resend timer runs from a frame's end. Main flushes its last ack before it quits | `QOG_LINK_TXASYNC` on main (`QOG_BOARD_LINKOVERLAP`) | the stream's bytes and their order are unchanged, only when they leave | link wait 11 to 12 ms down to about 1 to 3 ms (the answer's transfer), but see the scanout below | main +36 B |
| While the display draws inside an exchange its queued sound operations wait in the ring instead of being pumped (a frame could stall the render while main runs its frame and does not read). After 10 ms without pumping the resend timer starts again instead of resending at once | `QOG_LINK_OVERLAP`, `QOG_LINK_LISTEN_GAP=10` (same option) | same operations in the same order, main runs them after the exchange as before | no duplicate tick frames after each draw | 4 B a side |
| The display writes DATA frames straight from its transmit ring with a carried CRC, no 514-byte frame buffer | `QOG_LINK_TXRINGSEND` (display, always) | the same bytes on the wire | | display -514 B |
| Demos and timedemo use the pipelined screen update. The update's sound exchanges (each `S_ExtraUpdate` in the renderer and `S_Update`) are queued instead of each waiting for main | `QOG_LINK_PIPELINE_DEMO` (`QOG_BOARD_PIPEDEMO`) | frame k's update still runs before frame k+1 reads its demo messages, with frame k's realtime, frame count, angles and key_dest (the earlier pipeline) | about 8 sound round trips a demo frame, estimated 2 to 5 ms | 0 |
| The 3D view starts while the last frame is scanned out. VID_LockBuffer does not wait in SCR_UpdateScreen's 3D part, R_EdgeDrawing waits only until the scanout has read the rows the world's edge cache and vertex cache use (they are the frame buffer's first rows, the scanout reads top down, each row once into a line buffer), and waits for the whole scanout after R_RenderWorld. 2D drawing before and after the view waits for the whole scanout as before | `QOG_VID_SCANRACE` (`QOG_BOARD_SCANRACE`, needs LOCKWAIT) | no row changes before the scanout has read it (checked on the PC at every wait) | see the critical path below | display 2 B, qog_video 8 B |
| The status bar rows are sent to the LCD only when they change (the panel keeps its pixels, as id's own VID_Update leaves the status bar out when nothing drew it). A frame whose status bar would be the repaint of the same saved state the panel already shows, with nothing else drawn over it (`SCR_QogHudQuiet`, no console, menu, dialog, loading plaque, intermission, or centre print reaching the rows), skips the repaint (hud 1.7 ms) and scans 152 rows instead of 200. A palette change since the last full scan repaints late and sends all rows | `QOG_HUD_SKIP` (`QOG_BOARD_HUDSKIP`) | the skipped rows equal what the panel shows (checked on the PC every such frame) | hud 1.7 ms, the scanout 18.3 to 13.9 ms in those frames | 13 B |

### The critical path after the changes (estimate)

Once the link is fast, the frame is bounded by the scanout, because the world's front end keeps its caches in the frame buffer (the earlier vertex cache, the edge cache), the next view's R_RenderWorld can only start when the scanout has read them. On E1M1 the edge cache is 13,497 edges, 84.4 rows, and the vertex cache 512 entries of 32 B after it, 51 rows, so the race waits for row 136, about 12.4 ms after VID_Update at 91.5 us a row (the earlier 18.3 ms for 200 rows). Link, parsing and input now take about 4 ms of that, the race wait is the rest.

| E1M1 play, ms a frame | Board now | Estimate |
| --- | ---: | ---: |
| link | 11 to 12 | 1 to 3 |
| scanwait (whole scanout waits) | 5 to 6 | about 0 |
| race (new, row waits) | | 7 to 9 |
| hud | 1.7 | about 0 (most frames) |
| view less hud | 65 to 68 | the same |
| frame | 83 to 86 | about 77 to 80 (12.5 to 13 fps) |

timedemo demo3 (e1m6, edge cache 64.8 rows, race to row 116, 10.6 ms), 107.5 ms a frame with a view of 75.7 ms, so 32 ms outside the view. The pipelined demo removes the synchronous sound exchanges and the race overlaps the scanout with the demo's parsing. Estimate 88 to 95 ms a frame, 10.5 to 11.4 fps (9.3 now).

The biggest remaining item outside the view is the race wait, a cache placement question for earlier work, with 256 vertex cache entries the race would stop at row 110 (10 ms) instead of 136 (12.4 ms), and an edge cache that is not in the frame buffer would let the view start at once.

### What to read on the board

- `[time cl]` line. `link` should fall from 11 to 12 to about 1 to 3, `scanwait` to about 0, the new `race` (after `scanwait`) is the time the view waited for rows, `hud` near 0. `frame` is the result.
- `[quake_main] link wait ... txstall <ms>` once a second on COM7, ms in that second main waited to write to the link. About 0 with the DMA. Built with `-DQOG_BOARD_LINKOVERLAP=OFF` it shows the old stall (expected about 800 ms a second) and confirms the cause above.
- `[quake_display] status bar rows sent in <n> of <m> frames` once a second on COM8.
- A/B. `-DQOG_BOARD_SCANRACE=OFF`, `-DQOG_BOARD_HUDSKIP=OFF`, `-DQOG_BOARD_PIPEDEMO=OFF` on the configure line.
- Look for a torn or stale status bar (ammo changing, pain flashes, the console, centre prints over the status bar). None on the PC.

### PC verification (hidden, `-nosound`, own build folders in `build-ak`, own git-ignored `data\id1`, link ports 27941 to 27951, one run at a time)

References are untouched dd17213 builds. `build-ak/pc-ref` (default) and `build-ak/pcb-ref` (the earlier board-flag pair flags). `build-ak/pcb` adds `QOG_LINK_OVERLAP QOG_LINK_TXRINGSEND QOG_LINK_LISTEN_GAP=10 QOG_LINK_PIPELINE_DEMO QOG_VID_SCANRACE QOG_HUD_SKIP QOG_VERTCACHE QOG_VERTCACHE_CLIPDIST` to the client and `QOG_LINK_TXASYNC QOG_LINK_LISTEN_GAP=10` to the server (link_pc.c holds each frame for its wire time and sends it at the end, as the DMA would), `build-ak/pcbk` the same plus `QOG_HUD_SKIP_CHECK`. With `QOG_HUD_SKIP` the PC keeps the panel's picture (VID_Update copies the rows it sends) and shots and frame hashes are taken from it, so they show what the LCD shows.

| Test | Result |
| --- | --- |
| `qogtest_hash`, quakeog_pc and the pcb pair | ce86f894f9de, ce86f894f9de |
| `qogtest_f_hash` quakeog_pc, `qogtest_r_fhash` pcb and pcbk pairs | e92bc9e9650d each |
| 121 shot frames (`qogtest_k_quiet` + `qogtest_d_shots` and the 8 fhash frames), quakeog_pc against pc-ref | 121 of 121 byte identical |
| the 94 shots the pair takes (86 demo and spawn shots, 8 fhash), pcb and pcbk against pcb-ref | 94 of 94 byte identical each, with demos pipelined (3,077 of the demo run's frames, 77 in the reference) |
| `qog_scanrace` (no byte changed before the scanout passed it) | 0 of 6,986 checks (r_fhash), 0 of 886 (hash), 0 of 1,858 (benchmark) |
| `qog_hudskip` with the check (the rows not sent, repainted anyway, against the panel's) | 2,433 of 5,313 r_fhash frames sent no status bar rows, 0 differed |
| the earlier `qogtest_af_menu.cfg` on pcb (e1m1, menu, Benchmark, yes, result page, gray) | runs, `[bench] timedemo demo3 1090 frames`, 1,108 frames pipelined, result page and back to the menu. The shots differ from the reference only by real time (this script runs without host_framerate), as two reference runs do |

Board link (`cmake -B build-ak --preset target` with the full configure line).

| | `end` | Free beside the 2 KB heap reserve |
| --- | --- | ---: |
| quake_display | 0x2003f610 (main 0x2003f7f0) | 496 B (16 before) |
| quake_main | 0x2003f544 (main 0x2003f520) | 700 B (736 before) |

quake_display is 368,504 B of its 913,408 B slot, quake_main 1,309,684 B of 2 MB.

Files, port/common/qog_link.c and .h, port/rp2040/link_rp2040.c, port/pc/link_pc.c, engine/net_link.c, engine/host.c, engine/screen.c, port/rp2040/vid_rp2040.c, port/pc/vid_pc.c, platform/qog_video.c and .h, engine/qog_phase.h, port/common/qog_phase.c, apps/quake/CMakeLists.txt. Shared files, each in its own `#ifdef` block, engine/r_main.c (R_HudRepaint taken out of R_RenderView unchanged, the skip, the race waits in R_EdgeDrawing), engine/r_draw.c (one wait in R_QogVertCacheBegin, the earlier file), engine/vid.h, engine/d_local.h. d_edge.c is not touched.

## 2026-11-24, timedemo demo3 on the board (lead, on hardware)

The board menu's Benchmark ran id's `timedemo demo3` on the episode 1 image with the demos (13.69 MB display data).

`[bench] timedemo demo3 1090 frames 117.2 seconds 9.3 fps, render Fast, clock 250 MHz, flash 125 MHz, view 100, 320x200`

- For comparison, MG24Quake publishes about 29.2 fps for the same timedemo on a Cortex-M33 with FPU at 136 MHz.
- During demos the pipelined screen update is off, so each frame is the client's full frame plus one link exchange. Play in E1M1 runs at about 12 fps.
- Last phase line of the run (ms per view) is setup 0.2, world 13.7, bmodels 4.3, lmap 1.5, grad 2.4, spans 23.6, turb 0.4, models 14.3, vmodel 5.2, parts 3.4, total 75.7, edgescan 4.1. Monsters (models 14.3) are the second biggest item after the spans in the demo, where E1M1's start view had none.
- The z-edge fallback ran once in the run (`zedge 1`) and the game went on, as earlier work designed it.
- Stack free on the display was 5,576 B at its lowest.

## 2026-11-24, Benchmark in the board menu, id's timedemo demo3 with its result on the screen (PC and ARM build, registered data)

The user wants a number to set beside MG24Quake's published `timedemo demo3` (about 29 fps on a Cortex-M33). The board's Single Player menu has a fifth line, Benchmark, which runs id's `timedemo demo3` (e1m6) through the command buffer as id's console would, and shows the result on the screen when the demo ends. Nothing ran on hardware.

On the board, step by step.
1. Single Player, Benchmark (down four times from New Game), gray.
2. If a game runs, the in-menu prompt asks "end the game and run the timedemo benchmark?" (gray yes, green + blue no), because id's `timedemo` disconnects first. With no game it starts at once.
3. If `demo3.dem` is not in the display's data image, a page says so and how to write the image (`mkboard.py --demos`), and the game goes on. The check comes before the prompt, so the game is never ended for nothing.
4. Otherwise the menu closes and `timedemo demo3` runs. id's `CL_PlayDemo_f` disconnects, the client closes its link socket and the server half on main shuts its server down (`Link_ServerPeerClosed`), then waits for the client's frame ticks as it does between levels. Every client frame still makes its exchange with main (an idle server frame, the watchdog is kicked in the link wait) and the demo's sound calls go to main's mixer as in play. The pipelined screen update is off during a demo (`Host_PipelineCan`), so each frame is the client's full frame plus the exchange.
5. When the demo ends id prints `N frames X seconds Y fps` and the board then prints one line on the console, which the display echoes over USB (COM8), for example
   `[bench] timedemo demo3 <N> frames <X> seconds <Y> fps, render Fast, clock 250 MHz, flash 125 MHz, view 100, 320x200`
   (the PC prints `PC` in place of the clocks). The clocks are read from the hardware at that moment (`clock_get_hz(clk_sys)` and the SSI divider, `Sys_QogClockKHz` and `Sys_QogFlashKHz` in sys_rp2040.c), the render mode is the `r_fast` cvar (Exact or Fast), the view size `scr_viewsize`.
6. The result page stays until gray or green + blue, the demo name, frames and seconds, the fps in the big font, the render mode, clock, flash clock, view size and screen size. Either button goes back to the Single Player menu with Benchmark selected. The game is over (id's timedemo ended it), so New Game, Levels or Load go on from there.
7. A demo stopped before its end (a level started from the menu during the run) prints `[bench] timedemo demo3 stopped after N frames ... no result` and the page says "stopped before the end" with no fps. Opening the menu during the run is allowed and slows that run.

RAM, none. The display's `end` is 0x2003f7f0, the same as main 6205974, main's 0x2003f520. The result is kept in fields that nothing reads once a timedemo is over, frames and seconds in `cls.td_startframe` and `cls.td_starttime` (set by `CL_FinishTimeDemo`), how it ended in `cls.td_lastframe`, the demo's name in `cls.mapstring` (id's map command writes it, nothing reads it). The page is a menu state (`m_bench`), its strings are in flash. quake_display is 366,456 B of its 913,408 B slot.

The data image needs the demos. mkboard leaves them out unless `--demos` is given, and the earlier episode 1 command did not give it. Built from the merged tree with `--demos` (`python tools\mkboard.py --pak data\id1\pak0.pak --pak data\id1\pak1.pak --manifests build-pc-dev\manifests --maps start e1m1 e1m2 e1m3 e1m4 e1m5 e1m6 e1m7 e1m8 --exe build-pc-dev\Release\quakeog_pc.exe --stub-maps --main --display-no-sounds --display-no-progs --demos -o build-board\qb_ep1`).

| Image | Without demos | With demos |
| --- | --- | --- |
| display pak | 3,562,912 B | 4,097,984 B (demo1 to demo3, 535,072 B more) |
| display image | 13,831,152 B, ends 0x10E30BF0, 88.3% | 14,355,440 B (13.69 MB), ends 0x10EB0BF0, 91.7% of 14.94 MB |
| main image | 6,029,312 B | byte identical (mkboard puts no demo in main's image) |

So only the display's data UF2 changes, main's stays. Flash it as step 3 of the episode 1 flashing in docs/handoff.md. With the demos in the image the board still boots into e1m1 (`+map e1m1` runs before quake.rc's `startdemos`, which then sees a running server and starts no loop), checked on the PC pair from the image.

PC checks (hidden, `-nosound`, own build folders `build-af/pc`, `build-af/pc-ref` (untouched 6205974) and `build-af/pcb` (the split pair with the board client flags of the earlier list, `QOG_RFAST_ON`, `QOG_BOARD_MENU`, `QOG_AOT`, the server's board flags), own git-ignored `data\id1`, link ports 27661 to 27669, one run at a time).

| Test | Result |
| --- | --- |
| `qogtest_hash`, quakeog_pc and board-flag pair | ce86f894f9de, ce86f894f9de |
| `qogtest_f_hash`, quakeog_pc | e92bc9e9650d |
| 121 shot frames (`qogtest_k_quiet` + `qogtest_d_shots` 113 and the 8 fhash frames) against `build-af/pc-ref` | 121 of 121 byte identical |
| `qogtest_af_menu.cfg`, board-flag pair, full paks, e1m1, menu, Benchmark, prompt, yes, the result page shot after 1,400 frames, gray, quit | result page, back to the menu, both halves quit cleanly |
| the same from the episode 1 images with demos, board hunks (`-heapsize 174` and `75`) | the same, `[bench]` line, no link error |
| `qogtest_af_menu2.cfg` (no game running, no prompt) | runs, result |
| `qogtest_af_nodemo.cfg` from the images without demos | the missing page, no prompt, e1m1 keeps running |
| boot from the image with demos, `+map e1m1` | e1m1, no demo loop |

PC timedemo numbers, for reference only, 1090 frames in 19.0 to 20.0 s, 54.4 to 57.5 fps for quakeog_pc, the board-flag pair, Exact and Fast alike. The PC's main loop runs at most 72 frames a second and sleeps in steps of a millisecond (sys_pc.c), so this is the PC's pacing, not a speed. The board number is the one to publish.

To compare with MG24Quake, the honest details are on the page and in the `[bench]` line, the render mode (Fast is the board default, Exact draws id's pixels), the clocks (250 MHz, flash 125 MHz), the full view (100) on 320x200 with the status bar, the one-byte z-buffer, sound mixed on main, and each frame's exchange with main.

Test scripts (in the branch's git-ignored data folder, copy both into each image's `id1` as L79 says). `qogtest_af_menu.cfg`, `qogtest_af_menu2.cfg`, `qogtest_af_nodemo.cfg`, `qogtest_af_after1.cfg`, `qogtest_af_after2.cfg`, `qogtest_af_exact.cfg`.

Files, engine/menu.c (Benchmark line, prompt text, result page, all under `QOG_BOARD_MENU`), engine/menu.h, engine/cl_demo.c (`QOG_BOARD_MENU` blocks in CL_StopPlayback, CL_FinishTimeDemo and CL_TimeDemo_f), engine/sys.h, port/rp2040/sys_rp2040.c and port/pc/sys_pc.c (the clock functions). tools/mkboard.py is not changed (earlier work works there).

## 2026-11-24, Episode 1 on the board, the new flash map, the server stack and the edge fallback (PC and ARM build, registered data)

Start and all of episode 1 now fit both chips, with sound. The display's data moved from 0x10400000 to 0x10100000 and main's from 0x10300000 to 0x10200000 (the earlier proposal), the server's stack went from 35 to 21 KB after measuring its deepest chain in board bytes, which gives the server hunk 174 KB (160 before), and the one-byte z-buffer's edge fallback draws one view without z instead of stopping the game. Nothing ran on hardware. Merged with main 801a6b9 before the final numbers, and with 1610f89 (the save volume format, the full view) for the last board link and hashes.

Flash map (platform/qog_flashmap.h, the one source, tools/mkdata.py reads it, mkboard and mkpak use mkdata's values, check_app_size.py reads it, the PC maps its packs at `QOG_DATA_XIP_ADDR`).

| Chip | Region | Before | Now |
| --- | --- | --- | --- |
| display | BSP bootloader, app metadata | 0x000000 to 0x020FFF | unchanged |
| display | app slot | 0x021000 to 0x3FFFFF (3.9 MB) | 0x021000 to 0x0FFFFF, 913,408 B. quake_display is 364,448 B (39.9%) |
| display | data | 0x400000 to 0xFEFFFF (11.94 MB) | 0x100000 to 0xFEFFFF, 15,663,104 B (14.94 MB) |
| display | save slots | 0xFF0000, 64 KB | unchanged |
| main | app slot | 0x000000 to 0x2FFFFF (3 MB) | 0x000000 to 0x1FFFFF, 2 MB. quake_main is 1,301,356 B (62.1%) |
| main | server data | 0x300000 to 0x7FFFFF (5 MB) | 0x200000 to 0x7FFFFF, 6,291,456 B |
| main | FAT volume | 0x800000, 8 MB | unchanged, never touched |

- Preprocessor checks in the header stop the build if regions overlap or lose their 64 KB alignment, and quake_main checks at compile time that its data ends at the BSP's `FWOG_FS_FLASH_OFFSET` (sys_rp2040.c).
- quake_display now has the post-build size check too (`qog_check_app_size`), so a display app that outgrows 913,408 B fails its build like an oversize quake_main. The display update of main writes the app at 0x21000 and erases only the sectors it needs (bl_receiver.c), so it never reaches 0x100000.
- mkboard passes `-flashbase` to both bakes, so the PC maps the packs at the board's address whatever its default. The PC's default is now 0x10100000, so a `data\id1\qogbake.bin` baked before is skipped with "is for address 10400000" until `qog_bake` runs again. The main image runs on the PC with `-basedir <out>_main -flashbase 0x10200000`.
- tools/tests/test_mkdata.py checks the offsets and that no region overlaps.

Episode 1 images (`mkboard.py ... --maps start e1m1 e1m2 e1m3 e1m4 e1m5 e1m6 e1m7 e1m8 --stub-maps --main --display-no-sounds --display-no-progs`, from the merged build).

| Image | Bytes | Region | Spare |
| --- | ---: | --- | ---: |
| display `qb_ep1.uf2` (pak 3,562,912 B, image 10,226,672 B at 0x10470000, ends 0x10E30BF0) | 13,831,152 (13.19 MB) | 88.3% of 14.94 MB | 1,831,952 B |
| main `qb_ep1_main.uf2` (pak with 184 sounds and progs.dat 3,023,856 B, image 2,948,000 B at 0x104F0000, ends 0x107C0000) | 6,029,312 (5.75 MB) | 95.8% of 6 MB | 262,144 B |

Server stack (`QOG_SV_STACK_KB` 35 to 21).

The static chain (tools/stack_chain.py) cuts recursion, so the PC now measures the server in board bytes. A measurement build of quakeog_sv (`-DQOG_SV_STACKPROBE=ON`, port/pc/stackprobe_pc.c) is compiled with /Gh /GH /Ob0, every function enters and leaves through hooks that keep a shadow call stack, and each call weighs its function's frame in the ARM build (`tools/stack_chain.py --su-table`, from the .su files of qog_engine_sv and quake_main). Functions the board build inlined weigh nothing (the caller's frame holds them). It also tracks the deepest chain that runs the mixer.

| Run (split pair, episode 1 images, sound on, both halves silent, server flags of the board) | Deepest, board bytes, before | After `QOG_PUSHMOVE_ALLOCA` |
| --- | ---: | ---: |
| `qogtest_y_mem_<map>` fight, save, load, start and e1m1 to e1m8 | 10,696 to 11,592 (`Host_Loadgame_f` > `SV_SpawnServer` > `SV_Physics_Pusher` > `SV_PushMove` 9,768) | 6,920 to 9,936 (e1m3, the lists 6,656 B and the hull recursion under them) |
| `qog_tour_fight start e1m1 ... e1m8` | 14,400 (`SV_PushMove` > `SV_Move` > 43 levels of `SV_RecursiveHullCheck`, 104 B each) | 10,984 |
| deepest that runs the mixer | 1,816 (`S_Update_` > `S_PaintChannels` > `S_LoadSound` 1,368) | 1,816 |

- `SV_PushMove` held two lists of 600 entries on its frame, 9,768 B. `QOG_PUSHMOVE_ALLOCA` (engine/sv_phys.c, both flag lists, exact) takes them from the stack for `sv.max_edicts` entries, the loop adds at most one entry per edict below `sv.num_edicts`, which never passes `sv.max_edicts` even when QuakeC spawns during the loop. The most in episode 1 is e1m3, 416 edicts, 6,656 B. Same moves, the state hashes below are unchanged.
- The recursion the measurement found is bounded by the map. `tools/hull_depth.py` reads the depth of each hull's tree. Episode 1's deepest is hull 0 (points) of e1m3, 85 levels, then e1m4 71. The worst chain I can construct is a point entity (a grenade) pushed by a plat on e1m3's deepest leaf, 752 B down to the clip, the lists 6,656 B, 86 levels at 104 B, about 16.4 KB, and an error path through a QuakeC runtime error in a blocked function (`PR_RunError` > `Host_Error` > `Con_Printf` 4,120 B) about 16.1 KB. Neither ran in any test.
- The lead's format of a missing save volume (main 1610f89) puts a 4 KB work buffer on this stack in `fat_ready` (4,128 B frame), reached from a save at about 2 KB deep, so about 6.5 KB plus FatFs, far below the deepest chain.
- 21 KB is 21,504 B, 10.3 KB above the measured worst (10,984 B plus up to 0.5 KB of newlib that the PC does not see), and 5.1 KB above the constructed bound, so at least 4 KB of margin either way. The mixer needs 1,816 B above its 6,144 B of paint buffer and ring at the bottom of the stack (7,960 B). Main's `stack free` on COM7 shows what the board uses.

Server hunk (`QOG_SV_HUNK_KB` 160 to 174, the 14 KB the stack gave). Smallest `-heapsize` of the server half that runs `qogtest_y_mem_<map>.cfg` from the episode 1 images, sound on for both halves (the split pair never opens an audio device, the client's output is snd_link_pc.c's counter), against the earlier numbers without sound.

| Map | Without sound | With sound | Spare at 174 KB |
| --- | ---: | ---: | ---: |
| start | 90 | 96 | 78 |
| e1m1 | 112 | 117 | 57 |
| e1m2 | 139 | 144 | 30 |
| e1m3 | 165 | 171 | 3 |
| e1m4 | 155 | 161 | 13 |
| e1m5 | 154 | 160 | 14 |
| e1m6 | 140 | 146 | 28 |
| e1m7 | 88 | 93 | 81 |
| e1m8 | 115 | 121 | 53 |

Sound adds 5 to 6 KB on every level (channels, the name pool and the sound headers that stay). e1m3 is the most at 171 KB, 3 KB below the new hunk.

At 174 KB for the server and 75 KB for the client, with sound, all nine scripts, the changelevel script (`qogtest_ad_chg.cfg`, e1m1 then `changelevel e1m2`), and the whole fight tour of start to e1m8 run.

The edge fallback (engine/r_edge.c, `R_QogEdgesToStack`).

On the board the one-byte z-buffer leaves the edge table so little room that a few views fill the span buffer with spans that wait for edges still needed. The earlier exact fallback moves those edges to a table on the stack. The board has no stack reserved for it, so the earlier guard stopped the game with an error. Now, when the stack cannot hold the table, `R_QogFlushNoZ` draws the waiting spans at once, every pixel exactly as id draws it, but with `d_zwidth` 0 and `d_pzbuffer` at the frame buffer's last view row, which no span of this view has drawn yet and the last flush draws over, their z lands there and the edge table stays intact. The rows are noted, and once every edge is done `R_QogClearNoZ` sets their z to the farthest value. So the world is exact and only models, sprites and particles in those rows of that one view are drawn in front of the world. The stack fallback is unchanged when the stack holds it. No static RAM, the PC measurement counters the board never reads make room for the three ints (quake_display `end` stays 0x2003f7e8). The display's diag line ends with `zedge <n> noz <m>`.

| | |
| --- | --- |
| Views that reach the fallback | the episode 1 fight tour (split pair with the board client flags, full paks or the images), frames 1500, 1501, 8348, 8349 (4, earlier work saw 4). Another run of the same tour with sound and the board hunk sizes, 937, 1491, 1492, 8344, 8345 (the split tour does not repeat exactly) |
| Before and after, same state | `QOG_ZEDGE_COMPARE` (PC test builds) draws each such view a second time with the graceful path after putting the particles and dynamic lights back, 193, 183, 164 and 145 pixels differ (0.30, 0.29, 0.26, 0.23% of the frame), in rows 87 to 136, all models and particles behind a ledge showing through |
| Views that never reach it | byte identical, the 113 `qogtest_d_shots` frames and the 8 `qogtest_f_hash` frames of the merged build against an untouched build of main 801a6b9 |

Screenshots (`frame_<n>_before_exact.png`, `frame_<n>_after_noz.png`, `frame_<n>_diff_marked.png` with the changed pixels in red, 2x) are kept locally in a `zedge` folder.

Tests (hidden, `+_cl_color 0`, own build folders `build-ac`, own git-ignored `data\id1` with a new `qogbake.bin`, link ports 27641 to 27650, one run at a time).

| Test | Result |
| --- | --- |
| `qogtest_hash`, quakeog_pc and split pair, `-nosound` | ce86f894f9de, ce86f894f9de |
| `qogtest_f_hash` (md5 of 32 statehash lines), quakeog_pc | e92bc9e9650d |
| `qogtest_r_fhash`, default split pair and board-flag split pair | e92bc9e9650d, e92bc9e9650d |
| 121 shot frames against main 801a6b9 | 121 of 121 byte identical |
| `qogtest_j_ep1.cfg` board-flag pair, full paks and the episode 1 images | 9 hashes d4361b206cac, the same |
| `qogtest_j_menu.cfg`, full paks and images | runs to the end, no error |
| `qogtest_ad_chg.cfg` from the images, sound on, 174 and 75 KB | runs, 1 hash |
| `qogtest_y_mem_<map>` start to e1m8, images, sound, 174 and 75 KB | all 9 run |
| fight tour start to e1m8, images, sound, 174 and 75 KB | runs to the end, 5 fallback views, none stops |
| tools/tests (`python -m unittest discover -s tests` in tools) | 15 tests pass, 2 of them new |

Board link (`cmake --preset target -B build-ac/arm` with the configure line in docs/handoff.md).

| | `end` | Free beside the 2 KB heap reserve |
| --- | --- | ---: |
| quake_display | 0x2003f7e8, the same as main 801a6b9 | 24 B |
| quake_main (hunk 174 KB, stack 21 KB) | 0x2003f520 after merging main 1610f89, 0x2003f51c before it, the same as with 160 and 35 | 736 B |

Files, platform/qog_flashmap.h, tools/mkdata.py, tools/mkboard.py, tools/check_app_size.py, tools/stack_chain.py (`--su-table`), tools/hull_depth.py (new), tools/tests/test_mkdata.py, port/pc/sys_pc.c, port/pc/stackprobe_pc.c (new), port/pc/vid_pc.c (`-zedgeshots`, compare shots, exit counts), port/rp2040/sys_rp2040.c (FAT check), port/rp2040/link_rp2040.c (diag), engine/sv_phys.c, engine/r_edge.c, engine/view.c (compare mode only), engine/r_local.h (one extern), pc/CMakeLists.txt, apps/quake/CMakeLists.txt, apps/qogbench/display/main.c (the data address), README.md. Shared with earlier work, none of r_bsp.c, r_draw.c, r_main.c, d_scanlit.c. r_edge.c and view.c are touched in their own blocks.

For the user.
- The fallback's picture change, in the few views that reach it on the board, models and particles in some rows show through the world for one frame (shown above). Setting those rows' z to the nearest value instead would hide them there, and hide the weapon too.
- Server stack margin, 21 KB covers the measured worst by 10 KB and a constructed worst case by about 5 KB. 22 KB (hunk 173) would add a KB of stack margin, e1m3 with sound would then have 2 KB of hunk to spare instead of 3.
- Episodes 2 and 3 fit the display's new region one at a time (13.98 and 13.17 MB) and main's (5.97 and 5.96 MB, the earlier sizes). Episode 4 does not fit either. Their server hunk with sound is not measured.
- 4 KB of the server stack is `Con_Printf`'s message buffer, and `Con_DPrintf` takes another 4 KB frame even when `developer` is 0. A smaller server message buffer would change long prints (not done).

## 2026-11-24, Board measurements of the split game (lead, on hardware)

Read from the board's USB logs (display COM8, main COM7) with QOG_PHASE_TIME and QOG_LINK_DIAG, standing in E1M1 at the spawn point, each line an average over about 2 s.

| Build | fps | Frame ms | View ms | world | spans | vmodel | edgescan | Server frame ms |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 200 MHz, flash 50 MHz, sound (e2854de) | 7.5 | 132 | 109 | 38.3 | 43.2 | 7.6 | 7.9 | 17.2 |
| 250 MHz at 1.20 V, flash 125 MHz (259905d) | 10.4 | 96 | 78 | 26.3 | 30.7 | 4.9 | 5.8 | 11.8 |
| 250 MHz plus Fast mode (an earlier branch merge) | 10.9 | 91 | 71 | 22.5 | 29.8 | 4.6 | 6.1 | not read |

- The clock log lines read "clock 250000 kHz at 1200 mV" and "flash 125.0 MHz ok (div 2 from 4, rx delay 1, 128 KB checked)". No fault, hang or error line on either chip in any of these runs.
- Fast mode cut the span arithmetic by about 2.9 M cycles a view (the earlier estimate) but spans fell only 0.9 ms, so the span time looks bound by XIP misses rather than compute. The earlier XIP counter build is meant to confirm it.
- Stack free on the board with sound and the 73 KB client hunk (76d0172), display 4,192 B, main 19,204 B.
- Sound through the speaker was heard by the user on e2854de. The game has run 15 minutes in E1M1 without a restart since the strict aliasing fix. Saving on the board has not been tried. Update, November 2026, saving and loading were later tried on the board and work.
## 2026-11-24, XIP and float counters, world front end and alias vertices faster, same pixels (PC and ARM build, registered data)

A measurement build that tells whether a render phase waits on flash or on float helpers, and four exact speed-ups of the display chip's 3D view, all with the same pixels as main. Nothing ran on hardware. The savings are estimates from the PC's work counts and the Thumb code at 250 MHz, the lead measures them.

### Measurement build (off by default, needs `QOG_BOARD_TIMING`)

```powershell
cmake -B build-zx --preset target -DQOG_AOT=ON -DQOG_AOT_PAK=<pak0.pak> -DQOG_SOUND=ON -DQOG_CL_STACK_KB=32 -DQOG_BOARD_XIPSTATS=ON
cmake --build build-zx --target quake_main
```

Add `-DQOG_BOARD_TEXNOALLOC=ON` or `-DQOG_BOARD_CMAPNOALLOC=ON` for the flash experiments below. The float counts are a second build, `-DQOG_BOARD_FPCOUNT=ON -DQOG_CL_STACK_KB=31` (it needs 1 KB of the client's stack, measurement only, watch the stack watermark line). Its times run slow (each helper call passes a counting stub in flash), so read times and XIP numbers from the XIPSTATS build and only the counts from this one.

Every 2 s on COM8, after the three `[time cl]` lines.

```
[time cl] xip acc/miss setup a/m world a/m bmodels a/m lmap a/m grad a/m spans a/m zspans a/m sky a/m turb a/m models a/m vmodel a/m parts a/m total a/m edgescan a/m
[time cl] work per view nodes <n> leafs <n> faces <n> clipedge <n> emitedge <n> cached <n> project <n> clipdot <n>
FPCOUNT only:
[time cl] fp f/d setup f/d world f/d ... total f/d edgescan f/d
[time cl] fp calls per view core0 fadd <n> fsub fmul fdiv fcmp f2i i2f f2d fmath dadd dsub dmul ddiv dcmp d2i d2f i2d dmath idiv l64 core1 f <n> d <n>
[time cl] fp ns per call fadd <ns> ... l64 <ns> meanf <ns> meand <ns>
[time cl] fp est ms core0 all <ms> setup <ms> world <ms> ...
```

- xip, XIP cache accesses and misses (accesses less hits, XIP_CTRL CTR_ACC and CTR_HIT) per view in each phase, both cores together (core 1 draws half the spans inside the spans phase). Misses times the cost of one (an 8-byte line from the 125 MHz flash, a few hundred ns) against the phase's time says how much of the phase waits on flash. The counters saturate at 2^32, so the report clears them every 2 s. Phase marks run from RAM (`qog_phase_mark`, `qog_phase_add`) and add no XIP accesses of their own, but cost about 0.2 ms a frame.
- work, the world front end's calls per view, and the points projected and the plane distances computed (what `QOG_VERTCACHE` saves).
- fp, core 0's float (f) and double (d) helper calls in each phase. `objcopy --redefine-syms` (port/rp2040/qog_fpcount.syms) renames the engine library's calls to stubs (port/rp2040/qog_fpcount.S) that count per core and jump on to the SDK's routine with the caller's registers (only ip changes). The ns per call are timed once on core 0 at the first report (2,000 calls, best of three). est ms is the calls times the mean measured cost, so a phase whose est ms is near its time is float bound.

RAM, the XIPSTATS build ends at 0x2003f7f0 (16 B above the 2 KB heap reserve), the FPCOUNT build at 0x2003f550 with the 31 KB stack.

### Exact speed-ups (on for the board client, apps/quake/CMakeLists.txt)

| Change | Flag (CMake option) | Why the pixels are the same | Estimated saving per view, 250 MHz, E1M1 spawn | RAM |
| --- | --- | --- | --- | --- |
| World vertices projected once a frame. A direct-mapped cache keyed by vertex number keeps u, v, 1/z and ceil(v) of each world vertex R_EmitEdge projects, in memory nothing uses while R_RenderWorld runs (the frame buffer's view rows past the edge cache, or the z-buffer between the edge cache and the edge table), emptied every frame. PC E1M1 spawn, 1,092 projections a view became 506 | `QOG_VERTCACHE` (`QOG_BOARD_VERTCACHE`) | a hit returns what id's statements (now in R_QogProject) computed for the same vertex in the same frame. Brush entities (insubmodel) and clipped points never use it | 586 projections of about 1,750 cycles less 1,600 lookups of about 30, about 4 ms of world | 16 B static, 512 to 4,096 entries in the frame buffer (E1M1 on the board 512 with CLIPDIST) |
| Each world vertex's distance to the four view planes measured once a frame, in the same entries. R_ClipEdge's 954 dot products a view became 464 | `QOG_VERTCACHE_CLIPDIST` (`QOG_BOARD_CLIPDIST`) | the same expression, once per vertex and plane in a frame (the view planes do not change while the world is drawn). An unknown distance holds a NaN pattern, a real NaN with those bits is only computed again | 490 dot products of about 370 cycles, about 0.7 ms | entries 32 B instead of 16 |
| BSP node frustum tests in a function off the recursion, the box corners converted once per node, compares instead of a difference (main's `QOG_FLOAT_EXACT` already made the doubles floats), the side tests as an int | `QOG_WORLDNODE_FAST` (`QOG_BOARD_WORLDNODE`) | `(double)dot - (double)dist <= 0` is `dot <= dist` exactly, and for a float x `x < -0.01` and `x > 0.01` are `x < -0.01f` and `x > 0.01f` | about 0.3 ms. R_RecursiveWorldNode's frame 96 to 48 B, 48 B less stack at every level of the BSP recursion | 0 |
| Alias model vertices made by both cores, core 0 the even ones and core 1 the odd ones (R_AliasPreparePoints and the unclipped R_AliasTransformAndProjectFinalVerts), models of 24 vertices or more. Triangles stay on core 0 in id's order | `QOG_ALIAS_CORE1` (`QOG_BOARD_ALIASCORE1`, with `QOG_BOARD_CORE1`) | each vertex is computed by id's statements from data nothing changes meanwhile and written only to its own finalvert | v_shot (68 vertices) about 0.3 ms, a soldier (170) about 0.7 ms | 0, core 1's chain about 150 B |

PC verification, hidden and silent, against a build of the same tree with the flags off (`build-pc-mbase`, which gives main's hashes). `qogtest_hash` (`ce86f894f9de`), `qogtest_f_hash` (`e92bc9e9650d`) and its 8 shots, `qogtest_k_quiet` + `qogtest_d_shots` 113 frames, all identical, and the split pair's `qogtest_hash`, `qogtest_r_fhash` and `qogtest_d_shots` identical to the single process. With `QOG_SPANS_CORE1` the PC runs the core 1 job on a second thread, first or last (`-core1first`, `-core1last`), identical in all three. With `+r_fast 1` (main's Fast mode, whose 113 frames differ from r_fast 0) the 113 frames and the fhash shots are identical to the flags-off build with r_fast 1 too. A build with the cache cut to 64 entries (`QOG_VERTCACHE_MAX=64`, many collisions) is identical too. An integer FloorDivMod was checked against id's double version over 3.8 M pairs (0 differences) and then dropped, since main's `QOG_FLOAT_EXACT` has the same change.

Board RAM (`arm-none-eabi-nm`, merged with main 76d0172), quake_display `end` 0x2003f650 with the four flags off, 0x2003f660 on (16 B), quake_main 0x2003f51c (unchanged), 416 B above the display's 2 KB heap reserve. Stack, R_RecursiveWorldNode 48 B a level less, R_EmitEdge with R_QogProject 144 B against 96 B at the chain's end, R_ClipEdge 80 to 56 B (up to 4 deep).

### Flash misses in the spans

Fast mode cut 2.9 M cycles of arithmetic and the spans moved by 0.9 ms, which points at flash. Per lit pixel the spans read a texel and a colour map entry from flash (the colour map is `gfx/colormap.lmp` in the mapped pack, 16 KB, as big as the XIP cache) and the light grid from RAM. Exact options, in the order I would try them.
- `QOG_BOARD_TEXNOALLOC` and `QOG_BOARD_CMAPNOALLOC` (off), texels or the colour map read through the XIP no-allocate alias 0x11000000 (d_scanlit.c). Hits come from the cache, misses do not allocate, so streaming texels cannot push out the span code, the colour map rows in use and the light grid code. It can also be worse (a texel line read twice is fetched twice), so both are off until two A/B flashes with the xip line decide.
- Colour map rows in RAM, 64 rows of 256 B, no RAM for all of them. A cache of the rows a surface uses would need about 2 KB.
- Hot span loop in RAM, one mip variant of the exact drawer is about 2 KB, the display has 416 B. Only if the xip line shows misses the code causes and RAM comes back.
- Texture prefetch by DMA into RAM, no RAM to put a 4 KB tile in while the spans draw.
- Interpolator for the texel address (SIO interp0, lane 0 s, lane 1 t, base the texture), about 6 cycles a pixel of address arithmetic per core, about 0.6 ms a view, compute only. Not done.

### Picture-changing ideas (for the user, not done)

- Edge u stepping in 16.16 fixed point instead of float in R_EmitEdge, edge positions move by a sub-pixel, maybe 1 to 2 ms of world.
- Node culling with integer box tests against quantised planes, rare one-node differences, about 1 ms.
## 2026-11-24, Every episode in the split client's RAM, and level exits on the board client (PC and ARM build, registered data)

The board client's hunk goes from 73 to 75 KB and now holds every level of the game with at least 3.8 KB to spare on a fight tour, seven exact cuts take 7.4 KB off the worst level (e2m6, 78.4 to 71.2 KB) and free 1,656 B of static RAM for the hunk. Before them, a bug of the z-buffer receive buffer stopped the board client at every level exit. Nothing ran on hardware.

Bug found and fixed (`QOG_RECONNECT_FIRST`, both server halves, engine/sv_main.c), with the earlier `QOG_LINK_RX_ZTEMP` every `changelevel` stopped the board-flag client with "link, 16 bytes of messages wait while the view is drawn". SV_SendReconnect sent the reconnect message and then had the client run `reconnect` in the same exchange. The client's reconnect draws the loading plaque over the old level's view while that message waited in the z-buffer. The server now has the client run `reconnect` first. Nothing the reconnect runs reads the receive buffer and a delivery runs no client code, so the client does the same work. `qogtest_ad_chg.cfg` (map e1m1, changelevel e1m2) gives the single process's state hash on the board-flag pair, and stopped before. Every level exit in play is a changelevel, while the test scripts changed levels with `map`, which disconnects first (L70).

Cuts (all on the board client list in apps/quake/CMakeLists.txt, each behind a flag of its own).

| Cut | Flag | Saved on e2m6 |
| --- | --- | ---: |
| Beam segments of temp entities are only the drawn part of entity_t (40 bytes, not 88), they never get a network update or efrags | `QOG_TEMPENT_SMALL` (cl_tent.c) | 3,136 B of hunk with 64 segments |
| entity_t 88 to 76 bytes. Only statics have efrags and only R_StoreEfrags reads visframe, through them, so both share 8 bytes with the network state in a union. The bmodel being drawn keeps its top node in one variable (`ENT_TOPNODE`, set before its faces are drawn, read only then). A static entity is 48 bytes. R_RemoveEfrags of an emptied client entity is left out, its efrag was always NULL | `QOG_ENTITY_NOEFRAG`, `QOG_ENTITY_NOTOPNODE` (render.h, cl_main.c, r_efrag.c, one or two lines in r_main.c and r_bsp.c) | 2,600 B (13 blocks of 16 entities) and 440 B of statics |
| Hunk blocks get an 8-byte header (size and four letters of the name, no sentinel) and 8-byte rounding, not 16 and 16 | `QOG_HUNK_HDR8` (zone.c) | about 1 KB over about 110 blocks |
| An item box's four frame bit arrays share packed blocks instead of four 32-byte blocks | `QOG_ITEMBOX_PACKED` (model.c) | 960 B with 11 item boxes |
| Menu pic names point into the mapped pack's directory (as the mixer's sound names do), 12 bytes a slot, not 32. A pic not in the pack stops with id's "failed to load" | `QOG_CACHEPIC_NAMEPTR` (draw.c, common.c, common.h) | 1,120 B static |
| Light style values are shorts ((char - 'a') * 22 or 256 fits) | `QOG_LIGHTSTYLE_SHORT` (r_shared.h, r_main.c) | 512 B static |

Client hunk per level (hunk low at the end of each level of a `qog_tour_fight`, PC split pair with the board client flags, on that episode's board image `mkboard.py --stub-maps --main --display-no-sounds --display-no-progs`, KB). Before is main 76d0172's client with the fixed server. The tour now takes out level exits and the two dead teleporters first (L72). Tour numbers move by a block of entities from run to run (L64).

| Map | Before | After | | Map | Before | After | | Map | Before | After |
| --- | ---: | ---: | --- | --- | ---: | ---: | --- | --- | ---: | ---: |
| e2m1 | 65.0 | 60.5 | | e3m1 | 63.9 | 60.4 | | e4m1 | 60.2 | 57.4 |
| e2m2 | 61.8 | 58.5 | | e3m2 | 63.5 | 59.4 | | e4m2 | 65.4 | 61.8 |
| e2m3 | 67.5 | 63.8 | | e3m3 | 58.5 | 55.8 | | e4m3 | 72.2 | 69.1 |
| e2m4 | 70.7 | 66.3 | | e3m4 | 71.9 | 64.7 | | e4m4 | 71.6 | 66.9 |
| e2m5 | 70.1 | 63.4 | | e3m5 | 69.8 | 64.1 | | e4m5 | 78.4 | 71.0 |
| e2m6 | 78.4 | 71.2 | | e3m6 | 71.1 | 67.2 | | e4m6 | 74.0 | 67.2 |
| e2m7 | 72.9 | 69.0 | | e3m7 | 60.2 | 58.7 | | e4m7 | 70.1 | 66.3 |
| start | 53.1 | 52.2 | | | | | | e4m8 | 74.0 | 65.8 |
| | | | | | | | | end | 58.6 | 53.5 |

Episode 1 after (its own image, the same tour), start 52.2, e1m1 53.0, e1m2 58.6, e1m3 66.3, e1m4 67.0, e1m5 60.8, e1m6 62.6, e1m7 46.8, e1m8 53.8 (the earlier before, e1m4 70.4). Worst per episode after, 1 67.0, 2 71.2, 3 67.2, 4 71.0. The earlier whole-game figure (80.4 on e2m6) was taken on data/id1, where the PC keeps a 1,744-byte `packpos` block the board never has (L71). e4m5 and e4m8 were never toured before because of the teleport error, and e4m5 is as big as e2m6.

e2m6 by block, before and after (bytes), entities 18,512 to 15,912, temp entities 5,760 to 2,624, statics 4,752 to 4,312, item boxes 1,408 into the packed blocks (1,520 to 1,968), the rest only the smaller headers. Total 80,288 to 72,920.

Exactness (hidden, `-nosound`, `+_cl_color 0`, own data folder and link ports), on the board-flag split pair `qogtest_hash` md5 ce86f894f9de and `qogtest_f_hash` e92bc9e9650d, and the 121 frames (113 of `qogtest_d_shots` after `qogtest_k_quiet` and the 8 fhash frames) byte identical with the same pair built from main. `qogtest_ad_beam.cfg` (lightning gun on e1m1 and e2m6, 21 beam segments a frame, 14 frames) byte identical with the same state hash. The default pair and quakeog_pc give ce86f894f9de and e92bc9e9650d.

Board links (`cmake -B build-ad/arm --preset target -DQOG_AOT=ON -DQOG_SOUND=ON -DQOG_CL_STACK_KB=32 -DQOG_SV_HUNK_KB=160`, free is beside the 2 KB heap reserve).

| | main 76d0172, 73 KB | cuts, 73 KB | cuts, 75 KB (the new default) |
| --- | --- | --- | --- |
| quake_display `end` | 0x2003f650, 432 B free | 0x2003efd8, 2,088 B free | 0x2003f7d8, 40 B free |
| quake_main `end` | 0x2003f51c | 0x2003f51c | 0x2003f51c |

quake_display.bin 363,320 B. The client stack stays 32 KB, no frame on the deep path grew.

The e4m5 and e4m8 teleport error is a game data bug, not the port and not the script. In each map a `trigger_teleport` (e4m5 entity 508, model *95, at 816 2208 256, and e4m8 entity 400, model *75, at -456 808 32) targets `t178` and `t273`, which name only an `info_null`. QuakeC's `info_null` removes itself at spawn, so `teleport_touch` finds no target and calls objerror, a Host_Error ("Program error"). id's interpreter in one process (quakeog_pc without compiled QuakeC) stops the same way when the player is put on them (`qog_tour_spot 824 2216 296` on e4m5, `qog_tour_spot -448 816 64` on e4m8). The tour reached them by noclip, whether a player can reach them in normal play is not known. `start` has one more (to `t11`), never touched.

Not done, or to watch.
- Once a full episode 4 tour stopped the server half at `end` with "MOVETYPE_PUSH with a non bsp model" (world.c). Three tours of `end` alone (two with the cuts, one before) did not. The server half's code is main's apart from the reconnect order and the PC tour.
- Not approved, sizes on the board client, particles 512 to 256 9,216 B, console text 4 to 2 KB 2,048 B, command buffer 4 to 2 KB 2,048 B, edge-drawing surfaces 600 to 500 3,200 B of stack, zone 8 to 6 KB 2,048 B. None is needed now.
- Further exact candidates, not done, efrag_t as 8 bytes of 16-bit indices (1.3 KB on e2m6), the model table as parallel arrays (576 B static), the entity colormap as a byte (entities 72 bytes, about 1 KB on e4m5).
- Files shared with earlier work, engine/r_main.c (the light style array type and two `ENT_TOPNODE` lines), engine/r_bsp.c (two `ENT_TOPNODE` reads). With earlier work, engine/sv_main.c (SV_SendReconnect) and apps/quake/CMakeLists.txt.
- Test scripts (`qogtest_ad_chg.cfg`, `qogtest_ad_beam.cfg`, `qogtest_ad_tele5.cfg`, `qogtest_ad_tele8.cfg`) are in the branch's git-ignored data/id1, the tour runs in build-ad/tours.sh.

## 2026-11-24, Fast render mode and single precision front end math (PC and ARM build, registered data)

The user asked for a picture that loses a little, like MG24Quake's, but where the loss is hard to see. The board client now has a render mode switch, the `r_fast` cvar (archived), shown in the board Options menu as `Render  Exact / Fast` (engine/menu.c, item 20, left, right or gray toggles it). It is 1 by default on the board client (`QOG_BOARD_FAST`, on, adds `QOG_RFAST QOG_RFAST_ON` to the client's flags) and 0 in every other build. `r_fast 0` draws exactly what main draws. Nothing ran on hardware. The board savings below are estimates from the generated Thumb code and the PC's work counts, the lead measures them.

| Item | Where | Kind | Pixels changed (121 shots, view rows) | Estimated board saving per view at 250 MHz |
| --- | --- | --- | ---: | --- |
| a. Perspective divide every 8, 16 or 32 pixels, before each sub-span the drawer takes the longest one over which 1/z changes by at most 8% (`LITSPAN_FAST_ZREL`). Floors and ceilings (1/z constant along a row) and walls seen straight on take 32, walls at a steep angle close by keep 8 | engine/d_litspan.h `D_LITSPANS_FAST_BODY` | approximate | 1.49% alone (fixed 32 as MG24Quake, 14.3%, worst frame 38%. Fixed 16, 6.1%) | E1M1 fly-through, 3,866 sub-spans a view instead of 6,815 runs of 8, about 560 cycles each, less 140 cycles a span for the last sub-span's multiply. About 1.4 M cycles |
| b. Light blended exactly at both ends of each run of 8 and stepped linearly between (`QOG_LITSPAN_LINLIGHT`'s one-pass run). Textures of other sizes keep the exact light | same | approximate | 1.28% alone (runs of 16, 4.2% together with a and d) | pixel loop 21 instructions (objdump) against the exact two passes of 37 + 14, about 25 cycles a pixel with the blends, about 1.2 M cycles |
| c. `QOG_FLOAT_EXACT`, single precision where id wrote double but the float result is provably the same. R_EmitEdge's near clip compare (`x < 0.01` is `x <= (float)0.01`) and ceil (ceilf), R_RecursiveWorldNode's plane distances (only signs and compares with 0 or `(float)BACKFACE_EPSILON`), D_PolysetCalcGradients' ceil, FloorDivMod on the ints themselves in D_PolysetSetUpForLineScan, `1.0 / x` stored in a float in r_draw.c, r_alias.c and d_part.c (GCC already emitted a float divide there, MSVC did not) | engine/mathlib.h (QOG_FRECIP, QOG_CEILF), r_draw.c, r_bsp.c, r_alias.c, d_polyse.c, d_part.c | exact, both modes | 0, hashes identical | objdump, R_EmitEdge and R_RecursiveWorldNode now call no double routine (they called 4 f2d, 2 d2iz, 2 dcmp, 2 ceil, and 7 f2d, 2 dsub, 5 dcmp). Per fly-through view 588 ceils, 722 near clip compares, 1,112 frustum plane tests, 180 node sides, about 0.17 M cycles, 0.7 ms world. Alias, about 25 FloorDivMod calls a view at about 800 cycles, 0.1 ms vmodel |
| d. 0x10000 / zi from the integer divider, the mantissa's top 16 bits into 2^32 - 1, the exponent set directly, about 16 good bits (`litspan_frecip`). Zero, negative, tiny and huge values take the float divide | engine/d_litspan.h | approximate | 0.06% alone | a float divide (about 100 cycles) becomes about 50, at every sub-span and span start, about 0.3 M cycles |
| All of a, b, d (the shipped Fast mode) | | | **2.76%**, worst frame d_demo3_15 9.2% | about 2.9 M cycles a view of span work on the two cores, 11.6 ms of CPU, so spans 30.7 ms falls by about 6 ms (compute only) to about 9.5 ms (if the memory stalls shrink with the work), to roughly 21 to 25 ms |

Frame estimate with Fast on, 95.9 ms becomes about 85 to 89 ms, 11.2 to 11.8 fps. The picture loss, the changed pixels are texels moved by one at sub-span joins and light steps of one colormap row, the largest colour step 248 is one texel shifted on a high contrast edge. Side by side the frames look the same (`fast` folder below).

Variants measured on the PC (each its own build, `-DQOG_PC_EXTRA_DEFS`, 113 qogtest_d_shots frames and the 8 qogtest_f_hash shots against main).

| Build | Pixels changed | Worst frame |
| --- | ---: | --- |
| linear light runs of 8 only | 1.28% | d_spawn_e3m3 5.2% |
| integer reciprocal only | 0.06% | f_e4m3 0.38% |
| adaptive 8/16/32, ZREL 0.08, only | 1.49% | d_demo1_12 9.0% |
| fixed 16 | 6.14% | d_demo3_06 21% |
| fixed 32 (MG24Quake) | 14.27% | d_demo3_06 38% |
| shipped, adaptive 0.08, light runs of 8, reciprocal | 2.76% | d_demo3_15 9.2% |
| ZREL 0.04 (4,505 sub-spans a view, not 3,866) | 1.84% | d_demo3_19 7.3% |
| ZREL 0.15 (3,166 sub-spans a view) | 6.51% | d_demo3_15 24% |
| light runs of 16 | 4.17% | d_demo3_15 12.6% |
| light runs of 32 | 6.59% | d_demo3_19 19.6% |

`LITSPAN_FAST_ZREL`, `LITSPAN_FAST_LRUN` (0 keeps the exact light), `LITSPAN_FAST_SUB` and `LITSPAN_FAST_RECIP` are compile-time knobs in d_litspan.h.

Exact mode proof (hidden, `-nosound`, `+_cl_color 0`, own folders `build-aa-pc` and `build-aa-ref`, own git-ignored `data\id1`), against an untouched build of 8a3e4d8, `qogtest_hash` gives `ce86f894f9de` (MD5 of its two lines), `qogtest_f_hash` 32 hashes with MD5 `e92bc9e9650d`, and the 113 `qogtest_d_shots` frames and the 8 fhash frames are byte identical. The Fast runs keep the same `qogtest_f_hash` (rendering does not touch game state). A PC build with `QOG_SPANS_CORE1` draws the same Fast frames as one without, so the second core's half is right.

Code and RAM.
- d_scanlit.c. `D_DrawSpans8LitFast` is a function of its own. Both cores pick the drawer before the call (`D_LITSTEP`), so the Fast frame (376 B) never stacks on the exact one (432 B, unchanged). Core 1's chain grows by nothing.
- r_main.c. `r_fast` and `r_qogfast`, an int set once per view in R_RenderView_, so both cores and every surface of a view use one mode. r_local.h, two externs.
- quake_display after merging main (259905d). `end` 0x2003ee74, 20 B above main's 0x2003ee60 (the cvar 24 B and the int, less padding). text 362,836 B, the Fast drawer 3.3 KB. quake_main `end` 0x2003f564, unchanged (the server half gets neither flag's code).
- qogbench (apps/qogbench) prints a third line every 2 s for the lead. `[bench] rfast p2 exact=<us> rfast=<us> | short exact=<us> rfast=<us> | floor exact=<us> rfast=<us> | fdiv=<ns> frecip=<ns>`, the shipped exact drawer and the Fast drawer on the slanted plane, on game-like spans, on a floor seen level (where Fast takes 32), and one float divide against `litspan_frecip`.
- `QOG_RFAST_STATS` (PC measurement only) and `qog_rfaststats` print the Fast drawer's sub-spans by length, pixels, spans and the exact drawer's runs of 8 for the same spans. `data\id1\qogtest_aa_stats.cfg` in the branch's own data folder runs it over 400 fly-through views of E1M1.

Files both earlier work may touch, engine/r_bsp.c (one declaration block, two compares), engine/r_draw.c (a macro after the includes and one-line edits at each reciprocal, ceil and near clip), engine/r_main.c (the cvar and one line in R_RenderView_), engine/r_local.h (two externs), engine/d_scanlit.c (the drawer choice). Each edit is a few lines.

Looked at and left, or for the user to decide (ideas to get nearer 27 fps, none done).
- A coarser mip level (id's `d_mipscale` above 1, for example 1.5 in Fast) reads fewer texture bytes from flash, which is likely where the spans' memory stalls are. Blurrier distant walls, more visible than the items above.
- Fast mode's last sub-span of a span multiplies the gradients by its length (an int to float and three multiplies, about 140 cycles a span more than the exact drawer's cached products). A 32-entry cache costs 384 B of stack.
- The front end (world 26.3 ms) has no cheap approximation that stays hard to see, it is the BSP walk and edge setup, whose cost is mostly memory and exact float work. That is the earlier field.
- The view model (4.9 ms), an affine alias drawer is already affine. Skipping its per-vertex light or drawing it in fewer rows would show.
- 27 fps is a frame of 37 ms. The view alone is 75.7 ms today and Fast takes about 6 to 10 ms of it. Getting there needs the front end halved as well, or a smaller picture, which the user has not approved.
## 2026-11-24, Episode 1 in the split client's RAM, the display image without sounds, whole-game numbers (PC and ARM build, registered data)

The split client now runs start and all of episode 1 in its hunk, QOG_CL_HUNK_KB goes from 68 to 73 KB, from four exact cuts that free 10.6 KB of static RAM and hunk. `tools/mkboard.py --display-no-sounds --display-no-progs` leaves 2.86 MB of files the client never opens out of the display image, but episode 1 still needs 13.19 MB of the 11.94 MB region, and main's image with episode 1's sounds needs 5.75 MB of its 5 MB. Both fit if the data offsets move (proposed below, not applied). Main frees 4 KB and its server hunk goes from 156 to 160 KB, still short of e1m3 (165 KB without sound). Nothing ran on hardware.

Cuts (`QOG_ENTITY_TIGHT` and `QOG_CON_DEBUGLOG_DIRECT` in both flag lists, the other two on the board client list only).

| Cut | Flag | Saved |
| --- | --- | ---: |
| entity_t 104 to 88 bytes. frame is a short, effects, skin and trivial_accept are bytes, the ST_RAND start is kept as the 15-bit number id draws and turned into id's float where it is read (`ENT_SYNCBASE`). The network state goes last, so a static entity is only the first 52 bytes, its baseline is read into a scratch entity | `QOG_ENTITY_TIGHT` (engine/render.h, cl_main.c, cl_parse.c, r_alias.c, r_sprite.c) | 16 B a client entity, 52 B a static entity, about 4.5 KB of hunk on e1m3 |
| MSG_ReadString returns a string that ends in a 0 within 2047 characters in place in the message (every string a server writes). Any other ending (a 0xFF byte, the end of the message, 2047 characters) is copied the way id copies it, into 2 KB of the z-buffer after the status bar rows | `QOG_MSG_STRING_INPLACE` (engine/common.c) | 2,048 B static |
| The link's receive buffer (a reliable message and a datagram, 7,536 B) lives in the z-buffer after that, not on the hunk. It holds messages only from their delivery until CL_ReadFromServer has read them all, and no view is drawn in between. The server half sends messages only in its frame's answer and in a console line's answer, both read after any pipelined view and before the frame's own, and sound answers carry none. R_RenderView stops with an error if bytes still wait (`Link_RxCheckIdle`), so a break of that rule can never draw over a message | `QOG_LINK_RX_ZTEMP` (engine/net_link.c, r_main.c) | 7,536 B of hunk |
| Con_DebugLog writes its text directly (both callers pass "%s" and the text) | `QOG_CON_DEBUGLOG_DIRECT` (engine/console.c) | 1,024 B static |
| main only. The BSP frame sender wrapped as on the display, the same bytes in three writes | `QOG_LINK_STREAM_SEND` on quake_main | 4,168 B static on main |

The z-buffer between views now holds, after the 15,360 status bar bytes, MSG_ReadString's fallback (2,048 B), the receive buffer (7,536 B), and at its end the precache names of CL_ParseServerInfo (at most one message). Link_RxBuffer checks the room.

Client hunk per level (split PC pair, board client flags plus `QOG_CL_PEAKS`, from the episode 1 board images, KB). Script is the smallest `-heapsize` that runs `qogtest_y_mem_<map>.cfg` (the earlier fight, save and load on that map, deterministic). Tour is the hunk low at the end of each level of a `qog_tour_fight` of start to e1m8 (it varies by a block of entities between runs).

| Map | Script before | Script after | Tour before | Tour after |
| --- | ---: | ---: | ---: | ---: |
| start | 66 | 55 | 63.5 | 53.1 |
| e1m1 | 66 | 57 | 63.9 | 55.0 |
| e1m2 | 72 | 62 | 70.0 | 59.7 |
| e1m3 | 82 | 69 | 84.3 | 69.8 |
| e1m4 | 80 | 68 | 82.8 | 70.4 |
| e1m5 | 75 | 64 | 74.9 | 63.8 |
| e1m6 | 75 | 64 | 77.8 | 66.5 |
| e1m7 | 58 | 49 | 59.0 | 50.0 |
| e1m8 | 65 | 55 | 66.6 | 56.8 |

At `-heapsize 73` all nine scripts and the whole fight tour of start to e1m8 run.

Exactness (hidden, `-nosound`, `+_cl_color 0`, against untouched builds of e2854de in the same worktree, and again after merging main 259905d). `qogtest_hash` ce86f894f9de and `qogtest_f_hash` e92bc9e9650d (md5 of the statehash lines) on quakeog_pc, on the default split pair and on the split pair with the board client flags. `qogtest_r_fhash` 32 hashes on both pairs. 121 frames (113 of `qogtest_d_shots` after `qogtest_k_quiet` and the 8 fhash frames) byte identical, quakeog_pc against its base and the board-flag client against its base (one run showed a notify line in rows 1 to 23 of f_e1m3 at another moment, the rerun was identical). `qogtest_j_ep1.cfg` (9 hashes), `qogtest_j_menu.cfg` and the e1m3 script give the same hashes from the image with sounds and progs.dat and from the image without them. The receive buffer check never fired.

Board links (`cmake -B build-y/arm --preset target -DQOG_AOT=ON -DQOG_SOUND=ON -DQOG_CL_STACK_KB=32`, merged with main 259905d).

| | main 259905d | this branch |
| --- | --- | --- |
| quake_display `end` | 0x2003ee60, 2,464 B free | 0x2003f63c with a 73 KB hunk, 452 B free |
| quake_main `end` | 0x2003f564, 668 B free | 0x2003f51c with a 160 KB hunk, 740 B free |
| quake_display.bin | | 359,816 B |
| quake_main.bin | | 1,296,636 B, 41.2% of the slot |

Server hunk (smallest `-heapsize` of the server half for the same scripts, QOG_AOT, the board's server flags, run with `-nosound`, so the board needs 4 to 8 KB more for sound, earlier work measured 116 to 120 on e1m1), start 90, e1m1 112, e1m2 139, e1m3 165, e1m4 155, e1m5 154, e1m6 140, e1m7 88, e1m8 115. With 160 KB start, e1m1, e1m2, e1m6, e1m7 and e1m8 fit with sound, e1m4 and e1m5 only without it, e1m3 needs about 170. Hunk low after the script on other levels, e2m6 154.8, e4m5 167.2, e4m8 152.0, end 101.2.

Display data (`mkboard.py --stub-maps --display-no-sounds --display-no-progs`, start plus the episode, manifests of every level recorded in the branch's `build-y/manifests`).

| Set | Display image | Main image (with its sounds) |
| --- | ---: | ---: |
| episode 1, sounds kept | 16,845,808 B (16.07 MB) | 6,029,312 B |
| episode 1, no sounds | 14,224,368 B (13.57 MB) | 6,029,312 B |
| episode 1, no sounds or progs.dat | 13,831,152 B (13.19 MB) | 6,029,312 B (5.75 MB) |
| episode 2 | 14,654,720 B (13.98 MB) | 6,262,784 B (5.97 MB) |
| episode 3 | 13,812,656 B (13.17 MB) | 6,246,400 B (5.96 MB) |
| episode 4 and end | 16,237,888 B (15.49 MB) | 6,967,296 B (6.64 MB) |
| whole game | 39,819,120 B (37.97 MB) | 15,261,696 B (14.55 MB) |

Proposed offsets (not applied. `platform/qog_flashmap.h`, `tools/mkdata.py` and mkboard's constants change together, the bootloader's 128 KB stays).
- Display data at 0x100000. The app slot becomes 913,408 B (the app is 359,816 B), the data region 15,663,104 B (14.94 MB). Episodes 1, 2 and 3 fit one at a time (1.75, 0.96 and 1.77 MB spare), episode 4 is 574,784 B over. At 0x200000 (13.94 MB) episodes 1 and 3 fit, episode 2 is 40,192 B over.
- Main data at 0x200000. The main slot becomes 2 MB (the app is 1.30 MB), the region 6 MB up to the FAT volume. Episodes 1, 2 and 3 fit (256, 28 and 44 KB spare), episode 4 is 675,840 B over. Today main's 5 MB do not hold episode 1 with its sounds (786,432 B over).

Client hunk per episode after the cuts (worst level, tour or script), episode 1 70.4 KB (e1m4), episode 2 80.4 (e2m6), episode 3 73.9 (e3m4), episode 4 76.1 (e4m6, e4m5 and e4m8 stop the tour with a QuakeC teleport error, their scripts 73.4 and 69.0). With about 1.5 KB of margin episode 1 needs 73 KB, episode 3 76, episode 4 78, episode 2 and the whole game 82, 9 KB more than the board has.

Not approved, for the user (sizes on the board client).
- particles 512 to 256, 9,216 B (closes the whole game alone)
- console text 4 KB to 2 KB, 2,048 B, and the command buffer 4 KB to 2 KB, 2,048 B
- edge-drawing surfaces 600 to 500, 3,200 B of stack (the e1m8 tour peaks at 509)
- zone 8 to 6 KB, 2,048 B (earlier work saw 6 KB fail at level loads)

Level swap (estimates, nothing built). A level is about 1.1 MB of display data on average and 1.6 MB at most (start), plus 0.2 to 0.5 MB on main. Main cannot hold the whole game (14.55 MB of server data alone against its 5 MB region and the FAT volume's 8 MB), so the next level comes from the PC companion over USB, or one episode is written at a time. The display must stop running from flash while it erases and programs, and with 452 B of RAM spare it takes 4 KB sectors one at a time from main over the link. For 1.6 MB the link at 12.5 Mbaud takes about 1.4 s (1.1 MB/s after framing, wire to stream 93% on the PC), 64 KB block erases 25 x 150 ms typical 3.8 s (up to 50 s worst case by the datasheet), page programs at about 0.5 MB/s 3.2 s, and USB full speed CDC at about 1 MB/s 1.6 s overlapping the link. About 6 to 9 s per level typical, an episode at a time (14 MB) about 70 to 90 s.

Not done, or to watch.
- The one-byte z-buffer's edge fallback ran 4 times in an episode 1 fight tour on the PC (3, 3 and 1 in episodes 2, 3 and 4, at most 341 edges). On the board each stops the game (`QOG_ZEDGE_STACK_GUARD`). Open before, not changed here.
- The server hunk was measured without sound because PC tests run with `-nosound`.
- The receive buffer and the in-place strings are on the board client list only, the single-process PC game keeps id's buffers.
- Test scripts (`qogtest_y_mem_<map>.cfg`, `qogtest_y_manifest*.cfg`) are in the branch's git-ignored `data/id1`.

Episode 1 images for the board once the offsets move (today the display image is 1.31 MB too big and main's 0.75 MB).

```powershell
python tools\mkboard.py --pak data\id1\pak0.pak --pak data\id1\pak1.pak --manifests build-pc-dev\manifests `
    --maps start e1m1 e1m2 e1m3 e1m4 e1m5 e1m6 e1m7 e1m8 --exe build-pc-dev\Release\quakeog_pc.exe `
    --stub-maps --main --display-no-sounds --display-no-progs -o build-board\qb
```

## 2026-11-24, 250 MHz and 125 MHz flash for the Quake apps (ARM build, not run on hardware)

The user approved both clock options of the earlier table below, clk_sys 200 to 250 MHz and the QSPI flash divider 4 to 2, on both chips. They are set at run time by the two Quake apps only (port/rp2040/clock_rp2040.c, called from sys_rp2040.c right after `board_init()`). The BSP is not changed, its 200 MHz, 1.15 V and flash CLKDIV 4 stay the defaults of every other app and of the display bootloader, which must keep working at its settings because the display CPU has no BOOTSEL button. A per-target `FWOG_SYS_CLK_KHZ` would not reach `board_init()` (it is compiled once into the fwog_display_bsp and fwog_main_bsp libraries), so the apps change the clock after it.

| Step (in order) | What and why |
| --- | --- |
| Voltage | 1.20 V (`QOG_SYS_MV=1200`), set before the clock with the BSP's 10 ms settle. The BSP's `fwog_vreg_mv_for_khz` clamps at 1.15 V on purpose (200 MHz at 1.15 V is the rated point). 1.20 V is the lowest step widely reported stable at 250 MHz on RP2040 boards (many run 250 MHz at the default 1.10 V, 1.25 V and 1.30 V are what 270 to 300 MHz overclocks use). One step above the BSP keeps the extra power on a battery board small. `-DQOG_SYS_MV=1250` if a board proves unstable |
| Clock | `set_sys_clock_pll` with the VCO 1500 MHz, post dividers 6 and 1 that `check_sys_clock_khz(250000)` finds. clk_peri follows (board header), clk_ref and the 1 MHz timer tick do not change |
| Re-derived after it | display, the backlight PWM divider (board.c's 10 kHz, wrap 255) and the I2C1 rate (`i2c_set_baudrate`, charger, RTC, accelerometer, expander). main, the FPGA clock on GPOUT1. Its divider is raised for 250 MHz before the switch (20 MHz at 200 MHz, never above the gateware's 25 MHz) and set again after (250 / 10 = 25 MHz) |
| Flash divider | clk_sys / 2 = 125 MHz, from RAM, interrupts off, before core 1 starts, the same way on both chips. The part is the W25Q128JVPIQ (bsp/main_cpu/fs/fs_geom.h), rated 133 MHz for the quad I/O fast read 0xEB that boot2_w25q080 sets up (continuous mode 0xA0, 8 address and mode nibbles, 4 dummy clocks). Div 2 is tried only when clk_sys / 2 is at most 133 MHz |
| RX sample delay | 1 system clock, what the SDK's boot2_w25q080.S writes for every divider including 2 (its comment, with CLKDIV 2 data is captured as the next SCLK edge launches). That is also what runs on overclocked Picos at 125 to 133 MHz flash. If 1 fails the check, 2 is tried |
| Check and fallback | the first 128 KB of the app's own image are hashed (FNV-1a over words) through the uncached alias 0x13000000 at the old divider with interrupts on, then twice at the new one with interrupts off. Any difference writes the old divider and delay back before one instruction is fetched from flash again, and the DIAG line says fallback |
| Main's saves | the SDK's flash erase and program end by running boot2 again, which writes CLKDIV 4 back. `_open`, `_write` and `_close` of the FAT volume call `qog_flash_fast_reapply()`, which puts the checked divider back (a register read when nothing changed) |

The routine that switches the divider was disassembled in both ELFs (`flash_hash`, `ssi_set`, `flash_try.constprop.0`, all at 0x200000c0 to 0x2000017a on the display, 0x20000120 on main, in RAM). Between `cpsid i` and `msr PRIMASK` there are only `bl` to the other two RAM functions, stores to the SSI at 0x18000000 (SSIENR +8, BAUDR +0x14, RX_SAMPLE_DLY +0xF0) and loads from the 0x13xxxxxx alias that is being checked. No literal load or branch reaches 0x10xxxxxx. The flash veneers the linker made (`__flash_try.constprop.0_veneer`, `__ssi_set_veneer`) are only taken from flash code before the SSI is touched, and `ssi_set` returns to its flash caller after SSIENR is 1 again.

Everything derived from a clock, checked at 250 MHz.

| Item | At 250 MHz |
| --- | --- |
| Link UART, display app | `fwog_link_uart_init` at Host_Init, after the boost, 250 MHz / 16 / 12.5 Mbaud = 1.25, IBRD 1 FBRD 16, exactly 12.5 Mbaud |
| Link UART, main and the display bootloader at boot | main boosts before `fwog_display_update_run` (the display is still held in reset, the link is down), so display_update brings the link up at 250 MHz with IBRD 1 FBRD 16. The bootloader stays at 200 MHz, IBRD 1 FBRD 0. Both are exactly 12.5 Mbaud from their own clocks |
| LCD SPI | `st7789_init_begin` at VID_Init. `st7789_actual_spi_hz(250 MHz, FWOG_LCD_MAX_HZ)` = prescale 2, postdiv 2 = 62.5 MHz, the panel's limit. Scanout about 18.3 ms instead of 22.4 (estimate) |
| Core 1 scanout | DMA paced by the SPI, no cycle counts. Core 1 starts in VID_Init, after the switch |
| Speaker I2S PIO | `(clk * 4 / 11025) >> 8` = 354, 250 MHz / (64 x 354) = 11,035 Hz (0.09% fast, was 11,042 Hz). The play position rate is derived from the same numbers |
| WS2812 PIO | `ws2812_init` in game_main, after the switch, divider from clock_get_hz |
| I2C, backlight PWM | re-derived (above) |
| Timers, sleeps, the hang detector, QOG_PHASE_TIME | the 1 MHz tick from clk_ref (unchanged), `timer_hw->timerawl` |
| FPGA clock | 25 MHz (above) |
| USB, ADC | PLL_USB, unchanged |
| Hardcoded 200 MHz in port/, platform/, engine qog files | none found (grep for 200000, MHz, clock_get_hz, busy_wait, cycle counts). The only 200 MHz figure was a comment in snd_rp2040.c, updated |

Build options (apps/quake/CMakeLists.txt, both on by default, nothing else changes).

| Option | Default | Off |
| --- | --- | --- |
| `QOG_SYS_KHZ` | 250000 | `-DQOG_SYS_KHZ=0` keeps the BSP's 200 MHz and 1.15 V |
| `QOG_SYS_MV` | 1200 | 1150 to 1300 |
| `QOG_FLASH_FAST` | ON | `-DQOG_FLASH_FAST=OFF` keeps the divider the bootloader or boot2 set (4) |

RAM (`-DQOG_AOT=ON -DQOG_SOUND=ON -DQOG_CL_STACK_KB=32`, arm-none-eabi-nm), quake_display `end` 0x2003ee60, 228 B above main's 0x2003ed7c (196 B of RAM code, 36 B of state), so about 2.4 KB stay spare beside the 2 KB heap reserve. quake_main `end` 0x2003f564 (the same 232 B object). With both options off the display's `end` is 0x2003ed84. The PC build does not compile these files, and its `qogtest_hash` still gives `ce86f894f9de` (build-pcab, hidden, `-nosound`).

What the board log should say (COM8 display, COM7 main, each printed at the switch, usually before USB is up, and once more about 6 s after boot).

```
[quake] clock 250000 kHz at 1200 mV (BSP 200000 kHz), clk_peri 250000 kHz
[quake] flash 125.0 MHz ok (div 2 from 4, rx delay 1, 128 KB checked, reapplied 0)
```

and further on `[lcd] spi 62500000 Hz (clk_peri 250000000 Hz)`, `[link] uart0 12500000 baud (requested 12500000), clk_peri 250000000` and on main `[fpga] clk 25000000 Hz (clk_sys 250000000 / 10)`. Main's `reapplied` count grows after a save. A failed flash check prints `[quake] flash 62.5 MHz fallback (div 4, rx delay 1), read check FAILED at div 2` and the game runs on at 62.5 MHz. An unstable core at 1.20 V would show as hard faults, `Sys_Error`s or hangs that do not happen with `-DQOG_SYS_KHZ=0`, then try `-DQOG_SYS_MV=1250`.

## 2026-11-24, Menus for five buttons and no keyboard (PC and ARM build, registered data)

id's menus expect a keyboard and a network. The board client now builds them with `QOG_BOARD_MENU` (engine/menu.c), set only for the display chip's engine (`_cl_defs` in apps/quake/CMakeLists.txt). Off, menu.c preprocesses to the same tokens as before (checked with the ARM compiler's `-E`, plain and split client), and the default PC build's `qogtest_hash` still gives `ce86f894f9de` (MD5 of its two `qog_statehash` lines). Nothing ran on hardware.

| Menu | On the board |
| --- | --- |
| Main | Single Player, Options, Help, Quit. Multiplayer is gone (one board, no network, and the player name field went with it). The line is cut out of id's `gfx/mainmenu.lmp` (`M_DrawTransPicRows`) |
| Single Player | New Game, Load, Save and a fourth line, Levels, in id's menu font at twice its size (`M_PrintBig`, id has no picture for it) |
| Levels (new) | The maps the packs hold, base pack first, every `maps/<name>.bsp` in the pack directories except the item boxes `maps/b_*` (`COM_ForEachMap`, engine/common.c). So the list follows the data image mkboard writes, nothing is hardcoded. id's title next to each of id's maps (the `levels[]` table of the game options menu, now const). Pages of 16, up and down move, left and right turn pages, gray starts, green + blue goes back. Starting a level sends id's New Game commands through Cbuf. `disconnect` if a game runs, `maxplayers 1`, `deathmatch 0`, `coop 0`, `map <name>` |
| Start a new game? (new) | id asks with `SCR_ModalMessage`, which waits in a loop of its own for Y or N. The board has neither key and reads its buttons only in the main loop and the link wait, so that loop would never end. The board asks inside the menu, gray yes, green + blue no. New Game and Levels both ask while a game runs |
| Options | Controls, Reset to defaults, Screen size, Brightness, Sound Volume, Always Run. "Go to console" is gone (no keyboard), and so are Mouse Speed, CD Music Volume, Invert Mouse, Lookspring, Lookstrafe and Video Options (no mouse, CD or video modes on the board) |
| Controls (was Customize controls) | The buttons in the game (mapping v4 of platform/qog_input.c) and in menus (vid_rp2040.c `menu_keys`), in id's menu font under id's title picture. Green + blue leaves it |
| Quit | gray yes, green + blue no. id's jokes ask for Y and N, so the board shows its own box, quit and stop the game, then hold red for 6 s to switch the board off |
| Load, Save, Help | id's, the buttons already drive them |

No menu left takes typed text, so no letter picker was needed. The chat line (`messagemode`, key_message) is reachable only through a key binding the buttons cannot press.

RAM. Leaving out the network menus (multiplayer, setup, net, serial, modem, LAN, game options, search, server list) and id's key binding page takes their tables with them, the mission pack level tables, the episode tables, the quit jokes, the bind names, the player colour tables (512 B) and the name fields. The kept level table and every new table are const, in flash. The new state is two ints (Levels cursor, which menu asked to confirm).

| quake_display (`-DQOG_AOT=ON -DQOG_SOUND=ON -DQOG_CL_STACK_KB=32`) | Before (e2854de) | After |
| --- | ---: | ---: |
| `end` (arm-none-eabi-nm) | 0x2003f5a0 | 0x2003ed7c (2,084 B lower) |
| menu.c RAM (nm, B and D symbols) | 2,657 B | 592 B |
| text | 376,044 B | 357,884 B |

quake_main is unchanged (the server half does not get the flag).

PC verification (hidden, `-nosound`, own build folders `build-pcx` and `build-pcx-bm`, own data folder `build-pcx\data\id1`, git-ignored). The test build adds the flag with `-DQOG_PC_EXTRA_DEFS=QOG_BOARD_MENU -DQOG_SPLIT_CL_EXTRA=QOG_BOARD_MENU`, which also registers `qog_key <key> ...` (port/pc/vid_pc.c, test builds only), it presses and releases keys, so a script drives the menus as the buttons do.

- `qogtest_x_menu.cfg`, every new page and prompt, 16 screenshots. New Game asked and answered no, the Levels pages (38 maps from the registered paks, 3 pages), a level confirm answered no, Options, Controls, the volume slider, the quit prompt answered no and then yes, which quit the game.
- `qogtest_x_level.cfg` (single process), Levels, e1m3, yes. The game loaded "the Necropolis". `qogtest_x_newgame.cfg`, New Game, yes, loaded "Introduction" (start).
- `qogtest_x_level_split.cfg` on the split pair (`quakeog_cl` with the flag, `quakeog_sv`), the same level start crossed the link and the client entered "the Necropolis". The script has to end with the key that starts the level, because the menu's Cbuf text runs after the rest of a script (L52), so the pair was stopped by the 40 s timeout afterwards.

To watch on the board, the Levels page lists what the data image holds, and today the client hunk fits only e1m1, so picking start (or any map past the hunk) stops the board with a hunk error until the earlier work lands. Quit ends in `Sys_Quit`, which on the display is `Sys_Error("quit")`, the picture freezes with the power-off still alive.

## 2026-11-24, Sound for the split game, mixed on main and played by the display (PC and ARM build, registered data)

The split game has sound. id's mixer (snd_dma.c, snd_mix.c, snd_mem.c) runs on the main chip for the display chip's sound calls, and the display plays the 8-bit samples it gets back on the I2S amplifier through PIO and DMA. On the PC the mixer of the split pair paints exactly what the single-process game paints for the same scripts, and the state hashes stay the same. Both board apps link with sound on. Nothing ran on hardware and nothing was played on this PC, every PC run mixed silently.

Why mix on main (numbers before this work, from the briefing and the builds), id's sound needs channels 6,656 B, a paint buffer 4,096 B, known_sfx 34,816 B (512 x 68), cache headers about 80 B per sound and an output ring. The display had 560 B free (344 B after the earlier options) and every chip-side choice on it would cost the game hunk or the frame buffer. The client's sound calls are few and small, the server half knows nothing of the listener, and the samples are 11 KB/s on a 1.25 MB/s link. So the mixer moves to main, the calls cross the link, and the display keeps only an output ring.

| Piece | Where | Flag |
| --- | --- | --- |
| The client's S_ calls become link items (`LK_SND`, operations `LKS_*` in engine/net_link.h). S_PrecacheSound, S_StartSound, S_LocalSound, S_Update and S_ExtraUpdate are exchanges like a console line, main runs them at once and its random sequence comes back with its state, so the random number S_StartSound draws (two identical sounds started in a frame) stays in the shared sequence. The rest are sent in order without waiting. Inside another exchange (and during the earlier pipelined screen update) an exchange is only queued, main runs it after that exchange in the same order, so it mixes the same samples | engine/snd_link.c (replaces snd_dma.c, snd_mix.c and snd_mem.c on the client), engine/net_link.c | `QOG_SND_REMOTE` (client) |
| What id's mixer read from client state comes with the calls, the view entity, and with S_Update the listener's leaf's ambient levels. The play position is the client's output's | engine/snd_dma.c | `QOG_SND_HOST` (server) |
| GetSoundtime takes id's own path for an output that counts its samples (`__sun__`'s SNDDMA_GetSamples) | engine/snd_dma.c | `QOG_SND_GETSAMPLES` |
| Main's "DMA buffer" is a ring of 2,048 8-bit mono samples, id's 8-bit output. Submit sends what the mixer wrote as `LK_SNDPCM` items between the update and its answer | port/common/qog_snd_host.c | |
| Sound names point into the pack directory in flash, so known_sfx is 8 bytes a sound (id 68). Names no pack has go in a 128-byte pool | engine/snd_dma.c, engine/common.c (`COM_FlashName`) | `QOG_SFX_NAMEPTR`, `QOG_MAX_SFX=256` (the registered game has 225 sound files) |
| Channels 104, not 128, 12 plus static sounds, and the whole game uses at most 99 (e2m6. e1m4 84, start 67, e1m1 26, measured with `-sndtrace` over every map) | engine/sound.h | `QOG_MAX_CHANNELS=104` |
| The paint buffer (id's 512 pairs, 4 KB) and the ring hold samples only while one update runs, so on main they are the lowest 6 KB of the server's stack array, used only between server frames from the link's items, where the stack is a few KB deep (interrupts have their own stack). The stack watermark starts above them. known_sfx (2 KB) takes the free half of main's SCRATCH_Y bank | port/common/qog_snd_host.c, port/rp2040/sys_rp2040.c | `QOG_SND_HOSTMEM`, `QOG_SND_BOARDMEM` |
| The display plays the samples, PIO runs the BSP's audio_i2s program at 200 MHz / (64 x 283) = 11,042 Hz (the BSP's integer divider), a DMA channel reads a 2 KB ring of signed bytes into the TX FIFO (an 8-bit FIFO write is replicated, so byte b goes out as b x 257 on both channels), and a second DMA channel chained from it rewrites the first's read address, so the ring loops with no interrupt and no CPU work. The play position is the read address, the pass of the ring comes from the timer (same crystal). While the link waits, played samples nothing replaced are silenced, so a busy server leaves silence, not a 186 ms loop. Sys_Error stops the bit clock | port/rp2040/snd_rp2040.c, port/rp2040/link_rp2040.c | `QOG_SND_REMOTE` |
| main's data image carries every sound of the display's pak, already 11025 Hz 8-bit mono (mkpak's conversion of the eight 22 kHz sounds was checked against id's ResampleSfx with loadas8bit, byte for byte, and end to end below) | tools/mkboard.py `--main` (`--main-no-sounds` leaves them out) | |
| The board option, on by default. Off builds both halves with -nosound as before | apps/quake/CMakeLists.txt | `QOG_SOUND` |

A bug found on the way and fixed. `Hunk_AllocPacked` (`QOG_HUNK_PACKED`) cleared the bytes it grew into before moving the cache out of them, so a cache block lying there lost its header and the move freed a NULL user. With sound on, the PC game crashed at the first map load (the sound headers sit right above the low hunk), on the base commit too. It now grows, moves the cache, then clears, as Hunk_AllocName does. Sound-off runs are unchanged.

A lesson about id's mixer, the paint buffer size is part of its output. `SND_PaintChannelFrom8` paints each segment from the start of the paint buffer, so when a looped sound restarts inside a chunk its second segment lands at a spot that depends on the chunk boundaries. A 128-pair paint buffer changed the start map's output. The board keeps id's 512.

PC verification (silent mixing only, `-hidden`, no audio device opened. `qog_sndhash` hashes every painted pair and every output sample in time order, `qog_sndrx` hashes what the client received. reference, the single-process `quakeog_pc` with `-sndmono8`, the board's 8-bit mono format, and `-heapsize 7000` so loadas8bit is on as on the board).

| Test | Reference | Split pair, server mixing |
| --- | --- | --- |
| `qogtest_w_snd1.cfg` (e1m1, a fight with rockets, lightning and nails, save, load, map start. 1,744,704 samples) | 4 state hashes, 4 paint and output hashes | identical state hashes (rand calls 539, 20824, 21668, 22669), identical paint and output hashes, the client received exactly the output |
| the same from the two board images (client `-basedir build-w\qb`, server `-basedir build-w\qb_main -flashbase 0x10300000`, names in flash, the converted lightning sounds in place) | | identical |
| the same with the earlier pipelined client (`QOG_LINK_PIPELINE`, 2,832 updates queued, 0 sound starts queued) | | identical |
| `qogtest_w_demo.cfg` (map start, demo1, demo2, demo3. 2,509,289 samples) | 4 hashes, the same on two runs | identical |

The default builds of this branch, sound off, against an untouched build of main (9e41be9), hidden, `-nosound`, `+_cl_color 0`, `qogtest_hash` 2 state hashes, `qogtest_f_hash` 32 hashes, `qogtest_d_shots` after `qogtest_k_quiet` 113 frames plus the 8 fhash frames, all identical, and the split pair (`-ServerExe`) gives the same 2 hashes and 32 `qogtest_r_fhash` hashes as the single process.

Server hunk with sound, from the board images, the board's server flags and compiled QuakeC (`qogtest_t_mem_*.cfg`, fight, save, load), e1m1 needs 120 KB (116 fails, 116 runs without sound), start runs in 100 KB. Sound adds 5.5 KB of hunk (channels 5,408 B, the name pool, headers) plus up to 8.4 KB of sound headers in the cache, which the hunk gives back under pressure. `QOG_SV_HUNK_KB` stays 156.

Board links (`cmake --preset target -B build-final -DQOG_AOT=ON`, everything else default, `-DQOG_CL_STACK_KB=32` is the new default with sound).

| | Before (main 9e41be9) | With sound |
| --- | ---: | ---: |
| quake_display RAM free (less the 2 KB heap reserve) | 344 B | 628 B |
| quake_display stack array | 34 KB | 32 KB |
| quake_main RAM free | 1,616 B | 880 B |
| main SCRATCH_Y | 2 KB interrupt stack | + known_sfx 2 KB |
| main data image (start, e1m1) | | pak 2.06 MB with 124 sounds (1.64 MB), image 0.69 MB, 2.69 MB of 5 MB |
| quake_main app | | 1,309,940 B, 41.6% of the slot |

Display stack. `tools/stack_chain.py --root game_main` gives 44,144 B as an upper bound with every impossible path, 1.4 KB below the earlier 45,520 B for main (the client's sound code is gone). A sound exchange inside S_ExtraUpdate in the renderer adds about 1.1 KB (Link_ClientWait, two poll_rx frames of 344 B). The board watermark showed 5.4 KB spare at 34 KB before, so about 2.3 KB should remain at 32 KB. The display prints its free stack over USB with `QOG_LINK_DIAG`.

Not done, or to watch on the board.

- Latency, an update is mixed `_snd_mixahead` (0.1 s) ahead of the display's play position. With the pipelined client, updates from the screen update are queued and mixed after the server frame, and their samples reach the display at its next link wait. If the sound breaks up, `_snd_mixahead 0.15` (archived, at most 0.18 with the 2 KB ring) gives more room.
- The 22 kHz sounds play as 11 kHz 8-bit, exactly as id's loader made them with less than 8 MB of memory. The amplifier's level is the `volume` cvar (0.7).
- The display's flash image still holds the sounds, which the client no longer opens. Leaving them out would free about 1.6 MB of the display's flash for more maps (a later mkboard option).
- Display core 1 is not used by sound. Link frames are unchanged, sound travels as stream items (`LK_SND` 23, `LK_SNDPCM` 24, `LK_SNDDONE` 25), no new frame types.
- New test scripts are in the branch's git-ignored `build-w\data\id1` (`qogtest_w_snd1.cfg`, `qogtest_w_demo.cfg`, `qogtest_w_statics.cfg`), not in the shared data folder.

Build and flash (one CPU in BOOTSEL at a time, from the repo root).

```powershell
python tools\mkboard.py --pak data\id1\pak0.pak --pak data\id1\pak1.pak --manifests build-pc-dev\manifests `
    --maps start e1m1 --exe build-pc-dev\Release\quakeog_pc.exe --stub-maps --main -o build-board\qb
python third_party\wiliOGbsp\tools\fw.py flash quake --uf2 build-board\qb_main.uf2
cmake --preset target -DQOG_AOT=ON -DQOG_AOT_PAK=data/id1/pak0.pak -DQOG_SOUND=ON -DQOG_CL_STACK_KB=32
cmake --build build --target quake_main
python third_party\wiliOGbsp\tools\fw.py flash quake --uf2 build\apps\quake\quake_main.uf2
```

The display's data image is unchanged by this work, reflash `build-board\qb.uf2` through the display's BOOTSEL drive only if the maps changed. Main's data image must be rewritten, it now holds the sounds.

## 2026-11-23, Board speed, timing, both chips and both cores at work (PC and ARM build, registered data)

Four board-only changes to the split game, all options in apps/quake/CMakeLists.txt and all on by default. Three are exact by construction and one keeps id's order of everything the halves exchange. Nothing ran on hardware, the gains below are reasoning until the coordinating session flashes the board and reads the new timing lines.

| Change | Flag (CMake option) | Exact | What it hides or splits | RAM |
| --- | --- | --- | --- | --- |
| Frame phase times over USB every 2 s on both chips | `QOG_PHASE_TIME` (`QOG_BOARD_TIMING`) | measurement only | | 160 B per half |
| The next frame's input, link exchange and parsing run during the 22.4 ms LCD scanout | `QOG_VID_LOCKWAIT` (`QOG_BOARD_LOCKWAIT`) | yes, same pixels | up to 22.4 ms a frame | 1 B |
| Core 1 of the display chip draws every second span of each lit surface | `QOG_SPANS_CORE1` (`QOG_BOARD_CORE1`) | yes, same pixels | about half of the lit span time | 12 B (job slot in platform/qog_video.c), core 1 chain about 520 B of its 2 KB stack |
| The client draws frame k while the server half runs frame k+1 | `QOG_LINK_PIPELINE` (`QOG_BOARD_PIPELINE`) | state hashes and frames identical on the PC | the server's frame and the link round trip | about 48 B |

RAM (`arm-none-eabi-nm`, `__HeapLimit - __end__` less the 2 KB heap reserve, QOG_AOT), quake_display 552 B free with the four options off, 344 B with them on. quake_main 1,776 B free off, 1,616 B on (the timing). Sizes unchanged, client hunk 68 KB, stack 34 KB, server hunk 167 KB, server stack 24 KB. Deepest client chain (`tools/stack_chain.py --build build --lib apps/quake/libqog_engine_cl.a --root game_main`, an upper bound) 45,480 B off, 45,520 B on. D_DrawSpans8Lit gains a 40 B wrapper frame above the 432 B drawer. Flash +3.6 KB on the display.

How each works.
- Lock wait. id's code calls VID_LockBuffer before it draws into the frame (D_EnableBackBufferAccess at the top of SCR_UpdateScreen, draw.c, menu.c). With the flag, quakedef.h makes it a port function, vid_rp2040.c's VID_Update hands the frame to core 1 and returns, and the next VID_LockBuffer waits for the scanout. The z-buffer temporaries that parsing uses (`QOG_PRECACHE_ZTEMP`, `QOG_LOADGAME_ZTEMP`) already stay clear of the status bar rows. The PC build with the flag hashes the 64,000 scanned bytes at VID_Update and again at the next lock (`qog_lockcheck`, printed at quit), 0 changes in 3,549 scanouts of qogtest_d_shots and 3,512 of qogtest_r_fhash.
- Core 1 spans. D_DrawSurfaces sets a lit surface up on core 0 as before, then core 1 draws spans 2, 4, 6 and core 0 spans 1, 3, 5, and core 0 waits for core 1 before the next surface. A span writes only its own pixels and z, and the drawer reads only the surface's state, which nothing changes until both are done. platform/qog_video.c runs the job in core 1's idle loop between scanouts (spans are drawn only after the lock wait). A future core 1 hook (sound) must return quickly or each surface waits for it. On the PC the half runs on a real second thread (default), or on the game's thread before (`-core1first`) or after (`-core1last`) core 0's half, 113 qogtest_d_shots frames byte identical in each mode, three thread runs in a row.
- Pipeline. A frame of play (signed on, not a demo, key_game, no intermission, pause or loading plaque) leaves SCR_UpdateScreen, S_Update and CL_DecayLights pending. The next frame takes its input, sends its tick and pushes it onto the wire (Link_ClientTick, qog_link_push), runs the pending update while the main chip runs its server frame, then waits for the answer (Link_ClientDone). The update runs with the realtime, host_frametime, host_framecount, cl.viewangles and key_dest of its own frame. V_DriftPitch, the only part of the update that changes what the next input sends, runs at the frame's end (V_CalcRefdef skips it in the update). A console line other than `wait` runs the pending update first (Cbuf_QogIdle), so qog_shot, server commands and level changes keep id's order. The render calls no rand(). Key presses on the board are read before the update, but key_dest is restored for it, so a menu appears in the same frame as before. While the client draws, the server's answer waits in the UART flow control, and a short answer may be resent once or twice by the link's timeout (harmless duplicates).

PC verification (hidden, `-nosound`, own basedir, against a build of 1371717 in `build-pc-base`).

| Run | Result |
| --- | --- |
| This commit, all new flags off, quakeog_pc qogtest_hash, qogtest_f_hash, qogtest_k_quiet + qogtest_d_shots | 2 + 32 hashes identical, 8 + 113 frames byte identical |
| Same build, split pair, qogtest_hash, qogtest_r_fhash, qogtest_d_shots | identical to base |
| Split client with `QOG_LINK_PIPELINE`, `QOG_VID_LOCKWAIT`, `QOG_SPANS_CORE1` and `QOG_PHASE_TIME`, server with `QOG_PHASE_TIME` | qogtest_hash 2 hashes (rand calls 539 and 1990), qogtest_r_fhash 32 hashes and 8 frames, qogtest_d_shots 113 frames, all identical. 397 of 400 and 3,392 of about 3,500 frames were pipelined, the rest flushed for console lines |
| quakeog_pc with `QOG_VID_LOCKWAIT` and `QOG_SPANS_CORE1` | qogtest_hash, qogtest_f_hash and qogtest_d_shots identical in all three core 1 modes |

On the PC the server's frame is far below a millisecond, so the pipeline shows only in the link wait, the client waited 8.52 s in lockstep and 3.02 s pipelined over the qogtest_r_fhash run (2.4 to 0.9 ms a frame at the PC link's 300 KB/s).

Expected on the board (estimates, the timing lines will replace them).
- Lock wait, saves the part of the 22.4 ms scanout that the next frame's input, link wait and parsing cover, all of it in lockstep when the server and link take 20 ms or more. With the pipeline on, the link wait mostly moves under the render, and the lock wait then covers only parsing and input (a few ms).
- Pipeline, saves the server's frame plus the link round trip per frame (`[time sv] svframe` and `[time cl] link` show both). Lock wait and pipeline compete for the same slack, together they hide about max(scanout, server frame), not the sum.
- Core 1, earlier work estimated the E1M1 lit spans at about 25 ms a frame (48,400 pixels at about 65 cycles plus 6,800 runs of float work). Half on core 1 saves up to about 12 ms. Texture and colormap reads that miss the shared 16 KB XIP cache are served one core at a time by the QSPI, so 6 to 10 ms is the likely range.

Console format (display chip COM8 `[time cl]`, main chip COM7 `[time sv]`, every 2 s, mean ms per frame with one decimal).

```
[time cl] frames <n> fps <x.y> ms frame <ms> input <ms> link <ms> read <ms> screen <ms> view <ms> hud <ms> present <ms> scanwait <ms> sound <ms>
[time cl] views <n> ms setup <ms> world <ms> bmodels <ms> lmap <ms> grad <ms> spans <ms> zspans <ms> sky <ms> turb <ms> models <ms> vmodel <ms> parts <ms> total <ms> edgescan <ms>
[time cl] per view litsurfs <n> lmcells <n> litspans <n> litpx <n> runs <n> zpx <n> skypx <n> turbpx <n>
[time sv] frames <n> fps <x.y> ms wait <ms> svframe <ms> phys <ms> send <ms>
```

frame is a whole Host_Frame. link is the wait for the server half (Link_ClientWait only). screen is SCR_UpdateScreen and includes view (V_RenderView, total, the status bar repaint hud and the crosshair) and scanwait (the lock wait). The rest of screen is the 2D overlay. present is VID_Update. With the pipeline, frame k's screen is counted in frame k+1. The view line is earlier, world is the BSP walk and edge emission, edgescan the edge sorting and span making, models the alias models, spans the lit spans with their z (QOG_LITSPAN_ZFUSE). svframe is Host_ServerFrame, phys its SV_RunClients and SV_Physics, send SV_SendClientMessages, wait the time main waits for the client's tick.

Build and flash (the coordinating session).

```powershell
cmake --preset target -DQOG_AOT=ON -DQOG_AOT_PAK=<path to pak0.pak> -DQOG_SPLIT_MAIN_EMBED=quake_display
cmake --build build --target quake_main
python third_party\wiliOGbsp\tools\fw.py flash quake --uf2 build\apps\quake\quake_main.uf2
```

For an A/B measurement add `-DQOG_BOARD_PIPELINE=OFF`, `-DQOG_BOARD_LOCKWAIT=OFF` or `-DQOG_BOARD_CORE1=OFF` to the configure line and rebuild. `-DQOG_BOARD_TIMING=OFF` removes the report.

Looked at and left.
- DMA memset and memcpy of frame-sized buffers. The only per-frame ones are the edge cache memset (about 15 KB on E1M1, tens of microseconds) and the status bar tile repaint, which reads a pic from flash that DMA would not read faster. The `hud` phase now shows the repaint's cost.
- Hot loops in RAM. The benchmark found no gain for the span loop, and the display chip has 344 B left.
- Faster scanout without a clock change, chaining the line DMA across two channels would remove the gaps between lines, at most about 1.9 ms of the 22.4 ms (the wire alone is 20.5 ms at 50 MHz).
- The RP2040 interpolator could compute the texel address in the lit span loop (masked shift and add), exact, not tried.
- Unrelated finding, on the PC without `id1/qogbake.bin` in the basedir, quakeog_pc of 1371717 crashes in Cache_Free while loading e1m1's sprites.

Clock options (report only, nothing enabled).

| Option | What changes | Gain | Risks |
| --- | --- | --- | --- |
| clk_sys 200 to 250 MHz (`FWOG_SYS_CLK_KHZ` in bsp/common/clocks.h, both chips) | clk_peri follows clk_sys (`PICO_CLOCK_ADJUST_PERI_CLOCK_WITH_SYS_CLOCK`). The link UART divisor becomes 1.25 (IBRD 1, FBRD 16), still exactly 12.5 Mbaud. The LCD SPI becomes 250/4 = 62.5 MHz, the panel's `FWOG_LCD_MAX_HZ`, a scanout of about 18.3 ms instead of 22.4. Flash at CLKDIV 4 becomes 62.5 MHz. Everything derived from clock_get_hz moves with it (backlight PWM, WS2812 PIO, I2C, PDM, main's FPGA clock divider, which rounds up to stay at or under 25 MHz) | about 25% on CPU and flash bound work, 4 ms of scanout | an overclock (200 MHz at 1.15 V is the rated point), and `fwog_vreg_mv_for_khz` deliberately clamps at 1.15 V, so 250 MHz needs a voltage policy change (1.2 to 1.3 V is usual) on a battery board. Both chips must change together. 260 to 266 MHz is worse for the LCD (the SPI drops to clk/6, 44 MHz). The display bootloader brings the clock up first with its own build of the value |
| QSPI flash CLKDIV 4 to 2 (100 MHz at 200 MHz sys, `PICO_FLASH_SPI_CLKDIV` in bsp/boards/freewili_og.h, even values only) | XIP misses (538 ns per random byte today, 8.5 MB/s sequential) roughly halve. Textures, the colormap, the baked BSP and code are read through it. The W25Q128 is rated 133 MHz | likely 10 to 25%, the largest of the options for the renderer, also on main (traces through clip nodes in flash) | the bootloader's XIP setup stays in force for the app (the app's boot2 is skipped), so on the display chip the divider only changes with a rebuilt and reflashed display bootloader. A bootloader whose XIP fails at 100 MHz leaves the display CPU with no way back, since it has no BOOTSEL button. A safer route is for the app to switch the divider at run time from RAM code and check a known flash region, falling back to 4. The RX sample delay may need tuning at 100 MHz |

## 2026-11-23, The client half links on the display chip for E1M1 (PC and ARM build, registered data)
## 2026-11-23, The main chip's server data and saves for the split game (PC and ARM build, registered data)

The main RP2040 now has its own data image, written by the same `tools/mkboard.py` run as the display's, and the split game saves and loads on main's FAT volume. On the PC the split pair runs from the two images (the server image for the server process, the display image for the client) with the same state hashes, frames and save files as the single-process game on the full paks. `quake_main` links with 1,796 bytes of RAM to spare. Nothing ran on hardware.

Server data image (`mkboard.py --main`, details in docs/game-data.md).

| Maps | Region 0x10300000 to 0x107FFFFF (5 MB) |
| --- | --- |
| `start` to `e1m4` (the display image that fits, 12,471,904 B at 0x10400000, unchanged) | pak 420,880 B at 0x10300000 (`progs.dat`, `gfx/pop.lmp`, 68 model stubs), server image 1,955,424 B at 0x10370000, end 0x1054E000, 2,416,640 B (46.1%) |
| `start` and all of episode 1 | pak 422,432 B, server image 2,948,000 B (81 models), 3,407,872 B (65.0%) |

- `qog_bake -bakestubs -bakeserver` (writer only, engine/model.c) keeps each map's planes, nodes, leafs, clip nodes and hulls, submodels, entities and visibility, and an alias model's or sprite's `model_t`. Textures, texinfo, surfaces, edges, vertexes, mark surfaces and light are left out. The image's layout word is the build's XOR `QOGB_SERVER_LAYOUT`, which only `QOG_SPLIT_SERVER` takes. Each model file in the server's pak is a stub (132 bytes for a map, 16 for a model, `qogbake_mstub_t`). Nothing in the image points into a pak, so the PC bakes it from the display's full pak for the address `-bakebase` gives.
- The server also opens `gfx/pop.lmp`, without it the game is the shareware version, stops with "You must have the registered version to use modified games", and QuakeC would read another `registered` cvar.
- The PC maps its packs at another address with `-flashbase <address>` (port/pc/sys_pc.c), so the server process runs main's layout with `-basedir <out>_main -flashbase 0x10300000`.
- Flash layout check, the BSP puts main's FAT volume at flash offset 8 MB for 8 MB (`fs/fs_geom.h` `FWOG_FS_FLASH_OFFSET`) and links main apps at the start of flash (`fwog_main_app` adds no offset), as `platform/qog_flashmap.h` has it. A UF2 copy writes only its own sectors, so the app (at most 3 MB, checked post-build) and the server data UF2 (whole 4 KB sectors from 0x10300000) leave each other and the FAT volume alone. No change was needed.

Saves (`QOG_SYS_MAIN` in port/rp2040/sys_rp2040.c, `QOG_SPLIT_CLIENT` in engine/menu.c, engine/net_link.c, port/common/qog_link.c).

- Save and load already run on the server with id's stdio (`fopen`, `fprintf`, `fscanf`, `fgetc`). On main, newlib's system calls `_open`, `_close`, `_read`, `_write`, `_lseek` and `_fstat` now reach the BSP's FAT API (`fwog_fs_*`, library `fwog_main_fs`), so the engine's save code is unchanged. `./id1/s0.sav` is `quake/s0.sav` on the volume. One file at a time, as the BSP allows. The volume is mounted at the first open and never formatted by the game, a board without a volume says so on main's console and cannot save (the BSP's `bench` app has `fs format`).
- Flash write hazards, nothing in `quake_main` starts core1, and the BSP's disk driver writes one 4 KB sector per FatFs write with interrupts off and kicks the watchdog after each chunk of at most 8 sectors.
- The client's load and save menus get the list over the link, side frames 0x44 SAVES_ASK `[type][seq]` and 0x45 SAVES `[type][seq][12][12 x (found byte, 39-byte comment)]` (483 bytes) outside the stream (port/common/qog_link.h). The client asks between exchanges and again every 250 ms until the answer with its sequence number arrives (5 s at most). The server reads each slot as `M_ScanSaves` does, from its own files. `qog_saves` prints the list on the split client.
- The single-process game is unchanged, everything is behind `QOG_SPLIT_SERVER`, `QOG_SPLIT_CLIENT`, `QOG_SYS_MAIN`, `QOG_LINK_SCRATCH` or the bake writer's `-bakeserver`.

Server RAM.

- A server half gives a map none of the renderer's per-frame arrays (surface, node and leaf stamps, dlight bits, efrag heads and leaf keys, also for each item box map). The smallest server `-heapsize` for e3m5 with a fight, a save and a load went from 172 KB (the earlier number, reproduced) to 152 KB.
- the earlier 172 KB was not enough for every map, e1m3 with a fight, a save and a load needs 185 KB on the untouched build (the load's 8 KB line buffer fails). With the cut e1m3 needs 167 KB with the full paks and 165 KB with the server image, e1m4 157 (155), e1m5 156. `start`, e1m1, e1m2, e1m6, e1m7 and e1m8 run in 152 KB. Measured with new `qogtest_t_mem_<map>.cfg` scripts (copies of `qogtest_r_mem_e3m5.cfg`) on a QOG_AOT PC build with the board's server flags. `QOG_SV_HUNK_KB` is now 167, the worst of `start` and episode 1. Episodes 2 to 4 were not measured beyond e3m5.
- The link's two 2 KB rings move to the RP2040's SCRATCH_X bank on main (`QOG_LINK_SCRATCH`, set only on `qog_engine_sv`), core1 never starts there, so no core1 stack is linked in it.
- `quake_main` (`-DQOG_AOT=ON`, stand-in display image `qog_display` while `quake_display` does not link), .data 8,144 + .bss 249,964 + heap 2,048 B, 1,796 B of RAM left, SCRATCH_X 4,096 B, app 960,828 B (30.5% of the 3 MB slot). The FAT library adds about 8.9 KB of static RAM (FatFs volume and file sector buffers, its long file name buffer). Before the hunk and ring changes it overflowed RAM by 7,420 B.
- Stack, the deepest chain from `server_main` is 18,544 B of the 24 KB array (`tools/stack_chain.py --app quake_main --lib apps/quake/libqog_engine_sv.a --root server_main`). The save path from `Host_Savegame_f` is 5,072 B plus newlib's `fprintf`, which has no `.su` line.

PC verification (hidden, silent, `-nosound`, `+_cl_color 0`, against an untouched build of 7d619f3, `build-pc-base`, on the full registered paks).

| Test | Base, single process | Split pair from the two images (`build-board\qb`, `build-board\qb_main`) |
| --- | --- | --- |
| `qogtest_hash.cfg` | 2 hashes, rand calls 539 and 1990 | identical, on the interpreted build and on QOG_AOT with the board's flags and a 167 KB server hunk |
| `qogtest_t_fhash.cfg` (`qogtest_r_fhash` on `start` to `e1m4`, save name `t_f1`, fight, save, load) | 20 hashes, 5 frames | 20 hashes identical (also on QOG_AOT with a 167 KB server hunk), 5 frames byte identical, the save file byte identical to base's |
| `qogtest_t_saves.cfg` (save `s0` on e1m1 and `s5` on e1m2 after a fight, `qog_saves`, the load menu) | load menu frame | the list came over the link, the load menu frame byte identical, the saves only in the server's folder |

The single-process build of this commit (`quakeog_pc`) gives the same `qogtest_hash` (2 hashes), `qogtest_f_hash` (32 hashes, 8 frames) and `qogtest_d_shots` after `qogtest_k_quiet` (113 frames) as base, all byte identical.

Flash order (one CPU in BOOTSEL at a time, nothing was flashed here), the display data UF2 through the display's BOOTSEL drive, the server data UF2 through main's, then the app with `fw flash`, which pushes the display image and sends RUN.

- Edits outside new code, engine/model.c and model.h (bake writer and loader), engine/menu.c (`M_ScanSaves` in the split client, the only client-side file touched), engine/net_link.c, port/common/qog_link.c and .h, port/pc/sys_pc.c, port/rp2040/sys_rp2040.c, tools/mkboard.py, and in apps/quake/CMakeLists.txt only `qog_engine_sv`'s definitions and `quake_main`'s hunk and libraries.
- Not done, the board's FAT path was built, not run. The client opens no QuakeC, so `progs.dat` (0.39 MB) could leave the display's pak, which is not changed here. The `qogtest_t_*.cfg` scripts are in the worktree's git-ignored `build-board\full\id1`, not in the shared `data\id1`.

## 2026-11-23, The game split across both RP2040s, server on main, client on the display (PC and ARM build, registered data)

The server half (SV_*, QuakeC, physics, edicts) and the client half (CL_*, renderer, status bar, input) are now separate builds joined by a new net driver over the inter-chip link. On the PC the two halves run as two processes over TCP at the UART's speed and give the same state hashes and frames as the single-process game. On the board `quake_main` (server) links with 1,712 bytes of RAM to spare. `quake_display` (client) still overflows RAM by 118,024 bytes with the E1M1 hunk (136,456 with e3m5's). Nothing ran on hardware.

How the split works (engine/net_link.c, flag `QOG_NET_LINK` with `QOG_SPLIT_SERVER` or `QOG_SPLIT_CLIENT`).

- The link driver replaces the loopback driver with loopback semantics, a reliable message makes the sender wait until the receiver has read it (canSend), the receive buffers have the loopback's sizes and are read in place, and an overflow behaves as there.
- Lockstep. The client stops where id's Host_Frame ran the local server (`Link_ClientFrame`), sends what its frame produced and waits. The server half runs its frame and answers. Console lines the client does not know run on the server at once, inside the client's command buffer (`Link_ForwardCommand`, a synchronous exchange), so `map`, `load`, `changelevel`, `qog_statehash` happen at the same point as in one process. `qog_sv <line>` forces a line to the server.
- What id's server did to client state directly comes back as items the client carries out at the same point. `reconnect` (SV_SendReconnect), `connect local` (map, load), `disconnect` (load), Host_ClearMemory's client half, `scr_centertime_off`, Host_Error and Host_EndGame. A client that closes its connection stops the server, as CL_Disconnect did. The server's Con_Printf goes to the client's console. Text the server adds to the command buffer (QuakeC `changelevel`, `localcmd`) joins the client's, which is the game's only command buffer, so scripted waits keep id's order.
- Shared globals cross with every item that follows a change, the client's realtime, host_frametime and key_dest (SV_Physics pauses in menus), the server's host_frametime, `sv.active`, `sv.paused`, `sv.max_edicts`, `svs.maxclients`, the map name and noclip_anglehack. Every cvar set on one half is set on the other. The shared random sequence (qog_rand) stays one sequence, each side sends its call count and the other advances its generator to it, so QuakeC's random() and the particles draw exactly the numbers they drew in one process.
- In the client build `sv` and `svs` are that report (engine/server.h), and `Host_ShutdownServer` only clears `sv.active`, so id's client code (CL_LerpPoint, CL_KeepaliveMessage, menus) is unchanged. The server build has no console, renderer, sound, keys or menus (port/common/qog_split_sv.c keeps V_CalcRoll and a console that prints to the log and to the client).

Transport (port/common/qog_link.c, port/pc/link_pc.c, port/rp2040/link_rp2040.c), a reliable byte stream over the BSP's link frames `[0x7E][len16][payload][crc16]` with the BSP's CRC, Quake types 0x40 DATA, 0x41 ACK, 0x42 HELLO and 0x43 HELLOK (0x01 to 0x1F stay the bootloader's, 0x20 to 0x22 the breakout I/O's, which the display hands to `fwog_ioexp_link_handle`). One frame in flight, resent after a timeout with back off, duplicates acknowledged again, the decoder resynchronizes after a bad CRC, and a full receive ring sends no ack so the sender waits (with the UART's RTS/CTS below it). Board sizes 512-byte frames and 2 KB rings on both chips. On the main chip every wait kicks the watchdog, on the display every wait keeps `fwog_power_poll` running.

PC results (hidden and silent with `-nosound`, against an untouched build of 1c85a91, `build-pc-base`, link rate limited to 300,000 bytes per second).

| Test | Single process (base) | Split pair |
| --- | --- | --- |
| `qogtest_hash.cfg` | 2 hashes | identical, rand calls 539 and 1990 as in base |
| `qogtest_r_fhash.cfg` (copy of `qogtest_q_fhash` with its own save name, 8 maps, fight, save, load) | 32 hashes, 8 frames | all identical |
| `qogtest_k_quiet.cfg` + `qogtest_d_shots.cfg` (demos and `map dm6`) | 113 frames | all byte identical |
| `qogtest_hash.cfg` with `-linkcorrupt 40 -linkdrop 60` on both | | identical, 39 resends, 20 CRC drops |
| `qogtest_r_fhash.cfg` on QOG_AOT with the board's flags and link sizes | | identical to base |

The single-process build of this commit (`quakeog_pc`, all new flags off) gives the same 2 + 32 hashes, 8 + 113 frames as base.

Link traffic (wire bytes, framing and acks included, fhash run), per frame client to server average 61, server to client average 235. In 3,360 frames of play the busiest frame was 164 bytes up and 560 down. The worst burst is a level change or load, 5,241 bytes to the client in one frame (17.5 ms at 300 KB/s, 4.2 ms at the BSP's 12.5 Mbaud) and 1,237 bytes up. The lockstep adds the server's frame to the client's frame time, the halves do not yet run in parallel.

RAM of each half. Hunks are the smallest `-heapsize` of a PC build with QOG_AOT and the board's flags that runs the map, a fight, a save and a load (`qogtest_r_mem_e1m1.cfg`, `qogtest_r_mem_e3m5.cfg`). Single chip for comparison, 194 KB.

| | E1M1 | e3m5 (worst map) | Static RAM and stack on the board |
| --- | ---: | ---: | --- |
| Server (`quakeog_sv`, server zone 6 KB, load buffer 8 KB, `QOG_LOADGAME_STRSIZE`) | 130 KB | 172 KB | sv 11,360, BSP display update and link buffers 12,441, stack 24 KB (deepest chain 18,608 B, `tools/stack_chain.py --root server_main`) |
| Client (`quakeog_cl`) | 97 KB | 115 KB | video 145,920, stack 47 KB, engine statics 57,356, SDK and BSP 23,737, link 6,329 |

Board links (`cmake --preset target -DQOG_AOT=ON`).

- `quake_main` with `QOG_SV_HUNK_KB` 172 and `QOG_SV_STACK_KB` 24, links, .data 7,840 + .bss 250,352 + heap 2,048, 1,712 bytes of RAM left. App 884,092 bytes with a stand-in display image (`-DQOG_SPLIT_MAIN_EMBED=qog_display`, because quake_display does not link yet), 3 MB app slot.
- `quake_display` (client), RAM overflow 136,456 bytes with `QOG_CL_HUNK_KB` 115, 118,024 bytes with 97 (E1M1). Before the split the single chip build overflowed by 223,504. The client keeps the 47 KB stack array, the render chain is 47,584 bytes from R_RenderView (the merge coordinator's measurement) plus about 300 above it, and the earlier 48 KB exact edge fallback is not reserved (it would add about 41 KB to an overflow that is already 118 KB).

Main flash layout (platform/qog_flashmap.h), app with the embedded display image 0x000000 to 0x2FFFFF (3 MB, checked post-build), server data 0x300000 to 0x7FFFFF (5 MB), BSP FAT volume 0x800000 to 0xFFFFFF (untouched). Server-side lumps of `start` and episode 1 (entities, planes, visibility, nodes, clip nodes, leafs, models) are 2,167,292 bytes in the paks plus progs.dat 413,116, so the server's copy fits 5 MB, while a full copy of the display image (11.9 MB) does not. The server image (a pak of what the server opens and a bake for 0x10300000 without textures, lighting and surfaces, written by the same `tools/mkboard.py` run as the display's) is not written yet.

New engine flags (all off by default). `QOG_NET_LINK`, `QOG_SPLIT_SERVER`, `QOG_SPLIT_CLIENT`, `QOG_LOADGAME_STRSIZE`. Port flags. `QOG_SYS_MAIN`, `QOG_LINK_MAIN`, `QOG_LINK_CHUNK`, `QOG_LINK_TXRING`, `QOG_LINK_RXRING`. CMake. `QOG_SPLIT` (ON), `QOG_CL_HUNK_KB`, `QOG_SV_HUNK_KB`, `QOG_SV_STACK_KB`, `QOG_SPLIT_MAIN_EMBED` in apps/quake, `QOG_SPLIT_SV_EXTRA` and `QOG_SPLIT_CL_EXTRA` in pc.

- Edits outside new files, engine/host.c, host_cmd.c, cmd.c, cvar.c, net_main.c, net_none.c, server.h, sv_main.c, sv_user.c, world.c (each behind the flags), port/common/qog_mem.c (reports leave out the missing half), port/pc/sys_pc.c (server loop), port/rp2040/sys_rp2040.c (`QOG_SYS_MAIN`), tools/pctest.ps1 (`-ServerExe`, `-ServerArgs`), tools/check_app_size.py (`main`), and one new block at the end of pc/CMakeLists.txt and apps/quake/CMakeLists.txt.
- Not in the split builds, the single-process PC tools `qog_tour`, `qog_manifest`, `qog_fly` (stubs in port/pc/split_cl_pc.c), the server's `viewmodel`, `viewframe`, `viewnext`, `viewprev` (they write the client's model list), and the QOG_BAKE_WRITER.
- Known differences from one process, none hit by the tests, the order of client and server prints inside one exchange, a load whose file fails on the server after the client cleared its demo loop, config.cfg written by the client lacks server-only archived cvars, and the save and load menus read save files on the client's side (on the board they live on main).

Left before the split runs on the board, the client half must fit (118 KB over on E1M1, the video's 146 KB is the biggest block), the server data image (mkboard `--main` with a server bake), saves on main's FAT volume, the smoketest of the link rate, and a first run on hardware.

## 2026-11-23, World renderer speed (PC and ARM build, registered data)

Four exact speedups of the lit span and z span path (byte-identical pictures, on in all three flag lists), two approximate span drawers (off, in no list, for the user to judge), render phase timing for the board's fly-through, and benchmark lines that time and check the engine's own new span code on the board. Nothing ran on hardware. The board savings below are estimates from the generated Thumb code and the E1M1 fly-through's work counts until the benchmark and the fly-through are flashed.

| Change | Flag | Kind | Estimated board saving (E1M1 fly-through) | RAM |
| --- | --- | --- | --- | --- |
| Lit spans without modulo. Power-of-two textures wrap with masks, other sizes (3% of demo1's lit pixels, 0.1% of E1M1's) step wrapped coordinates with compare and subtract. The texture origin is folded into the light grid pointer, each mip level has its own pixel loop with constant shifts, and each run of up to 8 pixels is drawn in two unrolled passes (light blend, then texel and colormap) so each pass fits the M0+'s low registers | `QOG_LITSPAN_FAST` | exact | inner loop about 150 to about 65 cycles a pixel (old, 73 instructions and two divider calls, new, 37 + 14 instructions, 4 multiplies, 3 + 3 stack reloads), about 4.1 M cycles or 20 ms a frame at 48,400 lit pixels | +40 B static, D_DrawSpans8Lit stack 216 to 296 B, flash +6.5 KB |
| The last run of each span reuses `d_*stepu * (spancount - 1)`, computed once per span length per surface | `QOG_LITSPAN_TAILMUL` | exact | an int to float and three float multiplies per span, about 1.4 ms at 1,620 spans | +96 B stack |
| z span start without double arithmetic. `(int)(zi * 0x8000 * 0x10000)` with zi a double holding a float is exact (both factors are powers of two), so it is the float's mantissa shifted by its exponent, truncated toward zero. abs(zi) >= 1, infinities and NaNs keep id's expression | `QOG_ZSPANS_FAST` | exact | float to double, two double multiplies and double to int per z span (about 200 cycles), about 1.7 ms | 0 |
| Lit spans write their own z (`D_QogZSpan`) from the 1/z they compute at each span start with id's expression, and D_DrawSurfaces skips D_DrawZSpans for them | `QOG_LITSPAN_ZFUSE` | exact | two int to float, two float multiplies and two float adds per lit span, a second walk of the spans and a call per surface, about 2.4 ms | stack +40 B (D_DrawSpans8Lit 392 to 432 B, D_QogZSpan 32 B) |
| Light blended exactly at both ends of each run and stepped linearly between them (one pass, up to 16 pixels) | `QOG_LITSPAN_LINLIGHT` | approximate, off | inner loop 21 instructions a pixel plus two blends a run, about 24 cycles a pixel, about 6 ms | 64 B flash table |
| One float divide per 16 pixels instead of 8, as id's D_DrawSpans16 did on x86 | `QOG_LITSPAN16` | approximate, off | half the per-run float work (3 adds, a divide, 2 multiplies, 2 conversions per run), about 6 to 7 ms | +96 B stack |
| Render phase times and work counts | `QOG_PHASE_TIME` | measurement, off in shipped builds, on in qogfly (`QOGFLY_PHASE`) | costs about 2,000 timer reads and three span-list walks a frame | +96 B static, qogfly only |

Pixel differences of the approximate drawers over the 113 `qogtest_d_shots` frames (3D view rows only, max colour step is the largest change of one RGB channel through the palette).

| Build | Frames differing | Pixels differing | Max colour step | Worst frame |
| --- | ---: | ---: | ---: | --- |
| `QOG_LITSPAN_LINLIGHT` | 113 | 1.27% | 60 | d_spawn_e3m3, 5.2% |
| `QOG_LITSPAN16` | 113 | 6.2% | 184 | d_demo3_06, 21% (texels shifted by one at grazing angles) |
| both | 113 | 9.1% | 192 | d_demo3_15, 23% |

- Work counts (PC, `QOG_PHASE_TIME`), E1M1 fly-through (1,618 frames 0.1 s apart) per frame 71 lit surfaces, 2,769 light cells built, 1,622 lit spans, 48,363 lit pixels (39 on the any-size path), 6,811 runs of up to 8 pixels, 45 sky and 224 turbulent pixels. timedemo demo1 per frame 60 lit surfaces, 3,346 light cells, 1,509 spans, 46,506 lit pixels (1,564 any-size), 6,514 runs. Per lit pixel the float work is about 60 cycles (a run's 3 adds, divide, 2 multiplies and 2 conversions plus each span's start), the light grid build about 0.4 ms a frame.
- PC speed, no measurable change. timedemo demo1 runs at the 60 fps cap with every build, and the `-qogfly` wall time over 8,000 frames varied by 30% between runs of the same build. On x86 the modulos and floats this work removes are cheap. On the PC the phase split of the E1M1 fly-through is 0.24 of 0.43 ms in lit spans.
- Verification, hidden and silent, against an untouched build of 4626b83 (`build-pc-base`), each run with `+_cl_color 0` and the frame-compared scripts after `qogtest_k_quiet.cfg`, `qogtest_hash` state hashes identical, 113 `qogtest_d_shots` and 64 `qogtest_e_shots` frames byte-identical (the 8 files `qogtest_e_shots1` to `7` are chained by `qogtest_e_shots.cfg` and are not meant to run alone), `qogtest_n_flyg` 400 view hashes and 4 shots identical. Per-frame hashes. `qogtest_h_demos` 3,610 frames and `qogtest_h_fight1` to `4` 37,159 frames equal except runs of at most 10 frames at startup and at each map load. Frames 700 to 715 of fight 1 were dumped from both builds, the differing ones differ only in one 6-row console text line scrolling up (the load timing), the 3D view is identical.
- On the board, `apps/qogbench` now includes `engine/d_litspan.h` and checks the new drawer against the old code byte for byte at every mip level, on a power-of-two and a 320×200 texture, full and ragged spans (`chk_p2`, `chk_any`), and the z start against id's double expression over 2 span sets (`chk_z`). Each should print 0.
- Engine files changed. `engine/d_scanlit.c`, `engine/d_scan.c` (D_QogZiFixed, D_QogZSpan), `engine/d_edge.c` (phase hooks, the z skip), `engine/r_main.c` (phase hooks only). New. `engine/d_litspan.h`, `engine/qog_phase.h`, `port/common/qog_phase.c`. One new block at the end of each flag list.
- Looked at and left, placing the span code in RAM (the earlier benchmark showed no gain and RAM is short, the new D_DrawSpans8Lit is 9.5 KB of flash code but one mip variant plus the float part, about 2 KB, is hot per surface), R_BuildLightMap (about 0.4 ms a frame on E1M1, fusing its three passes would save about 0.15 ms), exact replacements of the per-run float divide (the ROM routines are already the fast path, any fixed-point form changes s and t).

Board measurement, prepared and not run (the coordinating session flashes).

```powershell
cmake --preset target -DQOGFLY_HUNK_KB=18 -DQOGFLY_STACK_KB=34
cmake --build build --target qogbench_main
python third_party\wiliOGbsp\tools\fw.py flash qogbench --uf2 build\apps\qogbench\qogbench_main.uf2
python third_party\wiliOGbsp\tools\fw.py console
cmake --build build --target qogfly_main
python third_party\wiliOGbsp\tools\fw.py flash qogfly --uf2 build\apps\qogfly\qogfly_main.uf2
```

The benchmark keeps its first line and adds a second every 2 s (each group is refreshed every third round, about 3 s).

```
[bench] fast old_p2=<us> fast_p2=<us> fast_any=<us> chk_p2=<n> chk_any=<n> | approx lin_p2=<us> s16_p2=<us> s16lin_p2=<us> | short old=<us> fast=<us> zid=<us> zfast=<us> chk_z=<n>
```

old_p2 is today's code on a 256×128 texture in flash with the texture origin a whole light cell, fast_p2 the shipped drawer on the same scene, fast_any the shipped drawer on the 320×200 picture, lin, s16 and s16lin the approximate drawers. short repeats old and new on game-like spans (17 to 47 pixels), and zid and zfast are the z spans with id's double start and with `QOG_ZSPANS_FAST`. Every value is microseconds per 320×152 frame. The fly-through prints after its status line every 2 s.

```
[phase] frames <n> ms setup <ms> world <ms> bmodels <ms> scan <ms> lmap <ms> grad <ms> spans <ms> zspans <ms> sky <ms> turb <ms> models <ms> vmodel <ms> parts <ms> total <ms> edgescan <ms>
[phase] per frame: litsurfs <n> lmcells <n> litspans <n> litpx <n> anypx <n> runs <n> zpx <n> skypx <n> turbpx <n>
```

Times are means per frame over the 2 s. scan includes the surface drawing (lmap, grad, spans, zspans, sky, turb), edgescan is scan less those, the edge sorting and span making. With `QOG_LITSPAN_ZFUSE` the lit surfaces' z is inside spans. `-DQOGFLY_PHASE=OFF` removes it. The fly-through's static RAM grows by 132 bytes (36 for the span drawer, 96 for the phase sums), flash by 8.1 KB.

## 2026-11-23, Demo playback on the board (PC and ARM build, registered data)

`quake_display` (the split game's client half, `QOG_SPLIT`) now links, a 68 KB hunk, a 34 KB stack and the one-byte depth buffer the user approved, with 1,104 bytes of RAM free beside the 2 KB heap reserve. Before, it overflowed by 118,024 bytes with E1M1's hunk. 68 KB is the smallest PC hunk of the split client with these flags that runs E1M1 with a fight, a save and a load. The other start to e1m4 maps need up to 85 KB and do not fit yet, and the earlier edge fallback, which the one-byte z-buffer reaches now and then, stops the game on the board (both below). Every other change is exact. Nothing ran on hardware.

Merges first, an earlier branch (demo player) and an earlier branch (renderer speed, exact flags) into an earlier branch. Conflicts only in the flag lists (`apps/quake/CMakeLists.txt`, `pc/CMakeLists.txt`, `tools/ram_meter.py`, every block kept, an earlier branch's before the split block so both halves get them) and in this log (every entry kept). The split PC client stubs the demo host's PC commands like the other single-process tools (`port/pc/split_cl_pc.c`). Against a build of 7d619f3 (an earlier branch, scratch copy), the merged single-process game and the merged split pair gave the same `qogtest_hash` (2) and `qogtest_r_fhash` (32) state hashes, the same 8 fhash frames and the same 113 `qogtest_d_shots` frames after `qogtest_k_quiet`. That merged tree is `build-pc-base`.

Board client settings (`apps/quake/CMakeLists.txt`, the client library `qog_engine_cl` and `quake_display` only, never the PC game list). Savings are RAM of the link at the same hunk unless marked hunk.

| Change | Flag | Saved |
| --- | --- | ---: |
| One-byte z-buffer with its near steps, 1,800 edges so the edge table fits it, the world's edge cache in the frame buffer's view rows (approved, picture below) | `QOG_ZBUFFER8`, `QOG_ZBUFFER8_NEAR=7`, `QOG_NUMSTACKEDGES=1800`, `QOG_EDGECACHE_FRAME=152` | 48,640 |
| Client entities in hunk blocks of 16 only for the entity numbers a level uses, 16-bit slot numbers, at most 600, more stops with Host_Error. Extends an earlier branch's `QOG_CL_ENTSLOTS` | `QOG_CL_ENTSLOTS=600`, `QOG_CL_ENTBLOCK=16` (new) | hunk, E1M1 26.3 KB of id's array to 8.4 KB (e4m3 280 numbers in use) |
| Beam segments from the level's hunk in blocks of 8 when a frame needs them, id's 64 kept (the fight tour fills 64 and drops segments as id does), a scratch entity before signon | `QOG_TEMPENT_BLOCK=8` (new) | 6,520 static, hunk only on levels with beams |
| Stack, the renderer's bmodel warning prints without Con_Printf's 4 KB buffer, CL_NextDemo's command line 32 bytes, 32-byte edge-drawing surfaces (16-bit keys, face index, entity by visedict), player colours through a 256-byte texel map instead of a 16 KB colour map on the stack | `QOG_CON_PRINTTEXT`, `QOG_NEXTDEMO_STR`, `QOG_SURF_T_TINY`, `QOG_TRANSLATE_INLINE` (new) | stack 47 to 34 KB, 13,312 |
| Zone 16 to 8 KB, cvar values stay at their initial strings until a set changes them, command tokens in a 640-byte buffer, light style texts in the zone at their length instead of 64 x 64 bytes | `QOG_CVAR_LITERAL`, `QOG_CMD_ARGV_ARENA=640`, `QOG_LIGHTSTYLE_ZONE` (new), `QOG_ZONE_BOARD_KB=8` | hunk 8 KB, static 3,200 net |
| Link, the client's rings 1 KB (a frame that does not fit is not acknowledged and comes again), both in scratch X below core 1's stack (an earlier branch's `QOG_LINK_SCRATCH` hunk, the same lines), the BSP frame sender wrapped by one that writes the same bytes without its 4,165-byte buffer | `QOG_LINK_TXRING/RXRING=1024`, `QOG_LINK_SCRATCH`, `QOG_LINK_STREAM_SEND` with `--wrap=fwog_link_uart_send_frame` | 8,261 |
| The console's 2 KB command history in scratch Y below the boot stack | `QOG_KEYLINES_SCRATCH` | 2,048 |
| Lit surface hash 1,024 to 256 entries as the demo player, overflow takes the earlier exact tree walk (slower in big fights) | `QOG_DLIGHT_SURFS=256` | 4,608 |
| PVS row for `QOG_PVS_LEAFS` leafs, key names const, client precache tables of the server's sizes (208, 176), the unused 16-bit palette table in flash, a 64-byte surface cache header | `QOG_PVS_ROW_SMALL`, `QOG_KEYNAMES_CONST`, `QOG_CL_PRECACHE_SV`, `QOG_VID_NO_8TO16`, `QOG_VID_SURFCACHE_MIN` (new) | 3,088 |
| the earlier edge fallback stops with an error when the stack cannot hold its table | `QOG_ZEDGE_STACK_GUARD=1024` (new) | 4 bytes |

Link of `quake_display` (`cmake --preset target -DQOG_AOT=ON`, `QOG_CL_HUNK_KB` 68, `QOG_CL_STACK_KB` 34), links, .data 4,096 + .bss 250,176, 1,104 bytes free plus the 2 KB `.heap` reserve. Video 97,280, hunk 69,632, stack 34,816, the rest 59,312 (engine statics, SDK, BSP, libc, heap reserve). `quake_main` still links with 1,712 bytes free, `qogdemo_display` with 1,704 plus its heap reserve, `qogfly_display` with its board values (18 and 34 KB).

Stack (`tools/stack_chain.py --root game_main` on a copy of only the client's objects, the earlier cuts plus S_LoadSound, S_FindName, S_PaintChannels and S_Update_ excluded because SNDDMA_Init fails on the board so sound never starts, and R_RenderPoly, which only id's debug polygon modes reach), deepest chain 32,712 bytes, a demo start's loading plaque drawn under Cbuf_Execute, R_EdgeDrawing 29,752 (800 surfaces of 32 bytes and the span reserve), a bmodel clip to Sys_Error. The alias chain is 20.5 KB from R_RenderView_. 2,104 bytes are left for interrupts. The earlier exact edge fallback (up to 1,804 edges of 24 bytes, 43 KB) is not reserved and on the board now stops with an error.

Peaks on the split PC client with the board client flags (`qog_tour` and `qog_tour_fight` run by the server half, `QOG_CL_PEAKS`, hunk with an 8 KB zone, hunk low at the end of each level, KB).

| Map | Fight tour hunk | Plain tour hunk | Entity numbers in use | Temp entities | Edges | Surfaces |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| start | 65.5 | 72.9 | 142 | 0 | 969 | 464 |
| e1m1 | 65.9 | 64.2 | 90 | 0 | 1,053 | 430 |
| e1m2 | 72.0 | 72.0 | 112 | 0 | 783 | 368 |
| e1m3 | 84.7 | 81.4 | 184 | 0 | 667 | 338 |
| e1m4 | 84.8 | 83.2 | 183 | 0 | 866 | 401 |
| e1m5 to e1m8 | 59.4 to 78.9 | 61.0 to 76.9 | up to 170 | up to 13 | up to 1,081 | up to 511 |
| worst of the game | e2m6 96.5 | | e4m3 254 (280 in one run) | 64, segments dropped as in id | e3m6 1,751 | e3m6 670 |

`-heapsize` that runs the earlier fight, save and load scripts, E1M1 68 KB (`qogtest_r_mem_e1m1.cfg`, 67 fails), e3m5 79 KB (`qogtest_r_mem_e3m5.cfg`). Before these flags, 97 and 115. The zone peaks at 5.9 KB of 8 (light styles included). 1,800 edges and 800 surfaces cover the game's peaks. The fight tour stops on e4m5 (below), e4m5 to e4m8 come from a plain tour with the 16 KB zone, 94.7, 90.0, 90.4, 86.4 KB, less 8.

Edge fallback with the one-byte z-buffer (`z edges at exit`, PC), fight tour start to e1m8 0, episode 2 group 2, episode 3 group 2. Plain tour start to e1m4 0 and e1m5 to e1m8 0 in a second run, but 84 in a first run of start to e1m8 (the same run saw 1,483 edges on e1m1 against 861 to 1,053 in every other, an unusual view). With the 16-bit z-buffer no run reached it. On the board each one would now stop the game with an error (`QOG_ZEDGE_STACK_GUARD`), the table it moves (up to 43 KB) does not fit any board stack. A swap of the table into the frame buffer's free rows was tried and dropped, in a test build with forced stalls (`QOG_ZEDGE_TEST_SPANS=400`) the live edges never fitted below the scan line.

Verification, hidden and silent, `-nosound`.

- Single-process `quakeog_pc` of this branch against `build-pc-base`, `qogtest_hash` 2 hashes, `qogtest_r_fhash` 32 hashes and 8 frames, `qogtest_d_shots` 113 frames, all identical.
- Split pair of this branch (`-ServerExe`), the same 2 + 32 hashes and 8 + 113 frames.
- Split client built with the board client flags except the depth buffer (`-DQOG_SPLIT_CL_EXTRA`, `build-pc-f`), server with the board's, the same 2 + 32 hashes and 8 + 113 frames.
- With the depth buffer as well (`build-pc-z`), the same 2 + 32 state hashes. Frames, 29 of 113 demo frames identical, 0.048% of view pixels differ, at most 155 pixels (d_demo1_20), status bar rows identical. The 8 fhash map frames 0.039%, at most 34 pixels.

Not done or not fitting.

- Start to e1m4 (an earlier branch's first data image) needs up to 85 KB of hunk (e1m3, e1m4 fight tour), 17 KB more than the board has. The whole game needs 97 KB (e2m6). Exact options left are small. Bigger ones change behaviour, the console text (4 KB) and command buffer (4 KB) sizes, or the message buffer (7.5 KB) in the z-buffer during signon, which is exact only while in-game messages stay small.
- Sound, no room (1.1 KB free). Earlier work estimated about 25 KB.
- `qog_tour_fight` stops on e4m5 with a QuakeC "Program error" on the server half (target t178), also in the measurement build that had only the slot counter, so e4m5 to e4m8 have plain tour figures only.

New engine flags (off by default). `QOG_CL_ENTBLOCK`, `QOG_TEMPENT_BLOCK`, `QOG_CON_PRINTTEXT`, `QOG_NEXTDEMO_STR`, `QOG_SURF_T_TINY`, `QOG_TRANSLATE_INLINE`, `QOG_CVAR_LITERAL`, `QOG_CMD_ARGV_ARENA`, `QOG_LIGHTSTYLE_ZONE`, `QOG_PVS_ROW_SMALL`, `QOG_KEYNAMES_CONST`, `QOG_CL_PRECACHE_SV`, `QOG_VID_NO_8TO16`, `QOG_KEYLINES_SCRATCH`, `QOG_ZEDGE_STACK_GUARD`, `QOG_CL_PEAKS` (measurement, also the largest edge fallback). Port flags. `QOG_LINK_STREAM_SEND`, `QOG_VID_SURFCACHE_MIN`, `QOG_LINK_SCRATCH` (an earlier branch's lines), `QOG_SPLIT_TOUR` (PC server half runs `qog_tour`). Edits, engine/cl_main.c, cl_parse.c, cl_tent.c, client.h, cmd.c, console.c, console.h, cvar.c, d_edge.c, d_polyse.c, keys.c, model.c, r_alias.c, r_bsp.c, r_draw.c, r_edge.c, r_main.c, r_shared.h, vid.h, port/common/qog_link.c, qog_mem.c, qog_split_sv.c, port/pc/sys_pc.c, tour_pc.c, vid_pc.c, port/rp2040/link_rp2040.c, sys_rp2040.c, vid_rp2040.c, pc/CMakeLists.txt, apps/quake/CMakeLists.txt. No new qogtest scripts (writing to the shared data folder was refused), measurements use `qog_tour` on the command line and the earlier `qogtest_r_*` scripts. Lessons L49 and L50.

## 2026-11-23, The game split across both RP2040s, server on main, client on the display (PC and ARM build, registered data)

The server half (SV_*, QuakeC, physics, edicts) and the client half (CL_*, renderer, status bar, input) are now separate builds joined by a new net driver over the inter-chip link. On the PC the two halves run as two processes over TCP at the UART's speed and give the same state hashes and frames as the single-process game. On the board `quake_main` (server) links with 1,712 bytes of RAM to spare. `quake_display` (client) still overflows RAM by 118,024 bytes with the E1M1 hunk (136,456 with e3m5's). Nothing ran on hardware.

How the split works (engine/net_link.c, flag `QOG_NET_LINK` with `QOG_SPLIT_SERVER` or `QOG_SPLIT_CLIENT`).

- The link driver replaces the loopback driver with loopback semantics, a reliable message makes the sender wait until the receiver has read it (canSend), the receive buffers have the loopback's sizes and are read in place, and an overflow behaves as there.
- Lockstep. The client stops where id's Host_Frame ran the local server (`Link_ClientFrame`), sends what its frame produced and waits. The server half runs its frame and answers. Console lines the client does not know run on the server at once, inside the client's command buffer (`Link_ForwardCommand`, a synchronous exchange), so `map`, `load`, `changelevel`, `qog_statehash` happen at the same point as in one process. `qog_sv <line>` forces a line to the server.
- What id's server did to client state directly comes back as items the client carries out at the same point. `reconnect` (SV_SendReconnect), `connect local` (map, load), `disconnect` (load), Host_ClearMemory's client half, `scr_centertime_off`, Host_Error and Host_EndGame. A client that closes its connection stops the server, as CL_Disconnect did. The server's Con_Printf goes to the client's console. Text the server adds to the command buffer (QuakeC `changelevel`, `localcmd`) joins the client's, which is the game's only command buffer, so scripted waits keep id's order.
- Shared globals cross with every item that follows a change, the client's realtime, host_frametime and key_dest (SV_Physics pauses in menus), the server's host_frametime, `sv.active`, `sv.paused`, `sv.max_edicts`, `svs.maxclients`, the map name and noclip_anglehack. Every cvar set on one half is set on the other. The shared random sequence (qog_rand) stays one sequence, each side sends its call count and the other advances its generator to it, so QuakeC's random() and the particles draw exactly the numbers they drew in one process.
- In the client build `sv` and `svs` are that report (engine/server.h), and `Host_ShutdownServer` only clears `sv.active`, so id's client code (CL_LerpPoint, CL_KeepaliveMessage, menus) is unchanged. The server build has no console, renderer, sound, keys or menus (port/common/qog_split_sv.c keeps V_CalcRoll and a console that prints to the log and to the client).

Transport (port/common/qog_link.c, port/pc/link_pc.c, port/rp2040/link_rp2040.c), a reliable byte stream over the BSP's link frames `[0x7E][len16][payload][crc16]` with the BSP's CRC, Quake types 0x40 DATA, 0x41 ACK, 0x42 HELLO and 0x43 HELLOK (0x01 to 0x1F stay the bootloader's, 0x20 to 0x22 the breakout I/O's, which the display hands to `fwog_ioexp_link_handle`). One frame in flight, resent after a timeout with back off, duplicates acknowledged again, the decoder resynchronizes after a bad CRC, and a full receive ring sends no ack so the sender waits (with the UART's RTS/CTS below it). Board sizes 512-byte frames and 2 KB rings on both chips. On the main chip every wait kicks the watchdog, on the display every wait keeps `fwog_power_poll` running.

PC results (hidden and silent with `-nosound`, against an untouched build of 1c85a91, `build-pc-base`, link rate limited to 300,000 bytes per second).

| Test | Single process (base) | Split pair |
| --- | --- | --- |
| `qogtest_hash.cfg` | 2 hashes | identical, rand calls 539 and 1990 as in base |
| `qogtest_r_fhash.cfg` (copy of `qogtest_q_fhash` with its own save name, 8 maps, fight, save, load) | 32 hashes, 8 frames | all identical |
| `qogtest_k_quiet.cfg` + `qogtest_d_shots.cfg` (demos and `map dm6`) | 113 frames | all byte identical |
| `qogtest_hash.cfg` with `-linkcorrupt 40 -linkdrop 60` on both | | identical, 39 resends, 20 CRC drops |
| `qogtest_r_fhash.cfg` on QOG_AOT with the board's flags and link sizes | | identical to base |

The single-process build of this commit (`quakeog_pc`, all new flags off) gives the same 2 + 32 hashes, 8 + 113 frames as base.

Link traffic (wire bytes, framing and acks included, fhash run), per frame client to server average 61, server to client average 235. In 3,360 frames of play the busiest frame was 164 bytes up and 560 down. The worst burst is a level change or load, 5,241 bytes to the client in one frame (17.5 ms at 300 KB/s, 4.2 ms at the BSP's 12.5 Mbaud) and 1,237 bytes up. The lockstep adds the server's frame to the client's frame time, the halves do not yet run in parallel.

RAM of each half. Hunks are the smallest `-heapsize` of a PC build with QOG_AOT and the board's flags that runs the map, a fight, a save and a load (`qogtest_r_mem_e1m1.cfg`, `qogtest_r_mem_e3m5.cfg`). Single chip for comparison, 194 KB.

| | E1M1 | e3m5 (worst map) | Static RAM and stack on the board |
| --- | ---: | ---: | --- |
| Server (`quakeog_sv`, server zone 6 KB, load buffer 8 KB, `QOG_LOADGAME_STRSIZE`) | 130 KB | 172 KB | sv 11,360, BSP display update and link buffers 12,441, stack 24 KB (deepest chain 18,608 B, `tools/stack_chain.py --root server_main`) |
| Client (`quakeog_cl`) | 97 KB | 115 KB | video 145,920, stack 47 KB, engine statics 57,356, SDK and BSP 23,737, link 6,329 |

Board links (`cmake --preset target -DQOG_AOT=ON`).

- `quake_main` with `QOG_SV_HUNK_KB` 172 and `QOG_SV_STACK_KB` 24, links, .data 7,840 + .bss 250,352 + heap 2,048, 1,712 bytes of RAM left. App 884,092 bytes with a stand-in display image (`-DQOG_SPLIT_MAIN_EMBED=qog_display`, because quake_display does not link yet), 3 MB app slot.
- `quake_display` (client), RAM overflow 136,456 bytes with `QOG_CL_HUNK_KB` 115, 118,024 bytes with 97 (E1M1). Before the split the single chip build overflowed by 223,504. The client keeps the 47 KB stack array, the render chain is 47,584 bytes from R_RenderView (the merge coordinator's measurement) plus about 300 above it, and the earlier 48 KB exact edge fallback is not reserved (it would add about 41 KB to an overflow that is already 118 KB).

Main flash layout (platform/qog_flashmap.h), app with the embedded display image 0x000000 to 0x2FFFFF (3 MB, checked post-build), server data 0x300000 to 0x7FFFFF (5 MB), BSP FAT volume 0x800000 to 0xFFFFFF (untouched). Server-side lumps of `start` and episode 1 (entities, planes, visibility, nodes, clip nodes, leafs, models) are 2,167,292 bytes in the paks plus progs.dat 413,116, so the server's copy fits 5 MB, while a full copy of the display image (11.9 MB) does not. The server image (a pak of what the server opens and a bake for 0x10300000 without textures, lighting and surfaces, written by the same `tools/mkboard.py` run as the display's) is not written yet.

New engine flags (all off by default). `QOG_NET_LINK`, `QOG_SPLIT_SERVER`, `QOG_SPLIT_CLIENT`, `QOG_LOADGAME_STRSIZE`. Port flags. `QOG_SYS_MAIN`, `QOG_LINK_MAIN`, `QOG_LINK_CHUNK`, `QOG_LINK_TXRING`, `QOG_LINK_RXRING`. CMake. `QOG_SPLIT` (ON), `QOG_CL_HUNK_KB`, `QOG_SV_HUNK_KB`, `QOG_SV_STACK_KB`, `QOG_SPLIT_MAIN_EMBED` in apps/quake, `QOG_SPLIT_SV_EXTRA` and `QOG_SPLIT_CL_EXTRA` in pc.

- Edits outside new files, engine/host.c, host_cmd.c, cmd.c, cvar.c, net_main.c, net_none.c, server.h, sv_main.c, sv_user.c, world.c (each behind the flags), port/common/qog_mem.c (reports leave out the missing half), port/pc/sys_pc.c (server loop), port/rp2040/sys_rp2040.c (`QOG_SYS_MAIN`), tools/pctest.ps1 (`-ServerExe`, `-ServerArgs`), tools/check_app_size.py (`main`), and one new block at the end of pc/CMakeLists.txt and apps/quake/CMakeLists.txt.
- Not in the split builds, the single-process PC tools `qog_tour`, `qog_manifest`, `qog_fly` (stubs in port/pc/split_cl_pc.c), the server's `viewmodel`, `viewframe`, `viewnext`, `viewprev` (they write the client's model list), and the QOG_BAKE_WRITER.
- Known differences from one process, none hit by the tests, the order of client and server prints inside one exchange, a load whose file fails on the server after the client cleared its demo loop, config.cfg written by the client lacks server-only archived cvars, and the save and load menus read save files on the client's side (on the board they live on main).

Left before the split runs on the board, the client half must fit (118 KB over on E1M1, the video's 146 KB is the biggest block), the server data image (mkboard `--main` with a server bake), saves on main's FAT volume, the smoketest of the link rate, and a first run on hardware.
- Engine files changed. `engine/cl_demo.c`, `engine/cl_main.c`, `engine/cl_parse.c`, `engine/cl_tent.c`, `engine/client.h`, `engine/d_local.h`, `engine/model.c`, `engine/r_main.c`, `engine/view.c`, all behind the flags above. One new block each in `pc/CMakeLists.txt`, `apps/quake/CMakeLists.txt` and `tools/ram_meter.py`, and the `QOG_PC_DEMO_BOARD` and `QOG_PC_DEMO_Z8` test options in `pc/CMakeLists.txt` (off by default).
- The bake layout hash did not change, so the shared `data/id1/qogbake.bin` still loads.

## 2026-11-23, The full game on the board, honest configuration (PC and ARM build, registered data)

`quake_display` now links with everything the game needs counted, QOG_AOT on, sound off, a hunk sized from the measured E1M1 need and the game stack as an array in main RAM. It does not fit. RAM overflow 255,504 bytes before the cuts below, 223,504 bytes after them (216,336 bytes if loading a save is not needed). Exact cuts cannot close the gap, and the picture options do not either. Nothing ran on hardware.

Honest configuration (`apps/quake/CMakeLists.txt`, built with `-DQOG_AOT=ON`).

- Hunk `QOG_HUNK_KB` 194, the smallest `-heapsize` that runs E1M1, a fight, a save and a load on a PC build with QOG_AOT and the board-only flags (new `qogtest_q_mem.cfg`). Play without a load needs 187 KB (`qogtest_q_play.cfg`). Interpreted QuakeC needs 38,624 bytes more for its globals.
- Stack `QOG_STACK_KB` 47, an array in main RAM that `port/rp2040/sys_rp2040.c` moves the stack into, as the fly-through does. The deepest chain is 45,968 bytes (new `tools/stack_chain.py`, `--root game_main`), a demo start draws a loading plaque, R_EdgeDrawing 36,184, a brush clip warning through Con_Printf 4,120, S_LoadSound 1,368 and an error path. The alias chain is 33,176, the server chain 22,888, one more level of compiled QuakeC 120. Cut by hand as impossible at that depth, Con_Printf to SCR_UpdateScreen (the console only, no 3D view, while not signed on), COM_FindFile to COM_CopyFile (no cache dir), S_ExtraUpdate (sound off), the debug-only R_DrawCulledPolys and R_ZDrawSubmodelPolys.
- Not in the 47 KB, the earlier exact edge fallback (`R_QogEdgesToStack`) can alloca 48,120 bytes under R_EdgeDrawing, a chain of about 88,200 bytes. It never ran in any test (demos, 32-map tour, 75% build below). Sizing the stack for it costs 41 KB more.
- `-Wl,--allow-multiple-definition` as in apps/qogfly. `QOG_MIN_MEMORY=0` replaces id's 5.3 MB hunk floor, which stopped Host_Init on the board.

| Step | Flag | Saved | Overflow (bytes) |
| --- | --- | ---: | ---: |
| Honest configuration, hunk 223 KB (E1M1 play, no load) | | | 255,504 |
| Key down, console and menu key flags as bytes, not 4-byte enums | `QOG_KEYS_BYTES` | 2,304 static | 253,200 |
| No sound channels and paint buffer with `-nosound` (S_StopSound checks) | `QOG_SND_OFF_NOHUNK` | 10,784 hunk | |
| Edict margin 64 past the map's estimate, board only (fight tour peak 35, reuse fallback behind it). Edicts 65,952 to 52,640, edcold 15,232 to 12,160, cl_ents 32,992 to 26,336 | `QOG_EDICT_MARGIN_BOARD=64` | 23,040 hunk | |
| Client precache names at the end of the z-buffer while server info is read (no 3D view before signon) | `QOG_PRECACHE_ZTEMP` | 2,800 peak on E1M1 | 216,336 (hunk 187 KB) |
| Load game line buffer at the end of the z-buffer, the status bar rows left clear | `QOG_LOADGAME_ZTEMP` | 32,784 peak of a load | 223,504 (hunk 194 KB with a load) |

Biggest RAM users after the cuts (map file memory map, 485,640 bytes in the 262,144-byte RAM region).

| User | Bytes |
| --- | ---: |
| Hunk (E1M1, edicts 52,640, cl_ents 26,336, particles 18,448, zone 16,400, edcold 12,160, item box models 8,512, map arrays 7,696, loopback to client 7,552, QuakeC globals 7,280, clients 6,784, packed blocks 6,384, command and message buffers 5,152, console text 4,112, baselines 3,584, strings of a loaded save 6.8 KB) | 198,656 |
| Frame buffer and z-buffer | 145,920 |
| Stack array | 48,128 |
| Engine statics (sv 11,360, cl_temp_entities 6,656, cl_lightstyle 4,352, dl_bits 4,096, cl 2,832, mod_known 2,304, MSG_ReadString 2,048, key_lines 2,048, dl_surf 2,048, menu_cachepics 1,792, ...) | 76,447 |
| SDK, newlib, qog_video, surface cache header, heap reserve, vectors | 16,489 |

Picture options (off, the user's decision), against 223,504.

| Option | Video | Compatible with K's edges in the z-buffer | Overflow |
| --- | ---: | --- | ---: |
| `QOG_RENDER_SCALE=75` (240x114) | -42,560 | Yes. The 54,720-byte z-buffer holds the 52,704-byte edge table and clip buffers, but not the edge cache in front of it, which goes to the hunk (E1M1 hunk low 186.3 to 212.7 KB, +27 KB). Demos and the 32-map tour ran with no edge moves and no stack fallbacks | about 208,600 |
| `QOG_ZBUFFER8` | -48,640 | No. R_NewMap stops, "a z-buffer of 48640 bytes holds no table of 2000 edges". Needs `QOG_NUMSTACKEDGES` at most 1,830 (full-game peak 1,730) and the edge cache on the hunk (+27,024), and then the edge table fills the whole z-buffer, so every frame reaches it and the 48 KB stack fallback becomes likely. Without edges in the z-buffer the stack grows 48,584 | about 202,000 with 1,830 edges, about 223,000 without |
| Both | -69,920 | No for the full game, 27,360 bytes hold 944 edges (E1M1 peak 422, full game 1,730). With edges back on the stack (+48,584) the E1M1 edge cache (27,024) still fits the z-buffer, the e3m5 one (33,846) goes to the hunk | about 202,200 on E1M1 |

Sound (computed from the structures, the mixer was not run), channels 6,672, paint buffer 4,112, id's `known_sfx` 512 x 68 = 34,816, a cache header of about 80 bytes per precached sound (E1M1 about 110 sounds, 8.8 KB), sample data read in place from flash, plus a DMA ring not written yet. About 54 KB, about 25 KB with known_sfx sized to the 176 precache slots. It does not fit while the game does not.

Input, the IN_ path is Sys_SendKeyEvents in `port/rp2040/vid_rp2040.c`, which turns `qog_input` actions into key events bound by id's default.cfg (arrows, CTRL fire or ENTER in menus, SPACE jump, c down, / next weapon, ESCAPE menu). Previous weapon pressed backslash, which id binds to +mlook. It now sends `impulse 12` (fixed in the PC board mode too).

- Verification, hidden and silent with `-nosound` and `+_cl_color 0`, against an untouched build of 4626b83 (`build-pc-base`). `qogtest_hash` state hashes identical on the interpreted build, the QOG_AOT build and a QOG_AOT build with the board-only flags. `qogtest_q_fhash.cfg` (a copy of `qogtest_f_hash` with its own save name, 8 maps, fight, save and load) 32 hashes and 8 end frames identical on the same three. `qogtest_d_shots` with `qogtest_k_quiet.cfg` 113 frames byte-identical on the interpreted and the board-flag builds (without it the notify line shows the shot path, which names the build folder). `qogtest_reg` ran its 32 maps with no errors on the interpreted and the board-flag builds. `qogfly_main` links with the values the board run used (`QOGFLY_HUNK_KB` 18, `QOGFLY_STACK_KB` 34).
- What is left is not a matter of exact cuts. The whole engine outside the video needs about 340 KB on E1M1 with 116 KB of RAM left beside the video. The options with real size are per-class edicts with compiled QuakeC (edicts and edcold 64.8 KB now), a sparse client entity table (26 KB), message buffers in the z-buffer between frames (about 17 KB, needs proof they are empty at R_RenderView), the 18 KB particle block (changes the picture), or the two-chip split in docs/plans/fit-plan.md.
- Edits outside the earlier files. `engine/host.c`, `engine/snd_dma.c`, `engine/host_cmd.c`, `engine/cl_parse.c`, `engine/sv_main.c`, `engine/keys.c` (each behind its flag), `port/pc/vid_pc.c` (board mode input), one new block each in `pc/CMakeLists.txt`, `apps/quake/CMakeLists.txt` and `tools/ram_meter.py`, and the hunk and stack sizes of `quake_display`.
## 2026-11-23, Demo playback on the board (PC and ARM build, registered data)

A client-only host plays the recorded demos (demo1 on e1m3, demo2 on e1m4, demo3 on e1m6) with id's client, renderer and status bar, without server, QuakeC, console or sound, monsters, weapons, shooting, particles, explosions, the status bar and the bonus flashes. On the PC every host frame equals the normal game's playdemo frame. The board app pair `apps/qogdemo` links with 73,728 bytes of hunk, a 34,816-byte stack and 3,788 bytes free. It uses the one-byte z-buffer, which changes 0.073% of the view's pixels. Nothing ran on hardware.

| Part | Where |
| --- | --- |
| Host, Host_Init's order without server, QuakeC, network, console, keys, menus and sound, plus `sv_gravity` (the client's particles fall by it). Each frame is the client half of `_Host_Frame` (rand, frame time, command buffer, `CL_ReadFromServer`, screen, `CL_DecayLights`), and the screen is `SCR_UpdateScreen`'s game view path (V_RenderView, centre print, status bar, intermission overlays, V_UpdatePalette, VID_Update). Nothing is drawn while id's screen would show the forced console or the loading plaque. The demo loop is id's (`CL_NextDemo`, `Host_EndGame` longjmps out of the frame) | `port/common/qog_demo.c` |
| PC. `-qogdemo [-demos ...] [-demodt 0.05] [-demoseed 0] [-demohash] [-demodump a b] [-demoshots n] [-demohunk] [-demomanifest file]` runs the host and prints the peaks per demo. In the game `qog_demoloop <seed> <demos>` starts the same loop between two frames, `qog_quitafter <frames>` lets a script end so the command buffer stays free for the text a demo stuffs, `qog_srand` restarts the random sequence | `port/pc/demo_pc.c`, one call each in `port/pc/sys_pc.c` and `port/pc/vid_pc.c` |
| Board, plays demo1, demo2, demo3 in a loop, gray skips to the next demo, and prints a status line every 2 s. `display/demo_wrap.c` and `display/demo_sv.c` define what the client and renderer refer to in the host, server, console, keys, menus, network and sound objects, so none of them is linked (the server state alone is 11,360 bytes) | `apps/qogdemo` |

Engine flags (each off by default, only `QOG_DEMO_FLASH` and `QOG_ITEMBOX_NOLEAFS` are in the PC and board game lists).

| Flag | What | Saved |
| --- | --- | ---: |
| `QOG_DEMO_FLASH` | A demo in a mapped pack is read in place and each message parsed where it lies (net_message points into the image, given back when playback stops). id's three freads are kept, including a field past the end staying as it was and an empty message stopping playback. No FILE, no message buffer | newlib FILE use, 7.5 KB loop buffer on the demo player |
| `QOG_ITEMBOX_NOLEAFS` | Item box maps (`maps/b_*.bsp`) get no efrag heads or leaf keys. Only `cl.worldmodel`'s are ever used | 1,088 B of hunk per item box, 9.8 KB on e1m3 |
| `QOG_CL_ENTSLOTS=n` | Client entities in n slots, given to entity numbers as they are first used (slot 0 is the world), with id's colormap rule. Loops over all entities still go in number order. Exact | e1m3 62,432 to 20,608 B of hunk with 192 slots |
| `QOG_MAX_TEMP_ENTITIES=n` | Fewer beam segments (id 64) | 4,992 B static with 16 |
| `QOG_EDGECACHE_FRAME=rows` | The world's edge cache in the frame buffer's first rows when the z-buffer has no room for it. Nothing reads them before the world's spans draw over them, and the previous scanout has ended. R_EdgeDrawing stops with an error if the 3D view does not cover them | 25 to 30 KB of hunk with the one-byte z-buffer |
| `QOG_ZBUFFER8_NEAR=shift` | With the earlier `QOG_ZBUFFER8`, 1/z below 1024 as before (shift 3), nearer in steps of 1 << shift above 128, so the weapon keeps its depth order (with one shift everything nearer than 16 units ties and the weapon shows its back faces) | 0 (picture) |

Board settings (`apps/qogdemo/CMakeLists.txt`, its own engine library `qog_engine_demo` as `apps/qogfly`), 480 edge-drawing surfaces (demo peak 412), 1,800 stack edges so the edge table fits the one-byte z-buffer (peak 995), command buffer 256 bytes, 8 cached pics, 256-entry lit surface hash, `QOG_CL_ENTSLOTS=192`, `QOG_MAX_TEMP_ENTITIES=16`, `QOG_ZBUFFER8`, `QOG_ZBUFFER8_NEAR=7`, `QOG_EDGECACHE_FRAME=152`, zone 4 KB, hunk 72 KB, stack 34 KB.

Peaks per demo (PC, board data layout), entity numbers up to 299 / 299 / 260, of which 178 / 169 / 150 are ever used, visible entities 47 / 31 / 55, particles 512 (the pool is full in each), static entities 44 / 40 / 16, models 157 / 139 / 160, dlights 3 / 3 / 7, surfaces 383 / 379 / 412, edges 852 / 789 / 995, temp entities and beams 0, zone 3,184 B. Hunk with the board settings, 25,840 B after the engine start, 63,952 / 64,160 / 59,056 B in the demos (the PC figure with one pak, as on the board).

RAM on the board (`qogdemo_display`, region 262,144 bytes, linked).

| User | Bytes |
| --- | ---: |
| Frame buffer and one-byte z-buffer (`video_mem`) | 97,280 |
| Hunk (`QOGDEMO_HUNK_KB` 72) | 73,728 |
| Stack (`QOGDEMO_STACK_KB` 34, R_EdgeDrawing 23,384, R_AliasDrawModel 9,256, D_PolysetDraw 6,504) | 34,816 |
| Engine statics (`cl_lightstyle` 4,352, `cl` 2,832, MSG_ReadString's 2,048, `mod_known` 2,304, `cls` 1,284, ...) | 37,726 |
| SDK, BSP and libc | 6,079 |
| qog_video and the rest of vid_rp2040 (surface cache header 1,024, 16-bit palette 512) | 4,599 |
| App (`demo_wrap.c`, `demo_sv.c`, `qog_demo.c`, the rest of `main.c` and `sys_rp2040.c`) | 472 |
| Heap left (`free` in the status line) | 3,788 |

Without the one-byte z-buffer the player needs 44,852 bytes more than the board has, without the entity slots another 41,824.

Verification, hidden and silent.

- Host against game, full paks. `qogtest_o_ref1.cfg` to `_ref3.cfg` (`qog_demoloop 0 demoN`, `con_notifytime -1`, `crosshair 0`, `qog_quitafter`) against `-qogdemo -demos demoN -demohash`, 1,486, 1,399 and 1,647 host frames, each run found as one contiguous run in the game's frame hashes (pixels and palette). `qogtest_o_loop.cfg` (the three demo loop) against `-qogdemo`, 4,532 host frames, the three demos contiguous, the game has one more frame between demos (the loading plaque, which the host does not draw).
- Board data layout (`build-board\qogdemo`) against the game on the full paks, the same three results with the normal build and with a PC build of the board settings that keep the picture (`-DQOG_PC_DEMO_BOARD=ON`, entity slots, temp entities, lit surface hash, cached pics, command buffer, edge cache flag).
- One-byte z-buffer (`-DQOG_PC_DEMO_Z8=ON` as well) against that build, every 50th frame of the loop, 91 frames, 13 identical, 0.073% of view pixels differ on average, at most 126 pixels (far models and particle edges), status bar rows identical. With `QOG_ZBUFFER8` alone it was 0.81%, at most 1,144 pixels (the weapon). No edge moves or stack fallbacks of the edges in the z-buffer in any run.
- Normal game against a build of 4626b83 (`build-pc-base`). `qogtest_hash.cfg` state hashes identical, 113 frames of `qogtest_d_shots.cfg` byte-identical (run after `qogtest_k_quiet.cfg`, as two base runs differ in the notify rows otherwise).
- `apps/qogfly` still links.

Lessons from the PC comparison, the host's own `rand()` call is MSVC's or newlib's, because only engine files are built with `-Drand=qog_rand` (it now calls `qog_rand`), `sv_gravity` is a server cvar the client's particles read, and a script's waits hold back the "bf" a demo stuffs (docs/lessons.md L46 to L48).

Build and flash.

```powershell
python tools\mkboard.py --pak ..\quake-2040\data\id1\pak0.pak --pak ..\quake-2040\data\id1\pak1.pak `
    --manifests build-pc-dev\manifests-demo --maps start --demos `
    --exe build-pc-dev\Release\quakeog_pc.exe --stub-maps -o build-board\qogdemo
cmake --preset target -DQOGDEMO_HUNK_KB=72 -DQOGDEMO_STACK_KB=34
cmake --build build --target qogdemo_main
```

The manifests. `start.txt` from the game (`+exec qogtest_o_manifest.cfg`), `demos.txt` from the host (`-qogdemo -zone 4 -demomanifest build-pc-dev/manifests-demo/demos.txt`, full paks). The data image is `build-board\qogdemo.uf2` (10,479,264 bytes of data, pak 5,117,392 with map stubs at 0x10400000 holding the three demos, qogbake.bin 5,301,920 at 0x108F0000, 68 models, 83.7% of the region). The app is `build\apps\qogdemo\qogdemo_main.uf2` (`python third_party/wiliOGbsp/tools/fw.py flash qogdemo --uf2 build/apps/qogdemo/qogdemo_main.uf2`). The USB console prints `[qogdemo] engine started, hunk <used>/<size>, free <bytes>` once (possibly before USB is up), id's messages as they come (`Playing demo from demo1.dem.`, the level name, `You got the shells`, ...), and every 2 s `[qogdemo] <demo> t <cl.time>  fps <f>  ms frame <ms> wait <ms>  free <bytes>  stack <bytes>  hunk <used>/<size>`, frames per second over the last 2 s, the mean time per drawn frame in qog_demo_frame and of it the wait for the previous scanout, the heap left, the deepest stack use so far and the hunk in use. id clamps a frame to 0.1 s, so below 10 fps a demo plays slower than recorded.

- Engine files changed. `engine/cl_demo.c`, `engine/cl_main.c`, `engine/cl_parse.c`, `engine/cl_tent.c`, `engine/client.h`, `engine/d_local.h`, `engine/model.c`, `engine/r_main.c`, `engine/view.c`, all behind the flags above. One new block each in `pc/CMakeLists.txt`, `apps/quake/CMakeLists.txt` and `tools/ram_meter.py`, and the `QOG_PC_DEMO_BOARD` and `QOG_PC_DEMO_Z8` test options in `pc/CMakeLists.txt` (off by default).
- The bake layout hash did not change, so the shared `data/id1/qogbake.bin` still loads.
## 2026-11-23, World renderer speed (PC and ARM build, registered data)

Four exact speedups of the lit span and z span path (byte-identical pictures, on in all three flag lists), two approximate span drawers (off, in no list, for the user to judge), render phase timing for the board's fly-through, and benchmark lines that time and check the engine's own new span code on the board. Nothing ran on hardware. The board savings below are estimates from the generated Thumb code and the E1M1 fly-through's work counts until the benchmark and the fly-through are flashed.

| Change | Flag | Kind | Estimated board saving (E1M1 fly-through) | RAM |
| --- | --- | --- | --- | --- |
| Lit spans without modulo. Power-of-two textures wrap with masks, other sizes (3% of demo1's lit pixels, 0.1% of E1M1's) step wrapped coordinates with compare and subtract. The texture origin is folded into the light grid pointer, each mip level has its own pixel loop with constant shifts, and each run of up to 8 pixels is drawn in two unrolled passes (light blend, then texel and colormap) so each pass fits the M0+'s low registers | `QOG_LITSPAN_FAST` | exact | inner loop about 150 to about 65 cycles a pixel (old, 73 instructions and two divider calls, new, 37 + 14 instructions, 4 multiplies, 3 + 3 stack reloads), about 4.1 M cycles or 20 ms a frame at 48,400 lit pixels | +40 B static, D_DrawSpans8Lit stack 216 to 296 B, flash +6.5 KB |
| The last run of each span reuses `d_*stepu * (spancount - 1)`, computed once per span length per surface | `QOG_LITSPAN_TAILMUL` | exact | an int to float and three float multiplies per span, about 1.4 ms at 1,620 spans | +96 B stack |
| z span start without double arithmetic. `(int)(zi * 0x8000 * 0x10000)` with zi a double holding a float is exact (both factors are powers of two), so it is the float's mantissa shifted by its exponent, truncated toward zero. abs(zi) >= 1, infinities and NaNs keep id's expression | `QOG_ZSPANS_FAST` | exact | float to double, two double multiplies and double to int per z span (about 200 cycles), about 1.7 ms | 0 |
| Lit spans write their own z (`D_QogZSpan`) from the 1/z they compute at each span start with id's expression, and D_DrawSurfaces skips D_DrawZSpans for them | `QOG_LITSPAN_ZFUSE` | exact | two int to float, two float multiplies and two float adds per lit span, a second walk of the spans and a call per surface, about 2.4 ms | stack +40 B (D_DrawSpans8Lit 392 to 432 B, D_QogZSpan 32 B) |
| Light blended exactly at both ends of each run and stepped linearly between them (one pass, up to 16 pixels) | `QOG_LITSPAN_LINLIGHT` | approximate, off | inner loop 21 instructions a pixel plus two blends a run, about 24 cycles a pixel, about 6 ms | 64 B flash table |
| One float divide per 16 pixels instead of 8, as id's D_DrawSpans16 did on x86 | `QOG_LITSPAN16` | approximate, off | half the per-run float work (3 adds, a divide, 2 multiplies, 2 conversions per run), about 6 to 7 ms | +96 B stack |
| Render phase times and work counts | `QOG_PHASE_TIME` | measurement, off in shipped builds, on in qogfly (`QOGFLY_PHASE`) | costs about 2,000 timer reads and three span-list walks a frame | +96 B static, qogfly only |

Pixel differences of the approximate drawers over the 113 `qogtest_d_shots` frames (3D view rows only, max colour step is the largest change of one RGB channel through the palette).

| Build | Frames differing | Pixels differing | Max colour step | Worst frame |
| --- | ---: | ---: | ---: | --- |
| `QOG_LITSPAN_LINLIGHT` | 113 | 1.27% | 60 | d_spawn_e3m3, 5.2% |
| `QOG_LITSPAN16` | 113 | 6.2% | 184 | d_demo3_06, 21% (texels shifted by one at grazing angles) |
| both | 113 | 9.1% | 192 | d_demo3_15, 23% |

- Work counts (PC, `QOG_PHASE_TIME`), E1M1 fly-through (1,618 frames 0.1 s apart) per frame 71 lit surfaces, 2,769 light cells built, 1,622 lit spans, 48,363 lit pixels (39 on the any-size path), 6,811 runs of up to 8 pixels, 45 sky and 224 turbulent pixels. timedemo demo1 per frame 60 lit surfaces, 3,346 light cells, 1,509 spans, 46,506 lit pixels (1,564 any-size), 6,514 runs. Per lit pixel the float work is about 60 cycles (a run's 3 adds, divide, 2 multiplies and 2 conversions plus each span's start), the light grid build about 0.4 ms a frame.
- PC speed, no measurable change. timedemo demo1 runs at the 60 fps cap with every build, and the `-qogfly` wall time over 8,000 frames varied by 30% between runs of the same build. On x86 the modulos and floats this work removes are cheap. On the PC the phase split of the E1M1 fly-through is 0.24 of 0.43 ms in lit spans.
- Verification, hidden and silent, against an untouched build of 4626b83 (`build-pc-base`), each run with `+_cl_color 0` and the frame-compared scripts after `qogtest_k_quiet.cfg`, `qogtest_hash` state hashes identical, 113 `qogtest_d_shots` and 64 `qogtest_e_shots` frames byte-identical (the 8 files `qogtest_e_shots1` to `7` are chained by `qogtest_e_shots.cfg` and are not meant to run alone), `qogtest_n_flyg` 400 view hashes and 4 shots identical. Per-frame hashes. `qogtest_h_demos` 3,610 frames and `qogtest_h_fight1` to `4` 37,159 frames equal except runs of at most 10 frames at startup and at each map load. Frames 700 to 715 of fight 1 were dumped from both builds, the differing ones differ only in one 6-row console text line scrolling up (the load timing), the 3D view is identical.
- On the board, `apps/qogbench` now includes `engine/d_litspan.h` and checks the new drawer against the old code byte for byte at every mip level, on a power-of-two and a 320×200 texture, full and ragged spans (`chk_p2`, `chk_any`), and the z start against id's double expression over 2 span sets (`chk_z`). Each should print 0.
- Engine files changed. `engine/d_scanlit.c`, `engine/d_scan.c` (D_QogZiFixed, D_QogZSpan), `engine/d_edge.c` (phase hooks, the z skip), `engine/r_main.c` (phase hooks only). New. `engine/d_litspan.h`, `engine/qog_phase.h`, `port/common/qog_phase.c`. One new block at the end of each flag list.
- Looked at and left, placing the span code in RAM (the earlier benchmark showed no gain and RAM is short, the new D_DrawSpans8Lit is 9.5 KB of flash code but one mip variant plus the float part, about 2 KB, is hot per surface), R_BuildLightMap (about 0.4 ms a frame on E1M1, fusing its three passes would save about 0.15 ms), exact replacements of the per-run float divide (the ROM routines are already the fast path, any fixed-point form changes s and t).

Board measurement, prepared and not run (the coordinating session flashes).

```powershell
cmake --preset target -DQOGFLY_HUNK_KB=18 -DQOGFLY_STACK_KB=34
cmake --build build --target qogbench_main
python third_party\wiliOGbsp\tools\fw.py flash qogbench --uf2 build\apps\qogbench\qogbench_main.uf2
python third_party\wiliOGbsp\tools\fw.py console
cmake --build build --target qogfly_main
python third_party\wiliOGbsp\tools\fw.py flash qogfly --uf2 build\apps\qogfly\qogfly_main.uf2
```

The benchmark keeps its first line and adds a second every 2 s (each group is refreshed every third round, about 3 s).

```
[bench] fast old_p2=<us> fast_p2=<us> fast_any=<us> chk_p2=<n> chk_any=<n> | approx lin_p2=<us> s16_p2=<us> s16lin_p2=<us> | short old=<us> fast=<us> zid=<us> zfast=<us> chk_z=<n>
```

old_p2 is today's code on a 256×128 texture in flash with the texture origin a whole light cell, fast_p2 the shipped drawer on the same scene, fast_any the shipped drawer on the 320×200 picture, lin, s16 and s16lin the approximate drawers. short repeats old and new on game-like spans (17 to 47 pixels), and zid and zfast are the z spans with id's double start and with `QOG_ZSPANS_FAST`. Every value is microseconds per 320×152 frame. The fly-through prints after its status line every 2 s.

```
[phase] frames <n> ms setup <ms> world <ms> bmodels <ms> scan <ms> lmap <ms> grad <ms> spans <ms> zspans <ms> sky <ms> turb <ms> models <ms> vmodel <ms> parts <ms> total <ms> edgescan <ms>
[phase] per frame: litsurfs <n> lmcells <n> litspans <n> litpx <n> anypx <n> runs <n> zpx <n> skypx <n> turbpx <n>
```

Times are means per frame over the 2 s. scan includes the surface drawing (lmap, grad, spans, zspans, sky, turb), edgescan is scan less those, the edge sorting and span making. With `QOG_LITSPAN_ZFUSE` the lit surfaces' z is inside spans. `-DQOGFLY_PHASE=OFF` removes it. The fly-through's static RAM grows by 132 bytes (36 for the span drawer, 96 for the phase sums), flash by 8.1 KB.

## 2026-11-23, E1M1 fly-through runs on the board (hardware)

- The renderer-only fly-through (apps/qogfly) draws the real E1M1 from the flash data image on the display CPU, the camera following a 79-point route. Measured over 75 s, 5.7 to 9.3 fps (average 7.0), 85 to 152 ms render per frame, plus a 22.5 ms wait for the single-buffered scanout. 13 KB of an 18 KB hunk in use, 3.3 KB of RAM free. The user's photo shows E1M1's textures, lighting and palette correctly.
- Board engine built with 4-byte enums (`-fno-short-enums`, lessons L46). Without it the board refused the baked image.
- `QOG_SND_ONINIT` and `QOG_LOOP_ONINIT`, sound channels, the paint buffer and the loopback receive buffers come from the hunk when those systems start, so hosts that never start them pay nothing. Static RAM 97.0 to 80.3 KB, hunk +20 KB in the game (the same bytes, moved).
- `QOG_PACKDIR_CHUNKED`, the pack directory is read 2 KB at a time instead of through a 21 KB temporary block. Same CRC, checks and hunk order.
- `QOG_PARTICLES_PRESET`, a host may keep its own particle array, so the fly-through takes no 18 KB particle block.
- apps/qogfly builds its own engine library with 480 edge-drawing surfaces (E1M1 tour peak 443). The game keeps 800. Hunk 18 KB, stack 34 KB.
- Data image, start plus E1M1 with map stubs, 7.15 MB (tools/mkboard.py, built with the same build so the image layout matches).

## 2026-11-23, Renderer tables in the z-buffer (PC and ARM build, registered data)

R_EdgeDrawing's stack frame on the ARM build 84,768 to 36,184 bytes (−48,584). The world's edge cache leaves the hunk, E1M1 hunk low 364.0 to 337.3 KB (−27,312 bytes), about 33,870 bytes on the largest map (e3m5, 16,923 edges). Engine static RAM +40 bytes (board overflow 59,148 to 59,188 bytes). The picture is unchanged. Nothing ran on hardware.

| Change | Flag | Saved (measured) |
| --- | --- | ---: |
| The edge table and its four sentinels live at the end of the z-buffer during R_EdgeDrawing, the brush clip buffers at its start. Spans go in the free end of the surface table plus `QOG_SPAN_MIN` (512) spans of reserve | `QOG_EDGES_IN_ZBUFFER` | 48,584 B stack (ARM) |
| The world's 16-bit edge cache lives at the start of the z-buffer while R_RenderWorld runs, filled with "never" every frame. Brush entity faces stop storing in it (nothing reads those entries). A map whose cache does not fit in front of the edge table gets it on the hunk in R_NewMap | `QOG_EDGECACHE_ZBUFFER` | 27,312 B hunk on E1M1 (PC) |

- How the edges share the z-buffer, a world span writes z only in its own row, and spans are made top to bottom. R_ScanEdges moves the used edges (linked by 16-bit offsets, so one memmove and a new base) to the end of the z-buffer. Spans of rows above the edges are flushed whenever the span buffer fills, as id does, and once more before the first row that reaches the edges. When the buffer fills while spans wait, the edges no longer needed are dropped and the rest moved down (`R_QogMoveEdges`). If the waiting spans still overlap them, the live edges move to a table on the stack just large enough (`R_QogEdgesToStack`, at most the old 48,120 bytes), and the scan goes on with id's flushing. Each pixel is drawn by one span whichever flush draws it.
- Fallbacks over the full game with the shipped room (the demos, the tour of e1m1 e3m6 e4m7, the fight tour of every map, the shots), the stack fallback never ran, the edge move ran 39 times (all in the tour), at most 1,289 spans waited where the room was at least 1,178. With the span room cut to 330 spans (a test build) the demos alone needed 4,928 moves and 832 stack fallbacks, and every frame was still the same.
- Why the surfaces and spans stay out of the z-buffer, the last flush writes the z rows under the edge table while it still reads every waiting span and its surface, so something has to hold them outside the z-buffer. The stack fallback exists for the same reason, the waiting spans have no upper bound, so a frame can always be built that needs the old table. On the board the fallback path needs up to 48,120 bytes more stack than the normal one.
- Edge cache, entries from earlier frames never match in id's code (an edge below edge_p owned by the same map edge was emitted this frame, which rewrote the entry), so a fresh cache each frame gives the same answers. The largest full-game map (e3m5) needs 33,846 bytes, which fits in front of the 48,096-byte edge table with 15,338 to spare.
- Z-buffer use in one frame, byte offsets in the 97,280 bytes, R_RenderWorld, edge cache at 0 (up to 33,846) and edge table at 49,184 to the end. R_DrawBEntitiesOnList, brush clip buffers at 0 (4,608) and the edge table. R_ScanEdges, z rows from the top down to the used edge table packed at the end, which shrinks as edges die. After the last flush, all z. Between frames, the first 15,360 bytes hold the status bar rows (QOG_HUD_IN_ZBUFFER) and the rest is free.
- Temp region for later changes (not used yet, the message buffers are the earlier). `r_ztemp_base` and `r_ztemp_bytes` (r_shared.h) describe the whole z-buffer. Free for anything short-lived from the end of R_RenderView to the start of the next R_RenderView, bytes 15,360 to 97,280 (81,920 bytes) while the status bar and the scanout use the first 15,360, and the whole buffer between R_RenderView's start and R_EdgeDrawing. Not across R_RenderView, and not while the board scans out the status bar rows.
- Looked at and left, the PVS row (`decompressed` in Mod_DecompressVis, 1 KB) is shared with the server's fatpvs and checkpvs and lives in model.c, and the node and leaf visframe stamps must persist between frames (R_MarkLeaves only runs when the view leaf changes). Both belong to earlier work.
- Verification, hidden and silent, against a build of 23a89cb, state hashes in `qogtest_hash`, `qogtest_f_hash` and `qogtest_e_shots`, 195 shots (`qogtest_d_shots`, `qogtest_e_shots`, `qogtest_f_hash`, `qogtest_demo`, `qogtest_sky`) byte-identical, and 44,529 frame hashes (`qogtest_h_demos`, `qogtest_h_tour`, `qogtest_h_fight1` to `4`) equal except console frames at startup and at map loads, which show the build time or the "server frames so far" timing (dumped and checked). Each flag was checked on its own commit, the edge table also with the span room cut to 330 and 700 spans, the edge cache also forced onto the hunk. The new `qogtest_k_quiet.cfg` sets `con_notifytime -1` for these runs, without it the notify line follows real time and two runs of the untouched build differ on a few frames.
- Edits outside the earlier files. `engine/model.c` (the edge cache allocation only, behind `QOG_EDGECACHE_ZBUFFER`), and one new block each in `pc/CMakeLists.txt`, `apps/quake/CMakeLists.txt` and `tools/ram_meter.py`.
- The bake layout hash did not change (no baked structure changed), so the shared `data/id1/qogbake.bin` still loads.

## 2026-11-23, Per-level map arrays (PC and ARM build, registered data)

The map's hunk block (the per-frame arrays `Mod_FinishBrushModel` allocates next to the edge cache) on E1M1 92,128 to 34,720 bytes, on e3m5 (the worst map) 116,112 to 43,024, on e4m7 115,136 to 42,528. Static RAM +6,164 bytes (the light hash). Net saving 51.2 KB on E1M1, 65.4 KB on e3m5. The edge cache (27,024 bytes on E1M1, the earlier) is untouched and still allocated in the same place. Every change keeps the picture byte-identical and is on in both flag lists. Nothing ran on hardware.

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

- `QOG_FRAMEBITS`, id's stamps are only compared for equality with a counter that never goes back, so an array is kept as one bit per entry plus the counter the bits belong to. A write with another counter clears the bits first. A fresh array has tag 0 and every bit set, like id's zeroed stamps. The clear costs numsurfaces/8 bytes of memset once per frame (690 bytes on E1M1) and the node and leaf bits once per `R_MarkLeaves`. A leaf's visframe was not recomputed from the PVS row instead, nodes need their own bits anyway, and the bits are smaller than the PVS row of a large map.
- `QOG_DLIGHT_SPARSE`, the dlightframe stamp is one bit per surface (as above) and the bits of the lit surfaces sit in a 1,024 entry open hash (`QOG_DLIGHT_SURFS`, 2-byte surface number and 4-byte bits, emptied with the bits). `R_PushDlights` only notes the world pass. It runs at the first question about a lit surface in the frame (`R_DlightLit`, behind `SURF_DLIGHTFRAME_IS`), after `R_RenderWorld` has stamped the visible surfaces, and marks only world surfaces visible in this frame, no other world surface is drawn (`R_RecursiveWorldNode` tests the same stamp). The brush model pass in `R_DrawBEntitiesOnList` marks as before. A second world pass for the same counter before the first ran (only when no view was drawn in between) runs the first one in full, as id's marks add up. If the hash fills, the lost lights are remembered and `R_DlightBits` finds their bits by walking the world tree and the drawn brush model's tree with `R_MarkLights`' arithmetic.
- Peaks over the full-game fight tour (`qogtest_h_fight1` to `4`, rockets, explosions, muzzle flashes), at most 573 lit visible surfaces in one frame (e1m6), so the hash is at most 56% full. Marking every surface in reach, as id does, gave 1,918 on e3m6, which is why the world pass is deferred.
- `QOG_LEAF16`, leaf keys are 16-bit (a frame's keys count nodes, leafs and drawn surfaces, `LEAF_KEY_SET` stops with an error rather than cut one). A leaf's efrag list head is one of 256 world heads selected by a byte per leaf, so `LEAF_EFRAGS` stays a pointer lvalue (`R_NewMap` in r_main.c is unchanged). Once the heads run out further leafs share the last one and `R_StoreLeafEfrags` skips the efrags of other leafs in it, which keeps id's order per leaf. Most heads in use over the fight tour, 191 including the NULL head 0 (e1m4).
- Verification, hidden and silent, against a build of 23a89cb (`build-pc-base`), state hashes identical in `qogtest_hash.cfg`, `qogtest_f_hash.cfg` (32), `qogtest_e_shots*.cfg` (base and new build run side by side, as the shared data folder made base's own hashes drift between runs). 177 frames of `qogtest_d_shots.cfg` and `qogtest_e_shots*.cfg` and the 8 `qogtest_f_hash` frames byte-identical. With `-nobake` `qogtest_hash` and the 113 demo frames identical. `qog_bake check` 0 differences, "Baked models, 133". Per-frame hashes over the demos and the four fight tours (`qogtest_l_demos.cfg`, `qogtest_l_fight1..4.cfg`, copies of the earlier scripts) were compared by dumping every frame of fight tours 1 and 4 from both builds, of 2,847 frames of tour 4, 2,801 are identical, 40 differ only in the notify rows 0 to 31 and 6 are the startup console with the build time. Of 5,913 frames of tour 1, 5,800 are identical, 71 differ only in rows 0 to 31, and the other 42 differ only in console text (build time, "7.7 megabyte data cache" for 7.6, the server time of the `qog_mem` report at each map change). The other differing hashes are notify lines in rows 0 to 31 that show for a real-time span and differ between two runs of the base build too (45 of 2,845 frames of fight tour 4 between two base runs).
- Fallbacks checked with a build using `QOG_DLIGHT_SURFS=4` and `QOG_EFRAG_HEADS=4` (the hash overflows in nearly every lit frame, most leafs share a head), the same per-frame hashes as the normal build over the demos and all four fight tours, apart from the same noise. Builds with `QOG_FRAMEBITS` alone and with `QOG_DLIGHT_SPARSE` added ran the demos and fight tours with the same kind of differences only (not dumped frame by frame), and the first gave `qogtest_hash` state hashes equal to base run next to it.
- Speed on the PC (timedemo demo1, two runs each, noisy machine), base 18.2 to 19.1 s for 970 frames over five runs, new build 17.5 to 20.1 s, no difference above the noise. Per frame the new work is one memset of the surface bits, a hash probe per lit drawn surface and the deferred world pass, which visits the same nodes as before.
- Edits outside the earlier files. `engine/r_bsp.c`, three lines in `R_RecursiveWorldNode` (the surface visframe write through `SURF_VISFRAME_SETNUM`, the efrag store through `R_StoreLeafEfrags`, the key through `LEAF_KEY_SET`). One new block each at the end of the lists in `pc/CMakeLists.txt`, `apps/quake/CMakeLists.txt` and `tools/ram_meter.py`.
- The bake layout hash did not change (`model_t` and `framestamp_t` keep their sizes, `modframe_t` is RAM only), so `data/id1/qogbake.bin` needs no rebuild.
- Board (`quake_display`, 64 KB placeholder hunk), static RAM overflow 65,308 bytes with these flags. The light hash adds 6,164 bytes of static RAM (`tools/ram_meter.py`), while each level's hunk needs 57 to 73 KB less. The deferred world pass runs from inside `D_DrawSurfaces`, so `R_MarkLights`' recursion (80 bytes a level on the ARM build, `R_RecursiveWorldNode` takes 88) now starts about 330 bytes deeper in the stack than `R_RecursiveWorldNode` does. Not measured on the board.

## 2026-11-23, E1M1 fly-through, the first real-scene board app (PC and ARM build, registered data)

A renderer-only host draws the real E1M1 from the baked image with a camera moving through the level, on the PC and as the board app pair `apps/qogfly`. No game logic, monsters or sound. On the PC its frames are byte-identical to the normal game's renderer at the same camera positions. On the board the display half does not link yet, RAM overflows by 134,644 bytes, as expected before agents K (edge tables out of the stack) and L (per-level map arrays) are merged. Nothing ran on hardware.

| Part | Where |
| --- | --- |
| Host, memory, packs, wad, palette and colormap in place, VID_Init, Draw_Init, R_Init, then `maps/e1m1.bsp` through Mod_ForName as `cl.worldmodel`, R_NewMap, every light style `m`, a 320×152 view with fov 90 as SCR_CalcRefdef sets it, R_RenderView per frame, text in the status bar rows | `port/common/qog_fly.c` |
| Path, info_player_start, then item, weapon and monster origins from the entity lump (eye in solid skipped, closer than 96 units skipped, as qog_tour), moved away from walls within 32 units. Spots that see each other (a line 24 units wide at eye height) form a graph, and the route visits every spot by shortest sight-line paths, back to the start. Where no sight line exists the camera cuts. Closed Catmull-Rom spline at 160 units per second, the view looks along the path's direction averaged over 1.2 s, pitch half the climb, at most 20° | `port/common/qog_fly.c` |
| PC. `-qogfly` runs the host standalone, `qog_fly` draws the same frames on the world the game loaded, `qog_fly_ref` compares the game's own view of each route point with the host's | `port/pc/fly_pc.c`, one call each in `port/pc/sys_pc.c` and `port/pc/vid_pc.c` |
| Board, the display app moves its stack into a main RAM array (R_EdgeDrawing alone needs 84,768 bytes), prints a status line every 2 s, and replaces at link time the engine calls that would link the console, menus, sound and server (`--wrap`) | `apps/qogfly`, `port/rp2040/sys_rp2040.c` (`QOG_SYS_NO_MAIN`, `qog_rp2040_hunk`) |

E1M1, 60 spots, a route of 79 points with 7 cuts, a 161.8 s loop. 7 of the 1,618 frames of one loop (0.1 s apart) have the eye in solid, where the spline swings into a wall for a moment. Before the cuts it was 273 of 2,400.

Verification (hidden and silent).

- `-qogfly` against `qog_fly` in the full game after `map e1m1` (new `qogtest_n_flyg.cfg`), 400 frames 0.4 s apart, all view hashes identical, with the full paks and with the board layout.
- `qog_fly_ref` (new `qogtest_n_ref.cfg`), the player stands at each of the 79 route points (noclip, god, notarget, not solid so it picks nothing up, not on ground), looking the way the path does there as the protocol rounds the angles, with `r_drawentities 0`, `r_drawviewmodel 0`, `crosshair 0` and light styles `m`. After the game has drawn the spot through V_RenderView, the host draws it at the same cl.time. 79 of 79 views identical, with the full paks and with the board layout. Two earlier differences were in the test, not the host, the console still scrolling up on the first spot, and a quad damage picked up on the way, whose light brightened the game's view.
- Board layout (`build-board\qogfly`, below) against the full paks, 2,400 standalone frames identical.
- Normal game unchanged against a build of 23a89cb. `qogtest_hash.cfg` state hashes identical, `qogtest_d_shots.cfg` 113 frames byte-identical.

Hunk, the host's whole hunk is 98,112 bytes on the PC (zone 3 KB, of which 1.3 KB used, 92,128 bytes of E1M1 per-level arrays, the route 1.4 KB, the rest command and cvar blocks). The route work space (6 KB) is on the stack, which is idle at that depth then. R_Init's 18 KB of particles are given back. `-heapsize 96` runs, 95 stops with Hunk_Alloc failed. The board uses `QOGFLY_HUNK_KB` 97.

RAM on the board (`qogfly_display`, region 262,144 bytes).

| User | Bytes |
| --- | ---: |
| Frame buffer and z-buffer (`video_mem`) | 145,920 |
| Hunk (`QOGFLY_HUNK_KB` 97) | 99,328 |
| Stack (`QOGFLY_STACK_KB` 94, R_EdgeDrawing 84,768 plus the render chain and interrupts) | 96,256 |
| Engine statics (`sv` 11,344 for R_SetupFrame's `sv.active`, `cl_lightstyle` 4,352, `cl` 2,824, `mod_known` 2,304, `menu_cachepics` 1,792, `blocklights` 1,296, ...) | 38,853 |
| SDK, BSP, libc, app | 7,881 |
| qog_video | 2,979 |
| Heap reserve | 2,048 |
| Overflow | 134,644 |

What is missing, about 84 KB of stack (earlier edge tables in the z-buffer) and about 51 KB more, which earlier per-level map arrays (92 KB of the hunk) can give. With the stack at about 10 KB the hunk may be at most about 45 KB.

Build and flash, once it fits.

```powershell
python tools\mkboard.py --pak data\id1\pak0.pak --pak data\id1\pak1.pak `
    --manifests build-pc-dev\manifests --maps start e1m1 `
    --exe build-pc-dev\Release\quakeog_pc.exe --stub-maps -o build-board\qogfly
cmake --preset target
cmake --build build --target qogfly_main
```

The data image is `build-board\qogfly.uf2` (7,498,464 bytes of data, pak 4,515,568 with map stubs at 0x10400000, qogbake.bin 2,976,480 at 0x10850000, 48 models). The app is `build\apps\qogfly\qogfly_main.uf2` (fw flash qogfly). The USB console prints `[qogfly] e1m1 loaded, 79 route points, 161.8 s loop, hunk <used>/<size>` once (possibly before USB is up) and then every 2 s `[qogfly] fps <f>  ms render <r> present <p>  pt <i>/79  free <bytes>  hunk <used>/<size>`, frames per second over the last 2 s, mean ms per frame in R_RenderView plus the text and in VID_Update (the wait for the scanout), the route point last passed, the RAM between the end of the heap and the heap limit, and the hunk in use. The status bar rows show the fps and render time. Nothing has measured it yet.

- Link setup of the board app, not engine changes. `-Wl,--allow-multiple-definition` (qog_engine links pico_stdlib PUBLIC, so the SDK's sources are in the library and in the app, and `d_local.h` defines five globals in every file that includes it), and `--wrap` for Con_Printf, Con_SafePrintf, Con_DPrintf, Host_Error, S_ExtraUpdate, qog_mem_register and qog_aim_register. The wraps cut 24,688 bytes of statics (the sound mixer, console, keys, menus, statehash). `sv` stays, for R_SetupFrame. `quake_display` has the same duplicate definitions and does not link either way.
- Engine files changed, none.

## 2026-11-23, Server, client and system memory (PC and ARM build, registered data)

Hunk low (PC, `qog_mem`) on E1M1 364.0 to 325.9 KB, on e4m7 (largest) 472.5 to 426.5 KB, on e3m4 460.3 to 401.0 KB. The two board-only sizes take another 12 KB off every map (E1M1 about 313.9 KB on the board). Engine static RAM on the ARM meter 93.4 to 89.2 KB, board link overflow 59,148 to 55,512 bytes. Nothing ran on hardware.

| Change | Flag | Builds | Saved |
| --- | --- | --- | --- |
| Entity lump strings, console command entries and loaded light styles share hunk blocks (`Hunk_AllocPacked`, zone.c). id gave each its own block, a 16-byte header plus 16-byte rounding | `QOG_HUNK_PACKED` | PC and board | E1M1 15,376 B (22,560 to 7,184), e4m7 about 22 KB (its total saving less the fixed ones) |
| The 16 KB player colour table is built on the stack for the one draw that needs it (`R_AliasDrawTranslated`). The entity's colormap points at its scoreboard entry | `QOG_TRANSLATE_ONDRAW` | PC and board | 16,384 B on every map |
| Scripts inserted into the command buffer with memmove, not copied through the zone | `QOG_CBUF_INPLACE` | PC and board | zone peak no longer includes up to 8 KB of commands (measured peak now 7,920 B) |
| net_message read in place in the loopback receive buffers. id's move of the rest of the buffer waits for the next read or send on that buffer. Demos are read into the idle client buffer | `QOG_LOOP_INPLACE` | PC and board | 6,528 B on every map |
| Palette read in place from the mapped pack, like the colormap | `QOG_PALETTE_FLASH` | PC and board | 800 B |
| keyshift and key_repeats as bytes (repeat count stops at 255), one slot in the network driver, LAN driver and server list tables, `cls.spawnparms` 1,024 bytes | `QOG_STATIC_TRIM` | PC and board | 4.2 KB static |
| All edicts in use, take the free edict freed longest ago before id's Sys_Error | `QOG_EDICT_REUSE` | PC and board | 0 (safety) |
| Command buffer 4 KB (measured peak 2,158 B) | `QOG_CMDBUF_SIZE=4096` | **board only** | 4 KB |
| Zone 16 KB | `QOG_ZONE_BOARD_KB=16` | **board only** | 8 KB |

- Board-only flags are set in `apps/quake/CMakeLists.txt` and `tools/ram_meter.py` only. The PC keeps 8 KB and 24 KB, which the long test scripts need. A 12 KB zone failed at startup from fragmentation (8,744 B used, 3,512 B free in pieces of at most 152 B), 16 KB ran every test. A PC build with both board values gave the base state hashes (`qogtest_hash`, the 8-map fight with save and load, save and load) and the base status bar frames.
- `QOG_TRANSLATE_ONDRAW`, before the first colour update the table is all zero, as id's untouched hunk was. The new 16 KB frame sits under `R_DrawEntitiesOnList`, beside R_EdgeDrawing (84.8 KB) and not under it, the alias chain is about 32 KB on the ARM build. It costs a 16 KB copy from the colormap per drawn player-coloured model, which only chase view (or a copy of the player's body) draws. The scores entry also explains why the earlier idea of pointing at the colormap is not exact, colours 0 0 still remap ranges 16 to 31 and 96 to 111, only colours 1 6 give the identity.
- `QOG_EDICT_REUSE` was checked with a 167-slot test build on e1m1 under super nailgun fire, the fallback ran 97 times and the run finished. A full array of live edicts still stops with id's error. `QOG_EDICT_MARGIN` stays 128, the fallback only helps when free slots exist, and a long fight can fill the array with live gibs and backpacks.
- Verification, hidden and silent, against a build of 23a89cb (`+_cl_color 0` on every run), identical state hashes in `qogtest_hash`, the 8-map fight with save and load and `qogtest_m_save` (copies with their own save names), also with `-DQOG_AOT=ON` against the interpreted base. Byte-identical shots, 113 `qogtest_d_shots`, 8 fight end frames, 10 new chase view frames (`qogtest_mm_chase.cfg`, four colour pairs, two maps), 12 menu pics apart from the blinking cursor cell. Per-frame hashes of `qogtest_h_hud.cfg` (997 frames) and the chase script match one of two base runs on every frame except the startup console (build time) and frames that show a notify line or a map load, which also differ between two base runs. `qogtest_reg.cfg` logs equal apart from memory lines and the render peaks, which also differ between two base runs (the turning in that script follows real time).
- Shared config. `data/id1/config.cfg` is saved by every run. A chase test that changed the player colours left `_cl_color` non-zero there, which changes the player's edict (team) and therefore every state hash. All my runs now pass `+_cl_color 0`. Other agents comparing hashes across time should do the same or check the saved colour.
- Looked at and left. `leafnums` (recomputing PVS leafs per frame would cost a BSP walk per entity on the M0+), `sv.signon` (6.5 KB static, worst map still needs 5 KB, and a late makestatic would overflow an exactly sized copy), `client_t.message` (datagrams are appended to the client buffer before the reliable message, so the two cannot share it), `MSG_ReadString`'s 2 KB buffer, `cl_lightstyle`, the particle count.
- Edit outside the earlier files. `engine/r_alias.c` (`R_AliasDrawTranslated` and the branch to it in `R_AliasDrawModel`).

## 2026-11-23, Flash budget for the board's data image (PC and ARM build, registered data)

Start plus episode 1 (`start`, `e1m1` to `e1m8`) in the board's data region, 20,833,408 to 16,845,808 bytes (19.87 to 16.07 MB of 11.94). It still does not fit, by 4,328,432 bytes. `start` to `e1m4` now fits (11.89 MB, 45,472 bytes to spare), where before only `start e1m1 e1m2` did. Engine static RAM on the ARM meter unchanged (118.2 KB). Nothing ran on hardware.

| Change | Flag | Data region |
| --- | --- | --- |
| Maps moved into the baked image. `qog_bake -bakestubs` copies what a map points at in the pack (light, vis, entities, vertexes, edges, surface edges, clip nodes, mark surfaces, texture pixels) into the image, each distinct texture's pixels once for all maps (1,794,520 bytes not stored twice). The pak keeps a 132-byte stub per map, matched by length and hash at any address. `mkboard --stub-maps` | `QOG_BAKE_STUBS` | −4,053,136 bytes (19.87 to 16.00 MB) |
| Sounds the client loads by name kept in the board pak (ambient loops, temp entity, menu and chat sounds). Six were missing, since the manifests are recorded with sound off | none (mkpak) | +110,512 bytes |
| The demo loop plays only the demos present. The board pak has none, and the attract loop stopped with a missing file error. It now opens on the console | `QOG_DEMOS_OPTIONAL` | 0 (demos were already left out, 0.52 MB) |
| The three pics no engine code draws (`gfx/sell.lmp`, `gfx/dim_mult.lmp`, `gfx/vidmodes.lmp`) left out of the board pak | none (mkpak) | −72,552 bytes |

- Layout for start plus episode 1, pak with stubs 6,577,520 bytes at `0x10400000`, 41,616 bytes of padding, `qogbake.bin` 10,226,672 bytes at `0x10A50000` (81 models), end at `0x11410BF0`.
- How it stays exact. `mkpak --maps-last` puts the maps after every other file, the bake runs on that full pak for the address after the pak with stubs (`-bakebase` may lie inside the full pak with `-bakestubs`, and any pack pointer past it makes the model unbakeable), and `mkboard` checks that no other file moves and that every map is in the image against its stub. The full pak is kept as `<out>\src.pak` for `qog_bake check -bakesrc`.
- Verification, hidden and silent, against a build of 466cff4 with the full paks, the new `qogtest_j_ep1.cfg` (then `_ep1b`, `_ep1c`) visits `start` and `e1m1` to `e1m8` with `host_framerate 0.05`, 2 frames and a state hash per map. All 9 hashes and 18 frames identical on the board layout and on the full paks with the new build. `qogtest_hash` identical on all three and with `-nobake`. `qog_bake check -bakesrc` 0 differences over 81 models. The same tour with the mixer on (`-silentsound +volume 0`, no audio device opened) and the new `qogtest_j_menu.cfg` (every menu) show no load error on the board layout and the same 9 hashes as the full paks. `start` to `e1m4` checked the same way.
- What is left needs the user. Exact sizes in docs/game-data.md. `QOG_DATA_OFFSET` to 0x200000 gives 2,097,152 bytes (app area 1.87 MB), to 0x180000 2,621,440 bytes (app area 1.37 MB, too small for compiled QuakeC), 4-bit ADPCM about 1,284,085 bytes, alias model stubs up to 182,596 bytes, `progs.dat` without statements 167,520 bytes. All five give at most 4,255,641 bytes, still short of 4,328,432. With the offset at 0x200000 alone `start` to `e1m5` fits (13.37 MB of 13.94).
- Edits outside the earlier files. `engine/host_cmd.c` (the demo loop, behind `QOG_DEMOS_OPTIONAL`), one declaration in `engine/common.h` (`COM_HasFile`), and one new block each in `pc/CMakeLists.txt`, `apps/quake/CMakeLists.txt` and `tools/ram_meter.py`.
- The shared `data/id1/qogbake.bin` needs no rebuild, the layout hash did not change, and without `-bakestubs` the bake writes what it wrote before.

## 2026-11-23, Video memory and renderer RAM (PC and ARM build, registered data)

Board static RAM overflow 186,540 to 145,688 bytes (−40,852). Video memory 158,720 to 145,920 bytes. The deepest render stack chain is about 3.4 KB shorter. Every part A change keeps the picture byte-identical and is on in both flag lists. The part B options change the picture and are off. Nothing ran on hardware.

| Change | Flag | Saved (measured on the ARM build) |
| --- | --- | ---: |
| sintable and intsintable precomputed in `engine/r_turbtab.h`, `r_avertexnormals`, the alias box edges and the polyset edge table `const` | `QOG_CONST_RTABLES` | 5,952 B static |
| Row offsets of the screen, the z-buffer and the alias skin computed with one multiply, not read from `d_scantable`, `zspantable` and `skintable` | `QOG_NO_ROWTABLES` | 3,516 B static |
| Sky composed per drawn pixel from the texture (front layer if non-zero, else back), no 16 KB `newsky` | `QOG_SKY_DIRECT` | 16,376 B static |
| Status bar rows 152 to 199 stored in the first 15 KB of the z-buffer, which is idle during the 2D overlay and the scanout | `QOG_HUD_IN_ZBUFFER` | 15,360 B video (15,032 B net) |
| R_ScanEdges spans and the brush clip buffers in the free end of the edge table, with 4,608 B of room past the unchanged edge limit | `QOG_SPANS_IN_EDGES` | R_ScanEdges 8,072 to 88 B, brush clip frame 4,664 to 64 B, R_EdgeDrawing 80,160 to 84,768 B |

- `QOG_HUD_IN_ZBUFFER`, id redraws the status bar only when something changed and otherwise relies on the frame buffer keeping it. After each 3D view R_RenderView repaints the rows with the tile background a full update leaves there, and when Sbar_Draw is going to skip the frame it draws the status bar again from the client state it was last drawn with (saved by `R_HudFrameEnd`, which both VID_Update ports call). A first version that simply redrew the status bar every frame was not exact, after damage that left the stats unchanged it showed the pain face where id still shows the old face (`qogtest_f_hash.cfg` end frame). The next 3D view may not start before the scanout ends, which the single frame buffer already requires.
- `QOG_SKY_DIRECT`, R_MakeSky only records the texture and shift it would have composited, and keeps them until the shift changes, so a map change with the same shift shows what id showed.
- `QOG_SPANS_IN_EDGES`, where spans are flushed does not change the picture (each pixel is drawn once). The span room is the free part of the edge table plus the 4,608 B reserve, at least 576 spans and up to the 8,191 the 16-bit links reach. On spawn views of e3m6, e1m1, e4m7 and demo1 it was 4,611 to 6,573 spans, and one frame used at most 2,977. The reserve holds the brush clip arrays (128 vertices, 256 edges) before R_ScanEdges runs. `qog_rpeaks` prints these figures on the PC.
- Not changed, with the reason, the z-buffer itself. Alias models, sprites and particles test and write it, and a reader can cover the whole view (a monster close up, an explosion), so storing z only where readers draw saves nothing in the worst case. World z also depends on where each span starts, so it cannot be rebuilt from surface planes without keeping every span of the frame. No exact scheme with a smaller worst case was found. The z-buffer is idle outside the 3D view, which `QOG_HUD_IN_ZBUFFER` uses and other short-lived buffers could use too.
- Not changed, the edge and surface tables (2,000 × 24 B and 800 × 40 B). They are live while D_DrawSurfaces writes z mid-scan, so they cannot share the z-buffer without holding every span of the frame, whose number has no bound.

Part B, off by default and not in the shipped flag lists (built with extra `-D` flags, 177 frames of `qogtest_d_shots.cfg` and `qogtest_e_shots*.cfg` compared with the part A build).

| Option | Video memory (with the part A flags) | Picture |
| --- | ---: | --- |
| `QOG_RENDER_SCALE=75`, 3D view drawn at 240×114 in the top left of its rectangle and scaled up in place before the 2D overlay, z-buffer only as large as the view (`QOG_ZBUFFER_VIEW`) | 103,360 B (−42,560) | Blockier 3D, 39% of view pixels differ, status bar and text unchanged, 56% of the 3D pixels to draw |
| `QOG_ZBUFFER8`, one byte of depth, id's 16-bit value shifted right by 3 and clamped to 255 | 97,280 B (−48,640) | 24 of 177 frames identical, 0.39% of view pixels differ, objects nearer than about 16 units tie, so the weapon covers its own muzzle flash, and models close to walls show coarser depth steps |
| Both | 76,000 B (−69,920) | As both above |

Verification (hidden and silent, against a build of 8dc10b6 with the same test tooling). `qogtest_d_shots.cfg` (113 frames, demos 1 to 3 including the underwater demo2, every map's spawn view), `qogtest_e_shots*.cfg` (64), `qogtest_sky.cfg`, `qogtest_demo.cfg`, `qogtest_f_hash.cfg` all byte-identical, state hashes identical in `qogtest_hash.cfg`, `qogtest_e_shots*.cfg` and `qogtest_f_hash.cfg`, no Short messages. New per-frame hashes (`qog_framehash`, new `qogtest_h_*.cfg` scripts) over the three demos, `qog_tour_fight` on every map of the full game (the e4m5 run stops at a QuakeC teleport error in both builds), `qog_tour e1m1 e3m6 e4m7` with edge and surface reports, and a status bar script (console, menu, pause, viewsize 80, scoreboard, death, intermission), 45,525 frames. Every frame equals a base run except the console frames at startup and at each map change, which show the build time or the `qog_mem` video figure. Those were dumped from both builds and differ only in those text cells (checked for every such frame in the first 4,000 of each run, and they fall in the same places later). Two runs of the same build differ on a few frames around map loads, so each frame is compared against two base runs.

## 2026-11-23, Model table in flash, server tables, board data image (PC and ARM build, registered data)

Engine static RAM on the ARM meter 202.8 to 124.5 KB. Board link overflow (`quake_display`, 64 KB placeholder hunk) 186,540 to 106,428 bytes. Nothing ran on hardware.

| Change | Flag | Static RAM |
| --- | --- | --- |
| A baked model's `model_t` is used in place in the image. The table keeps a name, a pointer and a load state per model of the current level (2.2 KB), a map's per-frame arrays hang off `mod_frames` through `model_t.frameslot` (1 KB). A model loaded the normal way gets its `model_t` on the level's hunk | `QOG_MOD_FLASH` | `mod_known` 78.8 to 2.2 KB, +1 KB `mod_frames`, total 202.8 to 127.0 KB |
| Server precache lists sized from full-game peaks (188 models on e3m4, 154 sounds), no copy of the map's model name | `QOG_SV_TABLES`, `QOG_SV_MAX_MODELS=208`, `QOG_SV_MAX_SOUNDS=176` | `sv` 11.8 to 11.1 KB |
| The all-visible PVS row is a constant in flash | `QOG_CONST_NOVIS` | −1 KB |
| Server PVS rows sized for 4,096 leafs (largest full-game map 2,156), checked at map load | `QOG_PVS_LEAFS=4096` | −1 KB |

- Image format, with `QOG_MOD_FLASH` a map's block holds the finished world and its inline models `*1` to `*n` (layout hash bit 32). The registered image grew from 13,448,304 to 14,425,024 bytes. `qog_bake check` also compares every inline model and the frame slot, 0 differences. Hunk use is unchanged in the baked path (the same per-frame arrays are allocated).
- `-bakefile <path>` reads and writes another image, `-bakebase <address>` bakes for a later address, and the PC maps such an image there.
- The most models one level used was 187 (e3m4), inside the 192-entry table. The table is emptied at every level change. In the normal path (`-nobake`) an alias model is now loaded again on each level, as its `model_t` is freed with the hunk.
- `tools/mkboard.py` builds the board's data image, a trimmed pak (mkpak now takes several paks, later ones winning), `qog_bake` run on the PC with that pak alone so the addresses are the board's, and the image at the next 64 KB boundary. `start e1m1 e1m2` is 9.03 MB of pak plus 1.30 MB of image, 10.36 MB of the 11.94 MB region. A whole episode does not fit, its trimmed pak alone is 16.5 MB. The PC ran that layout (`-basedir build-board\qogboard`) with the same state hashes as the full paks and `qog_bake check` 0 differences, also with the image moved to `0x10E00000` by `--bake-base`.
- `port/rp2040/sys_rp2040.c` serves the image as `id1/qogbake.bin` from the first 64 KB boundary after the pak. The board CMake has `-DQOG_AOT=ON` (with `QOG_AOT_PAK`, off by default) and a `QOG_HUNK_KB` cache value. Compiled QuakeC links on the board, 500 KB of flash, no change to static RAM. With a realistic hunk (about 393 KB for E1M1) the overflow would be about 435 KB.
- Verification against a build of c22ca4b, hidden and silent, the same state hashes in `qogtest_hash`, `qogtest_f_hash` (8 maps, fight, save and load) and `qogtest_m_save`, 113 `qogtest_d_shots` frames, 64 `qogtest_e_shots` frames and 8 `qogtest_f_hash` frames byte-identical, the 32-map `qogtest_reg` and the `e1m1 e3m6 e4m7` tour logs equal apart from the model counts and times. With `-nobake` the hashes and the 113 demo frames are identical too.
- Edits outside the earlier files, one line each in `engine/r_bsp.c` and `engine/r_draw.c` (the per-frame arrays through `MODFRAME`), two loop bounds and `checkpvs` in `engine/pr_cmds.c`, and the model and precache lines in `port/common/qog_mem.c`.
- Main must regenerate `data/id1/qogbake.bin` after the merge (the layout hash changed, the old image is refused and models load the normal way until then).

## 2026-11-23, Client memory and 2D pics (PC and ARM build, registered data)

E1M1 (`qog_mem`, PC), hunk low 392.8 to 364.0 KB, cache 62.6 to 0 KB, total 613.8 to 522.5 KB. Engine static RAM on the meter 202.8 to 196.5 KB. Board overflow 187,500 (the earlier figure for main) to 180,112 bytes. Nothing ran on hardware.

| Flag | Change | Saved |
| --- | --- | --- |
| `QOG_CONBACK_FLASH` | gfx/conback.lmp read in place from the mapped pack. The version number goes into a copy of each of the 8 rows it covers as that row is drawn | cache 62.6 KB |
| `QOG_ENTITY_PACKED` | client `entity_t` 184 to 104 bytes. Network origins and angles kept as the protocol's shorts and signed bytes (`ENT_COORD` and `ENT_ANGLE` give back the exact floats), `msgtime` a float (only ever set from a float read from the message), baseline as bytes, `update_type`, `dlightframe`, `dlightbits` gone. Renderer fields unchanged | E1M1 cl_ents 58,352 to 32,992 hunk, `cl_temp_entities` 11.5 to 6.5 KB static |
| `QOG_PARTICLE_PACKED` | `particle_t` 44 to 36 bytes, colour and type as bytes, list link a 16-bit index (d_iface.h, r_part.c) | hunk 4,096 |
| `QOG_PARTICLE_AVEL8` | entity particle rates as bytes, scaled to the same floats on use | static 1.4 KB |
| `QOG_CACHEPIC_FLASH` | `Draw_CachePic` returns menu, intermission, finale and plaque pics in place (little endian only, conback excluded since id writes into it) | cache 30.6 KB at an intermission, 222.9 KB with every menu open |

- Peaks, the fight tour (`qog_tour_fight e1m1 e3m6 e4m7`) fills all 512 particles, so `QOG_MAX_PARTICLES` stays. MG24Quake's 16-byte particle stores positions and velocities with less precision and would change the picture, so it was not used.
- Verified hidden and silent against the untouched worktree after each change, identical state hashes (`qogtest_hash`, the 8-map fight with save and load, `qogtest_m_save`), 203 frames byte-identical (demo 3, sky, the 113 D shots, the 64 E map shots, and the new `qogtest_g_con.cfg` console frames), the fight tour logs equal apart from memory lines. The new `qogtest_g_pics.cfg` (every menu and the pause pic) matched too. The only pixel differences are the blinking text cursor of the console input line and the menu name fields, which follow `realtime` and differ between two runs of the untouched build as well. Logs lose the `PackFile:` lines of the pics now read in place.
- Looked at and left. `scores` (16 KB) is already one entry in single player, it is that player's translation table, which the renderer indexes as a full colormap when chase view draws the player. `sizebuf` is the 8 KB command buffer, the 6.5 KB `net_message` and the 1 KB client message (all sized earlier). `cl_lightstyle` and the temp entity count stay at id's sizes.
- Found, not changed (server code), the "unknown" hunk blocks on E1M1 are 520 QuakeC strings from the entity lump (`ED_NewString`, pr_edict.c), 4,400 bytes of text in 17,472 bytes of blocks because each gets a 16-byte hunk header and is rounded to 16. One string arena per level would save about 13 KB. The rest (about 5 KB) is console commands at 32 bytes each.
- Not measured, speed on the board. The board build must map the pack (the pics are read as `qpic_t` with int fields, so their image positions must stay 4-byte aligned).

## 2026-11-23, Speed benchmark on the board (hardware)

`apps/qogbench` times the engine's world-drawing inner loop on the display CPU (200 MHz Cortex-M0+), drawing a perspective-textured plane over the whole 320×152 view. The span code is a copy of `engine/d_scanlit.c`. The texture is a 320×200 picture read from the flash data region. Flashed with `fw flash qogbench --uf2 build/apps/qogbench/qogbench_main.uf2`, results read from the display CPU's USB console (COM8). Steady over 10+ rounds.

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

What it means.
- World drawing alone costs about 52 ms per frame today (about 19 fps for the world, before edges, models, particles and game logic). Core 1's 22 ms LCD scanout runs alongside.
- Code placement does not matter for this loop. The per-pixel arithmetic does, two modulos and a 4-sample light blend per pixel.
- Clear next steps for speed, wrap texture coordinates without modulo (per span or with power-of-two masks, worth about 10 ms), and step the light linearly along each 8-pixel run instead of blending 4 samples per pixel.

## 2026-11-23, Sparse edicts, baseline pool, compiled QuakeC globals (PC and ARM build, registered data)

Edicts on E1M1 went from 277,712 to 84,992 bytes of hunk (edicts, field arena, baselines, field map), and on e4m3, the largest array, from 447,664 to 139,888. With compiled QuakeC the globals also went from 45,904 to 7,280 bytes. Hunk low, E1M1 995.5 to 807.3 KB interpreted, 769.6 KB compiled. e4m7 1,338.0 to 1,040.2 / 1,002.5 KB. Nothing ran on hardware.

| Step | Flag | E1M1 hunk | e4m3 hunk |
| --- | --- | ---: | ---: |
| Core fields in place, the rest as (field, value) pairs in a compacting arena, 12 words per slot | `QOG_EDICT_SPARSE` | 277,712 to 105,504 | 447,664 to 169,920 |
| Baselines only for edicts that get one at spawn, 16-bit index | `QOG_BASELINE_POOL` | to 95,136 | to 156,240 |
| avelocity, ltime, blocked, effects, skin, colormap out of the core (38 words, slot 208 B) | `QOG_EDICT_SPARSE` | to 84,992 | to 139,888 |
| Compiled QuakeC, constants as literals, most temporaries as C locals, packed RAM globals (1,813 of 11,471 words) | `QOG_AOT_GLOBALS` | progglobals 45,904 to 7,280 | same |

- Measured with `qog_tour` (32 maps, noclip, notarget) and the new `qog_tour_fight` (same tour, monsters awake, rocket launcher firing). Most edicts in use, 389 high water on e4m3 in the fight tour, 365 on e4m5 and e4m7 in the plain tour. The high water never went more than 35 slots over `SV_EdictsForMap`'s estimate, and every map kept at least 92 spare slots, so `QOG_EDICT_MARGIN` 128 stays. The arena peaked at 9.2 words per slot (12 allocated), and it compacts when full.
- Real play can go further than the tours, nails live up to 6 s, gibs 10 to 20 s, backpacks 120 s. A long fight with many gibs and packs could pass the margin. Overflow is still id's `Sys_Error`.
- Field access, engine code uses `EV_` macros for non-core entvars fields, QuakeC goes through `ED_FieldGet` and `ED_FieldSet`, and `OP_ADDRESS` returns the edict offset times 256 plus the field number. qc2c inlines the core words and the engine checks its map at load, falling back to the interpreter if they differ. The PC build regenerates the compiled code when `progs.h` changes.
- Compiled QuakeC (`-DQOG_AOT=ON`, registered `progs.dat` from pak0) is 496 KB of Cortex-M0+ flash with 27 KB of tables, and its largest generated stack frame is 64 bytes. On the PC, server frame time over the bench script was 0.84 to 0.96 s compiled, 0.96 to 1.04 s interpreted with sparse edicts, and 0.89 to 0.98 s interpreted with id's edicts (noisy, PC only).
- Board static RAM, overflow 187,264 to 187,500 bytes (+236, the arena bookkeeping and a vector ring). Engine flash +3 KB.
- Verification, identical `qog_statehash` to the untouched build on E1M1 (`qogtest_hash.cfg`) and on eight registered maps with combat, save and load (`qogtest_f_hash.cfg`). Checked interpreted, compiled, compiled with id's edict layout, and `-noaot`. Demo 3, sky and map frames were byte-identical. `qogtest_reg.cfg` (all 32 maps) logs matched apart from the memory lines, and hunk low was 148 to 301 KB smaller on every map.
- Not done. `leafnums` (36 bytes per slot) stays inline. The estimate still counts entities that skill flags inhibit (15 to 75 per map). `SV_PushMove` keeps its 9.7 KB stack frame (two `MAX_EDICTS` arrays).

## 2026-11-23, Maps, alias models and sprites baked into a flash image (PC and ARM build, registered data)

- New PC command `qog_bake` runs id's own loaders on every map, alias model and sprite in the packs and writes what they build into `id1/qogbake.bin` (git-ignored, a converted copy of the registered data). Pointers are resolved for the flash address the image is mapped at, the first 64 KB boundary after the packs, `0x13620000` with registered `pak0` and `pak1`. The PC maps it read only there. Format and use in `docs/game-data.md`.
- `QOG_BAKE` (PC and ARM), a map load copies the baked `model_t` and allocates only the per-frame arrays. An alias model or sprite load puts nothing in the cache or on the hunk. A layout hash refuses an image from a build with other structures, and a model whose file changed loads the normal way. `QOG_BAKE_WRITER` (PC only) adds the command. `-nobake` turns the image off.
- `qog_bake check` reloads all 133 models with id's loader and compares them field by field with the image, 0 differences.
- Map hunk blocks, E1M1, 473.6 to 90.0 KB. Worst of the 32 single-player maps, 598.0 KB (E4M7) to 113.4 KB (E3M5). What is left is the per-frame state (visframe and dlight stamps, dlight bits, efrag lists, the edge cache).
- E1M1 total hunk blocks 995.5 to 580.9 KB. Cache 107.1 to 62.6 KB on E1M1 and 152.5 to 62.6 KB on E4M7 (only the console background is left). Hunk high peak 18.5 KB to 0. `qog_mem` total on E1M1 1,261 to 802 KB, on E4M7 1,649 to 1,035 KB.
- Image size, 13,448,304 bytes for all 133 models. The 32 single-player maps take 12.1 MB (E1M1 392 KB, largest E4M7 497 KB), `start` with episode 1 3.05 MB, alias models and sprites 0.2 MB. The board's 12 MB region fits one episode next to its pak, not all four.
- Fix found on the way, with `QOG_MAX_MOD_KNOWN` a reused model slot kept the previous model's `synctype`, so a map loaded into an alias model's old slot got `ST_RAND` and the client drew a random syncbase, which shifted the random sequence from the 20th level of a long run on. Reused slots now start empty.
- Checked hidden and silent against the build before (with the slot fix), same state hashes, the same logs apart from hunk and cache figures and the `PackFile` lines of sprites (no longer loaded from the pack directory), and 75 frames byte-identical (demo 3, the 9 spawn shots, a save and load, and a spawn and a turn view on all 32 maps). The 32-map regression run and the level tour on E1M1, E3M5 and E4M7 match too.
- Board, static RAM overflow 187,256 to 187,268 bytes (three statics). The RP2040 port does not map the image yet, so the board build still loads models the normal way.
- Not changed, the tour stops on `end` with "MOVETYPE_PUSH with a non bsp model" in the build before as well, and long test scripts can run the 24 KB zone out (a 32-map script failed in `Z_Malloc` in both builds, so the new tests chain short scripts).

## 2026-11-23, Render tables packed, renderer stack cut (PC and ARM build)

Frames from `tools/arm_report.py` with the worktree's 1,600 edges, 640 surfaces, 1,000 spans and 384 alias vertices. Each flag defaults to id's code.

| Flag | Entry size | Frame before to after |
| --- | --- | --- |
| `QOG_EDGE_T_PACKED`, links are 16-bit byte offsets into the edge buffer, the four sentinels sit at its front, `owner` is the world edge index (the only kind the edge cache compares), `newedges`/`removeedges` 16-bit | `edge_t` 32 to 24 B | R_EdgeDrawing 84,592 to 71,856 |
| `QOG_ESPAN_T_PACKED`, u, v, count as shorts, link and `surf_t.spans` as 16-bit offsets into R_ScanEdges' buffer | `espan_t` 16 to 8 B | R_ScanEdges 16,112 to 8,072 |
| `QOG_SURF_T_SMALL` (needs the two above and `QOG_SURF_T_PACKED`), stack links are surface indices, last_u and spanstate 16-bit, insubmodel and flags bytes | `surf_t` 52 to 40 B | R_EdgeDrawing 71,856 to 64,160 |
| `QOG_FINALVERT_PACKED` (no assembly padding) and `QOG_ALIAS_NO_AUXVERTS` (the triangle clipper recomputes a near-clipped vertex with the same shared transform instead of keeping a 12-byte view-space entry per vertex) | `finalvert_t` 32 to 28 B, 12 B per vertex gone | R_AliasDrawModel 16,944 to 10,824 |

- Deepest render chain is about 29 KB shorter. With main's 2,000 edges and 800 surfaces R_EdgeDrawing should be about 2,000 × 24 + 800 × 40 + 96 ≈ 80 KB instead of 105,712.
- Static RAM about −0.9 KB (16-bit edge lists, no static sentinels). Board overflow 187,256 to 186,296 bytes.
- Limits checked at R_NewMap, at most 2,725 edges for 16-bit links, fewer than 65,535 map edges (largest registered map e3m5 has 16,923), fewer than 65,536 surfaces. Spans are checked at compile time (8,191).
- Verified hidden and silent against a build of 51e9d75 after each change, 113 shots (demo1 to demo3 every 40 frames, and the spawn view of every registered map) byte-identical, the same state hashes, and the same `qog_tour e1m1 e2m6 e3m6 e4m7` peaks. The near-plane alias clip path ran about 95,000 times in the shot run.
- The largest registered alias model is boss.mdl with 325 vertices, so `QOG_MAXALIASVERTS` 328 would save another 56 × 28 B on R_AliasDrawModel's stack. Not changed here (shared build line).
- Not measured, speed on the board. Each link now costs an add (edges, spans) or a multiply (surfaces) to decode.

## 2026-11-23, Limits checked against the full game (PC build, registered data)

- Registered PAK1 added locally in data/id1 (git-ignored). PAK0 from Steam is byte-identical to the shareware one.
- All 32 single-player maps load with no errors. Worst signon 5,052 of 6,500 bytes (e2m6). At least 94 spare edicts on every map. Zone peak 7.7 of 24 KB.
- New PC test command `qog_tour` (port/pc/tour_pc.c) visits every entity spot of a map in noclip and looks four ways. Spots with the eye inside solid are skipped, because from inside solid the whole map counts as visible.
- Tour peaks, edges about 1,737 and surfaces 668 on e3m6, which overflowed the old 1,600 and 640. Brush clip peaks 48 vertices and 148 edges on e3m3, inside 128 and 256.
- `QOG_NUMSTACKEDGES` 2000 and `QOG_NUMSTACKSURFACES` 800. R_EdgeDrawing's frame is 105,712 bytes on the board as a result.

## 2026-11-23, earlier work merged, renderer stack cut (PC and ARM build)

- earlier work (packed BSP structures) and earlier work (message buffers and client tables) are merged into main. Each merge was checked hidden and silent against the build before it, same state hashes, same all-level manifest output, and byte-identical demo and spawn frames.
- E1M1 hunk after both merges is 1,018,640 bytes in total blocks, down from 1,123,216 after B alone. The map's own blocks went from 657,312 to 484,960 with B.
- Board static RAM overflow, 229,744 to 187,256 bytes. The engine's static RAM on the meter is 203.6 KB of the 245 KB budget.
- `QOG_SURF_T_PACKED`, `surf_t` 64 to 52 bytes. The padding was for id's x86 assembly, which neither build compiles (`id386` needs `__i386__`). spanstate and insubmodel share a word. R_EdgeDrawing's frame went from 92,280 to 84,592 bytes.
- `QOG_MAX_BMODEL_VERTS` 128 and `QOG_MAX_BMODEL_EDGES` 256 (id 500 and 1000). qog_mem now prints these high-water marks, 22 vertices and 74 edges over all three demos. The buffers are reset for every brush polygon, and overflow drops only that fragment. R_DrawSolidClippedSubmodelPolygons' frame went from 18,056 to 4,664 bytes.
- Recheck both bmodel peaks and the edge and surface peaks (1,037 and 435 on shareware, limits 1,600 and 640) once registered data is in place.

## 2026-11-22, Message buffers and client/host tables sized from measured peaks (PC and ARM meter)

Peaks were measured with temporary counters over every shareware level, every menu, a save and load, and demos 1 to 3. Each limit is a `QOG_` define that defaults to id's value. Overflow still takes id's path (Sys_Error, a dropped client or a dropped datagram).

| Change | Measured peak | Before to after |
| --- | --- | --- |
| Loopback sockets, no send buffers, one receive buffer per direction (`QOG_LOOP_CLIENT_RX` 7532, `QOG_LOOP_SERVER_RX` 2048) | 3,844 B to the client, 40 B to the server | hunk 32.4 KB to 0.4 KB, static +9.4 KB |
| `QOG_MAX_MSGLEN` 6500 (client message, net_message, demo limit, like MG24Quake's retail build), `QOG_SIGNON_SIZE` 6500 | 3,838 B per message, 3,836 B signon | hunk −3.1 KB, static −1.7 KB |
| Mapped pack looked up in its flash directory, disk positions kept only where they differ (PC) | | hunk 25.2 KB to 1.3 KB (0 on the board) |
| Colormap read in place from flash (never written. The PC pages are read only) | | hunk −16 KB |
| QuakeC locals stack sized per progs, largest locals × 31 nestable calls (`QOG_LOCALSTACK_FROM_PROGS`) | 21 of 2,048 ints used | static 8 KB to 0, hunk +2 KB |
| Menu pic cache, 56 entries with 24-byte names (`QOG_MAX_CACHED_PICS`, `QOG_CACHEPIC_NAME`). The engine can name only 51, the longest 16 characters | 28 | static 8.5 KB to 1.5 KB |
| Static entities and efrags from the hunk per level (`QOG_STATICS_ON_HUNK`), still capped at 128 and 640 | 44 entities, 197 efrags, 16 per entity | static −33 KB, hunk up to +13 KB |
| gfx.wad lump directory used in place | | hunk −5.1 KB |
| Zone 24 KB (`QOG_ZONE_KB`) | 7.4 KB, plus up to 8 KB while a script is inserted | hunk −24 KB |
| Inline model names as a constant table (`QOG_CONST_LOCALMODELS`) | | static −1.25 KB |
| Spawn command and keepalive copy off the load-path stack | | stack −8 KB each on the ARM build |

Totals, E1M1 hunk low 1,268.6 to 1,166.5 KB. Peak hunk low over all levels 1,521.1 to 1,429.8 KB. Engine static RAM (ARM meter) 265.4 to 223.9 KB. State hashes, all 10 manifests and 24 demo frames (rows 16 to 199) are identical to the reference build.

Left as is. `cl_temp_entities` (lightning beams can legitimately use all 64), the 16 KB player translation table (needed for chase view and the renderer indexes it as a full colormap), the 8 KB command buffer, and `MAX_DATAGRAM` (a smaller one drops entities from the frame).

## 2026-11-22, Engine-only RAM cuts (PC, measured, all verified hidden and silent)

| Step | Change | Saved (E1M1) |
| --- | --- | ---: |
| A | Single player, one client slot, two loopback sockets (`QOG_SINGLE_PLAYER`) | 107 KB hunk |
| B | Edict array sized per map from its entity lump, minus what QuakeC frees at spawn, plus a margin of 128 (`QOG_EDICT_MARGIN`), 247 to 480 instead of 600 | 242 KB hunk |
| C | Client entity table sized the same way, on the hunk | 100 KB static |
| D | Mixer volume table replaced by an equivalent multiply | 32 KB static |
| E | `gfx.wad` read in place. Only its 5 KB directory is copied | 105 KB hunk |
| F | Model table 192 instead of 256 (`QOG_MAX_MOD_KNOWN`). The most one level references is 160 | 27 KB static |
| G | Sky composited straight from its texture (front if non-zero, else back), no masked copies, 128×128 output. Equivalent on both sky textures at all 128 scroll positions | 49 KB static |
| H | Particle pool 512 instead of 2,048 (`QOG_MAX_PARTICLES`), id's own minimum, as MG24Quake and RP2350-Quake use. **Visual trade-off** | 66 KB hunk |

E1M1 now, hunk low 1,639 KB, total dynamic 1,953 KB, static 271 KB (interpreter, since QuakeC translation waits for registered data). State hashes are unchanged throughout. Demo frames are identical to the reference build, status bar included. All nine levels and every menu run. Sound was verified by level only (peak 19,193, RMS 6,395), never played aloud.

Testing changes. `-hidden` now also mutes the speakers and drives playback by game time. New `tools/pctest.ps1` and `qog_soundlevel`.

## 2026-11-22, Asset manifests, pak builder, licence decision, BSP state split (PC)

- **Item 2, asset manifests.** a new engine hook `com_fileopened` reports every pack file opened. On the PC, `qog_manifest` writes a map's precached models and sounds plus every file opened, and `tools/mkpak.py` builds a trimmed pak aligned to 16 bytes, plus a UF2, from those lists. It also converts the two 22 kHz sounds. **Start + E1M1 is 7.08 MB and fits. The whole shareware episode is 16.6 MB and does not fit 11.94 MB**, deduplicating textures (−1.72 MB) and a smaller app area would both be needed.
- **Licence finding.** `SLICNSE.TXT` §3 forbids modifying, translating or making derivative versions of the shareware data, and id's engine refuses a trimmed shareware pak. **User decision, engine-only work for now.** The shareware data is used only by the unmodified PC build. The QuakeC translation is now opt-in (`-DQOG_AOT=ON`, registered data only), and the derived files generated earlier were deleted. Baking data into flash images waits for registered or retail data.
- **Item 1, engine part.** the per-frame state id kept inside the BSP structures (surface `visframe`, `dlightframe`, `dlightbits`; node and leaf `visframe`; leaf `efrags`, `key`; edge `cachededgeoffset`) moved to per-model RAM arrays behind accessor macros. **The map structures are now never written during play**, so they can be baked into flash once registered data is available. Edges are already read in place, −139 KB of hunk for E1M1. Six demo frames across three demos are byte-identical to the previous code, and the state hashes are unchanged.
- The PC port's Options > Video menu no longer crashes (it was a NULL hook).
- RP2350-Quake (zenodante) studied, it bakes immutable BSP and model data into XIP at fixed addresses, the same idea as our next step. Its shareware image is 15.9 MB, and it targets the RP2350's 520 KB of SRAM.

## 2026-11-22, First Quake data on the board (hardware)

- Quake's console background (`gfx/conback.lmp`, 320×200) and palette (`gfx/palette.lmp`) were extracted from the user's `pak0.pak`, packed with `tools/mkdata.py`, and written to the display chip's data region at 0x10400000. The steps. `fw bootsel --cpu display`, copy the UF2 to the RPI-RP2 drive, then `fw flash qog` so the main chip sends RUN (no button press needed).
- **The data-region write path is verified on hardware.** The console reports `data=ok`, and the app and its metadata survived. Lessons L8 is confirmed.
- **Measured flash read speed.** a 64,000-byte `memcpy` from XIP flash takes **7.55 ms, about 8.5 MB/s** sequential (clk_sys 200 MHz, flash at 50 MHz). The frame rate is 34 fps.
- Quake itself does not run on the board yet, E1M1 still needs about 2.8 MB of RAM against 264 KB, and the game data (about 17 MB) must be trimmed to about 12 MB.

## 2026-11-22, Models and sounds from flash (PC, measured)

- **Alias models** (−1,312 KB), frame vertices, skin pixels and triangles stay in flash. The cache keeps only the header, the descriptors and the 16.16 texture coordinates. A new absolute `flashbase` pointer in `aliashdr_t` stays valid when the cache moves the header, and `ALIAS_BULK()` resolves offsets. The player model went from 190 KB to 7 KB. Demo 1 frames are byte-identical with and without flash.
- **Sound switched on in the PC build**. `port/pc/snd_pc.c` (a waveOut ring at 11,025 Hz) plus id's real mixer. Before the in-place change, sounds cost about 1,306 KB of cache for E1M1.
- **Sounds from flash** (−1,235 KB), 8-bit mono sounds at the mixing rate keep their samples in flash, and the mixer applies the sign flip (`^ 0x80`), which equals id's `u - 128` conversion for every byte. 188 of 190 shareware sounds qualify.
- **E1M1 with sound, about 2,759 KB in total**, against about 7,270 KB for unmodified id code with sound.

## 2026-11-22, The three big RAM cuts (PC, measured)

All changes are commits against id's baseline in `engine/`, each verified before the next.

1. **Level data from flash** (−795 KB). New `Sys_FlashMap` port call and `COM_FlashFile` lookup. Brush models load from a read-only image. Lighting, visibility, entities, texture pixels, and (when aligned) vertices, surface edges and clip nodes are used in place. `texture_t` holds mip pointers instead of self-relative offsets. The PC maps the pak into read-only pages at 0x10400000 (the board's data-region address), so any stray write faults immediately.
2. **No surface cache** (−599 KB). New `engine/d_scanlit.c` lights each pixel from the texture and the 18×18 light grid as it draws. The E1M1 start frame matches id's render on 96.3% of pixels. `qog_shot` saves frames for comparisons like this.
3. **QuakeC compiled to C** (−569 KB together with flash alignment). `tools/qc2c.py` translates all 2,091 functions of `progs.dat` into C at build time, from the player's own pak (never committed). `progs.dat` stays in flash and only its 45 KB of globals sit in RAM. **E1M1 state hashes match id's interpreter exactly** at 6.25 s and 21.25 s with monsters patrolling, with and without flash.

Supporting work.
- A shared `qog_rand` (MSVC's generator) on every port, so PC and board make the same random choices.
- Repeatable PC runs, fixed memory layout, and `Host_Frame` called only when a frame is due.
- A semantic `qog_statehash` that hashes by QuakeC type, so layout changes don't affect it.
- The PC flash image realigns pack files to 16 bytes, the way the board converter will.

**E1M1 total, about 5,886 KB down to about 3,923 KB** (see `ram-meter.md`).

## 2026-11-22, E1M1 runs on the PC build, RAM baseline measured

- Shareware data downloaded and checked (`quake106.zip` MD5 matches the published value). `pak0.pak` extracted as data with a pure-Python LHA decoder, MD5 `5906e599…3abb`, placed in the git-ignored `data/id1/`.
- The user turned off Windows Smart App Control, which had been blocking each fresh unsigned build.
- **id's unmodified engine loads E1M1 on our Windows port** ("the Slipgate Complex, player entered the game").
- Meter fixes, id never sets the zone's size field, and the zone lives inside hunk low. Video buffers are now counted.
- **Baseline, about 5.9 MB in total for E1M1 against 245 KB** (map 1.77 MB, cache 1.42 MB, surface cache 600 KB, edicts 513 KB, progs 404 KB, static 435 KB). Full table and shrink plan in `ram-meter.md`.

## 2026-11-22, Clean engine base from id's source, PC build and RAM meter

**Direction change (user decision).** build from id's clean GPL source instead of forking a port. MG24Quake and RP2350-Quake become references to borrow from deliberately, with provenance recorded.

- `engine/`, id's WinQuake software-renderer files copied **unmodified** from id-Software/Quake `bf4ac42` as the baseline commit (`engine/PROVENANCE.md`). Every later change is a git diff against it. A `.gitattributes` keeps the CRLF bytes so diffs stay clean.
- First engine change. `_WIN32` renamed to `ID_WIN32` (41 lines). id used the compiler macro to mean "id's Win32 port", which on MSVC turned on the x86 assembly and the SciTech video library.
- `port/pc/` and `pc/CMakeLists.txt`, a 32-bit MSVC Windows build with a Win32 window and 8-bit blit, keyboard mode, and a board mode (F12, keys 1 to 5) driven by the firmware's own `qog_input.c`. **It builds and starts**, and stops at "couldn't load gfx.wad" until `pak0.pak` is supplied (`game-data.md`).
- `port/common/qog_mem.c`, runtime RAM meter (hunk low and high, cache, zone, with peaks) in the title bar plus a `qog_mem` console command. It reads id's allocator globals, so `zone.c` stays unmodified.
- `tools/ram_meter.py`, static RAM meter that compiles the engine for Cortex-M0+ and sizes every global. **First reading, 435.3 KB static** against a 245 KB budget. The biggest items are `cl_entities` (108 KB) and `mod_known` (97 KB).
- Commits now follow the user's message rules, a one-line title, a bullet body, no em dashes, semicolons or colons, and no tool attribution.

## 2026-11-22, Resolution measurements (hardware)

Output is always 320×200 on the panel, with single buffering. Set the test size with `cmake --preset target -DQOG_TEST_W=… -DQOG_TEST_H=…`.

| Source | Frame rate | Test render | Scanout | Static RAM (BSS) | Frame buffer |
| --- | --- | --- | --- | --- | --- |
| 320×200 | 32 fps | 9.6 ms | 22.4 ms | 72.5 KB | 62.5 KB |
| 240×150 | 36 fps | 5.4 ms | 22.4 ms | 44.6 KB | 35.2 KB |
| 160×100 | 41 fps | 2.4 ms | 22.4 ms | 24.6 KB | 15.6 KB |

What these numbers say.

- **Scanout costs 22.4 ms whatever the source size.** It's bound by the SPI link (64,000 output pixels at 50 MHz), and the scaling path costs nothing extra.
- **With one buffer, render and scanout run back to back**, so the frame time is the sum of the two (9.6 + 22.4 ms gives 32 ms, 31 fps). Single buffering puts a 22 ms tax on every frame.
- **Removing that tax without a second buffer.** only the span-drawing stage of Quake writes the frame buffer. Order the frame as present to game logic, physics and BSP/edge setup for the next frame to wait for the scanout to draw spans. The scanout then overlaps the work that doesn't touch the buffer, at no RAM cost.
- Other levers if needed, show 240×150 unscaled (11.5 ms scanout, smaller picture), or a faster SPI rate. The PL022 divider is even, so the next step up from 50 MHz is 100 MHz, beyond the ST7789's rating. Leave this alone.
- Platform overhead stays at about 8.5 KB of static RAM at all three sizes.

**Resolution recommendation.** develop at **240×150** (scaled 4:3 to 320×200), with 160×100 as the compile-time fallback. Combining the MG24 study with our measured budget leaves about 18 KB spare at 240×150 and about 42 KB at 160×100. 320×200 doesn't fit.

## 2026-11-22, Control map v2, red is tap-only (flashed)

- The plan's control table put "move back" on red, and the board showed that holding red starts the 6 s power-off. The new map (`platform/qog_input.c`).

  | Input | Alone | With green held |
  | --- | --- | --- |
  | Gray | Forward | Back |
  | Yellow | Turn left | Jump |
  | Blue | Turn right | Crouch / swim down |
  | Red | Next weapon (tap) | Previous weapon (tap) |
  | Green | Fire | Modifier |
  | Yellow + blue | Menu | |

- This replaces the plan's controls table. The test app now shows every action, weapon taps cycle the box colour, fire turns it red, menu turns it grey, jump hops it, crouch shrinks it.
- Blue was re-checked by the user and moves right, as intended.

## 2026-11-22, First run on the board (hardware)

- The board came with BSP-built firmware ("FWOG main/display wilirehab 002"), so the display bootloader was already installed. `fw flash qog` replaced wilirehab on both chips, and both now enumerate as `FWOG … qog 001`.
- **Bug found on hardware.** the first flash hung core 0 with a black screen. Frames were handed to core 1 through the SIO FIFO, and `multicore_lockout_victim_init()` installs a core-1 IRQ that eats FIFO words. I replaced the FIFO with a volatile pointer plus `__sev`/`__wfe` (lessons.md, L14).
- **Bug found on hardware.** the tilt zero was never set, because the first LIS3DH read came before a sample was ready. It now averages 8 samples with a 300 ms timeout.
- Also fixed, if video init failed, the test app printed nothing. It now reports.
- **Measured** at 320×200 with output 320×200 and single buffering.

  | Measurement | Value | Note |
  | --- | --- | --- |
  | Frame rate | **32 fps** | |
  | Scanout | **22.4 ms** per frame | Theory was 20.5 ms. The extra is setup plus a DMA re-arm per line |
  | Test-pattern render | 9.6 ms | Uses a `%` per pixel, so this measures the test pattern, not the pipeline |
  | Core-0 wait on scanout | about 0 µs | Core 1 finishes before core 0 needs the buffer again |

- The data region reads `bad magic`, there's old non-QOGD content at 0x400000, probably left by earlier firmware. The test data UF2 hasn't been written yet.
- Buttons and the action map respond. The 6 s red-hold LED countdown works.
- **Third fix, backlight.** The screen was blank because the app never called `board_backlight(255)` (lessons.md, L18). After the fix, the user's photo shows the scrolling bands, the box and the fps counter, **the video pipeline is verified on the panel.**
- **User button test.** gray moves the box up, red down, yellow left. Green turns the box red (palette index 0, as intended). Blue was reported as moving *left* too, where right is expected. re-checking. Red also starts the power-off countdown (L6 confirmed).
- **Orientation.** the LCD's 320-pixel axis runs along the board's long side, so with the board upright the 320×200 frame shows rotated 90°. Proposed, play holding the board sideways (landscape), with no rotation in code. Remap the buttons once the user confirms which edge they sit on.

## 2026-11-22, Phase 1 research landed

- `phase1-mg24quake-notes.md`, MG24Quake uses 275.9 KiB of RAM, with a 320×152 3D view plus a 48-row status bar. The biggest buffers are the z-buffer (95 KB, which also holds the edge tables and network buffers), the frame buffer (48.5 KB) and the zone (47 KB). Its estimate against 232 KB, 320×200 doesn't fit (45 KB over), 240×150 fits barely (5 KB spare), 160×100 fits comfortably (29 KB spare). Our measured budget is about 245 KB, which adds about 13 KB to each margin.
- `research-open-source-quake-bases.md`, no public Quake runs on an FPU-less MCU without PSRAM. The best bases are **zenodante/RP2350-Quake** (an MG24Quake fork on pico-sdk, flash-resident data, a core-1 LCD path and an SDL host build) and MG24Quake itself for its memory tricks. The combined work must be **GPL-3.0**.

## 2026-11-22, Project skeleton and Phase 2 platform layer (built, not run on hardware)

**Setup**
- The Pico VS Code extension already provides SDK 2.3.0, GCC 15.2 (`15_2_Rel1`), picotool 2.3.0, ninja and cmake under `~/.pico-sdk`. That's exactly what the BSP expects.
- Cloned `wiliOGbsp` and `MG24Quake` into `third_party/`. They are git-ignored and not vendored.
- Top-level `CMakeLists.txt` consumes the BSP via `add_subdirectory(third_party/wiliOGbsp/bsp)`, as its AGENTS.md documents for outside projects.
- `CMakePresets.json` points `picotool_DIR` and `pioasm_DIR` at the extension's prebuilt tools. Without this the build tries to compile picotool from source and fails (lessons.md, L1).

**Code**
- `platform/qog_flashmap.h`, display flash map. App ≤ 3.87 MB at 0x021000, game data at 0x400000 (up to 11.9 MB), saves at 0xFF0000 (64 KB).
- `platform/qog_data.[ch]`, QOGD image format and an in-place reader. The header has its own CRC, an optional full-body CRC, and name lookup returns XIP pointers.
- `platform/qog_video.[ch]`, an 8-bit frame buffer goes through the palette, gets scaled to 320×out_h, and is DMA'd to the ST7789 with 16-bit SPI frames, all on core 1 with two line buffers. Palette changes are latched per frame. There is a core-1 hook for sound later.
- `platform/qog_input.[ch]`, buttons map to game actions per the plan's control table (green as fire/modifier, yellow+blue for menu). Tilt comes from the LIS3DH with zero calibration and a dead zone.
- `apps/qog/`, display test app (pattern, button-driven box, fps, still image from flash) and a minimal main companion.
- `tools/mkdata.py`, packs files into a QOGD image and a UF2 at 0x10400000, with a `--test-pattern` mode. `tools/tests/test_mkdata.py`, 4 host tests, passing.
- `tools/check_app_size.py`, the build fails if the app would overlap the data region.

**Measured (from the build, not the board)**
- `qog_display`, 46 KB of flash, 72.5 KB of static RAM, of which 64 KB is the 320×200 test frame buffer. So the BSP plus the platform layer take about **8.5 KB of static RAM**.

**Plan deltas**
- The plan's "232 KB for Quake" assumed a 32 KB bare display app. Measured static use is about 8.5 KB, and stacks live in the separate 2×4 KB scratch banks. The working figure is now **about 245 KB, pending a runtime heap measurement**.
- The "main CPU runs stock firmware" wording changed, with this BSP the main CPU runs *our* 30-line companion (`apps/qog/main/main.c`), built from the template. It's easier to disclose because anyone can read all of it. Note that the BSP's main `board_init()` also loads the stock FPGA bitstream.
