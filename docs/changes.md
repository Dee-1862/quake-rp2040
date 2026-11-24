# Changes log

Newest first. Each entry says what changed, why, and whether it ran on hardware.

## 2026-11-24: Board measurements of the split game (lead, on hardware)

Read from the board's USB logs (display COM8, main COM7) with QOG_PHASE_TIME and QOG_LINK_DIAG, standing in E1M1 at the spawn point, each line an average over about 2 s.

| Build | fps | Frame ms | View ms | world | spans | vmodel | edgescan | Server frame ms |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 200 MHz, flash 50 MHz, sound (f75051b) | 7.5 | 132 | 109 | 38.3 | 43.2 | 7.6 | 7.9 | 17.2 |
| 250 MHz at 1.20 V, flash 125 MHz (c5fa930) | 10.4 | 96 | 78 | 26.3 | 30.7 | 4.9 | 5.8 | 11.8 |
| 250 MHz plus Fast mode (agent-aa merge) | 10.9 | 91 | 71 | 22.5 | 29.8 | 4.6 | 6.1 | not read |

- The clock log lines read "clock 250000 kHz at 1200 mV" and "flash 125.0 MHz ok (div 2 from 4, rx delay 1, 128 KB checked)". No fault, hang or error line on either chip in any of these runs.
- Fast mode cut the span arithmetic by about 2.9 M cycles a view (Agent AA's estimate) but spans fell only 0.9 ms, so the span time looks bound by XIP misses rather than compute. Agent Z's XIP counter build is meant to confirm it.
- Stack free on the board with sound and the 73 KB client hunk (9192bc9): display 4,192 B, main 19,204 B.
- Sound through the speaker was heard by the user on f75051b. The game has run 15 minutes in E1M1 without a restart since the strict aliasing fix. Saving on the board has not been tried.
## 2026-11-24: XIP and float counters, world front end and alias vertices faster, same pixels (Agent Z, PC and ARM build, registered data)

A measurement build that tells whether a render phase waits on flash or on float helpers, and four exact speed-ups of the display chip's 3D view, all with the same pixels as main. Nothing ran on hardware. The savings are estimates from the PC's work counts and the Thumb code at 250 MHz, the lead measures them.

### Measurement build (off by default, needs `QOG_BOARD_TIMING`)

```powershell
cmake -B build-zx --preset target -DQOG_AOT=ON -DQOG_AOT_PAK=<pak0.pak> -DQOG_SOUND=ON -DQOG_CL_STACK_KB=32 -DQOG_BOARD_XIPSTATS=ON
cmake --build build-zx --target quake_main
```

Add `-DQOG_BOARD_TEXNOALLOC=ON` or `-DQOG_BOARD_CMAPNOALLOC=ON` for the flash experiments below. The float counts are a second build, `-DQOG_BOARD_FPCOUNT=ON -DQOG_CL_STACK_KB=31` (it needs 1 KB of the client's stack, measurement only, watch the stack watermark line). Its times run slow (each helper call passes a counting stub in flash), so read times and XIP numbers from the XIPSTATS build and only the counts from this one.

Every 2 s on COM8, after the three `[time cl]` lines:

```
[time cl] xip acc/miss setup a/m world a/m bmodels a/m lmap a/m grad a/m spans a/m zspans a/m sky a/m turb a/m models a/m vmodel a/m parts a/m total a/m edgescan a/m
[time cl] work per view nodes <n> leafs <n> faces <n> clipedge <n> emitedge <n> cached <n> project <n> clipdot <n>
FPCOUNT only:
[time cl] fp f/d setup f/d world f/d ... total f/d edgescan f/d
[time cl] fp calls per view core0 fadd <n> fsub fmul fdiv fcmp f2i i2f f2d fmath dadd dsub dmul ddiv dcmp d2i d2f i2d dmath idiv l64 core1 f <n> d <n>
[time cl] fp ns per call fadd <ns> ... l64 <ns> meanf <ns> meand <ns>
[time cl] fp est ms core0 all <ms> setup <ms> world <ms> ...
```

- xip: XIP cache accesses and misses (accesses less hits, XIP_CTRL CTR_ACC and CTR_HIT) per view in each phase, both cores together (core 1 draws half the spans inside the spans phase). Misses times the cost of one (an 8-byte line from the 125 MHz flash, a few hundred ns) against the phase's time says how much of the phase waits on flash. The counters saturate at 2^32, so the report clears them every 2 s. Phase marks run from RAM (`qog_phase_mark`, `qog_phase_add`) and add no XIP accesses of their own, but cost about 0.2 ms a frame.
- work: the world front end's calls per view, and the points projected and the plane distances computed (what `QOG_VERTCACHE` saves).
- fp: core 0's float (f) and double (d) helper calls in each phase. `objcopy --redefine-syms` (port/rp2040/qog_fpcount.syms) renames the engine library's calls to stubs (port/rp2040/qog_fpcount.S) that count per core and jump on to the SDK's routine with the caller's registers (only ip changes). The ns per call are timed once on core 0 at the first report (2,000 calls, best of three). est ms is the calls times the mean measured cost, so a phase whose est ms is near its time is float bound.

RAM: the XIPSTATS build ends at 0x2003f7f0 (16 B above the 2 KB heap reserve), the FPCOUNT build at 0x2003f550 with the 31 KB stack.

### Exact speed-ups (on for the board client, apps/quake/CMakeLists.txt)

| Change | Flag (CMake option) | Why the pixels are the same | Estimated saving per view, 250 MHz, E1M1 spawn | RAM |
| --- | --- | --- | --- | --- |
| World vertices projected once a frame. A direct-mapped cache keyed by vertex number keeps u, v, 1/z and ceil(v) of each world vertex R_EmitEdge projects, in memory nothing uses while R_RenderWorld runs (the frame buffer's view rows past the edge cache, or the z-buffer between the edge cache and the edge table), emptied every frame. PC E1M1 spawn, 1,092 projections a view became 506 | `QOG_VERTCACHE` (`QOG_BOARD_VERTCACHE`) | a hit returns what id's statements (now in R_QogProject) computed for the same vertex in the same frame. Brush entities (insubmodel) and clipped points never use it | 586 projections of about 1,750 cycles less 1,600 lookups of about 30, about 4 ms of world | 16 B static, 512 to 4,096 entries in the frame buffer (E1M1 on the board 512 with CLIPDIST) |
| Each world vertex's distance to the four view planes measured once a frame, in the same entries. R_ClipEdge's 954 dot products a view became 464 | `QOG_VERTCACHE_CLIPDIST` (`QOG_BOARD_CLIPDIST`) | the same expression, once per vertex and plane in a frame (the view planes do not change while the world is drawn). An unknown distance holds a NaN pattern, a real NaN with those bits is only computed again | 490 dot products of about 370 cycles, about 0.7 ms | entries 32 B instead of 16 |
| BSP node frustum tests in a function off the recursion, the box corners converted once per node, compares instead of a difference (main's `QOG_FLOAT_EXACT` already made the doubles floats), the side tests as an int | `QOG_WORLDNODE_FAST` (`QOG_BOARD_WORLDNODE`) | `(double)dot - (double)dist <= 0` is `dot <= dist` exactly, and for a float x `x < -0.01` and `x > 0.01` are `x < -0.01f` and `x > 0.01f` | about 0.3 ms. R_RecursiveWorldNode's frame 96 to 48 B, 48 B less stack at every level of the BSP recursion | 0 |
| Alias model vertices made by both cores, core 0 the even ones and core 1 the odd ones (R_AliasPreparePoints and the unclipped R_AliasTransformAndProjectFinalVerts), models of 24 vertices or more. Triangles stay on core 0 in id's order | `QOG_ALIAS_CORE1` (`QOG_BOARD_ALIASCORE1`, with `QOG_BOARD_CORE1`) | each vertex is computed by id's statements from data nothing changes meanwhile and written only to its own finalvert | v_shot (68 vertices) about 0.3 ms, a soldier (170) about 0.7 ms | 0, core 1's chain about 150 B |

PC verification, hidden and silent, against a build of the same tree with the flags off (`build-pc-mbase`, which gives main's hashes): `qogtest_hash` (`ce86f894f9de`), `qogtest_f_hash` (`e92bc9e9650d`) and its 8 shots, `qogtest_k_quiet` + `qogtest_d_shots` 113 frames, all identical, and the split pair's `qogtest_hash`, `qogtest_r_fhash` and `qogtest_d_shots` identical to the single process. With `QOG_SPANS_CORE1` the PC runs the core 1 job on a second thread, first or last (`-core1first`, `-core1last`), identical in all three. With `+r_fast 1` (main's Fast mode, whose 113 frames differ from r_fast 0) the 113 frames and the fhash shots are identical to the flags-off build with r_fast 1 too. A build with the cache cut to 64 entries (`QOG_VERTCACHE_MAX=64`, many collisions) is identical too. An integer FloorDivMod was checked against id's double version over 3.8 M pairs (0 differences) and then dropped, since main's `QOG_FLOAT_EXACT` has the same change.

Board RAM (`arm-none-eabi-nm`, merged with main 9192bc9): quake_display `end` 0x2003f650 with the four flags off, 0x2003f660 on (16 B), quake_main 0x2003f51c (unchanged), 416 B above the display's 2 KB heap reserve. Stack: R_RecursiveWorldNode 48 B a level less, R_EmitEdge with R_QogProject 144 B against 96 B at the chain's end, R_ClipEdge 80 to 56 B (up to 4 deep).

### Flash misses in the spans

Fast mode cut 2.9 M cycles of arithmetic and the spans moved by 0.9 ms, which points at flash. Per lit pixel the spans read a texel and a colour map entry from flash (the colour map is `gfx/colormap.lmp` in the mapped pack, 16 KB, as big as the XIP cache) and the light grid from RAM. Exact options, in the order I would try them:
- `QOG_BOARD_TEXNOALLOC` and `QOG_BOARD_CMAPNOALLOC` (off): texels or the colour map read through the XIP no-allocate alias 0x11000000 (d_scanlit.c). Hits come from the cache, misses do not allocate, so streaming texels cannot push out the span code, the colour map rows in use and the light grid code. It can also be worse (a texel line read twice is fetched twice), so both are off until two A/B flashes with the xip line decide.
- Colour map rows in RAM: 64 rows of 256 B, no RAM for all of them. A cache of the rows a surface uses would need about 2 KB.
- Hot span loop in RAM: one mip variant of the exact drawer is about 2 KB, the display has 416 B. Only if the xip line shows misses the code causes and RAM comes back.
- Texture prefetch by DMA into RAM: no RAM to put a 4 KB tile in while the spans draw.
- Interpolator for the texel address (SIO interp0, lane 0 s, lane 1 t, base the texture): about 6 cycles a pixel of address arithmetic per core, about 0.6 ms a view, compute only. Not done.

### Picture-changing ideas (for the user, not done)

- Edge u stepping in 16.16 fixed point instead of float in R_EmitEdge: edge positions move by a sub-pixel, maybe 1 to 2 ms of world.
- Node culling with integer box tests against quantised planes: rare one-node differences, about 1 ms.
## 2026-11-24: Every episode in the split client's RAM, and level exits on the board client (Agent AD, PC and ARM build, registered data)

The board client's hunk goes from 73 to 75 KB and now holds every level of the game with at least 3.8 KB to spare on a fight tour: seven exact cuts take 7.4 KB off the worst level (e2m6, 78.4 to 71.2 KB) and free 1,656 B of static RAM for the hunk. Before them, a bug of the z-buffer receive buffer stopped the board client at every level exit. Nothing ran on hardware.

Bug found and fixed (`QOG_RECONNECT_FIRST`, both server halves, engine/sv_main.c): with Agent Y's `QOG_LINK_RX_ZTEMP` every `changelevel` stopped the board-flag client with "link: 16 bytes of messages wait while the view is drawn". SV_SendReconnect sent the reconnect message and then had the client run `reconnect` in the same exchange. The client's reconnect draws the loading plaque over the old level's view while that message waited in the z-buffer. The server now has the client run `reconnect` first. Nothing the reconnect runs reads the receive buffer and a delivery runs no client code, so the client does the same work. `qogtest_ad_chg.cfg` (map e1m1, changelevel e1m2) gives the single process's state hash on the board-flag pair, and stopped before. Every level exit in play is a changelevel, while the test scripts changed levels with `map`, which disconnects first (L70).

Cuts (all on the board client list in apps/quake/CMakeLists.txt, each behind a flag of its own):

| Cut | Flag | Saved on e2m6 |
| --- | --- | ---: |
| Beam segments of temp entities are only the drawn part of entity_t (40 bytes, not 88), they never get a network update or efrags | `QOG_TEMPENT_SMALL` (cl_tent.c) | 3,136 B of hunk with 64 segments |
| entity_t 88 to 76 bytes. Only statics have efrags and only R_StoreEfrags reads visframe, through them, so both share 8 bytes with the network state in a union. The bmodel being drawn keeps its top node in one variable (`ENT_TOPNODE`, set before its faces are drawn, read only then). A static entity is 48 bytes. R_RemoveEfrags of an emptied client entity is left out, its efrag was always NULL | `QOG_ENTITY_NOEFRAG`, `QOG_ENTITY_NOTOPNODE` (render.h, cl_main.c, r_efrag.c, one or two lines in r_main.c and r_bsp.c) | 2,600 B (13 blocks of 16 entities) and 440 B of statics |
| Hunk blocks get an 8-byte header (size and four letters of the name, no sentinel) and 8-byte rounding, not 16 and 16 | `QOG_HUNK_HDR8` (zone.c) | about 1 KB over about 110 blocks |
| An item box's four frame bit arrays share packed blocks instead of four 32-byte blocks | `QOG_ITEMBOX_PACKED` (model.c) | 960 B with 11 item boxes |
| Menu pic names point into the mapped pack's directory (as the mixer's sound names do), 12 bytes a slot, not 32. A pic not in the pack stops with id's "failed to load" | `QOG_CACHEPIC_NAMEPTR` (draw.c, common.c, common.h) | 1,120 B static |
| Light style values are shorts ((char - 'a') * 22 or 256 fits) | `QOG_LIGHTSTYLE_SHORT` (r_shared.h, r_main.c) | 512 B static |

Client hunk per level (hunk low at the end of each level of a `qog_tour_fight`, PC split pair with the board client flags, on that episode's board image `mkboard.py --stub-maps --main --display-no-sounds --display-no-progs`, KB). Before is main 9192bc9's client with the fixed server. The tour now takes out level exits and the two dead teleporters first (L72). Tour numbers move by a block of entities from run to run (L64).

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

Episode 1 after (its own image, the same tour): start 52.2, e1m1 53.0, e1m2 58.6, e1m3 66.3, e1m4 67.0, e1m5 60.8, e1m6 62.6, e1m7 46.8, e1m8 53.8 (Agent Y's before, e1m4 70.4). Worst per episode after: 1 67.0, 2 71.2, 3 67.2, 4 71.0. Agent Y's whole-game figure (80.4 on e2m6) was taken on data/id1, where the PC keeps a 1,744-byte `packpos` block the board never has (L71). e4m5 and e4m8 were never toured before because of the teleport error, and e4m5 is as big as e2m6.

e2m6 by block, before and after (bytes): entities 18,512 to 15,912, temp entities 5,760 to 2,624, statics 4,752 to 4,312, item boxes 1,408 into the packed blocks (1,520 to 1,968), the rest only the smaller headers. Total 80,288 to 72,920.

Exactness (hidden, `-nosound`, `+_cl_color 0`, own data folder and link ports): on the board-flag split pair `qogtest_hash` md5 ce86f894f9de and `qogtest_f_hash` e92bc9e9650d, and the 121 frames (113 of `qogtest_d_shots` after `qogtest_k_quiet` and the 8 fhash frames) byte identical with the same pair built from main. `qogtest_ad_beam.cfg` (lightning gun on e1m1 and e2m6, 21 beam segments a frame, 14 frames) byte identical with the same state hash. The default pair and quakeog_pc give ce86f894f9de and e92bc9e9650d.

Board links (`cmake -B build-ad/arm --preset target -DQOG_AOT=ON -DQOG_SOUND=ON -DQOG_CL_STACK_KB=32 -DQOG_SV_HUNK_KB=160`, free is beside the 2 KB heap reserve):

| | main 9192bc9, 73 KB | cuts, 73 KB | cuts, 75 KB (the new default) |
| --- | --- | --- | --- |
| quake_display `end` | 0x2003f650, 432 B free | 0x2003efd8, 2,088 B free | 0x2003f7d8, 40 B free |
| quake_main `end` | 0x2003f51c | 0x2003f51c | 0x2003f51c |

quake_display.bin 363,320 B. The client stack stays 32 KB, no frame on the deep path grew.

The e4m5 and e4m8 teleport error is a game data bug, not the port and not the script. In each map a `trigger_teleport` (e4m5 entity 508, model *95, at 816 2208 256, and e4m8 entity 400, model *75, at -456 808 32) targets `t178` and `t273`, which name only an `info_null`. QuakeC's `info_null` removes itself at spawn, so `teleport_touch` finds no target and calls objerror, a Host_Error ("Program error"). id's interpreter in one process (quakeog_pc without compiled QuakeC) stops the same way when the player is put on them (`qog_tour_spot 824 2216 296` on e4m5, `qog_tour_spot -448 816 64` on e4m8). The tour reached them by noclip, whether a player can reach them in normal play is not known. `start` has one more (to `t11`), never touched.

Not done, or to watch:
- Once a full episode 4 tour stopped the server half at `end` with "MOVETYPE_PUSH with a non bsp model" (world.c). Three tours of `end` alone (two with the cuts, one before) did not. The server half's code is main's apart from the reconnect order and the PC tour.
- Not approved, sizes on the board client: particles 512 to 256 9,216 B, console text 4 to 2 KB 2,048 B, command buffer 4 to 2 KB 2,048 B, edge-drawing surfaces 600 to 500 3,200 B of stack, zone 8 to 6 KB 2,048 B. None is needed now.
- Further exact candidates, not done: efrag_t as 8 bytes of 16-bit indices (1.3 KB on e2m6), the model table as parallel arrays (576 B static), the entity colormap as a byte (entities 72 bytes, about 1 KB on e4m5).
- Files shared with Agent Z: engine/r_main.c (the light style array type and two `ENT_TOPNODE` lines), engine/r_bsp.c (two `ENT_TOPNODE` reads). With Agent AC: engine/sv_main.c (SV_SendReconnect) and apps/quake/CMakeLists.txt.
- Test scripts (`qogtest_ad_chg.cfg`, `qogtest_ad_beam.cfg`, `qogtest_ad_tele5.cfg`, `qogtest_ad_tele8.cfg`) are in the branch's git-ignored data/id1, the tour runs in build-ad/tours.sh.

## 2026-11-24: Fast render mode and single precision front end math (Agent AA, PC and ARM build, registered data)

The user asked for a picture that loses a little, like MG24Quake's, but where the loss is hard to see. The board client now has a render mode switch, the `r_fast` cvar (archived), shown in the board Options menu as `Render  Exact / Fast` (engine/menu.c, item 20, left, right or gray toggles it). It is 1 by default on the board client (`QOG_BOARD_FAST`, on, adds `QOG_RFAST QOG_RFAST_ON` to the client's flags) and 0 in every other build. `r_fast 0` draws exactly what main draws. Nothing ran on hardware. The board savings below are estimates from the generated Thumb code and the PC's work counts, the lead measures them.

| Item | Where | Kind | Pixels changed (121 shots, view rows) | Estimated board saving per view at 250 MHz |
| --- | --- | --- | ---: | --- |
| a. Perspective divide every 8, 16 or 32 pixels: before each sub-span the drawer takes the longest one over which 1/z changes by at most 8% (`LITSPAN_FAST_ZREL`). Floors and ceilings (1/z constant along a row) and walls seen straight on take 32, walls at a steep angle close by keep 8 | engine/d_litspan.h `D_LITSPANS_FAST_BODY` | approximate | 1.49% alone (fixed 32 as MG24Quake: 14.3%, worst frame 38%. Fixed 16: 6.1%) | E1M1 fly-through: 3,866 sub-spans a view instead of 6,815 runs of 8, about 560 cycles each, less 140 cycles a span for the last sub-span's multiply. About 1.4 M cycles |
| b. Light blended exactly at both ends of each run of 8 and stepped linearly between (`QOG_LITSPAN_LINLIGHT`'s one-pass run). Textures of other sizes keep the exact light | same | approximate | 1.28% alone (runs of 16: 4.2% together with a and d) | pixel loop 21 instructions (objdump) against the exact two passes of 37 + 14, about 25 cycles a pixel with the blends, about 1.2 M cycles |
| c. `QOG_FLOAT_EXACT`: single precision where id wrote double but the float result is provably the same. R_EmitEdge's near clip compare (`x < 0.01` is `x <= (float)0.01`) and ceil (ceilf), R_RecursiveWorldNode's plane distances (only signs and compares with 0 or `(float)BACKFACE_EPSILON`), D_PolysetCalcGradients' ceil, FloorDivMod on the ints themselves in D_PolysetSetUpForLineScan, `1.0 / x` stored in a float in r_draw.c, r_alias.c and d_part.c (GCC already emitted a float divide there, MSVC did not) | engine/mathlib.h (QOG_FRECIP, QOG_CEILF), r_draw.c, r_bsp.c, r_alias.c, d_polyse.c, d_part.c | exact, both modes | 0, hashes identical | objdump: R_EmitEdge and R_RecursiveWorldNode now call no double routine (they called 4 f2d, 2 d2iz, 2 dcmp, 2 ceil, and 7 f2d, 2 dsub, 5 dcmp). Per fly-through view 588 ceils, 722 near clip compares, 1,112 frustum plane tests, 180 node sides: about 0.17 M cycles, 0.7 ms world. Alias: about 25 FloorDivMod calls a view at about 800 cycles, 0.1 ms vmodel |
| d. 0x10000 / zi from the integer divider: the mantissa's top 16 bits into 2^32 - 1, the exponent set directly, about 16 good bits (`litspan_frecip`). Zero, negative, tiny and huge values take the float divide | engine/d_litspan.h | approximate | 0.06% alone | a float divide (about 100 cycles) becomes about 50, at every sub-span and span start, about 0.3 M cycles |
| All of a, b, d (the shipped Fast mode) | | | **2.76%**, worst frame d_demo3_15 9.2% | about 2.9 M cycles a view of span work on the two cores: 11.6 ms of CPU, so spans 30.7 ms falls by about 6 ms (compute only) to about 9.5 ms (if the memory stalls shrink with the work), to roughly 21 to 25 ms |

Frame estimate with Fast on: 95.9 ms becomes about 85 to 89 ms, 11.2 to 11.8 fps. The picture loss: the changed pixels are texels moved by one at sub-span joins and light steps of one colormap row, the largest colour step 248 is one texel shifted on a high contrast edge. Side by side the frames look the same (`fast` folder below).

Variants measured on the PC (each its own build, `-DQOG_PC_EXTRA_DEFS`, 113 qogtest_d_shots frames and the 8 qogtest_f_hash shots against main):

| Build | Pixels changed | Worst frame |
| --- | ---: | --- |
| linear light runs of 8 only | 1.28% | d_spawn_e3m3 5.2% |
| integer reciprocal only | 0.06% | f_e4m3 0.38% |
| adaptive 8/16/32, ZREL 0.08, only | 1.49% | d_demo1_12 9.0% |
| fixed 16 | 6.14% | d_demo3_06 21% |
| fixed 32 (MG24Quake) | 14.27% | d_demo3_06 38% |
| shipped: adaptive 0.08, light runs of 8, reciprocal | 2.76% | d_demo3_15 9.2% |
| ZREL 0.04 (4,505 sub-spans a view, not 3,866) | 1.84% | d_demo3_19 7.3% |
| ZREL 0.15 (3,166 sub-spans a view) | 6.51% | d_demo3_15 24% |
| light runs of 16 | 4.17% | d_demo3_15 12.6% |
| light runs of 32 | 6.59% | d_demo3_19 19.6% |

`LITSPAN_FAST_ZREL`, `LITSPAN_FAST_LRUN` (0 keeps the exact light), `LITSPAN_FAST_SUB` and `LITSPAN_FAST_RECIP` are compile-time knobs in d_litspan.h.

Exact mode proof (hidden, `-nosound`, `+_cl_color 0`, own folders `build-aa-pc` and `build-aa-ref`, own git-ignored `data\id1`): against an untouched build of 876d2fe, `qogtest_hash` gives `ce86f894f9de` (MD5 of its two lines), `qogtest_f_hash` 32 hashes with MD5 `e92bc9e9650d`, and the 113 `qogtest_d_shots` frames and the 8 fhash frames are byte identical. The Fast runs keep the same `qogtest_f_hash` (rendering does not touch game state). A PC build with `QOG_SPANS_CORE1` draws the same Fast frames as one without, so the second core's half is right.

Code and RAM:
- d_scanlit.c: `D_DrawSpans8LitFast` is a function of its own. Both cores pick the drawer before the call (`D_LITSTEP`), so the Fast frame (376 B) never stacks on the exact one (432 B, unchanged). Core 1's chain grows by nothing.
- r_main.c: `r_fast` and `r_qogfast`, an int set once per view in R_RenderView_, so both cores and every surface of a view use one mode. r_local.h: two externs.
- quake_display after merging main (c5fa930): `end` 0x2003ee74, 20 B above main's 0x2003ee60 (the cvar 24 B and the int, less padding). text 362,836 B, the Fast drawer 3.3 KB. quake_main `end` 0x2003f564, unchanged (the server half gets neither flag's code).
- qogbench (apps/qogbench) prints a third line every 2 s for the lead: `[bench] rfast p2 exact=<us> rfast=<us> | short exact=<us> rfast=<us> | floor exact=<us> rfast=<us> | fdiv=<ns> frecip=<ns>`: the shipped exact drawer and the Fast drawer on the slanted plane, on game-like spans, on a floor seen level (where Fast takes 32), and one float divide against `litspan_frecip`.
- `QOG_RFAST_STATS` (PC measurement only) and `qog_rfaststats` print the Fast drawer's sub-spans by length, pixels, spans and the exact drawer's runs of 8 for the same spans. `data\id1\qogtest_aa_stats.cfg` in the branch's own data folder runs it over 400 fly-through views of E1M1.

Files both Agent AA and Agent Z may touch: engine/r_bsp.c (one declaration block, two compares), engine/r_draw.c (a macro after the includes and one-line edits at each reciprocal, ceil and near clip), engine/r_main.c (the cvar and one line in R_RenderView_), engine/r_local.h (two externs), engine/d_scanlit.c (the drawer choice). Each edit is a few lines.

Looked at and left, or for the user to decide (ideas to get nearer 27 fps, none done):
- A coarser mip level (id's `d_mipscale` above 1, for example 1.5 in Fast) reads fewer texture bytes from flash, which is likely where the spans' memory stalls are. Blurrier distant walls, more visible than the items above.
- Fast mode's last sub-span of a span multiplies the gradients by its length (an int to float and three multiplies, about 140 cycles a span more than the exact drawer's cached products). A 32-entry cache costs 384 B of stack.
- The front end (world 26.3 ms) has no cheap approximation that stays hard to see: it is the BSP walk and edge setup, whose cost is mostly memory and exact float work. That is Agent Z's field.
- The view model (4.9 ms): an affine alias drawer is already affine. Skipping its per-vertex light or drawing it in fewer rows would show.
- 27 fps is a frame of 37 ms. The view alone is 75.7 ms today and Fast takes about 6 to 10 ms of it. Getting there needs the front end halved as well, or a smaller picture, which the user has not approved.
## 2026-11-24: Episode 1 in the split client's RAM, the display image without sounds, whole-game numbers (Agent Y, PC and ARM build, registered data)

The split client now runs start and all of episode 1 in its hunk: QOG_CL_HUNK_KB goes from 68 to 73 KB, from four exact cuts that free 10.6 KB of static RAM and hunk. `tools/mkboard.py --display-no-sounds --display-no-progs` leaves 2.86 MB of files the client never opens out of the display image, but episode 1 still needs 13.19 MB of the 11.94 MB region, and main's image with episode 1's sounds needs 5.75 MB of its 5 MB. Both fit if the data offsets move (proposed below, not applied). Main frees 4 KB and its server hunk goes from 156 to 160 KB, still short of e1m3 (165 KB without sound). Nothing ran on hardware.

Cuts (`QOG_ENTITY_TIGHT` and `QOG_CON_DEBUGLOG_DIRECT` in both flag lists, the other two on the board client list only):

| Cut | Flag | Saved |
| --- | --- | ---: |
| entity_t 104 to 88 bytes. frame is a short, effects, skin and trivial_accept are bytes, the ST_RAND start is kept as the 15-bit number id draws and turned into id's float where it is read (`ENT_SYNCBASE`). The network state goes last, so a static entity is only the first 52 bytes, its baseline is read into a scratch entity | `QOG_ENTITY_TIGHT` (engine/render.h, cl_main.c, cl_parse.c, r_alias.c, r_sprite.c) | 16 B a client entity, 52 B a static entity, about 4.5 KB of hunk on e1m3 |
| MSG_ReadString returns a string that ends in a 0 within 2047 characters in place in the message (every string a server writes). Any other ending (a 0xFF byte, the end of the message, 2047 characters) is copied the way id copies it, into 2 KB of the z-buffer after the status bar rows | `QOG_MSG_STRING_INPLACE` (engine/common.c) | 2,048 B static |
| The link's receive buffer (a reliable message and a datagram, 7,536 B) lives in the z-buffer after that, not on the hunk. It holds messages only from their delivery until CL_ReadFromServer has read them all, and no view is drawn in between. The server half sends messages only in its frame's answer and in a console line's answer, both read after any pipelined view and before the frame's own, and sound answers carry none. R_RenderView stops with an error if bytes still wait (`Link_RxCheckIdle`), so a break of that rule can never draw over a message | `QOG_LINK_RX_ZTEMP` (engine/net_link.c, r_main.c) | 7,536 B of hunk |
| Con_DebugLog writes its text directly (both callers pass "%s" and the text) | `QOG_CON_DEBUGLOG_DIRECT` (engine/console.c) | 1,024 B static |
| main only. The BSP frame sender wrapped as on the display, the same bytes in three writes | `QOG_LINK_STREAM_SEND` on quake_main | 4,168 B static on main |

The z-buffer between views now holds, after the 15,360 status bar bytes, MSG_ReadString's fallback (2,048 B), the receive buffer (7,536 B), and at its end the precache names of CL_ParseServerInfo (at most one message). Link_RxBuffer checks the room.

Client hunk per level (split PC pair, board client flags plus `QOG_CL_PEAKS`, from the episode 1 board images, KB). Script is the smallest `-heapsize` that runs `qogtest_y_mem_<map>.cfg` (Agent R's fight, save and load on that map, deterministic). Tour is the hunk low at the end of each level of a `qog_tour_fight` of start to e1m8 (it varies by a block of entities between runs).

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

Exactness (hidden, `-nosound`, `+_cl_color 0`, against untouched builds of f75051b in the same worktree, and again after merging main c5fa930): `qogtest_hash` ce86f894f9de and `qogtest_f_hash` e92bc9e9650d (md5 of the statehash lines) on quakeog_pc, on the default split pair and on the split pair with the board client flags. `qogtest_r_fhash` 32 hashes on both pairs. 121 frames (113 of `qogtest_d_shots` after `qogtest_k_quiet` and the 8 fhash frames) byte identical, quakeog_pc against its base and the board-flag client against its base (one run showed a notify line in rows 1 to 23 of f_e1m3 at another moment, the rerun was identical). `qogtest_j_ep1.cfg` (9 hashes), `qogtest_j_menu.cfg` and the e1m3 script give the same hashes from the image with sounds and progs.dat and from the image without them. The receive buffer check never fired.

Board links (`cmake -B build-y/arm --preset target -DQOG_AOT=ON -DQOG_SOUND=ON -DQOG_CL_STACK_KB=32`, merged with main c5fa930):

| | main c5fa930 | this branch |
| --- | --- | --- |
| quake_display `end` | 0x2003ee60, 2,464 B free | 0x2003f63c with a 73 KB hunk, 452 B free |
| quake_main `end` | 0x2003f564, 668 B free | 0x2003f51c with a 160 KB hunk, 740 B free |
| quake_display.bin | | 359,816 B |
| quake_main.bin | | 1,296,636 B, 41.2% of the slot |

Server hunk (smallest `-heapsize` of the server half for the same scripts, QOG_AOT, the board's server flags, run with `-nosound`, so the board needs 4 to 8 KB more for sound, Agent W measured 116 to 120 on e1m1): start 90, e1m1 112, e1m2 139, e1m3 165, e1m4 155, e1m5 154, e1m6 140, e1m7 88, e1m8 115. With 160 KB start, e1m1, e1m2, e1m6, e1m7 and e1m8 fit with sound, e1m4 and e1m5 only without it, e1m3 needs about 170. Hunk low after the script on other levels: e2m6 154.8, e4m5 167.2, e4m8 152.0, end 101.2.

Display data (`mkboard.py --stub-maps --display-no-sounds --display-no-progs`, start plus the episode, manifests of every level recorded in the branch's `build-y/manifests`):

| Set | Display image | Main image (with its sounds) |
| --- | ---: | ---: |
| episode 1, sounds kept | 16,845,808 B (16.07 MB) | 6,029,312 B |
| episode 1, no sounds | 14,224,368 B (13.57 MB) | 6,029,312 B |
| episode 1, no sounds or progs.dat | 13,831,152 B (13.19 MB) | 6,029,312 B (5.75 MB) |
| episode 2 | 14,654,720 B (13.98 MB) | 6,262,784 B (5.97 MB) |
| episode 3 | 13,812,656 B (13.17 MB) | 6,246,400 B (5.96 MB) |
| episode 4 and end | 16,237,888 B (15.49 MB) | 6,967,296 B (6.64 MB) |
| whole game | 39,819,120 B (37.97 MB) | 15,261,696 B (14.55 MB) |

Proposed offsets (not applied. `platform/qog_flashmap.h`, `tools/mkdata.py` and mkboard's constants change together, the bootloader's 128 KB stays):
- Display data at 0x100000. The app slot becomes 913,408 B (the app is 359,816 B), the data region 15,663,104 B (14.94 MB). Episodes 1, 2 and 3 fit one at a time (1.75, 0.96 and 1.77 MB spare), episode 4 is 574,784 B over. At 0x200000 (13.94 MB) episodes 1 and 3 fit, episode 2 is 40,192 B over.
- Main data at 0x200000. The main slot becomes 2 MB (the app is 1.30 MB), the region 6 MB up to the FAT volume. Episodes 1, 2 and 3 fit (256, 28 and 44 KB spare), episode 4 is 675,840 B over. Today main's 5 MB do not hold episode 1 with its sounds (786,432 B over).

Client hunk per episode after the cuts (worst level, tour or script): episode 1 70.4 KB (e1m4), episode 2 80.4 (e2m6), episode 3 73.9 (e3m4), episode 4 76.1 (e4m6, e4m5 and e4m8 stop the tour with a QuakeC teleport error, their scripts 73.4 and 69.0). With about 1.5 KB of margin episode 1 needs 73 KB, episode 3 76, episode 4 78, episode 2 and the whole game 82, 9 KB more than the board has.

Not approved, for the user (sizes on the board client):
- particles 512 to 256, 9,216 B (closes the whole game alone)
- console text 4 KB to 2 KB, 2,048 B, and the command buffer 4 KB to 2 KB, 2,048 B
- edge-drawing surfaces 600 to 500, 3,200 B of stack (the e1m8 tour peaks at 509)
- zone 8 to 6 KB, 2,048 B (Agent S saw 6 KB fail at level loads)

Level swap (estimates, nothing built). A level is about 1.1 MB of display data on average and 1.6 MB at most (start), plus 0.2 to 0.5 MB on main. Main cannot hold the whole game (14.55 MB of server data alone against its 5 MB region and the FAT volume's 8 MB), so the next level comes from the PC companion over USB, or one episode is written at a time. The display must stop running from flash while it erases and programs, and with 452 B of RAM spare it takes 4 KB sectors one at a time from main over the link. For 1.6 MB the link at 12.5 Mbaud takes about 1.4 s (1.1 MB/s after framing, wire to stream 93% on the PC), 64 KB block erases 25 x 150 ms typical 3.8 s (up to 50 s worst case by the datasheet), page programs at about 0.5 MB/s 3.2 s, and USB full speed CDC at about 1 MB/s 1.6 s overlapping the link. About 6 to 9 s per level typical, an episode at a time (14 MB) about 70 to 90 s.

Not done, or to watch:
- The one-byte z-buffer's edge fallback ran 4 times in an episode 1 fight tour on the PC (3, 3 and 1 in episodes 2, 3 and 4, at most 341 edges). On the board each stops the game (`QOG_ZEDGE_STACK_GUARD`). Open before, not changed here.
- The server hunk was measured without sound because PC tests run with `-nosound`.
- The receive buffer and the in-place strings are on the board client list only, the single-process PC game keeps id's buffers.
- Test scripts (`qogtest_y_mem_<map>.cfg`, `qogtest_y_manifest*.cfg`) are in the branch's git-ignored `data/id1`.

Episode 1 images for the board once the offsets move (today the display image is 1.31 MB too big and main's 0.75 MB):

```powershell
python tools\mkboard.py --pak data\id1\pak0.pak --pak data\id1\pak1.pak --manifests build-pc-dev\manifests `
    --maps start e1m1 e1m2 e1m3 e1m4 e1m5 e1m6 e1m7 e1m8 --exe build-pc-dev\Release\quakeog_pc.exe `
    --stub-maps --main --display-no-sounds --display-no-progs -o build-board\qb
```

## 2026-11-24: 250 MHz and 125 MHz flash for the Quake apps (Agent AB, ARM build, not run on hardware)

The user approved both clock options of Agent V's table below: clk_sys 200 to 250 MHz and the QSPI flash divider 4 to 2, on both chips. They are set at run time by the two Quake apps only (port/rp2040/clock_rp2040.c, called from sys_rp2040.c right after `board_init()`). The BSP is not changed: its 200 MHz, 1.15 V and flash CLKDIV 4 stay the defaults of every other app and of the display bootloader, which must keep working at its settings because the display CPU has no BOOTSEL button. A per-target `FWOG_SYS_CLK_KHZ` would not reach `board_init()` (it is compiled once into the fwog_display_bsp and fwog_main_bsp libraries), so the apps change the clock after it.

| Step (in order) | What and why |
| --- | --- |
| Voltage | 1.20 V (`QOG_SYS_MV=1200`), set before the clock with the BSP's 10 ms settle. The BSP's `fwog_vreg_mv_for_khz` clamps at 1.15 V on purpose (200 MHz at 1.15 V is the rated point). 1.20 V is the lowest step widely reported stable at 250 MHz on RP2040 boards (many run 250 MHz at the default 1.10 V, 1.25 V and 1.30 V are what 270 to 300 MHz overclocks use). One step above the BSP keeps the extra power on a battery board small. `-DQOG_SYS_MV=1250` if a board proves unstable |
| Clock | `set_sys_clock_pll` with the VCO 1500 MHz, post dividers 6 and 1 that `check_sys_clock_khz(250000)` finds. clk_peri follows (board header), clk_ref and the 1 MHz timer tick do not change |
| Re-derived after it | display: the backlight PWM divider (board.c's 10 kHz, wrap 255) and the I2C1 rate (`i2c_set_baudrate`, charger, RTC, accelerometer, expander). main: the FPGA clock on GPOUT1. Its divider is raised for 250 MHz before the switch (20 MHz at 200 MHz, never above the gateware's 25 MHz) and set again after (250 / 10 = 25 MHz) |
| Flash divider | clk_sys / 2 = 125 MHz, from RAM, interrupts off, before core 1 starts, the same way on both chips. The part is the W25Q128JVPIQ (bsp/main_cpu/fs/fs_geom.h), rated 133 MHz for the quad I/O fast read 0xEB that boot2_w25q080 sets up (continuous mode 0xA0, 8 address and mode nibbles, 4 dummy clocks). Div 2 is tried only when clk_sys / 2 is at most 133 MHz |
| RX sample delay | 1 system clock, what the SDK's boot2_w25q080.S writes for every divider including 2 (its comment: with CLKDIV 2 data is captured as the next SCLK edge launches). That is also what runs on overclocked Picos at 125 to 133 MHz flash. If 1 fails the check, 2 is tried |
| Check and fallback | the first 128 KB of the app's own image are hashed (FNV-1a over words) through the uncached alias 0x13000000 at the old divider with interrupts on, then twice at the new one with interrupts off. Any difference writes the old divider and delay back before one instruction is fetched from flash again, and the DIAG line says fallback |
| Main's saves | the SDK's flash erase and program end by running boot2 again, which writes CLKDIV 4 back. `_open`, `_write` and `_close` of the FAT volume call `qog_flash_fast_reapply()`, which puts the checked divider back (a register read when nothing changed) |

The routine that switches the divider was disassembled in both ELFs (`flash_hash`, `ssi_set`, `flash_try.constprop.0`, all at 0x200000c0 to 0x2000017a on the display, 0x20000120 on main, in RAM). Between `cpsid i` and `msr PRIMASK` there are only `bl` to the other two RAM functions, stores to the SSI at 0x18000000 (SSIENR +8, BAUDR +0x14, RX_SAMPLE_DLY +0xF0) and loads from the 0x13xxxxxx alias that is being checked. No literal load or branch reaches 0x10xxxxxx. The flash veneers the linker made (`__flash_try.constprop.0_veneer`, `__ssi_set_veneer`) are only taken from flash code before the SSI is touched, and `ssi_set` returns to its flash caller after SSIENR is 1 again.

Everything derived from a clock, checked at 250 MHz:

| Item | At 250 MHz |
| --- | --- |
| Link UART, display app | `fwog_link_uart_init` at Host_Init, after the boost: 250 MHz / 16 / 12.5 Mbaud = 1.25, IBRD 1 FBRD 16, exactly 12.5 Mbaud |
| Link UART, main and the display bootloader at boot | main boosts before `fwog_display_update_run` (the display is still held in reset, the link is down), so display_update brings the link up at 250 MHz with IBRD 1 FBRD 16. The bootloader stays at 200 MHz, IBRD 1 FBRD 0. Both are exactly 12.5 Mbaud from their own clocks |
| LCD SPI | `st7789_init_begin` at VID_Init: `st7789_actual_spi_hz(250 MHz, FWOG_LCD_MAX_HZ)` = prescale 2, postdiv 2 = 62.5 MHz, the panel's limit. Scanout about 18.3 ms instead of 22.4 (estimate) |
| Core 1 scanout | DMA paced by the SPI, no cycle counts. Core 1 starts in VID_Init, after the switch |
| Speaker I2S PIO | `(clk * 4 / 11025) >> 8` = 354, 250 MHz / (64 x 354) = 11,035 Hz (0.09% fast, was 11,042 Hz). The play position rate is derived from the same numbers |
| WS2812 PIO | `ws2812_init` in game_main, after the switch, divider from clock_get_hz |
| I2C, backlight PWM | re-derived (above) |
| Timers, sleeps, the hang detector, QOG_PHASE_TIME | the 1 MHz tick from clk_ref (unchanged), `timer_hw->timerawl` |
| FPGA clock | 25 MHz (above) |
| USB, ADC | PLL_USB, unchanged |
| Hardcoded 200 MHz in port/, platform/, engine qog files | none found (grep for 200000, MHz, clock_get_hz, busy_wait, cycle counts). The only 200 MHz figure was a comment in snd_rp2040.c, updated |

Build options (apps/quake/CMakeLists.txt, both on by default, nothing else changes):

| Option | Default | Off |
| --- | --- | --- |
| `QOG_SYS_KHZ` | 250000 | `-DQOG_SYS_KHZ=0` keeps the BSP's 200 MHz and 1.15 V |
| `QOG_SYS_MV` | 1200 | 1150 to 1300 |
| `QOG_FLASH_FAST` | ON | `-DQOG_FLASH_FAST=OFF` keeps the divider the bootloader or boot2 set (4) |

RAM (`-DQOG_AOT=ON -DQOG_SOUND=ON -DQOG_CL_STACK_KB=32`, arm-none-eabi-nm): quake_display `end` 0x2003ee60, 228 B above main's 0x2003ed7c (196 B of RAM code, 36 B of state), so about 2.4 KB stay spare beside the 2 KB heap reserve. quake_main `end` 0x2003f564 (the same 232 B object). With both options off the display's `end` is 0x2003ed84. The PC build does not compile these files, and its `qogtest_hash` still gives `ce86f894f9de` (build-pcab, hidden, `-nosound`).

What the board log should say (COM8 display, COM7 main, each printed at the switch, usually before USB is up, and once more about 6 s after boot):

```
[quake] clock 250000 kHz at 1200 mV (BSP 200000 kHz), clk_peri 250000 kHz
[quake] flash 125.0 MHz ok (div 2 from 4, rx delay 1, 128 KB checked, reapplied 0)
```

and further on `[lcd] spi 62500000 Hz (clk_peri 250000000 Hz)`, `[link] uart0 12500000 baud (requested 12500000), clk_peri 250000000` and on main `[fpga] clk 25000000 Hz (clk_sys 250000000 / 10)`. Main's `reapplied` count grows after a save. A failed flash check prints `[quake] flash 62.5 MHz fallback (div 4, rx delay 1): read check FAILED at div 2` and the game runs on at 62.5 MHz. An unstable core at 1.20 V would show as hard faults, `Sys_Error`s or hangs that do not happen with `-DQOG_SYS_KHZ=0`, then try `-DQOG_SYS_MV=1250`.

## 2026-11-24: Menus for five buttons and no keyboard (Agent X, PC and ARM build, registered data)

id's menus expect a keyboard and a network. The board client now builds them with `QOG_BOARD_MENU` (engine/menu.c), set only for the display chip's engine (`_cl_defs` in apps/quake/CMakeLists.txt). Off, menu.c preprocesses to the same tokens as before (checked with the ARM compiler's `-E`, plain and split client), and the default PC build's `qogtest_hash` still gives `ce86f894f9de` (MD5 of its two `qog_statehash` lines). Nothing ran on hardware.

| Menu | On the board |
| --- | --- |
| Main | Single Player, Options, Help, Quit. Multiplayer is gone (one board, no network, and the player name field went with it). The line is cut out of id's `gfx/mainmenu.lmp` (`M_DrawTransPicRows`) |
| Single Player | New Game, Load, Save and a fourth line, Levels, in id's menu font at twice its size (`M_PrintBig`, id has no picture for it) |
| Levels (new) | The maps the packs hold, base pack first: every `maps/<name>.bsp` in the pack directories except the item boxes `maps/b_*` (`COM_ForEachMap`, engine/common.c). So the list follows the data image mkboard writes, nothing is hardcoded. id's title next to each of id's maps (the `levels[]` table of the game options menu, now const). Pages of 16, up and down move, left and right turn pages, gray starts, green + blue goes back. Starting a level sends id's New Game commands through Cbuf: `disconnect` if a game runs, `maxplayers 1`, `deathmatch 0`, `coop 0`, `map <name>` |
| Start a new game? (new) | id asks with `SCR_ModalMessage`, which waits in a loop of its own for Y or N. The board has neither key and reads its buttons only in the main loop and the link wait, so that loop would never end. The board asks inside the menu: gray yes, green + blue no. New Game and Levels both ask while a game runs |
| Options | Controls, Reset to defaults, Screen size, Brightness, Sound Volume, Always Run. "Go to console" is gone (no keyboard), and so are Mouse Speed, CD Music Volume, Invert Mouse, Lookspring, Lookstrafe and Video Options (no mouse, CD or video modes on the board) |
| Controls (was Customize controls) | The buttons in the game (mapping v4 of platform/qog_input.c) and in menus (vid_rp2040.c `menu_keys`), in id's menu font under id's title picture. Green + blue leaves it |
| Quit | gray yes, green + blue no. id's jokes ask for Y and N, so the board shows its own box: quit and stop the game, then hold red for 6 s to switch the board off |
| Load, Save, Help | id's, the buttons already drive them |

No menu left takes typed text, so no letter picker was needed. The chat line (`messagemode`, key_message) is reachable only through a key binding the buttons cannot press.

RAM. Leaving out the network menus (multiplayer, setup, net, serial, modem, LAN, game options, search, server list) and id's key binding page takes their tables with them: the mission pack level tables, the episode tables, the quit jokes, the bind names, the player colour tables (512 B) and the name fields. The kept level table and every new table are const, in flash. The new state is two ints (Levels cursor, which menu asked to confirm).

| quake_display (`-DQOG_AOT=ON -DQOG_SOUND=ON -DQOG_CL_STACK_KB=32`) | Before (f75051b) | After |
| --- | ---: | ---: |
| `end` (arm-none-eabi-nm) | 0x2003f5a0 | 0x2003ed7c (2,084 B lower) |
| menu.c RAM (nm, B and D symbols) | 2,657 B | 592 B |
| text | 376,044 B | 357,884 B |

quake_main is unchanged (the server half does not get the flag).

PC verification (hidden, `-nosound`, own build folders `build-pcx` and `build-pcx-bm`, own data folder `build-pcx\data\id1`, git-ignored). The test build adds the flag with `-DQOG_PC_EXTRA_DEFS=QOG_BOARD_MENU -DQOG_SPLIT_CL_EXTRA=QOG_BOARD_MENU`, which also registers `qog_key <key> ...` (port/pc/vid_pc.c, test builds only): it presses and releases keys, so a script drives the menus as the buttons do.

- `qogtest_x_menu.cfg`: every new page and prompt, 16 screenshots. New Game asked and answered no, the Levels pages (38 maps from the registered paks, 3 pages), a level confirm answered no, Options, Controls, the volume slider, the quit prompt answered no and then yes, which quit the game.
- `qogtest_x_level.cfg` (single process): Levels, e1m3, yes. The game loaded "the Necropolis". `qogtest_x_newgame.cfg`: New Game, yes, loaded "Introduction" (start).
- `qogtest_x_level_split.cfg` on the split pair (`quakeog_cl` with the flag, `quakeog_sv`): the same level start crossed the link and the client entered "the Necropolis". The script has to end with the key that starts the level, because the menu's Cbuf text runs after the rest of a script (L52), so the pair was stopped by the 40 s timeout afterwards.

To watch on the board: the Levels page lists what the data image holds, and today the client hunk fits only e1m1, so picking start (or any map past the hunk) stops the board with a hunk error until Agent Y's work lands. Quit ends in `Sys_Quit`, which on the display is `Sys_Error("quit")`: the picture freezes with the power-off still alive.

## 2026-11-24: Sound for the split game, mixed on main and played by the display (Agent W, PC and ARM build, registered data)

The split game has sound. id's mixer (snd_dma.c, snd_mix.c, snd_mem.c) runs on the main chip for the display chip's sound calls, and the display plays the 8-bit samples it gets back on the I2S amplifier through PIO and DMA. On the PC the mixer of the split pair paints exactly what the single-process game paints for the same scripts, and the state hashes stay the same. Both board apps link with sound on. Nothing ran on hardware and nothing was played on this PC: every PC run mixed silently.

Why mix on main (numbers before this work, from the briefing and the builds): id's sound needs channels 6,656 B, a paint buffer 4,096 B, known_sfx 34,816 B (512 x 68), cache headers about 80 B per sound and an output ring. The display had 560 B free (344 B after Agent V's options) and every chip-side choice on it would cost the game hunk or the frame buffer. The client's sound calls are few and small, the server half knows nothing of the listener, and the samples are 11 KB/s on a 1.25 MB/s link. So the mixer moves to main, the calls cross the link, and the display keeps only an output ring.

| Piece | Where | Flag |
| --- | --- | --- |
| The client's S_ calls become link items (`LK_SND`, operations `LKS_*` in engine/net_link.h). S_PrecacheSound, S_StartSound, S_LocalSound, S_Update and S_ExtraUpdate are exchanges like a console line: main runs them at once and its random sequence comes back with its state, so the random number S_StartSound draws (two identical sounds started in a frame) stays in the shared sequence. The rest are sent in order without waiting. Inside another exchange (and during Agent V's pipelined screen update) an exchange is only queued: main runs it after that exchange in the same order, so it mixes the same samples | engine/snd_link.c (replaces snd_dma.c, snd_mix.c and snd_mem.c on the client), engine/net_link.c | `QOG_SND_REMOTE` (client) |
| What id's mixer read from client state comes with the calls: the view entity, and with S_Update the listener's leaf's ambient levels. The play position is the client's output's | engine/snd_dma.c | `QOG_SND_HOST` (server) |
| GetSoundtime takes id's own path for an output that counts its samples (`__sun__`'s SNDDMA_GetSamples) | engine/snd_dma.c | `QOG_SND_GETSAMPLES` |
| Main's "DMA buffer" is a ring of 2,048 8-bit mono samples, id's 8-bit output. Submit sends what the mixer wrote as `LK_SNDPCM` items between the update and its answer | port/common/qog_snd_host.c | |
| Sound names point into the pack directory in flash, so known_sfx is 8 bytes a sound (id 68). Names no pack has go in a 128-byte pool | engine/snd_dma.c, engine/common.c (`COM_FlashName`) | `QOG_SFX_NAMEPTR`, `QOG_MAX_SFX=256` (the registered game has 225 sound files) |
| Channels 104, not 128: 12 plus static sounds, and the whole game uses at most 99 (e2m6; e1m4 84, start 67, e1m1 26, measured with `-sndtrace` over every map) | engine/sound.h | `QOG_MAX_CHANNELS=104` |
| The paint buffer (id's 512 pairs, 4 KB) and the ring hold samples only while one update runs, so on main they are the lowest 6 KB of the server's stack array, used only between server frames from the link's items, where the stack is a few KB deep (interrupts have their own stack). The stack watermark starts above them. known_sfx (2 KB) takes the free half of main's SCRATCH_Y bank | port/common/qog_snd_host.c, port/rp2040/sys_rp2040.c | `QOG_SND_HOSTMEM`, `QOG_SND_BOARDMEM` |
| The display plays the samples: PIO runs the BSP's audio_i2s program at 200 MHz / (64 x 283) = 11,042 Hz (the BSP's integer divider), a DMA channel reads a 2 KB ring of signed bytes into the TX FIFO (an 8-bit FIFO write is replicated, so byte b goes out as b x 257 on both channels), and a second DMA channel chained from it rewrites the first's read address, so the ring loops with no interrupt and no CPU work. The play position is the read address, the pass of the ring comes from the timer (same crystal). While the link waits, played samples nothing replaced are silenced, so a busy server leaves silence, not a 186 ms loop. Sys_Error stops the bit clock | port/rp2040/snd_rp2040.c, port/rp2040/link_rp2040.c | `QOG_SND_REMOTE` |
| main's data image carries every sound of the display's pak, already 11025 Hz 8-bit mono (mkpak's conversion of the eight 22 kHz sounds was checked against id's ResampleSfx with loadas8bit, byte for byte, and end to end below) | tools/mkboard.py `--main` (`--main-no-sounds` leaves them out) | |
| The board option, on by default. Off builds both halves with -nosound as before | apps/quake/CMakeLists.txt | `QOG_SOUND` |

A bug found on the way and fixed: `Hunk_AllocPacked` (`QOG_HUNK_PACKED`) cleared the bytes it grew into before moving the cache out of them, so a cache block lying there lost its header and the move freed a NULL user. With sound on, the PC game crashed at the first map load (the sound headers sit right above the low hunk), on the base commit too. It now grows, moves the cache, then clears, as Hunk_AllocName does. Sound-off runs are unchanged.

A lesson about id's mixer: the paint buffer size is part of its output. `SND_PaintChannelFrom8` paints each segment from the start of the paint buffer, so when a looped sound restarts inside a chunk its second segment lands at a spot that depends on the chunk boundaries. A 128-pair paint buffer changed the start map's output. The board keeps id's 512.

PC verification (silent mixing only, `-hidden`, no audio device opened; `qog_sndhash` hashes every painted pair and every output sample in time order, `qog_sndrx` hashes what the client received; reference: the single-process `quakeog_pc` with `-sndmono8`, the board's 8-bit mono format, and `-heapsize 7000` so loadas8bit is on as on the board):

| Test | Reference | Split pair, server mixing |
| --- | --- | --- |
| `qogtest_w_snd1.cfg` (e1m1, a fight with rockets, lightning and nails, save, load, map start; 1,744,704 samples) | 4 state hashes, 4 paint and output hashes | identical state hashes (rand calls 539, 20824, 21668, 22669), identical paint and output hashes, the client received exactly the output |
| the same from the two board images (client `-basedir build-w\qb`, server `-basedir build-w\qb_main -flashbase 0x10300000`: names in flash, the converted lightning sounds in place) | | identical |
| the same with Agent V's pipelined client (`QOG_LINK_PIPELINE`, 2,832 updates queued, 0 sound starts queued) | | identical |
| `qogtest_w_demo.cfg` (map start, demo1, demo2, demo3; 2,509,289 samples) | 4 hashes, the same on two runs | identical |

The default builds of this branch, sound off, against an untouched build of main (abd47a2): hidden, `-nosound`, `+_cl_color 0`: `qogtest_hash` 2 state hashes, `qogtest_f_hash` 32 hashes, `qogtest_d_shots` after `qogtest_k_quiet` 113 frames plus the 8 fhash frames, all identical, and the split pair (`-ServerExe`) gives the same 2 hashes and 32 `qogtest_r_fhash` hashes as the single process.

Server hunk with sound, from the board images, the board's server flags and compiled QuakeC (`qogtest_t_mem_*.cfg`, fight, save, load): e1m1 needs 120 KB (116 fails, 116 runs without sound), start runs in 100 KB. Sound adds 5.5 KB of hunk (channels 5,408 B, the name pool, headers) plus up to 8.4 KB of sound headers in the cache, which the hunk gives back under pressure. `QOG_SV_HUNK_KB` stays 156.

Board links (`cmake --preset target -B build-final -DQOG_AOT=ON`, everything else default, `-DQOG_CL_STACK_KB=32` is the new default with sound):

| | Before (main abd47a2) | With sound |
| --- | ---: | ---: |
| quake_display RAM free (less the 2 KB heap reserve) | 344 B | 628 B |
| quake_display stack array | 34 KB | 32 KB |
| quake_main RAM free | 1,616 B | 880 B |
| main SCRATCH_Y | 2 KB interrupt stack | + known_sfx 2 KB |
| main data image (start, e1m1) | | pak 2.06 MB with 124 sounds (1.64 MB), image 0.69 MB, 2.69 MB of 5 MB |
| quake_main app | | 1,309,940 B, 41.6% of the slot |

Display stack: `tools/stack_chain.py --root game_main` gives 44,144 B as an upper bound with every impossible path, 1.4 KB below Agent V's 45,520 B for main (the client's sound code is gone). A sound exchange inside S_ExtraUpdate in the renderer adds about 1.1 KB (Link_ClientWait, two poll_rx frames of 344 B). The board watermark showed 5.4 KB spare at 34 KB before, so about 2.3 KB should remain at 32 KB. The display prints its free stack over USB with `QOG_LINK_DIAG`.

Not done, or to watch on the board:

- Latency: an update is mixed `_snd_mixahead` (0.1 s) ahead of the display's play position. With the pipelined client, updates from the screen update are queued and mixed after the server frame, and their samples reach the display at its next link wait. If the sound breaks up, `_snd_mixahead 0.15` (archived, at most 0.18 with the 2 KB ring) gives more room.
- The 22 kHz sounds play as 11 kHz 8-bit, exactly as id's loader made them with less than 8 MB of memory. The amplifier's level is the `volume` cvar (0.7).
- The display's flash image still holds the sounds, which the client no longer opens. Leaving them out would free about 1.6 MB of the display's flash for more maps (a later mkboard option).
- Display core 1 is not used by sound. Link frames are unchanged: sound travels as stream items (`LK_SND` 23, `LK_SNDPCM` 24, `LK_SNDDONE` 25), no new frame types.
- New test scripts are in the branch's git-ignored `build-w\data\id1` (`qogtest_w_snd1.cfg`, `qogtest_w_demo.cfg`, `qogtest_w_statics.cfg`), not in the shared data folder.

Build and flash (one CPU in BOOTSEL at a time, from the repo root):

```powershell
python tools\mkboard.py --pak data\id1\pak0.pak --pak data\id1\pak1.pak --manifests build-pc-dev\manifests `
    --maps start e1m1 --exe build-pc-dev\Release\quakeog_pc.exe --stub-maps --main -o build-board\qb
python third_party\wiliOGbsp\tools\fw.py flash quake --uf2 build-board\qb_main.uf2
cmake --preset target -DQOG_AOT=ON -DQOG_AOT_PAK=data/id1/pak0.pak -DQOG_SOUND=ON -DQOG_CL_STACK_KB=32
cmake --build build --target quake_main
python third_party\wiliOGbsp\tools\fw.py flash quake --uf2 build\apps\quake\quake_main.uf2
```

The display's data image is unchanged by this work: reflash `build-board\qb.uf2` through the display's BOOTSEL drive only if the maps changed. Main's data image must be rewritten, it now holds the sounds.

## 2026-11-23: Board speed, timing, both chips and both cores at work (Agent V, PC and ARM build, registered data)

Four board-only changes to the split game, all options in apps/quake/CMakeLists.txt and all on by default. Three are exact by construction and one keeps id's order of everything the halves exchange. Nothing ran on hardware: the gains below are reasoning until the coordinating session flashes the board and reads the new timing lines.

| Change | Flag (CMake option) | Exact | What it hides or splits | RAM |
| --- | --- | --- | --- | --- |
| Frame phase times over USB every 2 s on both chips | `QOG_PHASE_TIME` (`QOG_BOARD_TIMING`) | measurement only | | 160 B per half |
| The next frame's input, link exchange and parsing run during the 22.4 ms LCD scanout | `QOG_VID_LOCKWAIT` (`QOG_BOARD_LOCKWAIT`) | yes, same pixels | up to 22.4 ms a frame | 1 B |
| Core 1 of the display chip draws every second span of each lit surface | `QOG_SPANS_CORE1` (`QOG_BOARD_CORE1`) | yes, same pixels | about half of the lit span time | 12 B (job slot in platform/qog_video.c), core 1 chain about 520 B of its 2 KB stack |
| The client draws frame k while the server half runs frame k+1 | `QOG_LINK_PIPELINE` (`QOG_BOARD_PIPELINE`) | state hashes and frames identical on the PC | the server's frame and the link round trip | about 48 B |

RAM (`arm-none-eabi-nm`, `__HeapLimit - __end__` less the 2 KB heap reserve, QOG_AOT): quake_display 552 B free with the four options off, 344 B with them on. quake_main 1,776 B free off, 1,616 B on (the timing). Sizes unchanged: client hunk 68 KB, stack 34 KB, server hunk 167 KB, server stack 24 KB. Deepest client chain (`tools/stack_chain.py --build build --lib apps/quake/libqog_engine_cl.a --root game_main`, an upper bound) 45,480 B off, 45,520 B on. D_DrawSpans8Lit gains a 40 B wrapper frame above the 432 B drawer. Flash +3.6 KB on the display.

How each works:
- Lock wait. id's code calls VID_LockBuffer before it draws into the frame (D_EnableBackBufferAccess at the top of SCR_UpdateScreen, draw.c, menu.c). With the flag, quakedef.h makes it a port function: vid_rp2040.c's VID_Update hands the frame to core 1 and returns, and the next VID_LockBuffer waits for the scanout. The z-buffer temporaries that parsing uses (`QOG_PRECACHE_ZTEMP`, `QOG_LOADGAME_ZTEMP`) already stay clear of the status bar rows. The PC build with the flag hashes the 64,000 scanned bytes at VID_Update and again at the next lock (`qog_lockcheck`, printed at quit): 0 changes in 3,549 scanouts of qogtest_d_shots and 3,512 of qogtest_r_fhash.
- Core 1 spans. D_DrawSurfaces sets a lit surface up on core 0 as before, then core 1 draws spans 2, 4, 6 and core 0 spans 1, 3, 5, and core 0 waits for core 1 before the next surface. A span writes only its own pixels and z, and the drawer reads only the surface's state, which nothing changes until both are done. platform/qog_video.c runs the job in core 1's idle loop between scanouts (spans are drawn only after the lock wait). A future core 1 hook (sound) must return quickly or each surface waits for it. On the PC the half runs on a real second thread (default), or on the game's thread before (`-core1first`) or after (`-core1last`) core 0's half: 113 qogtest_d_shots frames byte identical in each mode, three thread runs in a row.
- Pipeline. A frame of play (signed on, not a demo, key_game, no intermission, pause or loading plaque) leaves SCR_UpdateScreen, S_Update and CL_DecayLights pending. The next frame takes its input, sends its tick and pushes it onto the wire (Link_ClientTick, qog_link_push), runs the pending update while the main chip runs its server frame, then waits for the answer (Link_ClientDone). The update runs with the realtime, host_frametime, host_framecount, cl.viewangles and key_dest of its own frame. V_DriftPitch, the only part of the update that changes what the next input sends, runs at the frame's end (V_CalcRefdef skips it in the update). A console line other than `wait` runs the pending update first (Cbuf_QogIdle), so qog_shot, server commands and level changes keep id's order. The render calls no rand(). Key presses on the board are read before the update, but key_dest is restored for it, so a menu appears in the same frame as before. While the client draws, the server's answer waits in the UART flow control, and a short answer may be resent once or twice by the link's timeout (harmless duplicates).

PC verification (hidden, `-nosound`, own basedir, against a build of bbcb5ff in `build-pc-base`):

| Run | Result |
| --- | --- |
| This commit, all new flags off: quakeog_pc qogtest_hash, qogtest_f_hash, qogtest_k_quiet + qogtest_d_shots | 2 + 32 hashes identical, 8 + 113 frames byte identical |
| Same build, split pair: qogtest_hash, qogtest_r_fhash, qogtest_d_shots | identical to base |
| Split client with `QOG_LINK_PIPELINE`, `QOG_VID_LOCKWAIT`, `QOG_SPANS_CORE1` and `QOG_PHASE_TIME`, server with `QOG_PHASE_TIME` | qogtest_hash 2 hashes (rand calls 539 and 1990), qogtest_r_fhash 32 hashes and 8 frames, qogtest_d_shots 113 frames, all identical. 397 of 400 and 3,392 of about 3,500 frames were pipelined, the rest flushed for console lines |
| quakeog_pc with `QOG_VID_LOCKWAIT` and `QOG_SPANS_CORE1` | qogtest_hash, qogtest_f_hash and qogtest_d_shots identical in all three core 1 modes |

On the PC the server's frame is far below a millisecond, so the pipeline shows only in the link wait: the client waited 8.52 s in lockstep and 3.02 s pipelined over the qogtest_r_fhash run (2.4 to 0.9 ms a frame at the PC link's 300 KB/s).

Expected on the board (estimates, the timing lines will replace them):
- Lock wait: saves the part of the 22.4 ms scanout that the next frame's input, link wait and parsing cover, all of it in lockstep when the server and link take 20 ms or more. With the pipeline on, the link wait mostly moves under the render, and the lock wait then covers only parsing and input (a few ms).
- Pipeline: saves the server's frame plus the link round trip per frame (`[time sv] svframe` and `[time cl] link` show both). Lock wait and pipeline compete for the same slack: together they hide about max(scanout, server frame), not the sum.
- Core 1: agent P estimated the E1M1 lit spans at about 25 ms a frame (48,400 pixels at about 65 cycles plus 6,800 runs of float work). Half on core 1 saves up to about 12 ms. Texture and colormap reads that miss the shared 16 KB XIP cache are served one core at a time by the QSPI, so 6 to 10 ms is the likely range.

Console format (display chip COM8 `[time cl]`, main chip COM7 `[time sv]`, every 2 s, mean ms per frame with one decimal):

```
[time cl] frames <n> fps <x.y> ms frame <ms> input <ms> link <ms> read <ms> screen <ms> view <ms> hud <ms> present <ms> scanwait <ms> sound <ms>
[time cl] views <n> ms setup <ms> world <ms> bmodels <ms> lmap <ms> grad <ms> spans <ms> zspans <ms> sky <ms> turb <ms> models <ms> vmodel <ms> parts <ms> total <ms> edgescan <ms>
[time cl] per view litsurfs <n> lmcells <n> litspans <n> litpx <n> runs <n> zpx <n> skypx <n> turbpx <n>
[time sv] frames <n> fps <x.y> ms wait <ms> svframe <ms> phys <ms> send <ms>
```

frame is a whole Host_Frame. link is the wait for the server half (Link_ClientWait only). screen is SCR_UpdateScreen and includes view (V_RenderView: total, the status bar repaint hud and the crosshair) and scanwait (the lock wait). The rest of screen is the 2D overlay. present is VID_Update. With the pipeline, frame k's screen is counted in frame k+1. The view line is agent P's: world is the BSP walk and edge emission, edgescan the edge sorting and span making, models the alias models, spans the lit spans with their z (QOG_LITSPAN_ZFUSE). svframe is Host_ServerFrame, phys its SV_RunClients and SV_Physics, send SV_SendClientMessages, wait the time main waits for the client's tick.

Build and flash (the coordinating session):

```powershell
cmake --preset target -DQOG_AOT=ON -DQOG_AOT_PAK=<path to pak0.pak> -DQOG_SPLIT_MAIN_EMBED=quake_display
cmake --build build --target quake_main
python third_party\wiliOGbsp\tools\fw.py flash quake --uf2 build\apps\quake\quake_main.uf2
```

For an A/B measurement add `-DQOG_BOARD_PIPELINE=OFF`, `-DQOG_BOARD_LOCKWAIT=OFF` or `-DQOG_BOARD_CORE1=OFF` to the configure line and rebuild. `-DQOG_BOARD_TIMING=OFF` removes the report.

Looked at and left:
- DMA memset and memcpy of frame-sized buffers. The only per-frame ones are the edge cache memset (about 15 KB on E1M1, tens of microseconds) and the status bar tile repaint, which reads a pic from flash that DMA would not read faster. The `hud` phase now shows the repaint's cost.
- Hot loops in RAM. The benchmark found no gain for the span loop, and the display chip has 344 B left.
- Faster scanout without a clock change: chaining the line DMA across two channels would remove the gaps between lines, at most about 1.9 ms of the 22.4 ms (the wire alone is 20.5 ms at 50 MHz).
- The RP2040 interpolator could compute the texel address in the lit span loop (masked shift and add), exact, not tried.
- Unrelated finding: on the PC without `id1/qogbake.bin` in the basedir, quakeog_pc of bbcb5ff crashes in Cache_Free while loading e1m1's sprites.

Clock options (report only, nothing enabled):

| Option | What changes | Gain | Risks |
| --- | --- | --- | --- |
| clk_sys 200 to 250 MHz (`FWOG_SYS_CLK_KHZ` in bsp/common/clocks.h, both chips) | clk_peri follows clk_sys (`PICO_CLOCK_ADJUST_PERI_CLOCK_WITH_SYS_CLOCK`). The link UART divisor becomes 1.25 (IBRD 1, FBRD 16), still exactly 12.5 Mbaud. The LCD SPI becomes 250/4 = 62.5 MHz, the panel's `FWOG_LCD_MAX_HZ`, a scanout of about 18.3 ms instead of 22.4. Flash at CLKDIV 4 becomes 62.5 MHz. Everything derived from clock_get_hz moves with it (backlight PWM, WS2812 PIO, I2C, PDM, main's FPGA clock divider, which rounds up to stay at or under 25 MHz) | about 25% on CPU and flash bound work, 4 ms of scanout | an overclock (200 MHz at 1.15 V is the rated point), and `fwog_vreg_mv_for_khz` deliberately clamps at 1.15 V, so 250 MHz needs a voltage policy change (1.2 to 1.3 V is usual) on a battery board. Both chips must change together. 260 to 266 MHz is worse for the LCD (the SPI drops to clk/6, 44 MHz). The display bootloader brings the clock up first with its own build of the value |
| QSPI flash CLKDIV 4 to 2 (100 MHz at 200 MHz sys, `PICO_FLASH_SPI_CLKDIV` in bsp/boards/freewili_og.h, even values only) | XIP misses (538 ns per random byte today, 8.5 MB/s sequential) roughly halve. Textures, the colormap, the baked BSP and code are read through it. The W25Q128 is rated 133 MHz | likely 10 to 25%, the largest of the options for the renderer, also on main (traces through clip nodes in flash) | the bootloader's XIP setup stays in force for the app (the app's boot2 is skipped), so on the display chip the divider only changes with a rebuilt and reflashed display bootloader. A bootloader whose XIP fails at 100 MHz leaves the display CPU with no way back, since it has no BOOTSEL button. A safer route is for the app to switch the divider at run time from RAM code and check a known flash region, falling back to 4. The RX sample delay may need tuning at 100 MHz |

## 2026-11-23: The client half links on the display chip for E1M1 (Agent S, PC and ARM build, registered data)
## 2026-11-23: The main chip's server data and saves for the split game (Agent T, PC and ARM build, registered data)

The main RP2040 now has its own data image, written by the same `tools/mkboard.py` run as the display's, and the split game saves and loads on main's FAT volume. On the PC the split pair runs from the two images (the server image for the server process, the display image for the client) with the same state hashes, frames and save files as the single-process game on the full paks. `quake_main` links with 1,796 bytes of RAM to spare. Nothing ran on hardware.

Server data image (`mkboard.py --main`, details in docs/game-data.md):

| Maps | Region 0x10300000 to 0x107FFFFF (5 MB) |
| --- | --- |
| `start` to `e1m4` (the display image that fits, 12,471,904 B at 0x10400000, unchanged) | pak 420,880 B at 0x10300000 (`progs.dat`, `gfx/pop.lmp`, 68 model stubs), server image 1,955,424 B at 0x10370000, end 0x1054E000, 2,416,640 B (46.1%) |
| `start` and all of episode 1 | pak 422,432 B, server image 2,948,000 B (81 models), 3,407,872 B (65.0%) |

- `qog_bake -bakestubs -bakeserver` (writer only, engine/model.c) keeps each map's planes, nodes, leafs, clip nodes and hulls, submodels, entities and visibility, and an alias model's or sprite's `model_t`. Textures, texinfo, surfaces, edges, vertexes, mark surfaces and light are left out. The image's layout word is the build's XOR `QOGB_SERVER_LAYOUT`, which only `QOG_SPLIT_SERVER` takes. Each model file in the server's pak is a stub (132 bytes for a map, 16 for a model, `qogbake_mstub_t`). Nothing in the image points into a pak, so the PC bakes it from the display's full pak for the address `-bakebase` gives.
- The server also opens `gfx/pop.lmp`: without it the game is the shareware version, stops with "You must have the registered version to use modified games", and QuakeC would read another `registered` cvar.
- The PC maps its packs at another address with `-flashbase <address>` (port/pc/sys_pc.c), so the server process runs main's layout with `-basedir <out>_main -flashbase 0x10300000`.
- Flash layout check: the BSP puts main's FAT volume at flash offset 8 MB for 8 MB (`fs/fs_geom.h` `FWOG_FS_FLASH_OFFSET`) and links main apps at the start of flash (`fwog_main_app` adds no offset), as `platform/qog_flashmap.h` has it. A UF2 copy writes only its own sectors, so the app (at most 3 MB, checked post-build) and the server data UF2 (whole 4 KB sectors from 0x10300000) leave each other and the FAT volume alone. No change was needed.

Saves (`QOG_SYS_MAIN` in port/rp2040/sys_rp2040.c, `QOG_SPLIT_CLIENT` in engine/menu.c, engine/net_link.c, port/common/qog_link.c):

- Save and load already run on the server with id's stdio (`fopen`, `fprintf`, `fscanf`, `fgetc`). On main, newlib's system calls `_open`, `_close`, `_read`, `_write`, `_lseek` and `_fstat` now reach the BSP's FAT API (`fwog_fs_*`, library `fwog_main_fs`), so the engine's save code is unchanged. `./id1/s0.sav` is `quake/s0.sav` on the volume. One file at a time, as the BSP allows. The volume is mounted at the first open and never formatted by the game: a board without a volume says so on main's console and cannot save (the BSP's `bench` app has `fs format`).
- Flash write hazards: nothing in `quake_main` starts core1, and the BSP's disk driver writes one 4 KB sector per FatFs write with interrupts off and kicks the watchdog after each chunk of at most 8 sectors.
- The client's load and save menus get the list over the link: side frames 0x44 SAVES_ASK `[type][seq]` and 0x45 SAVES `[type][seq][12][12 x (found byte, 39-byte comment)]` (483 bytes) outside the stream (port/common/qog_link.h). The client asks between exchanges and again every 250 ms until the answer with its sequence number arrives (5 s at most). The server reads each slot as `M_ScanSaves` does, from its own files. `qog_saves` prints the list on the split client.
- The single-process game is unchanged: everything is behind `QOG_SPLIT_SERVER`, `QOG_SPLIT_CLIENT`, `QOG_SYS_MAIN`, `QOG_LINK_SCRATCH` or the bake writer's `-bakeserver`.

Server RAM:

- A server half gives a map none of the renderer's per-frame arrays (surface, node and leaf stamps, dlight bits, efrag heads and leaf keys, also for each item box map). The smallest server `-heapsize` for e3m5 with a fight, a save and a load went from 172 KB (Agent R's number, reproduced) to 152 KB.
- Agent R's 172 KB was not enough for every map: e1m3 with a fight, a save and a load needs 185 KB on the untouched build (the load's 8 KB line buffer fails). With the cut e1m3 needs 167 KB with the full paks and 165 KB with the server image, e1m4 157 (155), e1m5 156. `start`, e1m1, e1m2, e1m6, e1m7 and e1m8 run in 152 KB. Measured with new `qogtest_t_mem_<map>.cfg` scripts (copies of `qogtest_r_mem_e3m5.cfg`) on a QOG_AOT PC build with the board's server flags. `QOG_SV_HUNK_KB` is now 167, the worst of `start` and episode 1. Episodes 2 to 4 were not measured beyond e3m5.
- The link's two 2 KB rings move to the RP2040's SCRATCH_X bank on main (`QOG_LINK_SCRATCH`, set only on `qog_engine_sv`): core1 never starts there, so no core1 stack is linked in it.
- `quake_main` (`-DQOG_AOT=ON`, stand-in display image `qog_display` while `quake_display` does not link): .data 8,144 + .bss 249,964 + heap 2,048 B, 1,796 B of RAM left, SCRATCH_X 4,096 B, app 960,828 B (30.5% of the 3 MB slot). The FAT library adds about 8.9 KB of static RAM (FatFs volume and file sector buffers, its long file name buffer). Before the hunk and ring changes it overflowed RAM by 7,420 B.
- Stack: the deepest chain from `server_main` is 18,544 B of the 24 KB array (`tools/stack_chain.py --app quake_main --lib apps/quake/libqog_engine_sv.a --root server_main`). The save path from `Host_Savegame_f` is 5,072 B plus newlib's `fprintf`, which has no `.su` line.

PC verification (hidden, silent, `-nosound`, `+_cl_color 0`, against an untouched build of f6ce8fd, `build-pc-base`, on the full registered paks):

| Test | Base, single process | Split pair from the two images (`build-board\qb`, `build-board\qb_main`) |
| --- | --- | --- |
| `qogtest_hash.cfg` | 2 hashes, rand calls 539 and 1990 | identical, on the interpreted build and on QOG_AOT with the board's flags and a 167 KB server hunk |
| `qogtest_t_fhash.cfg` (`qogtest_r_fhash` on `start` to `e1m4`, save name `t_f1`, fight, save, load) | 20 hashes, 5 frames | 20 hashes identical (also on QOG_AOT with a 167 KB server hunk), 5 frames byte identical, the save file byte identical to base's |
| `qogtest_t_saves.cfg` (save `s0` on e1m1 and `s5` on e1m2 after a fight, `qog_saves`, the load menu) | load menu frame | the list came over the link, the load menu frame byte identical, the saves only in the server's folder |

The single-process build of this commit (`quakeog_pc`) gives the same `qogtest_hash` (2 hashes), `qogtest_f_hash` (32 hashes, 8 frames) and `qogtest_d_shots` after `qogtest_k_quiet` (113 frames) as base, all byte identical.

Flash order (one CPU in BOOTSEL at a time, nothing was flashed here): the display data UF2 through the display's BOOTSEL drive, the server data UF2 through main's, then the app with `fw flash`, which pushes the display image and sends RUN.

- Edits outside new code: engine/model.c and model.h (bake writer and loader), engine/menu.c (`M_ScanSaves` in the split client, the only client-side file touched), engine/net_link.c, port/common/qog_link.c and .h, port/pc/sys_pc.c, port/rp2040/sys_rp2040.c, tools/mkboard.py, and in apps/quake/CMakeLists.txt only `qog_engine_sv`'s definitions and `quake_main`'s hunk and libraries.
- Not done: the board's FAT path was built, not run. The client opens no QuakeC, so `progs.dat` (0.39 MB) could leave the display's pak, which is not changed here. The `qogtest_t_*.cfg` scripts are in the worktree's git-ignored `build-board\full\id1`, not in the shared `data\id1`.

## 2026-11-23: The game split across both RP2040s, server on main, client on the display (Agent R, PC and ARM build, registered data)

The server half (SV_*, QuakeC, physics, edicts) and the client half (CL_*, renderer, status bar, input) are now separate builds joined by a new net driver over the inter-chip link. On the PC the two halves run as two processes over TCP at the UART's speed and give the same state hashes and frames as the single-process game. On the board `quake_main` (server) links with 1,712 bytes of RAM to spare. `quake_display` (client) still overflows RAM by 118,024 bytes with the E1M1 hunk (136,456 with e3m5's). Nothing ran on hardware.

How the split works (engine/net_link.c, flag `QOG_NET_LINK` with `QOG_SPLIT_SERVER` or `QOG_SPLIT_CLIENT`):

- The link driver replaces the loopback driver with loopback semantics: a reliable message makes the sender wait until the receiver has read it (canSend), the receive buffers have the loopback's sizes and are read in place, and an overflow behaves as there.
- Lockstep. The client stops where id's Host_Frame ran the local server (`Link_ClientFrame`), sends what its frame produced and waits. The server half runs its frame and answers. Console lines the client does not know run on the server at once, inside the client's command buffer (`Link_ForwardCommand`, a synchronous exchange), so `map`, `load`, `changelevel`, `qog_statehash` happen at the same point as in one process. `qog_sv <line>` forces a line to the server.
- What id's server did to client state directly comes back as items the client carries out at the same point: `reconnect` (SV_SendReconnect), `connect local` (map, load), `disconnect` (load), Host_ClearMemory's client half, `scr_centertime_off`, Host_Error and Host_EndGame. A client that closes its connection stops the server, as CL_Disconnect did. The server's Con_Printf goes to the client's console. Text the server adds to the command buffer (QuakeC `changelevel`, `localcmd`) joins the client's, which is the game's only command buffer, so scripted waits keep id's order.
- Shared globals cross with every item that follows a change: the client's realtime, host_frametime and key_dest (SV_Physics pauses in menus), the server's host_frametime, `sv.active`, `sv.paused`, `sv.max_edicts`, `svs.maxclients`, the map name and noclip_anglehack. Every cvar set on one half is set on the other. The shared random sequence (qog_rand) stays one sequence: each side sends its call count and the other advances its generator to it, so QuakeC's random() and the particles draw exactly the numbers they drew in one process.
- In the client build `sv` and `svs` are that report (engine/server.h), and `Host_ShutdownServer` only clears `sv.active`, so id's client code (CL_LerpPoint, CL_KeepaliveMessage, menus) is unchanged. The server build has no console, renderer, sound, keys or menus (port/common/qog_split_sv.c keeps V_CalcRoll and a console that prints to the log and to the client).

Transport (port/common/qog_link.c, port/pc/link_pc.c, port/rp2040/link_rp2040.c): a reliable byte stream over the BSP's link frames `[0x7E][len16][payload][crc16]` with the BSP's CRC, Quake types 0x40 DATA, 0x41 ACK, 0x42 HELLO and 0x43 HELLOK (0x01 to 0x1F stay the bootloader's, 0x20 to 0x22 the breakout I/O's, which the display hands to `fwog_ioexp_link_handle`). One frame in flight, resent after a timeout with back off, duplicates acknowledged again, the decoder resynchronizes after a bad CRC, and a full receive ring sends no ack so the sender waits (with the UART's RTS/CTS below it). Board sizes 512-byte frames and 2 KB rings on both chips. On the main chip every wait kicks the watchdog, on the display every wait keeps `fwog_power_poll` running.

PC results (hidden and silent with `-nosound`, against an untouched build of eb5c08d, `build-pc-base`, link rate limited to 300,000 bytes per second):

| Test | Single process (base) | Split pair |
| --- | --- | --- |
| `qogtest_hash.cfg` | 2 hashes | identical, rand calls 539 and 1990 as in base |
| `qogtest_r_fhash.cfg` (copy of `qogtest_q_fhash` with its own save name, 8 maps, fight, save, load) | 32 hashes, 8 frames | all identical |
| `qogtest_k_quiet.cfg` + `qogtest_d_shots.cfg` (demos and `map dm6`) | 113 frames | all byte identical |
| `qogtest_hash.cfg` with `-linkcorrupt 40 -linkdrop 60` on both | | identical, 39 resends, 20 CRC drops |
| `qogtest_r_fhash.cfg` on QOG_AOT with the board's flags and link sizes | | identical to base |

The single-process build of this commit (`quakeog_pc`, all new flags off) gives the same 2 + 32 hashes, 8 + 113 frames as base.

Link traffic (wire bytes, framing and acks included, fhash run): per frame client to server average 61, server to client average 235. In 3,360 frames of play the busiest frame was 164 bytes up and 560 down. The worst burst is a level change or load, 5,241 bytes to the client in one frame (17.5 ms at 300 KB/s, 4.2 ms at the BSP's 12.5 Mbaud) and 1,237 bytes up. The lockstep adds the server's frame to the client's frame time: the halves do not yet run in parallel.

RAM of each half. Hunks are the smallest `-heapsize` of a PC build with QOG_AOT and the board's flags that runs the map, a fight, a save and a load (`qogtest_r_mem_e1m1.cfg`, `qogtest_r_mem_e3m5.cfg`). Single chip for comparison: 194 KB (Agent Q).

| | E1M1 | e3m5 (worst map) | Static RAM and stack on the board |
| --- | ---: | ---: | --- |
| Server (`quakeog_sv`, server zone 6 KB, load buffer 8 KB, `QOG_LOADGAME_STRSIZE`) | 130 KB | 172 KB | sv 11,360, BSP display update and link buffers 12,441, stack 24 KB (deepest chain 18,608 B, `tools/stack_chain.py --root server_main`) |
| Client (`quakeog_cl`) | 97 KB | 115 KB | video 145,920, stack 47 KB, engine statics 57,356, SDK and BSP 23,737, link 6,329 |

Board links (`cmake --preset target -DQOG_AOT=ON`):

- `quake_main` with `QOG_SV_HUNK_KB` 172 and `QOG_SV_STACK_KB` 24: links, .data 7,840 + .bss 250,352 + heap 2,048, 1,712 bytes of RAM left. App 884,092 bytes with a stand-in display image (`-DQOG_SPLIT_MAIN_EMBED=qog_display`, because quake_display does not link yet), 3 MB app slot.
- `quake_display` (client): RAM overflow 136,456 bytes with `QOG_CL_HUNK_KB` 115, 118,024 bytes with 97 (E1M1). Before the split the single chip build overflowed by 223,504. The client keeps the 47 KB stack array: the render chain is 47,584 bytes from R_RenderView (the merge coordinator's measurement) plus about 300 above it, and Agent K's 48 KB exact edge fallback is not reserved (it would add about 41 KB to an overflow that is already 118 KB).

Main flash layout (platform/qog_flashmap.h): app with the embedded display image 0x000000 to 0x2FFFFF (3 MB, checked post-build), server data 0x300000 to 0x7FFFFF (5 MB), BSP FAT volume 0x800000 to 0xFFFFFF (untouched). Server-side lumps of `start` and episode 1 (entities, planes, visibility, nodes, clip nodes, leafs, models) are 2,167,292 bytes in the paks plus progs.dat 413,116, so the server's copy fits 5 MB, while a full copy of the display image (11.9 MB) does not. The server image (a pak of what the server opens and a bake for 0x10300000 without textures, lighting and surfaces, written by the same `tools/mkboard.py` run as the display's) is not written yet.

New engine flags (all off by default): `QOG_NET_LINK`, `QOG_SPLIT_SERVER`, `QOG_SPLIT_CLIENT`, `QOG_LOADGAME_STRSIZE`. Port flags: `QOG_SYS_MAIN`, `QOG_LINK_MAIN`, `QOG_LINK_CHUNK`, `QOG_LINK_TXRING`, `QOG_LINK_RXRING`. CMake: `QOG_SPLIT` (ON), `QOG_CL_HUNK_KB`, `QOG_SV_HUNK_KB`, `QOG_SV_STACK_KB`, `QOG_SPLIT_MAIN_EMBED` in apps/quake, `QOG_SPLIT_SV_EXTRA` and `QOG_SPLIT_CL_EXTRA` in pc.

- Edits outside new files: engine/host.c, host_cmd.c, cmd.c, cvar.c, net_main.c, net_none.c, server.h, sv_main.c, sv_user.c, world.c (each behind the flags), port/common/qog_mem.c (reports leave out the missing half), port/pc/sys_pc.c (server loop), port/rp2040/sys_rp2040.c (`QOG_SYS_MAIN`), tools/pctest.ps1 (`-ServerExe`, `-ServerArgs`), tools/check_app_size.py (`main`), and one new block at the end of pc/CMakeLists.txt and apps/quake/CMakeLists.txt.
- Not in the split builds: the single-process PC tools `qog_tour`, `qog_manifest`, `qog_fly` (stubs in port/pc/split_cl_pc.c), the server's `viewmodel`, `viewframe`, `viewnext`, `viewprev` (they write the client's model list), and the QOG_BAKE_WRITER.
- Known differences from one process, none hit by the tests: the order of client and server prints inside one exchange, a load whose file fails on the server after the client cleared its demo loop, config.cfg written by the client lacks server-only archived cvars, and the save and load menus read save files on the client's side (on the board they live on main).

Left before the split runs on the board: the client half must fit (118 KB over on E1M1, the video's 146 KB is the biggest block), the server data image (mkboard `--main` with a server bake), saves on main's FAT volume, the smoketest of the link rate, and a first run on hardware.

## 2026-11-23: World renderer speed (Agent P, PC and ARM build, registered data)

Four exact speedups of the lit span and z span path (byte-identical pictures, on in all three flag lists), two approximate span drawers (off, in no list, for the user to judge), render phase timing for the board's fly-through, and benchmark lines that time and check the engine's own new span code on the board. Nothing ran on hardware. The board savings below are estimates from the generated Thumb code and the E1M1 fly-through's work counts until the benchmark and the fly-through are flashed.

| Change | Flag | Kind | Estimated board saving (E1M1 fly-through) | RAM |
| --- | --- | --- | --- | --- |
| Lit spans without modulo. Power-of-two textures wrap with masks, other sizes (3% of demo1's lit pixels, 0.1% of E1M1's) step wrapped coordinates with compare and subtract. The texture origin is folded into the light grid pointer, each mip level has its own pixel loop with constant shifts, and each run of up to 8 pixels is drawn in two unrolled passes (light blend, then texel and colormap) so each pass fits the M0+'s low registers | `QOG_LITSPAN_FAST` | exact | inner loop about 150 → about 65 cycles a pixel (old: 73 instructions and two divider calls, new: 37 + 14 instructions, 4 multiplies, 3 + 3 stack reloads), about 4.1 M cycles or 20 ms a frame at 48,400 lit pixels | +40 B static, D_DrawSpans8Lit stack 216 → 296 B, flash +6.5 KB |
| The last run of each span reuses `d_*stepu * (spancount - 1)`, computed once per span length per surface | `QOG_LITSPAN_TAILMUL` | exact | an int to float and three float multiplies per span, about 1.4 ms at 1,620 spans | +96 B stack |
| z span start without double arithmetic. `(int)(zi * 0x8000 * 0x10000)` with zi a double holding a float is exact (both factors are powers of two), so it is the float's mantissa shifted by its exponent, truncated toward zero. abs(zi) >= 1, infinities and NaNs keep id's expression | `QOG_ZSPANS_FAST` | exact | float to double, two double multiplies and double to int per z span (about 200 cycles), about 1.7 ms | 0 |
| Lit spans write their own z (`D_QogZSpan`) from the 1/z they compute at each span start with id's expression, and D_DrawSurfaces skips D_DrawZSpans for them | `QOG_LITSPAN_ZFUSE` | exact | two int to float, two float multiplies and two float adds per lit span, a second walk of the spans and a call per surface, about 2.4 ms | stack +40 B (D_DrawSpans8Lit 392 → 432 B, D_QogZSpan 32 B) |
| Light blended exactly at both ends of each run and stepped linearly between them (one pass, up to 16 pixels) | `QOG_LITSPAN_LINLIGHT` | approximate, off | inner loop 21 instructions a pixel plus two blends a run, about 24 cycles a pixel, about 6 ms | 64 B flash table |
| One float divide per 16 pixels instead of 8, as id's D_DrawSpans16 did on x86 | `QOG_LITSPAN16` | approximate, off | half the per-run float work (3 adds, a divide, 2 multiplies, 2 conversions per run), about 6 to 7 ms | +96 B stack |
| Render phase times and work counts | `QOG_PHASE_TIME` | measurement, off in shipped builds, on in qogfly (`QOGFLY_PHASE`) | costs about 2,000 timer reads and three span-list walks a frame | +96 B static, qogfly only |

Pixel differences of the approximate drawers over the 113 `qogtest_d_shots` frames (3D view rows only, max colour step is the largest change of one RGB channel through the palette):

| Build | Frames differing | Pixels differing | Max colour step | Worst frame |
| --- | ---: | ---: | ---: | --- |
| `QOG_LITSPAN_LINLIGHT` | 113 | 1.27% | 60 | d_spawn_e3m3, 5.2% |
| `QOG_LITSPAN16` | 113 | 6.2% | 184 | d_demo3_06, 21% (texels shifted by one at grazing angles) |
| both | 113 | 9.1% | 192 | d_demo3_15, 23% |

- Work counts (PC, `QOG_PHASE_TIME`): E1M1 fly-through (1,618 frames 0.1 s apart) per frame 71 lit surfaces, 2,769 light cells built, 1,622 lit spans, 48,363 lit pixels (39 on the any-size path), 6,811 runs of up to 8 pixels, 45 sky and 224 turbulent pixels. timedemo demo1 per frame 60 lit surfaces, 3,346 light cells, 1,509 spans, 46,506 lit pixels (1,564 any-size), 6,514 runs. Per lit pixel the float work is about 60 cycles (a run's 3 adds, divide, 2 multiplies and 2 conversions plus each span's start), the light grid build about 0.4 ms a frame.
- PC speed: no measurable change. timedemo demo1 runs at the 60 fps cap with every build, and the `-qogfly` wall time over 8,000 frames varied by 30% between runs of the same build. On x86 the modulos and floats this work removes are cheap. On the PC the phase split of the E1M1 fly-through is 0.24 of 0.43 ms in lit spans.
- Verification, hidden and silent, against an untouched build of 1f2afb6 (`build-pc-base`), each run with `+_cl_color 0` and the frame-compared scripts after `qogtest_k_quiet.cfg`: `qogtest_hash` state hashes identical, 113 `qogtest_d_shots` and 64 `qogtest_e_shots` frames byte-identical (the 8 files `qogtest_e_shots1` to `7` are chained by `qogtest_e_shots.cfg` and are not meant to run alone), `qogtest_n_flyg` 400 view hashes and 4 shots identical. Per-frame hashes: `qogtest_h_demos` 3,610 frames and `qogtest_h_fight1` to `4` 37,159 frames equal except runs of at most 10 frames at startup and at each map load. Frames 700 to 715 of fight 1 were dumped from both builds: the differing ones differ only in one 6-row console text line scrolling up (the load timing), the 3D view is identical.
- On the board, `apps/qogbench` now includes `engine/d_litspan.h` and checks the new drawer against the old code byte for byte at every mip level, on a power-of-two and a 320×200 texture, full and ragged spans (`chk_p2`, `chk_any`), and the z start against id's double expression over 2 span sets (`chk_z`). Each should print 0.
- Engine files changed: `engine/d_scanlit.c`, `engine/d_scan.c` (D_QogZiFixed, D_QogZSpan), `engine/d_edge.c` (phase hooks, the z skip), `engine/r_main.c` (phase hooks only). New: `engine/d_litspan.h`, `engine/qog_phase.h`, `port/common/qog_phase.c`. One new block at the end of each flag list.
- Looked at and left: placing the span code in RAM (the earlier benchmark showed no gain and RAM is short, the new D_DrawSpans8Lit is 9.5 KB of flash code but one mip variant plus the float part, about 2 KB, is hot per surface), R_BuildLightMap (about 0.4 ms a frame on E1M1, fusing its three passes would save about 0.15 ms), exact replacements of the per-run float divide (the ROM routines are already the fast path, any fixed-point form changes s and t).

Board measurement, prepared and not run (the coordinating session flashes):

```powershell
cmake --preset target -DQOGFLY_HUNK_KB=18 -DQOGFLY_STACK_KB=34
cmake --build build --target qogbench_main
python third_party\wiliOGbsp\tools\fw.py flash qogbench --uf2 build\apps\qogbench\qogbench_main.uf2
python third_party\wiliOGbsp\tools\fw.py console
cmake --build build --target qogfly_main
python third_party\wiliOGbsp\tools\fw.py flash qogfly --uf2 build\apps\qogfly\qogfly_main.uf2
```

The benchmark keeps its first line and adds a second every 2 s (each group is refreshed every third round, about 3 s):

```
[bench] fast old_p2=<us> fast_p2=<us> fast_any=<us> chk_p2=<n> chk_any=<n> | approx lin_p2=<us> s16_p2=<us> s16lin_p2=<us> | short old=<us> fast=<us> zid=<us> zfast=<us> chk_z=<n>
```

old_p2 is today's code on a 256×128 texture in flash with the texture origin a whole light cell, fast_p2 the shipped drawer on the same scene, fast_any the shipped drawer on the 320×200 picture, lin, s16 and s16lin the approximate drawers. short repeats old and new on game-like spans (17 to 47 pixels), and zid and zfast are the z spans with id's double start and with `QOG_ZSPANS_FAST`. Every value is microseconds per 320×152 frame. The fly-through prints after its status line every 2 s:

```
[phase] frames <n> ms setup <ms> world <ms> bmodels <ms> scan <ms> lmap <ms> grad <ms> spans <ms> zspans <ms> sky <ms> turb <ms> models <ms> vmodel <ms> parts <ms> total <ms> edgescan <ms>
[phase] per frame: litsurfs <n> lmcells <n> litspans <n> litpx <n> anypx <n> runs <n> zpx <n> skypx <n> turbpx <n>
```

Times are means per frame over the 2 s. scan includes the surface drawing (lmap, grad, spans, zspans, sky, turb), edgescan is scan less those, the edge sorting and span making. With `QOG_LITSPAN_ZFUSE` the lit surfaces' z is inside spans. `-DQOGFLY_PHASE=OFF` removes it. The fly-through's static RAM grows by 132 bytes (36 for the span drawer, 96 for the phase sums), flash by 8.1 KB.

## 2026-11-23: Demo playback on the board (Agent O, PC and ARM build, registered data)

`quake_display` (the split game's client half, `QOG_SPLIT`) now links: a 68 KB hunk, a 34 KB stack and the one-byte depth buffer the user approved, with 1,104 bytes of RAM free beside the 2 KB heap reserve. Before, it overflowed by 118,024 bytes with E1M1's hunk. 68 KB is the smallest PC hunk of the split client with these flags that runs E1M1 with a fight, a save and a load. The other start to e1m4 maps need up to 85 KB and do not fit yet, and Agent K's edge fallback, which the one-byte z-buffer reaches now and then, stops the game on the board (both below). Every other change is exact. Nothing ran on hardware.

Merges first: agent-o (demo player) and agent-p (renderer speed, exact flags) into agent-s. Conflicts only in the flag lists (`apps/quake/CMakeLists.txt`, `pc/CMakeLists.txt`, `tools/ram_meter.py`, every block kept, agent-p's before the split block so both halves get them) and in this log (every entry kept). The split PC client stubs the demo host's PC commands like the other single-process tools (`port/pc/split_cl_pc.c`). Against a build of f6ce8fd (agent-r, scratch copy), the merged single-process game and the merged split pair gave the same `qogtest_hash` (2) and `qogtest_r_fhash` (32) state hashes, the same 8 fhash frames and the same 113 `qogtest_d_shots` frames after `qogtest_k_quiet`. That merged tree is `build-pc-base`.

Board client settings (`apps/quake/CMakeLists.txt`, the client library `qog_engine_cl` and `quake_display` only, never the PC game list). Savings are RAM of the link at the same hunk unless marked hunk.

| Change | Flag | Saved |
| --- | --- | ---: |
| One-byte z-buffer with its near steps, 1,800 edges so the edge table fits it, the world's edge cache in the frame buffer's view rows (approved, picture below) | `QOG_ZBUFFER8`, `QOG_ZBUFFER8_NEAR=7`, `QOG_NUMSTACKEDGES=1800`, `QOG_EDGECACHE_FRAME=152` | 48,640 |
| Client entities in hunk blocks of 16 only for the entity numbers a level uses, 16-bit slot numbers, at most 600, more stops with Host_Error. Extends agent-o's `QOG_CL_ENTSLOTS` | `QOG_CL_ENTSLOTS=600`, `QOG_CL_ENTBLOCK=16` (new) | hunk, E1M1 26.3 KB of id's array to 8.4 KB (e4m3 280 numbers in use) |
| Beam segments from the level's hunk in blocks of 8 when a frame needs them, id's 64 kept (the fight tour fills 64 and drops segments as id does), a scratch entity before signon | `QOG_TEMPENT_BLOCK=8` (new) | 6,520 static, hunk only on levels with beams |
| Stack: the renderer's bmodel warning prints without Con_Printf's 4 KB buffer, CL_NextDemo's command line 32 bytes, 32-byte edge-drawing surfaces (16-bit keys, face index, entity by visedict), player colours through a 256-byte texel map instead of a 16 KB colour map on the stack | `QOG_CON_PRINTTEXT`, `QOG_NEXTDEMO_STR`, `QOG_SURF_T_TINY`, `QOG_TRANSLATE_INLINE` (new) | stack 47 to 34 KB, 13,312 |
| Zone 16 to 8 KB: cvar values stay at their initial strings until a set changes them, command tokens in a 640-byte buffer, light style texts in the zone at their length instead of 64 x 64 bytes | `QOG_CVAR_LITERAL`, `QOG_CMD_ARGV_ARENA=640`, `QOG_LIGHTSTYLE_ZONE` (new), `QOG_ZONE_BOARD_KB=8` | hunk 8 KB, static 3,200 net |
| Link: the client's rings 1 KB (a frame that does not fit is not acknowledged and comes again), both in scratch X below core 1's stack (agent-t's `QOG_LINK_SCRATCH` hunk, the same lines), the BSP frame sender wrapped by one that writes the same bytes without its 4,165-byte buffer | `QOG_LINK_TXRING/RXRING=1024`, `QOG_LINK_SCRATCH`, `QOG_LINK_STREAM_SEND` with `--wrap=fwog_link_uart_send_frame` | 8,261 |
| The console's 2 KB command history in scratch Y below the boot stack | `QOG_KEYLINES_SCRATCH` | 2,048 |
| Lit surface hash 1,024 to 256 entries as the demo player, overflow takes Agent L's exact tree walk (slower in big fights) | `QOG_DLIGHT_SURFS=256` | 4,608 |
| PVS row for `QOG_PVS_LEAFS` leafs, key names const, client precache tables of the server's sizes (208, 176), the unused 16-bit palette table in flash, a 64-byte surface cache header | `QOG_PVS_ROW_SMALL`, `QOG_KEYNAMES_CONST`, `QOG_CL_PRECACHE_SV`, `QOG_VID_NO_8TO16`, `QOG_VID_SURFCACHE_MIN` (new) | 3,088 |
| Agent K's edge fallback stops with an error when the stack cannot hold its table | `QOG_ZEDGE_STACK_GUARD=1024` (new) | 4 bytes |

Link of `quake_display` (`cmake --preset target -DQOG_AOT=ON`, `QOG_CL_HUNK_KB` 68, `QOG_CL_STACK_KB` 34): links, .data 4,096 + .bss 250,176, 1,104 bytes free plus the 2 KB `.heap` reserve. Video 97,280, hunk 69,632, stack 34,816, the rest 59,312 (engine statics, SDK, BSP, libc, heap reserve). `quake_main` still links with 1,712 bytes free, `qogdemo_display` with 1,704 plus its heap reserve, `qogfly_display` with its board values (18 and 34 KB).

Stack (`tools/stack_chain.py --root game_main` on a copy of only the client's objects, Agent Q's cuts plus S_LoadSound, S_FindName, S_PaintChannels and S_Update_ excluded because SNDDMA_Init fails on the board so sound never starts, and R_RenderPoly, which only id's debug polygon modes reach): deepest chain 32,712 bytes, a demo start's loading plaque drawn under Cbuf_Execute, R_EdgeDrawing 29,752 (800 surfaces of 32 bytes and the span reserve), a bmodel clip to Sys_Error. The alias chain is 20.5 KB from R_RenderView_. 2,104 bytes are left for interrupts. Agent K's exact edge fallback (up to 1,804 edges of 24 bytes, 43 KB) is not reserved and on the board now stops with an error.

Peaks on the split PC client with the board client flags (`qog_tour` and `qog_tour_fight` run by the server half, `QOG_CL_PEAKS`, hunk with an 8 KB zone, hunk low at the end of each level, KB):

| Map | Fight tour hunk | Plain tour hunk | Entity numbers in use | Temp entities | Edges | Surfaces |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| start | 65.5 | 72.9 | 142 | 0 | 969 | 464 |
| e1m1 | 65.9 | 64.2 | 90 | 0 | 1,053 | 430 |
| e1m2 | 72.0 | 72.0 | 112 | 0 | 783 | 368 |
| e1m3 | 84.7 | 81.4 | 184 | 0 | 667 | 338 |
| e1m4 | 84.8 | 83.2 | 183 | 0 | 866 | 401 |
| e1m5 to e1m8 | 59.4 to 78.9 | 61.0 to 76.9 | up to 170 | up to 13 | up to 1,081 | up to 511 |
| worst of the game | e2m6 96.5 | | e4m3 254 (280 in one run) | 64, segments dropped as in id | e3m6 1,751 | e3m6 670 |

`-heapsize` that runs Agent R's fight, save and load scripts: E1M1 68 KB (`qogtest_r_mem_e1m1.cfg`, 67 fails), e3m5 79 KB (`qogtest_r_mem_e3m5.cfg`). Before these flags: 97 and 115. The zone peaks at 5.9 KB of 8 (light styles included). 1,800 edges and 800 surfaces cover the game's peaks. The fight tour stops on e4m5 (below), e4m5 to e4m8 come from a plain tour with the 16 KB zone: 94.7, 90.0, 90.4, 86.4 KB, less 8.

Edge fallback with the one-byte z-buffer (`z edges at exit`, PC): fight tour start to e1m8 0, episode 2 group 2, episode 3 group 2. Plain tour start to e1m4 0 and e1m5 to e1m8 0 in a second run, but 84 in a first run of start to e1m8 (the same run saw 1,483 edges on e1m1 against 861 to 1,053 in every other, an unusual view). With the 16-bit z-buffer no run reached it. On the board each one would now stop the game with an error (`QOG_ZEDGE_STACK_GUARD`): the table it moves (up to 43 KB) does not fit any board stack. A swap of the table into the frame buffer's free rows was tried and dropped: in a test build with forced stalls (`QOG_ZEDGE_TEST_SPANS=400`) the live edges never fitted below the scan line.

Verification, hidden and silent, `-nosound`:

- Single-process `quakeog_pc` of this branch against `build-pc-base`: `qogtest_hash` 2 hashes, `qogtest_r_fhash` 32 hashes and 8 frames, `qogtest_d_shots` 113 frames, all identical.
- Split pair of this branch (`-ServerExe`): the same 2 + 32 hashes and 8 + 113 frames.
- Split client built with the board client flags except the depth buffer (`-DQOG_SPLIT_CL_EXTRA`, `build-pc-f`), server with the board's: the same 2 + 32 hashes and 8 + 113 frames.
- With the depth buffer as well (`build-pc-z`): the same 2 + 32 state hashes. Frames: 29 of 113 demo frames identical, 0.048% of view pixels differ, at most 155 pixels (d_demo1_20), status bar rows identical. The 8 fhash map frames 0.039%, at most 34 pixels.

Not done or not fitting:

- Start to e1m4 (agent-t's first data image) needs up to 85 KB of hunk (e1m3, e1m4 fight tour), 17 KB more than the board has. The whole game needs 97 KB (e2m6). Exact options left are small. Bigger ones change behaviour: the console text (4 KB) and command buffer (4 KB) sizes, or the message buffer (7.5 KB) in the z-buffer during signon, which is exact only while in-game messages stay small.
- Sound: no room (1.1 KB free). Agent Q estimated about 25 KB.
- `qog_tour_fight` stops on e4m5 with a QuakeC "Program error" on the server half (target t178), also in the measurement build that had only the slot counter, so e4m5 to e4m8 have plain tour figures only.

New engine flags (off by default): `QOG_CL_ENTBLOCK`, `QOG_TEMPENT_BLOCK`, `QOG_CON_PRINTTEXT`, `QOG_NEXTDEMO_STR`, `QOG_SURF_T_TINY`, `QOG_TRANSLATE_INLINE`, `QOG_CVAR_LITERAL`, `QOG_CMD_ARGV_ARENA`, `QOG_LIGHTSTYLE_ZONE`, `QOG_PVS_ROW_SMALL`, `QOG_KEYNAMES_CONST`, `QOG_CL_PRECACHE_SV`, `QOG_VID_NO_8TO16`, `QOG_KEYLINES_SCRATCH`, `QOG_ZEDGE_STACK_GUARD`, `QOG_CL_PEAKS` (measurement, also the largest edge fallback). Port flags: `QOG_LINK_STREAM_SEND`, `QOG_VID_SURFCACHE_MIN`, `QOG_LINK_SCRATCH` (agent-t's lines), `QOG_SPLIT_TOUR` (PC server half runs `qog_tour`). Edits: engine/cl_main.c, cl_parse.c, cl_tent.c, client.h, cmd.c, console.c, console.h, cvar.c, d_edge.c, d_polyse.c, keys.c, model.c, r_alias.c, r_bsp.c, r_draw.c, r_edge.c, r_main.c, r_shared.h, vid.h, port/common/qog_link.c, qog_mem.c, qog_split_sv.c, port/pc/sys_pc.c, tour_pc.c, vid_pc.c, port/rp2040/link_rp2040.c, sys_rp2040.c, vid_rp2040.c, pc/CMakeLists.txt, apps/quake/CMakeLists.txt. No new qogtest scripts (writing to the shared data folder was refused): measurements use `qog_tour` on the command line and Agent R's `qogtest_r_*` scripts. Lessons L49 and L50.

## 2026-11-23: The game split across both RP2040s, server on main, client on the display (Agent R, PC and ARM build, registered data)

The server half (SV_*, QuakeC, physics, edicts) and the client half (CL_*, renderer, status bar, input) are now separate builds joined by a new net driver over the inter-chip link. On the PC the two halves run as two processes over TCP at the UART's speed and give the same state hashes and frames as the single-process game. On the board `quake_main` (server) links with 1,712 bytes of RAM to spare. `quake_display` (client) still overflows RAM by 118,024 bytes with the E1M1 hunk (136,456 with e3m5's). Nothing ran on hardware.

How the split works (engine/net_link.c, flag `QOG_NET_LINK` with `QOG_SPLIT_SERVER` or `QOG_SPLIT_CLIENT`):

- The link driver replaces the loopback driver with loopback semantics: a reliable message makes the sender wait until the receiver has read it (canSend), the receive buffers have the loopback's sizes and are read in place, and an overflow behaves as there.
- Lockstep. The client stops where id's Host_Frame ran the local server (`Link_ClientFrame`), sends what its frame produced and waits. The server half runs its frame and answers. Console lines the client does not know run on the server at once, inside the client's command buffer (`Link_ForwardCommand`, a synchronous exchange), so `map`, `load`, `changelevel`, `qog_statehash` happen at the same point as in one process. `qog_sv <line>` forces a line to the server.
- What id's server did to client state directly comes back as items the client carries out at the same point: `reconnect` (SV_SendReconnect), `connect local` (map, load), `disconnect` (load), Host_ClearMemory's client half, `scr_centertime_off`, Host_Error and Host_EndGame. A client that closes its connection stops the server, as CL_Disconnect did. The server's Con_Printf goes to the client's console. Text the server adds to the command buffer (QuakeC `changelevel`, `localcmd`) joins the client's, which is the game's only command buffer, so scripted waits keep id's order.
- Shared globals cross with every item that follows a change: the client's realtime, host_frametime and key_dest (SV_Physics pauses in menus), the server's host_frametime, `sv.active`, `sv.paused`, `sv.max_edicts`, `svs.maxclients`, the map name and noclip_anglehack. Every cvar set on one half is set on the other. The shared random sequence (qog_rand) stays one sequence: each side sends its call count and the other advances its generator to it, so QuakeC's random() and the particles draw exactly the numbers they drew in one process.
- In the client build `sv` and `svs` are that report (engine/server.h), and `Host_ShutdownServer` only clears `sv.active`, so id's client code (CL_LerpPoint, CL_KeepaliveMessage, menus) is unchanged. The server build has no console, renderer, sound, keys or menus (port/common/qog_split_sv.c keeps V_CalcRoll and a console that prints to the log and to the client).

Transport (port/common/qog_link.c, port/pc/link_pc.c, port/rp2040/link_rp2040.c): a reliable byte stream over the BSP's link frames `[0x7E][len16][payload][crc16]` with the BSP's CRC, Quake types 0x40 DATA, 0x41 ACK, 0x42 HELLO and 0x43 HELLOK (0x01 to 0x1F stay the bootloader's, 0x20 to 0x22 the breakout I/O's, which the display hands to `fwog_ioexp_link_handle`). One frame in flight, resent after a timeout with back off, duplicates acknowledged again, the decoder resynchronizes after a bad CRC, and a full receive ring sends no ack so the sender waits (with the UART's RTS/CTS below it). Board sizes 512-byte frames and 2 KB rings on both chips. On the main chip every wait kicks the watchdog, on the display every wait keeps `fwog_power_poll` running.

PC results (hidden and silent with `-nosound`, against an untouched build of eb5c08d, `build-pc-base`, link rate limited to 300,000 bytes per second):

| Test | Single process (base) | Split pair |
| --- | --- | --- |
| `qogtest_hash.cfg` | 2 hashes | identical, rand calls 539 and 1990 as in base |
| `qogtest_r_fhash.cfg` (copy of `qogtest_q_fhash` with its own save name, 8 maps, fight, save, load) | 32 hashes, 8 frames | all identical |
| `qogtest_k_quiet.cfg` + `qogtest_d_shots.cfg` (demos and `map dm6`) | 113 frames | all byte identical |
| `qogtest_hash.cfg` with `-linkcorrupt 40 -linkdrop 60` on both | | identical, 39 resends, 20 CRC drops |
| `qogtest_r_fhash.cfg` on QOG_AOT with the board's flags and link sizes | | identical to base |

The single-process build of this commit (`quakeog_pc`, all new flags off) gives the same 2 + 32 hashes, 8 + 113 frames as base.

Link traffic (wire bytes, framing and acks included, fhash run): per frame client to server average 61, server to client average 235. In 3,360 frames of play the busiest frame was 164 bytes up and 560 down. The worst burst is a level change or load, 5,241 bytes to the client in one frame (17.5 ms at 300 KB/s, 4.2 ms at the BSP's 12.5 Mbaud) and 1,237 bytes up. The lockstep adds the server's frame to the client's frame time: the halves do not yet run in parallel.

RAM of each half. Hunks are the smallest `-heapsize` of a PC build with QOG_AOT and the board's flags that runs the map, a fight, a save and a load (`qogtest_r_mem_e1m1.cfg`, `qogtest_r_mem_e3m5.cfg`). Single chip for comparison: 194 KB (Agent Q).

| | E1M1 | e3m5 (worst map) | Static RAM and stack on the board |
| --- | ---: | ---: | --- |
| Server (`quakeog_sv`, server zone 6 KB, load buffer 8 KB, `QOG_LOADGAME_STRSIZE`) | 130 KB | 172 KB | sv 11,360, BSP display update and link buffers 12,441, stack 24 KB (deepest chain 18,608 B, `tools/stack_chain.py --root server_main`) |
| Client (`quakeog_cl`) | 97 KB | 115 KB | video 145,920, stack 47 KB, engine statics 57,356, SDK and BSP 23,737, link 6,329 |

Board links (`cmake --preset target -DQOG_AOT=ON`):

- `quake_main` with `QOG_SV_HUNK_KB` 172 and `QOG_SV_STACK_KB` 24: links, .data 7,840 + .bss 250,352 + heap 2,048, 1,712 bytes of RAM left. App 884,092 bytes with a stand-in display image (`-DQOG_SPLIT_MAIN_EMBED=qog_display`, because quake_display does not link yet), 3 MB app slot.
- `quake_display` (client): RAM overflow 136,456 bytes with `QOG_CL_HUNK_KB` 115, 118,024 bytes with 97 (E1M1). Before the split the single chip build overflowed by 223,504. The client keeps the 47 KB stack array: the render chain is 47,584 bytes from R_RenderView (the merge coordinator's measurement) plus about 300 above it, and Agent K's 48 KB exact edge fallback is not reserved (it would add about 41 KB to an overflow that is already 118 KB).

Main flash layout (platform/qog_flashmap.h): app with the embedded display image 0x000000 to 0x2FFFFF (3 MB, checked post-build), server data 0x300000 to 0x7FFFFF (5 MB), BSP FAT volume 0x800000 to 0xFFFFFF (untouched). Server-side lumps of `start` and episode 1 (entities, planes, visibility, nodes, clip nodes, leafs, models) are 2,167,292 bytes in the paks plus progs.dat 413,116, so the server's copy fits 5 MB, while a full copy of the display image (11.9 MB) does not. The server image (a pak of what the server opens and a bake for 0x10300000 without textures, lighting and surfaces, written by the same `tools/mkboard.py` run as the display's) is not written yet.

New engine flags (all off by default): `QOG_NET_LINK`, `QOG_SPLIT_SERVER`, `QOG_SPLIT_CLIENT`, `QOG_LOADGAME_STRSIZE`. Port flags: `QOG_SYS_MAIN`, `QOG_LINK_MAIN`, `QOG_LINK_CHUNK`, `QOG_LINK_TXRING`, `QOG_LINK_RXRING`. CMake: `QOG_SPLIT` (ON), `QOG_CL_HUNK_KB`, `QOG_SV_HUNK_KB`, `QOG_SV_STACK_KB`, `QOG_SPLIT_MAIN_EMBED` in apps/quake, `QOG_SPLIT_SV_EXTRA` and `QOG_SPLIT_CL_EXTRA` in pc.

- Edits outside new files: engine/host.c, host_cmd.c, cmd.c, cvar.c, net_main.c, net_none.c, server.h, sv_main.c, sv_user.c, world.c (each behind the flags), port/common/qog_mem.c (reports leave out the missing half), port/pc/sys_pc.c (server loop), port/rp2040/sys_rp2040.c (`QOG_SYS_MAIN`), tools/pctest.ps1 (`-ServerExe`, `-ServerArgs`), tools/check_app_size.py (`main`), and one new block at the end of pc/CMakeLists.txt and apps/quake/CMakeLists.txt.
- Not in the split builds: the single-process PC tools `qog_tour`, `qog_manifest`, `qog_fly` (stubs in port/pc/split_cl_pc.c), the server's `viewmodel`, `viewframe`, `viewnext`, `viewprev` (they write the client's model list), and the QOG_BAKE_WRITER.
- Known differences from one process, none hit by the tests: the order of client and server prints inside one exchange, a load whose file fails on the server after the client cleared its demo loop, config.cfg written by the client lacks server-only archived cvars, and the save and load menus read save files on the client's side (on the board they live on main).

Left before the split runs on the board: the client half must fit (118 KB over on E1M1, the video's 146 KB is the biggest block), the server data image (mkboard `--main` with a server bake), saves on main's FAT volume, the smoketest of the link rate, and a first run on hardware.
- Engine files changed: `engine/cl_demo.c`, `engine/cl_main.c`, `engine/cl_parse.c`, `engine/cl_tent.c`, `engine/client.h`, `engine/d_local.h`, `engine/model.c`, `engine/r_main.c`, `engine/view.c`, all behind the flags above. One new block each in `pc/CMakeLists.txt`, `apps/quake/CMakeLists.txt` and `tools/ram_meter.py`, and the `QOG_PC_DEMO_BOARD` and `QOG_PC_DEMO_Z8` test options in `pc/CMakeLists.txt` (off by default).
- The bake layout hash did not change, so the shared `data/id1/qogbake.bin` still loads.

## 2026-11-23: The full game on the board, honest configuration (Agent Q, PC and ARM build, registered data)

`quake_display` now links with everything the game needs counted: QOG_AOT on, sound off, a hunk sized from the measured E1M1 need and the game stack as an array in main RAM. It does not fit. RAM overflow 255,504 bytes before the cuts below, 223,504 bytes after them (216,336 bytes if loading a save is not needed). Exact cuts cannot close the gap, and the picture options do not either. Nothing ran on hardware.

Honest configuration (`apps/quake/CMakeLists.txt`, built with `-DQOG_AOT=ON`):

- Hunk `QOG_HUNK_KB` 194: the smallest `-heapsize` that runs E1M1, a fight, a save and a load on a PC build with QOG_AOT and the board-only flags (new `qogtest_q_mem.cfg`). Play without a load needs 187 KB (`qogtest_q_play.cfg`). Interpreted QuakeC needs 38,624 bytes more for its globals.
- Stack `QOG_STACK_KB` 47, an array in main RAM that `port/rp2040/sys_rp2040.c` moves the stack into, as the fly-through does. The deepest chain is 45,968 bytes (new `tools/stack_chain.py`, `--root game_main`): a demo start draws a loading plaque, R_EdgeDrawing 36,184, a brush clip warning through Con_Printf 4,120, S_LoadSound 1,368 and an error path. The alias chain is 33,176, the server chain 22,888, one more level of compiled QuakeC 120. Cut by hand as impossible at that depth: Con_Printf to SCR_UpdateScreen (the console only, no 3D view, while not signed on), COM_FindFile to COM_CopyFile (no cache dir), S_ExtraUpdate (sound off), the debug-only R_DrawCulledPolys and R_ZDrawSubmodelPolys.
- Not in the 47 KB: Agent K's exact edge fallback (`R_QogEdgesToStack`) can alloca 48,120 bytes under R_EdgeDrawing, a chain of about 88,200 bytes. It never ran in any test (demos, 32-map tour, 75% build below). Sizing the stack for it costs 41 KB more.
- `-Wl,--allow-multiple-definition` as in apps/qogfly. `QOG_MIN_MEMORY=0` replaces id's 5.3 MB hunk floor, which stopped Host_Init on the board.

| Step | Flag | Saved | Overflow (bytes) |
| --- | --- | ---: | ---: |
| Honest configuration, hunk 223 KB (E1M1 play, no load) | | | 255,504 |
| Key down, console and menu key flags as bytes, not 4-byte enums | `QOG_KEYS_BYTES` | 2,304 static | 253,200 |
| No sound channels and paint buffer with `-nosound` (S_StopSound checks) | `QOG_SND_OFF_NOHUNK` | 10,784 hunk | |
| Edict margin 64 past the map's estimate, board only (fight tour peak 35, reuse fallback behind it). Edicts 65,952 to 52,640, edcold 15,232 to 12,160, cl_ents 32,992 to 26,336 | `QOG_EDICT_MARGIN_BOARD=64` | 23,040 hunk | |
| Client precache names at the end of the z-buffer while server info is read (no 3D view before signon) | `QOG_PRECACHE_ZTEMP` | 2,800 peak on E1M1 | 216,336 (hunk 187 KB) |
| Load game line buffer at the end of the z-buffer, the status bar rows left clear | `QOG_LOADGAME_ZTEMP` | 32,784 peak of a load | 223,504 (hunk 194 KB with a load) |

Biggest RAM users after the cuts (map file memory map, 485,640 bytes in the 262,144-byte RAM region):

| User | Bytes |
| --- | ---: |
| Hunk (E1M1: edicts 52,640, cl_ents 26,336, particles 18,448, zone 16,400, edcold 12,160, item box models 8,512, map arrays 7,696, loopback to client 7,552, QuakeC globals 7,280, clients 6,784, packed blocks 6,384, command and message buffers 5,152, console text 4,112, baselines 3,584, strings of a loaded save 6.8 KB) | 198,656 |
| Frame buffer and z-buffer | 145,920 |
| Stack array | 48,128 |
| Engine statics (sv 11,360, cl_temp_entities 6,656, cl_lightstyle 4,352, dl_bits 4,096, cl 2,832, mod_known 2,304, MSG_ReadString 2,048, key_lines 2,048, dl_surf 2,048, menu_cachepics 1,792, ...) | 76,447 |
| SDK, newlib, qog_video, surface cache header, heap reserve, vectors | 16,489 |

Picture options (off, the user's decision), against 223,504:

| Option | Video | Compatible with K's edges in the z-buffer | Overflow |
| --- | ---: | --- | ---: |
| `QOG_RENDER_SCALE=75` (240x114) | -42,560 | Yes. The 54,720-byte z-buffer holds the 52,704-byte edge table and clip buffers, but not the edge cache in front of it, which goes to the hunk (E1M1 hunk low 186.3 to 212.7 KB, +27 KB). Demos and the 32-map tour ran with no edge moves and no stack fallbacks | about 208,600 |
| `QOG_ZBUFFER8` | -48,640 | No. R_NewMap stops: "a z-buffer of 48640 bytes holds no table of 2000 edges". Needs `QOG_NUMSTACKEDGES` at most 1,830 (full-game peak 1,730) and the edge cache on the hunk (+27,024), and then the edge table fills the whole z-buffer, so every frame reaches it and the 48 KB stack fallback becomes likely. Without edges in the z-buffer the stack grows 48,584 | about 202,000 with 1,830 edges, about 223,000 without |
| Both | -69,920 | No for the full game: 27,360 bytes hold 944 edges (E1M1 peak 422, full game 1,730). With edges back on the stack (+48,584) the E1M1 edge cache (27,024) still fits the z-buffer, the e3m5 one (33,846) goes to the hunk | about 202,200 on E1M1 |

Sound (computed from the structures, the mixer was not run): channels 6,672, paint buffer 4,112, id's `known_sfx` 512 x 68 = 34,816, a cache header of about 80 bytes per precached sound (E1M1 about 110 sounds, 8.8 KB), sample data read in place from flash, plus a DMA ring not written yet. About 54 KB, about 25 KB with known_sfx sized to the 176 precache slots. It does not fit while the game does not.

Input: the IN_ path is Sys_SendKeyEvents in `port/rp2040/vid_rp2040.c`, which turns `qog_input` actions into key events bound by id's default.cfg (arrows, CTRL fire or ENTER in menus, SPACE jump, c down, / next weapon, ESCAPE menu). Previous weapon pressed backslash, which id binds to +mlook. It now sends `impulse 12` (fixed in the PC board mode too).

- Verification, hidden and silent with `-nosound` and `+_cl_color 0`, against an untouched build of 1f2afb6 (`build-pc-base`): `qogtest_hash` state hashes identical on the interpreted build, the QOG_AOT build and a QOG_AOT build with the board-only flags. `qogtest_q_fhash.cfg` (a copy of `qogtest_f_hash` with its own save name, 8 maps, fight, save and load) 32 hashes and 8 end frames identical on the same three. `qogtest_d_shots` with `qogtest_k_quiet.cfg` 113 frames byte-identical on the interpreted and the board-flag builds (without it the notify line shows the shot path, which names the build folder). `qogtest_reg` ran its 32 maps with no errors on the interpreted and the board-flag builds. `qogfly_main` links with the values the board run used (`QOGFLY_HUNK_KB` 18, `QOGFLY_STACK_KB` 34).
- What is left is not a matter of exact cuts. The whole engine outside the video needs about 340 KB on E1M1 with 116 KB of RAM left beside the video. The options with real size are per-class edicts with compiled QuakeC (edicts and edcold 64.8 KB now), a sparse client entity table (26 KB), message buffers in the z-buffer between frames (about 17 KB, needs proof they are empty at R_RenderView), the 18 KB particle block (changes the picture), or the two-chip split in docs/fit-plan.md.
- Edits outside Agent Q's files: `engine/host.c`, `engine/snd_dma.c`, `engine/host_cmd.c`, `engine/cl_parse.c`, `engine/sv_main.c`, `engine/keys.c` (each behind its flag), `port/pc/vid_pc.c` (board mode input), one new block each in `pc/CMakeLists.txt`, `apps/quake/CMakeLists.txt` and `tools/ram_meter.py`, and the hunk and stack sizes of `quake_display`.
## 2026-11-23: Demo playback on the board (Agent O, PC and ARM build, registered data)

A client-only host plays the recorded demos (demo1 on e1m3, demo2 on e1m4, demo3 on e1m6) with id's client, renderer and status bar, without server, QuakeC, console or sound: monsters, weapons, shooting, particles, explosions, the status bar and the bonus flashes. On the PC every host frame equals the normal game's playdemo frame. The board app pair `apps/qogdemo` links with 73,728 bytes of hunk, a 34,816-byte stack and 3,788 bytes free. It uses the one-byte z-buffer, which changes 0.073% of the view's pixels. Nothing ran on hardware.

| Part | Where |
| --- | --- |
| Host: Host_Init's order without server, QuakeC, network, console, keys, menus and sound, plus `sv_gravity` (the client's particles fall by it). Each frame is the client half of `_Host_Frame` (rand, frame time, command buffer, `CL_ReadFromServer`, screen, `CL_DecayLights`), and the screen is `SCR_UpdateScreen`'s game view path (V_RenderView, centre print, status bar, intermission overlays, V_UpdatePalette, VID_Update). Nothing is drawn while id's screen would show the forced console or the loading plaque. The demo loop is id's (`CL_NextDemo`, `Host_EndGame` longjmps out of the frame) | `port/common/qog_demo.c` |
| PC: `-qogdemo [-demos ...] [-demodt 0.05] [-demoseed 0] [-demohash] [-demodump a b] [-demoshots n] [-demohunk] [-demomanifest file]` runs the host and prints the peaks per demo. In the game `qog_demoloop <seed> <demos>` starts the same loop between two frames, `qog_quitafter <frames>` lets a script end so the command buffer stays free for the text a demo stuffs, `qog_srand` restarts the random sequence | `port/pc/demo_pc.c`, one call each in `port/pc/sys_pc.c` and `port/pc/vid_pc.c` |
| Board: plays demo1, demo2, demo3 in a loop, gray skips to the next demo, and prints a status line every 2 s. `display/demo_wrap.c` and `display/demo_sv.c` define what the client and renderer refer to in the host, server, console, keys, menus, network and sound objects, so none of them is linked (the server state alone is 11,360 bytes) | `apps/qogdemo` |

Engine flags (each off by default, only `QOG_DEMO_FLASH` and `QOG_ITEMBOX_NOLEAFS` are in the PC and board game lists):

| Flag | What | Saved |
| --- | --- | ---: |
| `QOG_DEMO_FLASH` | A demo in a mapped pack is read in place and each message parsed where it lies (net_message points into the image, given back when playback stops). id's three freads are kept, including a field past the end staying as it was and an empty message stopping playback. No FILE, no message buffer | newlib FILE use, 7.5 KB loop buffer on the demo player |
| `QOG_ITEMBOX_NOLEAFS` | Item box maps (`maps/b_*.bsp`) get no efrag heads or leaf keys. Only `cl.worldmodel`'s are ever used | 1,088 B of hunk per item box, 9.8 KB on e1m3 |
| `QOG_CL_ENTSLOTS=n` | Client entities in n slots, given to entity numbers as they are first used (slot 0 is the world), with id's colormap rule. Loops over all entities still go in number order. Exact | e1m3 62,432 → 20,608 B of hunk with 192 slots |
| `QOG_MAX_TEMP_ENTITIES=n` | Fewer beam segments (id 64) | 4,992 B static with 16 |
| `QOG_EDGECACHE_FRAME=rows` | The world's edge cache in the frame buffer's first rows when the z-buffer has no room for it. Nothing reads them before the world's spans draw over them, and the previous scanout has ended. R_EdgeDrawing stops with an error if the 3D view does not cover them | 25 to 30 KB of hunk with the one-byte z-buffer |
| `QOG_ZBUFFER8_NEAR=shift` | With Agent H's `QOG_ZBUFFER8`: 1/z below 1024 as before (shift 3), nearer in steps of 1 << shift above 128, so the weapon keeps its depth order (with one shift everything nearer than 16 units ties and the weapon shows its back faces) | 0 (picture) |

Board settings (`apps/qogdemo/CMakeLists.txt`, its own engine library `qog_engine_demo` as `apps/qogfly`): 480 edge-drawing surfaces (demo peak 412), 1,800 stack edges so the edge table fits the one-byte z-buffer (peak 995), command buffer 256 bytes, 8 cached pics, 256-entry lit surface hash, `QOG_CL_ENTSLOTS=192`, `QOG_MAX_TEMP_ENTITIES=16`, `QOG_ZBUFFER8`, `QOG_ZBUFFER8_NEAR=7`, `QOG_EDGECACHE_FRAME=152`, zone 4 KB, hunk 72 KB, stack 34 KB.

Peaks per demo (PC, board data layout): entity numbers up to 299 / 299 / 260, of which 178 / 169 / 150 are ever used, visible entities 47 / 31 / 55, particles 512 (the pool is full in each), static entities 44 / 40 / 16, models 157 / 139 / 160, dlights 3 / 3 / 7, surfaces 383 / 379 / 412, edges 852 / 789 / 995, temp entities and beams 0, zone 3,184 B. Hunk with the board settings: 25,840 B after the engine start, 63,952 / 64,160 / 59,056 B in the demos (the PC figure with one pak, as on the board).

RAM on the board (`qogdemo_display`, region 262,144 bytes, linked):

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

Verification, hidden and silent:

- Host against game, full paks: `qogtest_o_ref1.cfg` to `_ref3.cfg` (`qog_demoloop 0 demoN`, `con_notifytime -1`, `crosshair 0`, `qog_quitafter`) against `-qogdemo -demos demoN -demohash`: 1,486, 1,399 and 1,647 host frames, each run found as one contiguous run in the game's frame hashes (pixels and palette). `qogtest_o_loop.cfg` (the three demo loop) against `-qogdemo`: 4,532 host frames, the three demos contiguous, the game has one more frame between demos (the loading plaque, which the host does not draw).
- Board data layout (`build-board\qogdemo`) against the game on the full paks: the same three results with the normal build and with a PC build of the board settings that keep the picture (`-DQOG_PC_DEMO_BOARD=ON`: entity slots, temp entities, lit surface hash, cached pics, command buffer, edge cache flag).
- One-byte z-buffer (`-DQOG_PC_DEMO_Z8=ON` as well) against that build, every 50th frame of the loop: 91 frames, 13 identical, 0.073% of view pixels differ on average, at most 126 pixels (far models and particle edges), status bar rows identical. With `QOG_ZBUFFER8` alone it was 0.81%, at most 1,144 pixels (the weapon). No edge moves or stack fallbacks of the edges in the z-buffer in any run.
- Normal game against a build of 1f2afb6 (`build-pc-base`): `qogtest_hash.cfg` state hashes identical, 113 frames of `qogtest_d_shots.cfg` byte-identical (run after `qogtest_k_quiet.cfg`, as two base runs differ in the notify rows otherwise).
- `apps/qogfly` still links.

Lessons from the PC comparison: the host's own `rand()` call is MSVC's or newlib's, because only engine files are built with `-Drand=qog_rand` (it now calls `qog_rand`), `sv_gravity` is a server cvar the client's particles read, and a script's waits hold back the "bf" a demo stuffs (docs/lessons.md L46 to L48).

Build and flash:

```powershell
python tools\mkboard.py --pak ..\quake-2040\data\id1\pak0.pak --pak ..\quake-2040\data\id1\pak1.pak `
    --manifests build-pc-dev\manifests-demo --maps start --demos `
    --exe build-pc-dev\Release\quakeog_pc.exe --stub-maps -o build-board\qogdemo
cmake --preset target -DQOGDEMO_HUNK_KB=72 -DQOGDEMO_STACK_KB=34
cmake --build build --target qogdemo_main
```

The manifests: `start.txt` from the game (`+exec qogtest_o_manifest.cfg`), `demos.txt` from the host (`-qogdemo -zone 4 -demomanifest build-pc-dev/manifests-demo/demos.txt`, full paks). The data image is `build-board\qogdemo.uf2` (10,479,264 bytes of data: pak 5,117,392 with map stubs at 0x10400000 holding the three demos, qogbake.bin 5,301,920 at 0x108F0000, 68 models, 83.7% of the region). The app is `build\apps\qogdemo\qogdemo_main.uf2` (`python third_party/wiliOGbsp/tools/fw.py flash qogdemo --uf2 build/apps/qogdemo/qogdemo_main.uf2`). The USB console prints `[qogdemo] engine started, hunk <used>/<size>, free <bytes>` once (possibly before USB is up), id's messages as they come (`Playing demo from demo1.dem.`, the level name, `You got the shells`, ...), and every 2 s `[qogdemo] <demo> t <cl.time>  fps <f>  ms frame <ms> wait <ms>  free <bytes>  stack <bytes>  hunk <used>/<size>`: frames per second over the last 2 s, the mean time per drawn frame in qog_demo_frame and of it the wait for the previous scanout, the heap left, the deepest stack use so far and the hunk in use. id clamps a frame to 0.1 s, so below 10 fps a demo plays slower than recorded.

- Engine files changed: `engine/cl_demo.c`, `engine/cl_main.c`, `engine/cl_parse.c`, `engine/cl_tent.c`, `engine/client.h`, `engine/d_local.h`, `engine/model.c`, `engine/r_main.c`, `engine/view.c`, all behind the flags above. One new block each in `pc/CMakeLists.txt`, `apps/quake/CMakeLists.txt` and `tools/ram_meter.py`, and the `QOG_PC_DEMO_BOARD` and `QOG_PC_DEMO_Z8` test options in `pc/CMakeLists.txt` (off by default).
- The bake layout hash did not change, so the shared `data/id1/qogbake.bin` still loads.
## 2026-11-23: World renderer speed (Agent P, PC and ARM build, registered data)

Four exact speedups of the lit span and z span path (byte-identical pictures, on in all three flag lists), two approximate span drawers (off, in no list, for the user to judge), render phase timing for the board's fly-through, and benchmark lines that time and check the engine's own new span code on the board. Nothing ran on hardware. The board savings below are estimates from the generated Thumb code and the E1M1 fly-through's work counts until the benchmark and the fly-through are flashed.

| Change | Flag | Kind | Estimated board saving (E1M1 fly-through) | RAM |
| --- | --- | --- | --- | --- |
| Lit spans without modulo. Power-of-two textures wrap with masks, other sizes (3% of demo1's lit pixels, 0.1% of E1M1's) step wrapped coordinates with compare and subtract. The texture origin is folded into the light grid pointer, each mip level has its own pixel loop with constant shifts, and each run of up to 8 pixels is drawn in two unrolled passes (light blend, then texel and colormap) so each pass fits the M0+'s low registers | `QOG_LITSPAN_FAST` | exact | inner loop about 150 → about 65 cycles a pixel (old: 73 instructions and two divider calls, new: 37 + 14 instructions, 4 multiplies, 3 + 3 stack reloads), about 4.1 M cycles or 20 ms a frame at 48,400 lit pixels | +40 B static, D_DrawSpans8Lit stack 216 → 296 B, flash +6.5 KB |
| The last run of each span reuses `d_*stepu * (spancount - 1)`, computed once per span length per surface | `QOG_LITSPAN_TAILMUL` | exact | an int to float and three float multiplies per span, about 1.4 ms at 1,620 spans | +96 B stack |
| z span start without double arithmetic. `(int)(zi * 0x8000 * 0x10000)` with zi a double holding a float is exact (both factors are powers of two), so it is the float's mantissa shifted by its exponent, truncated toward zero. abs(zi) >= 1, infinities and NaNs keep id's expression | `QOG_ZSPANS_FAST` | exact | float to double, two double multiplies and double to int per z span (about 200 cycles), about 1.7 ms | 0 |
| Lit spans write their own z (`D_QogZSpan`) from the 1/z they compute at each span start with id's expression, and D_DrawSurfaces skips D_DrawZSpans for them | `QOG_LITSPAN_ZFUSE` | exact | two int to float, two float multiplies and two float adds per lit span, a second walk of the spans and a call per surface, about 2.4 ms | stack +40 B (D_DrawSpans8Lit 392 → 432 B, D_QogZSpan 32 B) |
| Light blended exactly at both ends of each run and stepped linearly between them (one pass, up to 16 pixels) | `QOG_LITSPAN_LINLIGHT` | approximate, off | inner loop 21 instructions a pixel plus two blends a run, about 24 cycles a pixel, about 6 ms | 64 B flash table |
| One float divide per 16 pixels instead of 8, as id's D_DrawSpans16 did on x86 | `QOG_LITSPAN16` | approximate, off | half the per-run float work (3 adds, a divide, 2 multiplies, 2 conversions per run), about 6 to 7 ms | +96 B stack |
| Render phase times and work counts | `QOG_PHASE_TIME` | measurement, off in shipped builds, on in qogfly (`QOGFLY_PHASE`) | costs about 2,000 timer reads and three span-list walks a frame | +96 B static, qogfly only |

Pixel differences of the approximate drawers over the 113 `qogtest_d_shots` frames (3D view rows only, max colour step is the largest change of one RGB channel through the palette):

| Build | Frames differing | Pixels differing | Max colour step | Worst frame |
| --- | ---: | ---: | ---: | --- |
| `QOG_LITSPAN_LINLIGHT` | 113 | 1.27% | 60 | d_spawn_e3m3, 5.2% |
| `QOG_LITSPAN16` | 113 | 6.2% | 184 | d_demo3_06, 21% (texels shifted by one at grazing angles) |
| both | 113 | 9.1% | 192 | d_demo3_15, 23% |

- Work counts (PC, `QOG_PHASE_TIME`): E1M1 fly-through (1,618 frames 0.1 s apart) per frame 71 lit surfaces, 2,769 light cells built, 1,622 lit spans, 48,363 lit pixels (39 on the any-size path), 6,811 runs of up to 8 pixels, 45 sky and 224 turbulent pixels. timedemo demo1 per frame 60 lit surfaces, 3,346 light cells, 1,509 spans, 46,506 lit pixels (1,564 any-size), 6,514 runs. Per lit pixel the float work is about 60 cycles (a run's 3 adds, divide, 2 multiplies and 2 conversions plus each span's start), the light grid build about 0.4 ms a frame.
- PC speed: no measurable change. timedemo demo1 runs at the 60 fps cap with every build, and the `-qogfly` wall time over 8,000 frames varied by 30% between runs of the same build. On x86 the modulos and floats this work removes are cheap. On the PC the phase split of the E1M1 fly-through is 0.24 of 0.43 ms in lit spans.
- Verification, hidden and silent, against an untouched build of 1f2afb6 (`build-pc-base`), each run with `+_cl_color 0` and the frame-compared scripts after `qogtest_k_quiet.cfg`: `qogtest_hash` state hashes identical, 113 `qogtest_d_shots` and 64 `qogtest_e_shots` frames byte-identical (the 8 files `qogtest_e_shots1` to `7` are chained by `qogtest_e_shots.cfg` and are not meant to run alone), `qogtest_n_flyg` 400 view hashes and 4 shots identical. Per-frame hashes: `qogtest_h_demos` 3,610 frames and `qogtest_h_fight1` to `4` 37,159 frames equal except runs of at most 10 frames at startup and at each map load. Frames 700 to 715 of fight 1 were dumped from both builds: the differing ones differ only in one 6-row console text line scrolling up (the load timing), the 3D view is identical.
- On the board, `apps/qogbench` now includes `engine/d_litspan.h` and checks the new drawer against the old code byte for byte at every mip level, on a power-of-two and a 320×200 texture, full and ragged spans (`chk_p2`, `chk_any`), and the z start against id's double expression over 2 span sets (`chk_z`). Each should print 0.
- Engine files changed: `engine/d_scanlit.c`, `engine/d_scan.c` (D_QogZiFixed, D_QogZSpan), `engine/d_edge.c` (phase hooks, the z skip), `engine/r_main.c` (phase hooks only). New: `engine/d_litspan.h`, `engine/qog_phase.h`, `port/common/qog_phase.c`. One new block at the end of each flag list.
- Looked at and left: placing the span code in RAM (the earlier benchmark showed no gain and RAM is short, the new D_DrawSpans8Lit is 9.5 KB of flash code but one mip variant plus the float part, about 2 KB, is hot per surface), R_BuildLightMap (about 0.4 ms a frame on E1M1, fusing its three passes would save about 0.15 ms), exact replacements of the per-run float divide (the ROM routines are already the fast path, any fixed-point form changes s and t).

Board measurement, prepared and not run (the coordinating session flashes):

```powershell
cmake --preset target -DQOGFLY_HUNK_KB=18 -DQOGFLY_STACK_KB=34
cmake --build build --target qogbench_main
python third_party\wiliOGbsp\tools\fw.py flash qogbench --uf2 build\apps\qogbench\qogbench_main.uf2
python third_party\wiliOGbsp\tools\fw.py console
cmake --build build --target qogfly_main
python third_party\wiliOGbsp\tools\fw.py flash qogfly --uf2 build\apps\qogfly\qogfly_main.uf2
```

The benchmark keeps its first line and adds a second every 2 s (each group is refreshed every third round, about 3 s):

```
[bench] fast old_p2=<us> fast_p2=<us> fast_any=<us> chk_p2=<n> chk_any=<n> | approx lin_p2=<us> s16_p2=<us> s16lin_p2=<us> | short old=<us> fast=<us> zid=<us> zfast=<us> chk_z=<n>
```

old_p2 is today's code on a 256×128 texture in flash with the texture origin a whole light cell, fast_p2 the shipped drawer on the same scene, fast_any the shipped drawer on the 320×200 picture, lin, s16 and s16lin the approximate drawers. short repeats old and new on game-like spans (17 to 47 pixels), and zid and zfast are the z spans with id's double start and with `QOG_ZSPANS_FAST`. Every value is microseconds per 320×152 frame. The fly-through prints after its status line every 2 s:

```
[phase] frames <n> ms setup <ms> world <ms> bmodels <ms> scan <ms> lmap <ms> grad <ms> spans <ms> zspans <ms> sky <ms> turb <ms> models <ms> vmodel <ms> parts <ms> total <ms> edgescan <ms>
[phase] per frame: litsurfs <n> lmcells <n> litspans <n> litpx <n> anypx <n> runs <n> zpx <n> skypx <n> turbpx <n>
```

Times are means per frame over the 2 s. scan includes the surface drawing (lmap, grad, spans, zspans, sky, turb), edgescan is scan less those, the edge sorting and span making. With `QOG_LITSPAN_ZFUSE` the lit surfaces' z is inside spans. `-DQOGFLY_PHASE=OFF` removes it. The fly-through's static RAM grows by 132 bytes (36 for the span drawer, 96 for the phase sums), flash by 8.1 KB.

## 2026-11-23: E1M1 fly-through runs on the board (hardware)

- The renderer-only fly-through (apps/qogfly) draws the real E1M1 from the flash data image on the display CPU, the camera following a 79-point route. Measured over 75 s: 5.7 to 9.3 fps (average 7.0), 85 to 152 ms render per frame, plus a 22.5 ms wait for the single-buffered scanout. 13 KB of an 18 KB hunk in use, 3.3 KB of RAM free. The user's photo shows E1M1's textures, lighting and palette correctly.
- Board engine built with 4-byte enums (`-fno-short-enums`, lessons L46). Without it the board refused the baked image.
- `QOG_SND_ONINIT` and `QOG_LOOP_ONINIT`: sound channels, the paint buffer and the loopback receive buffers come from the hunk when those systems start, so hosts that never start them pay nothing. Static RAM 97.0 to 80.3 KB, hunk +20 KB in the game (the same bytes, moved).
- `QOG_PACKDIR_CHUNKED`: the pack directory is read 2 KB at a time instead of through a 21 KB temporary block. Same CRC, checks and hunk order.
- `QOG_PARTICLES_PRESET`: a host may keep its own particle array, so the fly-through takes no 18 KB particle block.
- apps/qogfly builds its own engine library with 480 edge-drawing surfaces (E1M1 tour peak 443). The game keeps 800. Hunk 18 KB, stack 34 KB.
- Data image: start plus E1M1 with map stubs, 7.15 MB (tools/mkboard.py, built with the same build so the image layout matches).

## 2026-11-23: Renderer tables in the z-buffer (Agent K, PC and ARM build, registered data)

R_EdgeDrawing's stack frame on the ARM build 84,768 → 36,184 bytes (−48,584). The world's edge cache leaves the hunk: E1M1 hunk low 364.0 → 337.3 KB (−27,312 bytes), about 33,870 bytes on the largest map (e3m5, 16,923 edges). Engine static RAM +40 bytes (board overflow 59,148 → 59,188 bytes). The picture is unchanged. Nothing ran on hardware.

| Change | Flag | Saved (measured) |
| --- | --- | ---: |
| The edge table and its four sentinels live at the end of the z-buffer during R_EdgeDrawing, the brush clip buffers at its start. Spans go in the free end of the surface table plus `QOG_SPAN_MIN` (512) spans of reserve | `QOG_EDGES_IN_ZBUFFER` | 48,584 B stack (ARM) |
| The world's 16-bit edge cache lives at the start of the z-buffer while R_RenderWorld runs, filled with "never" every frame. Brush entity faces stop storing in it (nothing reads those entries). A map whose cache does not fit in front of the edge table gets it on the hunk in R_NewMap | `QOG_EDGECACHE_ZBUFFER` | 27,312 B hunk on E1M1 (PC) |

- How the edges share the z-buffer: a world span writes z only in its own row, and spans are made top to bottom. R_ScanEdges moves the used edges (linked by 16-bit offsets, so one memmove and a new base) to the end of the z-buffer. Spans of rows above the edges are flushed whenever the span buffer fills, as id does, and once more before the first row that reaches the edges. When the buffer fills while spans wait, the edges no longer needed are dropped and the rest moved down (`R_QogMoveEdges`). If the waiting spans still overlap them, the live edges move to a table on the stack just large enough (`R_QogEdgesToStack`, at most the old 48,120 bytes), and the scan goes on with id's flushing. Each pixel is drawn by one span whichever flush draws it.
- Fallbacks over the full game with the shipped room (the demos, the tour of e1m1 e3m6 e4m7, the fight tour of every map, the shots): the stack fallback never ran, the edge move ran 39 times (all in the tour), at most 1,289 spans waited where the room was at least 1,178. With the span room cut to 330 spans (a test build) the demos alone needed 4,928 moves and 832 stack fallbacks, and every frame was still the same.
- Why the surfaces and spans stay out of the z-buffer: the last flush writes the z rows under the edge table while it still reads every waiting span and its surface, so something has to hold them outside the z-buffer. The stack fallback exists for the same reason: the waiting spans have no upper bound, so a frame can always be built that needs the old table. On the board the fallback path needs up to 48,120 bytes more stack than the normal one.
- Edge cache: entries from earlier frames never match in id's code (an edge below edge_p owned by the same map edge was emitted this frame, which rewrote the entry), so a fresh cache each frame gives the same answers. The largest full-game map (e3m5) needs 33,846 bytes, which fits in front of the 48,096-byte edge table with 15,338 to spare.
- Z-buffer use in one frame, byte offsets in the 97,280 bytes: R_RenderWorld, edge cache at 0 (up to 33,846) and edge table at 49,184 to the end. R_DrawBEntitiesOnList, brush clip buffers at 0 (4,608) and the edge table. R_ScanEdges, z rows from the top down to the used edge table packed at the end, which shrinks as edges die. After the last flush, all z. Between frames, the first 15,360 bytes hold the status bar rows (QOG_HUD_IN_ZBUFFER) and the rest is free.
- Temp region for later changes (not used yet, the message buffers are Agent M's): `r_ztemp_base` and `r_ztemp_bytes` (r_shared.h) describe the whole z-buffer. Free for anything short-lived from the end of R_RenderView to the start of the next R_RenderView, bytes 15,360 to 97,280 (81,920 bytes) while the status bar and the scanout use the first 15,360, and the whole buffer between R_RenderView's start and R_EdgeDrawing. Not across R_RenderView, and not while the board scans out the status bar rows.
- Looked at and left: the PVS row (`decompressed` in Mod_DecompressVis, 1 KB) is shared with the server's fatpvs and checkpvs and lives in model.c, and the node and leaf visframe stamps must persist between frames (R_MarkLeaves only runs when the view leaf changes). Both belong to Agents L and M.
- Verification, hidden and silent, against a build of c97750d: state hashes in `qogtest_hash`, `qogtest_f_hash` and `qogtest_e_shots`, 195 shots (`qogtest_d_shots`, `qogtest_e_shots`, `qogtest_f_hash`, `qogtest_demo`, `qogtest_sky`) byte-identical, and 44,529 frame hashes (`qogtest_h_demos`, `qogtest_h_tour`, `qogtest_h_fight1` to `4`) equal except console frames at startup and at map loads, which show the build time or the "server frames so far" timing (dumped and checked). Each flag was checked on its own commit, the edge table also with the span room cut to 330 and 700 spans, the edge cache also forced onto the hunk. The new `qogtest_k_quiet.cfg` sets `con_notifytime -1` for these runs: without it the notify line follows real time and two runs of the untouched build differ on a few frames.
- Edits outside Agent K's files: `engine/model.c` (the edge cache allocation only, behind `QOG_EDGECACHE_ZBUFFER`), and one new block each in `pc/CMakeLists.txt`, `apps/quake/CMakeLists.txt` and `tools/ram_meter.py`.
- The bake layout hash did not change (no baked structure changed), so the shared `data/id1/qogbake.bin` still loads.

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

## 2026-11-23: E1M1 fly-through, the first real-scene board app (Agent N, PC and ARM build, registered data)

A renderer-only host draws the real E1M1 from the baked image with a camera moving through the level, on the PC and as the board app pair `apps/qogfly`. No game logic, monsters or sound. On the PC its frames are byte-identical to the normal game's renderer at the same camera positions. On the board the display half does not link yet: RAM overflows by 134,644 bytes, as expected before agents K (edge tables out of the stack) and L (per-level map arrays) are merged. Nothing ran on hardware.

| Part | Where |
| --- | --- |
| Host: memory, packs, wad, palette and colormap in place, VID_Init, Draw_Init, R_Init, then `maps/e1m1.bsp` through Mod_ForName as `cl.worldmodel`, R_NewMap, every light style `m`, a 320×152 view with fov 90 as SCR_CalcRefdef sets it, R_RenderView per frame, text in the status bar rows | `port/common/qog_fly.c` |
| Path: info_player_start, then item, weapon and monster origins from the entity lump (eye in solid skipped, closer than 96 units skipped, as qog_tour), moved away from walls within 32 units. Spots that see each other (a line 24 units wide at eye height) form a graph, and the route visits every spot by shortest sight-line paths, back to the start. Where no sight line exists the camera cuts. Closed Catmull-Rom spline at 160 units per second, the view looks along the path's direction averaged over 1.2 s, pitch half the climb, at most 20° | `port/common/qog_fly.c` |
| PC: `-qogfly` runs the host standalone, `qog_fly` draws the same frames on the world the game loaded, `qog_fly_ref` compares the game's own view of each route point with the host's | `port/pc/fly_pc.c`, one call each in `port/pc/sys_pc.c` and `port/pc/vid_pc.c` |
| Board: the display app moves its stack into a main RAM array (R_EdgeDrawing alone needs 84,768 bytes), prints a status line every 2 s, and replaces at link time the engine calls that would link the console, menus, sound and server (`--wrap`) | `apps/qogfly`, `port/rp2040/sys_rp2040.c` (`QOG_SYS_NO_MAIN`, `qog_rp2040_hunk`) |

E1M1: 60 spots, a route of 79 points with 7 cuts, a 161.8 s loop. 7 of the 1,618 frames of one loop (0.1 s apart) have the eye in solid, where the spline swings into a wall for a moment. Before the cuts it was 273 of 2,400.

Verification (hidden and silent):

- `-qogfly` against `qog_fly` in the full game after `map e1m1` (new `qogtest_n_flyg.cfg`): 400 frames 0.4 s apart, all view hashes identical, with the full paks and with the board layout.
- `qog_fly_ref` (new `qogtest_n_ref.cfg`): the player stands at each of the 79 route points (noclip, god, notarget, not solid so it picks nothing up, not on ground), looking the way the path does there as the protocol rounds the angles, with `r_drawentities 0`, `r_drawviewmodel 0`, `crosshair 0` and light styles `m`. After the game has drawn the spot through V_RenderView, the host draws it at the same cl.time. 79 of 79 views identical, with the full paks and with the board layout. Two earlier differences were in the test, not the host: the console still scrolling up on the first spot, and a quad damage picked up on the way, whose light brightened the game's view.
- Board layout (`build-board\qogfly`, below) against the full paks: 2,400 standalone frames identical.
- Normal game unchanged against a build of c97750d: `qogtest_hash.cfg` state hashes identical, `qogtest_d_shots.cfg` 113 frames byte-identical.

Hunk: the host's whole hunk is 98,112 bytes on the PC (zone 3 KB, of which 1.3 KB used, 92,128 bytes of E1M1 per-level arrays, the route 1.4 KB, the rest command and cvar blocks). The route work space (6 KB) is on the stack, which is idle at that depth then. R_Init's 18 KB of particles are given back. `-heapsize 96` runs, 95 stops with Hunk_Alloc failed. The board uses `QOGFLY_HUNK_KB` 97.

RAM on the board (`qogfly_display`, region 262,144 bytes):

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

What is missing: about 84 KB of stack (agent K's edge tables in the z-buffer) and about 51 KB more, which agent L's per-level map arrays (92 KB of the hunk) can give. With the stack at about 10 KB the hunk may be at most about 45 KB.

Build and flash, once it fits:

```powershell
python tools\mkboard.py --pak data\id1\pak0.pak --pak data\id1\pak1.pak `
    --manifests build-pc-dev\manifests --maps start e1m1 `
    --exe build-pc-dev\Release\quakeog_pc.exe --stub-maps -o build-board\qogfly
cmake --preset target
cmake --build build --target qogfly_main
```

The data image is `build-board\qogfly.uf2` (7,498,464 bytes of data: pak 4,515,568 with map stubs at 0x10400000, qogbake.bin 2,976,480 at 0x10850000, 48 models). The app is `build\apps\qogfly\qogfly_main.uf2` (fw flash qogfly). The USB console prints `[qogfly] e1m1 loaded, 79 route points, 161.8 s loop, hunk <used>/<size>` once (possibly before USB is up) and then every 2 s `[qogfly] fps <f>  ms render <r> present <p>  pt <i>/79  free <bytes>  hunk <used>/<size>`: frames per second over the last 2 s, mean ms per frame in R_RenderView plus the text and in VID_Update (the wait for the scanout), the route point last passed, the RAM between the end of the heap and the heap limit, and the hunk in use. The status bar rows show the fps and render time. Nothing has measured it yet.

- Link setup of the board app, not engine changes: `-Wl,--allow-multiple-definition` (qog_engine links pico_stdlib PUBLIC, so the SDK's sources are in the library and in the app, and `d_local.h` defines five globals in every file that includes it), and `--wrap` for Con_Printf, Con_SafePrintf, Con_DPrintf, Host_Error, S_ExtraUpdate, qog_mem_register and qog_aim_register. The wraps cut 24,688 bytes of statics (the sound mixer, console, keys, menus, statehash). `sv` stays, for R_SetupFrame. `quake_display` has the same duplicate definitions and does not link either way.
- Engine files changed: none.

## 2026-11-23: Server, client and system memory (Agent M, PC and ARM build, registered data)

Hunk low (PC, `qog_mem`) on E1M1 364.0 → 325.9 KB, on e4m7 (largest) 472.5 → 426.5 KB, on e3m4 460.3 → 401.0 KB. The two board-only sizes take another 12 KB off every map (E1M1 about 313.9 KB on the board). Engine static RAM on the ARM meter 93.4 → 89.2 KB, board link overflow 59,148 → 55,512 bytes. Nothing ran on hardware.

| Change | Flag | Builds | Saved |
| --- | --- | --- | --- |
| Entity lump strings, console command entries and loaded light styles share hunk blocks (`Hunk_AllocPacked`, zone.c). id gave each its own block, a 16-byte header plus 16-byte rounding | `QOG_HUNK_PACKED` | PC and board | E1M1 15,376 B (22,560 → 7,184), e4m7 about 22 KB (its total saving less the fixed ones) |
| The 16 KB player colour table is built on the stack for the one draw that needs it (`R_AliasDrawTranslated`). The entity's colormap points at its scoreboard entry | `QOG_TRANSLATE_ONDRAW` | PC and board | 16,384 B on every map |
| Scripts inserted into the command buffer with memmove, not copied through the zone | `QOG_CBUF_INPLACE` | PC and board | zone peak no longer includes up to 8 KB of commands (measured peak now 7,920 B) |
| net_message read in place in the loopback receive buffers. id's move of the rest of the buffer waits for the next read or send on that buffer. Demos are read into the idle client buffer | `QOG_LOOP_INPLACE` | PC and board | 6,528 B on every map |
| Palette read in place from the mapped pack, like the colormap | `QOG_PALETTE_FLASH` | PC and board | 800 B |
| keyshift and key_repeats as bytes (repeat count stops at 255), one slot in the network driver, LAN driver and server list tables, `cls.spawnparms` 1,024 bytes | `QOG_STATIC_TRIM` | PC and board | 4.2 KB static |
| All edicts in use: take the free edict freed longest ago before id's Sys_Error | `QOG_EDICT_REUSE` | PC and board | 0 (safety) |
| Command buffer 4 KB (measured peak 2,158 B) | `QOG_CMDBUF_SIZE=4096` | **board only** | 4 KB |
| Zone 16 KB | `QOG_ZONE_BOARD_KB=16` | **board only** | 8 KB |

- Board-only flags are set in `apps/quake/CMakeLists.txt` and `tools/ram_meter.py` only. The PC keeps 8 KB and 24 KB, which the long test scripts need. A 12 KB zone failed at startup from fragmentation (8,744 B used, 3,512 B free in pieces of at most 152 B), 16 KB ran every test. A PC build with both board values gave the base state hashes (`qogtest_hash`, the 8-map fight with save and load, save and load) and the base status bar frames.
- `QOG_TRANSLATE_ONDRAW`: before the first colour update the table is all zero, as id's untouched hunk was. The new 16 KB frame sits under `R_DrawEntitiesOnList`, beside R_EdgeDrawing (84.8 KB) and not under it: the alias chain is about 32 KB on the ARM build. It costs a 16 KB copy from the colormap per drawn player-coloured model, which only chase view (or a copy of the player's body) draws. The scores entry also explains why the earlier idea of pointing at the colormap is not exact: colours 0 0 still remap ranges 16 to 31 and 96 to 111, only colours 1 6 give the identity.
- `QOG_EDICT_REUSE` was checked with a 167-slot test build on e1m1 under super nailgun fire: the fallback ran 97 times and the run finished. A full array of live edicts still stops with id's error. `QOG_EDICT_MARGIN` stays 128: the fallback only helps when free slots exist, and a long fight can fill the array with live gibs and backpacks.
- Verification, hidden and silent, against a build of c97750d (`+_cl_color 0` on every run): identical state hashes in `qogtest_hash`, the 8-map fight with save and load and `qogtest_m_save` (copies with their own save names), also with `-DQOG_AOT=ON` against the interpreted base. Byte-identical shots: 113 `qogtest_d_shots`, 8 fight end frames, 10 new chase view frames (`qogtest_mm_chase.cfg`, four colour pairs, two maps), 12 menu pics apart from the blinking cursor cell. Per-frame hashes of `qogtest_h_hud.cfg` (997 frames) and the chase script match one of two base runs on every frame except the startup console (build time) and frames that show a notify line or a map load, which also differ between two base runs. `qogtest_reg.cfg` logs equal apart from memory lines and the render peaks, which also differ between two base runs (the turning in that script follows real time).
- Shared config: `data/id1/config.cfg` is saved by every run. A chase test that changed the player colours left `_cl_color` non-zero there, which changes the player's edict (team) and therefore every state hash. All my runs now pass `+_cl_color 0`. Other agents comparing hashes across time should do the same or check the saved colour.
- Looked at and left: `leafnums` (recomputing PVS leafs per frame would cost a BSP walk per entity on the M0+), `sv.signon` (6.5 KB static, worst map still needs 5 KB, and a late makestatic would overflow an exactly sized copy), `client_t.message` (datagrams are appended to the client buffer before the reliable message, so the two cannot share it), `MSG_ReadString`'s 2 KB buffer, `cl_lightstyle`, the particle count.
- Edit outside Agent M's files: `engine/r_alias.c` (`R_AliasDrawTranslated` and the branch to it in `R_AliasDrawModel`).

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
