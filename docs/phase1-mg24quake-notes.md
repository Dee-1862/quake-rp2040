# Phase 1 notes: MG24Quake reference port

Source studied: `third_party/MG24Quake` (GPL-3.0), Release 2.0 (retail PAK support).
All paths below are relative to `third_party/MG24Quake/`. `QuakeMG24/` is abbreviated `Q/`,
so `Q/Quake/r_main.c:1084` means `QuakeMG24/Quake/r_main.c` line 1084.

Ground truth for sizes: besides reading the source, I parsed the symbol table of the shipped
ELF `Q/TheGamepad (Arduino Nano Matter)/QuakeMG24.axf` with a small Python ELF reader (no
toolchain is installed here). Section and symbol sizes quoted as "ELF" come from that binary, so
they are what the compiler actually laid out, with every macro resolved.

Target recap (MG24): Cortex-M33 with single-precision FPU, overclocked to about 136.5 MHz
(`OC_PLLN 4095 / OC_PLLD 1169`, `CPU_FREQUENCY = 39 MHz*4096/1170`, `Q/src/main.h:29-38`),
256 KB main SRAM plus 20 KB "SEQRAM" (radio sequencer RAM, reused as general RAM), 1536 KB
internal flash, two external SPI NOR flashes interleaved.

---

## 1. Memory layout

### 1.1 Linker script and RAM split

- Linker script: `Q/linkerfile.ld` (the copy in `Q/autogen/linkerfile.ld` is the SDK default).
  - `FLASH  0x08000000, LENGTH 0x9C000` (624 KB for code; the rest of the 1536 KB internal flash
    is used as a runtime cache, see section 3) — `Q/linkerfile.ld:30-36`.
  - `RAM    0x20000000, LENGTH 0x40000` (256 KB) — `Q/linkerfile.ld:34`.
  - `SEQRAM 0xB0000000, LENGTH 0x5000` (20 KB) — `Q/linkerfile.ld:35`, output section
    `.aux_ram_bss` at `Q/linkerfile.ld:237`. Variables go there through the
    `AUX_SECTION` attribute (`Q/Quake/quakedef.h:98-101`).
  - Order in RAM: `.stack` (at 0x20000000, so an overflow faults instead of corrupting data),
    then `.noinit`, `.data`, `.bss`, then `.heap` (size 0) — `Q/linkerfile.ld:138-231`.
- Stack size: `SL_STACK_SIZE 36264` — `Q/config/sl_memory_config.h:12`. Heap: `SL_HEAP_SIZE 0`.
  `malloc/calloc/realloc/free` are wrapped into the zone (`-Wl,--wrap=malloc ...` in
  `Q/TheGamepad (Arduino Nano Matter)/makefile:55`, `Q/src/dynMemWrapper.c:30,46`,
  `Q/Quake/z_zone.h:187-191`).

ELF section sizes:

| Section | Address | Bytes |
|---|---|---|
| `.stack` | 0x20000000 | 36,264 |
| `.data` | 0x20008da8 | 296 |
| `.bss` | 0x20008ed0 | 225,544 |
| **Main RAM used** | | **262,104 of 262,144 (40 bytes free)** |
| `.aux_ram_bss` (SEQRAM) | 0xB0000000 | 20,456 of 20,480 |
| **Total RAM** | | **282,560 B = 275.9 KiB** |
| `.text` + `.drawsurf` + tables (flash) | 0x08000000 | 617,684 + 9,656 + 20 |

### 1.2 Every large buffer (ELF sizes, with the declaration and the macro arithmetic)

Main RAM `.bss`:

| Symbol | Bytes | Declaration | Size derivation |
|---|---|---|---|
| `d_zbuffer` | 97,280 | `Q/Quake/d_vars.c:47` | `short[_3D_VIEWPORT_SIZE]`, `_3D_VIEWPORT_SIZE = 320*152` (`Q/Quake/quakedef.h:154`) → 48,640 × 2 |
| `displayData` | 49,680 | `Q/src/graphics.c:87`, type at `Q/src/graphics.h:37-52` | 8bpp frame buffer `[1][320*152]` = 48,640 + DMA line buffers `uint16_t[2][256]` = 1,024 + 16 bytes of state |
| `staticZone` | 48,412 | `Q/Quake/z_zone.c:146` | `MAX_STATIC_ZONE 48412` (`Q/Quake/quakedef.h:162`; 39,292 for shareware-only builds, `:168`) |
| `textureCacheBuffer` | 20,480 | `Q/Quake/d_surf.c:354` | `MAX_TEXTURE_SIZE (16384+4096)` (`Q/Quake/quakedef.h:163`) |
| `global` (`_g`) | 4,828 | `Q/Quake/d_polyse.c:55`, type in `Q/Quake/global_data.h` | most of the former renderer/client globals, including `blocklights[18*18]` (`global_data.h:347`) |
| `tmpLightMap` | 1,296 | `Q/Quake/d_surf.c:33` | `18*18*4` |
| `g_palette` | 512 | `Q/Quake/vid_sdl.c:438` | `uint16_t[256]` RGB565 palette |
| `checkpvs` | 384 | `Q/Quake/pr_cmds.c:736` | `MAX_MAP_LEAFS/8` |
| `__sf` (newlib stdio) | 312 | libc | |
| everything else | about 2,360 | | many small objects |

SEQRAM `.aux_ram_bss`:

| Symbol | Bytes | Declaration | Derivation |
|---|---|---|---|
| `particles` | 8,192 | `Q/Quake/r_part.c:46` | `MAX_PARTICLES 512` (`Q/Quake/r_local.h:34`) × 16 B |
| `cl_entities` | 3,456 | `Q/Quake/cl_main.c:76` | `(MAX_EDICTS 408 + 0 + MAX_TEMP_ENTITIES 24)` × 8 B (`entity_t` packed to 8 bytes, `Q/Quake/render.h:44-77`) |
| `audioBuffer` | 2,048 | `Q/src/audio.h:62` | `int8_t[AUDIO_BUFFER_LENGTH*2]`, `AUDIO_BUFFER_LENGTH 1024` (`audio.h:39`) |
| `channels` | 1,760 | `Q/Quake/snd_dma.c:52` | `MAX_CHANNELS 88` (`Q/Quake/sound.h:152`) × 20 B |
| `surfIdx` | 1,200 | `Q/Quake/d_edge.c:210` | `uint16_t[NUMSTACKSURFACES 600]` (surface sort list) |
| `con_text` | 913 | `Q/Quake/console.c:52` | `CON_TEXTSIZE (38*24+1)` (`console.c:43`; was 16384) |
| `sv` | 836 | `Q/Quake/sv_main.c:34` | `server_t` with its message buffers removed |
| `scr_centerstring` | 428 | `Q/Quake/screen.c:87` | (was 1024) |
| `progs` | 328 | | |
| `interleavedSpiData` | 288 | `Q/src/interleavedSpiFlash.c:42` | SPI receive buffer `rxBuffer[256+8]` (`interleavedSpiFlash.h:79`) |
| `keybindings` | 256 | `Q/Quake/keys.c:80` | one byte (command index) per key instead of `char*` |
| `gammatable` | 256 | | |

The pattern: id's hunk, cache and surface cache are gone entirely. What is left is one zone plus
the z-buffer, which is reused for nearly everything temporary.

### 1.3 Stack contents (36,264 B)

The edge, surface and span tables live on the stack (as in id's original), but much smaller:

- `surf_t lsurfs[NUMSTACKSURFACES]` in `R_EdgeDrawing` — `Q/Quake/r_main.c:1084`.
  `NUMSTACKSURFACES 600` (`Q/Quake/r_shared.h:103`); `surf_t` is 28 B (the active variant is
  at `Q/Quake/r_shared.h:228-254`) → **16,800 B**.
- `byte basespans[MAXSPANS*sizeof(espan_t)]` + `byte baseSpansV[MAXSPANS+1]` in `R_ScanEdges`
  (`Q/Quake/r_edge.c:1956,1963`). `MAXSPANS 3650` (`Q/Quake/r_shared.h:111`, the
  `EDGE_POINTERS` branch). `espan_t` is a 4-byte bitfield (`nextSpanIdx:13, u:9, count:9`,
  `Q/Quake/r_shared.h:122-141`); span v is stored separately as a byte → 14,600 + 3,651 = **18,251 B**.
- `R_ScanEdges` is called from `R_EdgeDrawing`, so the two arrays are live together:
  16,800 + 18,251 = 35,051 B. That leaves about 1.2 KB for the rest of that call chain.
- Other users take turns with the stack: the sound paint buffer
  `portable_samplepair_t paintbuffer[1024]` = 8 KB is a local (`Q/Quake/snd_mix.c:176`, comment:
  "snd update is done when small amount in stack is used"). Level load uses temporary arrays such as
  `MAX_TEMP_LOAD_NODES 256` × 28 B (`Q/Quake/model.c:1432`).
- A stack canary is filled at boot (`Q/src/main.c:397-400`).

### 1.4 Zone memory (48,412 B), replacing the hunk and cache

- A Doom-style zone allocator (`Q/Quake/z_zone.c`) with 16-bit block pointers when the zone is under
  128/256 KB (`z_zone.c:165-261`). Tags `PU_STATIC`, `PU_LEVEL`, `PU_WRAP`, and purgeable tags.
- It holds:
  - All edicts, allocated per class at its exact size. Each QuakeC class has its own C struct
    (`allocEdByType`, `Q/Quake/field_getters_setters.c:25-...`, called from `ED_Alloc`,
    `Q/Quake/pr_edict.c:219-290`). When the zone is full, the oldest gibs or backpacks are
    recycled (`pr_edict.c:232-262`).
  - Client per-entity data `client_entity_data[MAX_EDICTS]` (`Q/Quake/cl_parse.c:801`),
    lightstyles (`cl_demo.c:411`), `sv_areanodes` (`world.c:289`), texture headers
    (`model.c:733-734`), brush-model headers (`model.c:1984`), sprites (`model.c:2271`),
    sky table (`r_sky.c:185`), cvar strings, aliases, and temporary entities.
  - Optionally the 16 KB colormap, copied in at level load only when more than
    `MIN_ZONE_FREE_TO_CACHE_COLORMAP (16384+3900)` bytes are free (`Q/Quake/cl_parse.c:915-917`,
    `Q/Quake/quakedef.h:46`). Otherwise the colormap is read from internal flash.
- A savegame is a raw dump of the zone (`savegame_t.memzone[MAX_STATIC_ZONE]`,
  `Q/Quake/quakedef.h:736`; `SAVE_GAME_SIZE 65536`) written to external flash. This only works
  because the zone sits at a fixed address and holds 16-bit relative pointers.

### 1.5 Buffer sharing (the key RAM tricks)

1. **The status bar lives in the z-buffer.** Only the 3D area (320×152) has a real 8bpp frame
   buffer. Screen rows 152-199 (status bar, 320×48 = 15,360 B) are kept in the **last 15,360 bytes
   of `d_zbuffer`**: `#define aux_buffer ((byte*)(&d_zbuffer[320*DRAW_BUFFER_HEIGHT]) - 320*(SCREEN_HEIGHT-DRAW_BUFFER_HEIGHT))`
   at `Q/Quake/quakedef.h:73-74`. The display DMA reads those rows from there (`getLine()`,
   `Q/src/graphics.c:289-298`), and `Draw_*` switches buffers at y == 152 (`Q/Quake/draw.c:271,382,...`).
   The z-buffer is therefore 97,280 B, while the 3D view only uses 97,280 − 15,360 B of it for z.
2. **Edge tables live in the z-buffer** (`EDGES_IN_Z_BUFFER 1`, `Q/Quake/r_shared.h:45`).
   `R_EdgeDrawing` (`Q/Quake/r_main.c:1054-1082`) carves these regions, working downward from just
   above `aux_buffer`:
   - `edge_t ledges[NUMSTACKEDGES+5]`: `NUMSTACKEDGES 1600` (`r_shared.h:90`), `edge_t` = 24 B
     (`r_shared.h:459-468`) → 38,520 B
   - `newedges[152]` + `removeedges[152]` pointers → 1,216 B
   - `edgeOwners[1600]` uint16 → 3,200 B
   - `leafKey[MAX_MAP_LEAFS 3072]` short → 6,144 B
   To make this safe, `R_ScanEdges` forces a `D_DrawSurfaces()` flush at scanline
   `MAX_Y_FOR_RESTORE_EDGES` (`Q/Quake/r_edge.c:1938, 2082-2092`):
   (97,280 − 15,360 − 1,216 − 38,520)/640 − 1 = **row 64**. So z writes only reach rows 0..64
   while edges are live, and spans for the rows below are flushed only after all edges have been
   consumed. If the span buffer overflows after row 64, the frame bails out ("BLEAH", `r_edge.c:2086-2091`).
3. **The PVS / visleaves / visnodes bit arrays live in the z-buffer** (`VISLEAVES_IN_Z_BUFFER`,
   `Q/Quake/r_main.c:481-487`) at offset 0. They are dead before any z write happens.
4. **Cached edge offsets** (one `uint16` per world `medge`, `MAX_MAP_EDGES 17024` for retail,
   `Q/Quake/bspfile.h:49`) use the first 10,240 entries of `textureCacheBuffer` and the rest
   of the **z-buffer** at offset 1,664 ((17,024 − 10,240) × 2 = 13,568 B) —
   `getCachedEdgeOffsetBuffer()` at `Q/Quake/r_draw.c:146-170`. They are used during
   `R_RenderWorld`, before `textureCacheBuffer` is used for texels.
5. **The z-buffer as the "temp buffer"** (`Q/Quake/tempBuffer.c`): `allocFromTempBuffer()` hands
   out z-buffer memory after a `TEMP_MODEL_REGION` reserved for `mod_known[]` during load
   (`Q/Quake/model.c:2378-2387`). Users: the client/server message buffers
   (`MAX_MSGLEN 6500`, `sv_main.c:338`), `sv.datagram` and `sv.reliable_datagram` (1,024 each,
   `sv_main.c:1594,1605`), the signon buffer (8,192, `sv_main.c:1626`, `server.h:35`),
   `net_message` (`net_main.c:886`), `cls.message` (1,024) and `cls.signonreply` (8,192)
   (`cl_main.c:956-959`), and `fatpvs`/`decompressed` (`sv_main.c:75-77`). (inferred) This only
   works because single-player messages are produced and consumed within one host frame, before
   rendering overwrites the z-buffer.
6. **The warp effect uses the z-buffer** as the copy of the frame (`D_WarpScreen`,
   `Q/Quake/d_scan.c:108-130`). It also copies `intsintable` into the z-buffer tail.
7. `mod_known[]` is built in the z-buffer at level load and then frozen into internal flash
   (`finalizeModKnown`, `Q/Quake/model.c:2367-2372`).

Edge-phase occupancy of the 97,280 B z-buffer: 1,664 + 13,568 (front) + 49,080 (edges and tables)
+ 15,360 (status bar) = **79,672 B, leaving about 17.6 KB slack**.

### 1.6 Network/message buffers, sound and entities (summary)

- Network: no real networking (`net_none.c`, `net_loop.c`, `MAX_CLIENTS 1`, `DIRECT_SINGLE_PLAYER 1`,
  `quakedef.h:103,127`). Every sizebuf comes from the z-buffer temp buffer (item 5 above), so it
  costs 0 B of dedicated RAM.
- Sound: 11,025 Hz, 8-bit stereo output (`Q/src/audio.h:37`). `audioBuffer` 2 KB plus `channels` 1.76 KB
  in SEQRAM, the paint buffer (8 KB) on the stack, and sample data streamed from external flash per mix
  call (`Q/Quake/snd_mix.c:374-399`). `MAX_CONCURRENT_CHANNELS 8` (`quakedef.h:102`).
- Entities: server edicts are variable-size and live in the zone. Client `cl_entities` is
  8 B/entity in SEQRAM, with coordinates stored as shorts in 1/8-unit steps (`ENTITY_HAS_SHORT_COORDS`,
  `toFloatCoord`, `Q/Quake/quakedef.h:199-214`).

### 1.7 Total

282,560 B = **275.9 KiB** (262,104 main + 20,456 SEQRAM). On the RP2040 that is about **45 KB over
a 232 KB (237,568 B) budget**, and above the chip's whole 264 KB.

---

## 2. Resolution and display path

- Fixed at **320×200**: `SCREEN_WIDTH 320 / SCREEN_HEIGHT 200` (`Q/Quake/quakedef.h:71-72`).
  The 3D view is **320×152** (`DRAW_BUFFER_HEIGHT 152`, `quakedef.h:160`; `MAXHEIGHT 152`,
  `r_shared.h:43`) and the status bar is always 48 rows. The readme confirms that the screen size
  is fixed and the status bar can't be reduced.
- Panel: ST7789 240×320 SPI LCD (`Q/src/display.c:5,31`) driven by USART0 in synchronous mode.
  The 200 lines are centred on the 240-line panel (`CENTER_SCREEN`, `display.c:151-153`).
- Pixel format: the engine renders **8-bit palettized**. The display needs RGB565, so an interrupt-driven
  LDMA engine converts 256 pixels at a time through the 256-entry `uint16_t` palette into one of
  two 512-byte line buffers (`CONVERT_4_PIX` asm, `Q/src/graphics.c:59-71`,
  `fillNextDmaLineBuffer` at `graphics.c:300-334`, `startDisplayRefresh` at `graphics.c:234-287`).
  (inferred from the comment at `graphics.c:275`) The SPI clock is 40 MHz, so 256 px × 16 bit
  takes about 102 µs.
- **Single buffer, racing the DMA**: there is only one frame buffer (`displayFrameBuffer[1]`).
  Drawing code that may overtake the DMA calls `waitForDisplayDMA(y)` (`graphics.h:62-68`, used in
  `Q/Quake/draw.c:209,377,431,...`), and the final span flush waits while a full-screen update is running
  (`Q/Quake/r_edge.c:2137-2142`). The status bar rows are only sent when they changed
  (`displaySetNumberOfUpdateLines(152 or 200)`, `graphics.c:243-254`; `VID_Update` at
  `Q/Quake/vid_sdl.c:513-516`).

---

## 3. Constant data in flash and how external flash is read

### 3.1 Code-side constants in internal flash (`const` / `.rodata`)

- Precomputed tables made by the PC tool QTablesGenerator: `sintable`, `intsintable`
  (`r_shared.h` externs, `RUNTIME_TABLES` switch), and the fine sin/cos table behind
  `sin_t/cos_t/tan_t` (`Q/Quake/tabmath.c:326-343`).
- `anorm_dots.h`, `anorms.h`, `adivtab.h`, and the turbulence table (`Q/Quake/r_draw.c:~100-125`).
- QuakeC converted to C: all game logic is code (`Q/Quake/quakeProgs.c`, 1 MB of source), and
  strings are compiled-in indexes (`Q/Quake/pakStrings.h`, `quakeProgs.h`), so there is no `progs.dat`.
  Per-class field getters and setters (`field_getters_setters.c`) are compiled `-Os`
  (comment at `Q/linkerfile.ld:29`).
- Hot renderer functions are tagged `DRAWSURF_SEC` → `.drawsurf` section (`quakedef.h:52`,
  `Q/linkerfile.ld:84-88`). It is still in flash, just grouped (inferred: for flash-cache locality).

### 3.2 External flash: explicit SPI DMA reads, not memory-mapped

- Two SPI NOR chips run in parallel ("interleaved"), 16 bits per chip per step
  (chip 0 delivers bytes 0-1, chip 1 bytes 2-3, `Q/src/interleavedSpiFlash.c:104,155,347`), on two
  EUSARTs triggered together through PRS (`interleavedSpiFlash.c:354-361`). Plain
  `READ 0x03`/`FAST_READ 0x0B`, 3- or 4-byte addresses (`interleavedSpiFlash.h:44-57`).
  Header comment: "typically 8MB @ 40 MHz" (`interleavedSpiFlash.h:3`).
- **External-flash pointers are tagged**: anything with bit 30 set (`EXT_FLASH_BASE 0x40000000`,
  `Q/src/memory_defs.h:27,46`) is not dereferenced. It is passed to the `extMem*` API
  (`Q/src/extMemory.h`):
  - `extMemGetDataFromAddress(dest, src, len)`: blocking copy through the 264-byte `rxBuffer`
    (`extMemory.h:58-120`).
  - `extMemGetByteFromAddress()`: one byte by DMA (used per texel for alias-model skins in
    `Q/Quake/d_polyse.c:167,175,454,471`).
  - `extMemStartAsynchDataRead()` / `extMemWaitAsynchDataRead()`: asynchronous DMA into a caller
    buffer (`extMemory.h:130-148`). This is the basis of texture prefetch.
- The PAK image sits at `PAK_ADDRESS = EXT_FLASH_BASE+4` (`memory_defs.h:31`). Options and savegames
  are written to external flash too (`extMemWrite/extMemErase`, `Q/src/extMemory.c:56-84`;
  `host_cmd.c` makes 43 extMem calls).

### 3.3 Caching scheme

- **Internal flash as a per-level write-once cache** (`CACHE_TO_FLASH 1`, `quakedef.h:132`):
  `storeToInternalFlash()` (`Q/Quake/internalFlash.c:281-318`) programs data word by word into
  the internal flash after the code (`FLASH_ADDRESS = 0x08000000 + code size`,
  `memory_defs.h:37-43`; about 900 KB available). It reads from external flash when the source is a
  tagged pointer. The whole cache is rebuilt at every level load. What goes there (all call sites
  are in `Q/Quake/model.c` unless noted):
  - BSP: vertexes (`:962`), edges (`:1011`), texinfo (`:1105`), faces/surfaces (`:1239`),
    nodes and leafs (`:1470-1517`), clipnodes (`:1588,1715`), planes (`:1899`), marksurfaces (`:1779`),
    surfedges (`:1828`), visdata (`:926`), texture headers (`:881-888`), brush-model data (`:2119-2137`).
  - Alias model headers, stverts, skin descriptors and frame groups (`:375-433`). Sprite frames
    (`:2183-2335`). `mod_known` (`:2370`).
  - Some alias skins (`CACHE_SKINS_TO_FLASH`, `Q/Quake/cl_parse.c:889`), precache lists
    (`cl_parse.c:756-797`), lightstyles (`cl_parse.c:1833`), conchars (`draw.c:131`),
    colormap (`host.c:1017`).
  - Lightmaps are **not** cached; they stay in external flash (`LIGHTMAP_ON_INTERNAL_FLASH 0`,
    `model.c:897-911`).
- **World textures** are streamed per surface into `textureCacheBuffer`, a 20 KB area used as two
  slots (slot 0 fills from the start, slot 1 from the end; `tcb[2]`, `Q/Quake/d_surf.c:354-420`).
  Surfaces are sorted by texture (`SORT_SURFS`, `sortSurfs()` at `Q/Quake/d_edge.c:238`) and the
  next texture is prefetched by asynchronous DMA while the current one is drawn
  (`findAndStartNextPreloadTexture`, `d_surf.c:476+`; chunks of `MAX_TCB_LOAD_SIZE 8192`).
- **Lightmaps** are loaded per surface into `tmpLightMap` (1,296 B, `d_surf.c:33`) and expanded into
  `_g->blocklights`. Lighting is applied **per pixel with bilinear interpolation inside the span
  drawer**; there is no surface cache at all (`R_GetSurfacePixel*` macros, `Q/Quake/d_scan.c:1213-1354`).
- **Alias skins** are read per texel from external flash using converter-computed triangle offset
  tables (`d_polyse.c:167-218,720`), unless the skin was cached to internal flash
  (`_g->originalSkinInInternalFlash`, `d_polyse.c:977`).
- **Sounds**: 8-bit samples resampled to 11,025 Hz offline, streamed from external flash in small
  chunks per mix (`snd_mix.c:374-399`).
- **Sky**: rendered offline into 128×128×128-byte blobs (2 MB each, `Tools/MCUPackConverter/main.c`
  `createSkies`/`addFileToPak("skyNN")`).

RP2040 implication (inferred): on the RP2040 the data flash is memory-mapped through XIP (16 KB cache), so
the tagged-pointer and `extMem*` layer can become plain loads or DMA from XIP. But the RP2040 has **no
second, internally writable flash**. MG24's "cache to internal flash" step would have to be either
(a) done offline, with the converter emitting final in-memory structures that are used in place
through XIP, or (b) written to a scratch region of the same QSPI chip at level load (XIP stalls
during erase/program, and there is wear). Option (a) looks clearly better.

---

## 4. Floating-point usage (ranked by frequency per frame)

The MG24 has a single-precision FPU and the code is built with `-fsingle-precision-constant`
(74 occurrences in `Q/.cproject`), so `1.0/x` literals are float, not double. On the M0+ every float
operation becomes a soft-float library call. (Inferred: the RP2040 ROM float routines are fast, but
still roughly 50-100+ cycles for a divide.)

**Per-pixel: no float.** Inner loops are already fixed point:
- World spans: 16.16 s/t stepping, per-pixel bilinear lightmap and colormap lookup
  (`d_scan.c:1213-1354`, Cortex-M33 inline asm at `d_scan.c:~800-1000`). The asm uses
  **`UMULL`, `SMLABB`, `UBFX`, `MLA`**, none of which exist on the M0+ (M0+ has only 32×32→32 `MULS`),
  so it must be rewritten. The non-power-of-two texture wrap uses `(uint64_t)c*h>>32`.
- Z spans: integer `izi += izistep` per pixel (`d_scan.c:1911+`).
- Alias triangles: `d_polyse.c` uses fixed-point gradients and steps.
- Particles: integer z test, screen write.

**Per sub-span (every 32 or 64 pixels): the hottest float site.**
- `D_DrawSpans8` (`Q/Quake/d_scan.c:1366+`): per sub-span `z = 65536/zi` (float **divide**), two
  float muls and two float→int conversions, plus three float adds. The sub-span length is
  `MAX_SPAN_COUNT 32` px (64 px, `MAX_SPAN_COUNT2`, for distant surfaces), `d_scan.c:1365-1367`.
  id used 8; the comment says 8 would cost about 2 fps. So this is about 1 divide per 32 pixels.
  `D_DrawSprite` still uses 8 (`d_sprite.c:95`).
- `Turbulent8` (`d_scan.c:285`) and `D_SpriteDrawSpans` (`Q/Quake/d_sprite.c:58-200`): same structure.
- Sky: `D_Sky_uv_To_st` (`Q/Quake/d_sky.c:43-52`) is float, called per sky sub-span (inferred: every 32 px).

**Per span**
- `D_DrawSpans8` span setup: `sdivz/tdivz/zi = origin + dv*stepv + du*stepu` (9 float ops,
  `d_scan.c:~1395-1415`).
- `D_DrawZSpans`: one float evaluation of 1/z and a float→int per span (`d_scan.c:1921-1941`).

**Per edge / per vertex**
- `R_EmitEdge` (`Q/Quake/r_draw.c:207+`): vertex transform (3 dot products), `1.0f/z`, projection,
  float `u_step` division. `R_ClipEdge` (`r_draw.c:428`): plane distances and lerp.
  `CLIP_PLANE_USES_FLOAT 1` (`quakedef.h:134`).
- `R_AliasTransformFinalVert` (`Q/Quake/r_alias.c:398-436`): 3 dot products and a lighting dot
  product per vertex. `R_AliasProjectFinalVert` and `r_alias.c:185` do `1/z`. `r_aclip.c`
  works per clipped vertex.
- `R_RecursiveClipBPoly` (`Q/Quake/r_bsp.c:265-330`): per edge of each brush entity.
- `D_PolysetCalcGradients` (`d_polyse.c:623-680`): float per alias triangle.

**Per BSP node / per surface**
- `R_RecursiveWorldNode` (`r_bsp.c:771+`): float frustum tests per node (`r_bsp.c:809-824`).
- `R_RenderFace` (`r_draw.c:679`), `R_RenderBmodelFace` (`r_draw.c:1043`), plane-side test at
  `r_bsp.c:582,692`.
- `D_CalcGradients` (`Q/Quake/d_edge.c:149-210`): per drawn surface. Per-entity surface z gradients
  at `d_edge.c:344-351,409-416`. `D_MipLevelForScale` (`d_edge.c:59`).
- `surf_t` stores `d_ziorigin/d_zistepu/d_zistepv` as floats (`r_shared.h:233-235`). A fixed-point
  variant exists but is off (`SURF_HAS_FIXED_D_ZISTEP_DATA 0`, `r_shared.h:100-101`).

**Per entity / per particle**
- `R_DrawParticles` (`Q/Quake/r_part.c:910-1000`): float physics per particle (up to 512).
  `D_DrawParticle` (`d_part.c:89`): float transform and `zi`.
- `R_AliasSetUpTransform` (`r_alias.c:316`), `R_RotateBmodel` (`r_bsp.c:175`), `R_EntityRotate`,
  `R_LightPoint` (`r_light.c:409`, recursive) per entity.
- Game and server: `sv_phys.c` (58 float mentions), `world.c` (`SV_RecursiveHullCheck`, `world.c:819`,
  recursive per trace; the most expensive game-side float code, inferred), `sv_move.c`, `sv_user.c`,
  `pr_cmds.c` builtins (traceline, vectoangles, normalize/vlen with sqrt), and **all of the converted
  QuakeC** (`quakeProgs.c`, 365 mentions; QuakeC's only numeric type is float).
  `cl_main.c` interpolation, `snd_dma.c` spatialization per channel.

**Per frame**
- `R_SetupFrame`, `R_SetupFrustum`/screen edges (`Q/Quake/r_main.c:424-443`), `V_CalcRefdef`
  (`view.c`), `R_TransformFrustum`, and `D_WarpScreen` setup (`d_scan.c:62-120`).

**Already fixed point or tabled**
- `edge_t.u/u_step` are 16.16 (`fixed16_t`) and `nearzi` is 12 bits (`r_shared.h:459-468`).
- Spans are bitfields. Lightmaps are integer with bilinear filtering (`BLOCKLIGHT_TYPE uint32_t`,
  `quakedef.h:126`). The alias rasterizer is fixed point.
- Entity origins are `short` × 1/8 and angles are integer with `ANGLE_PRECISION 64`
  (`quakedef.h:139,199-214`).
- `sin_t/cos_t` are table lookups (`tabmath.c:328-343`). `fixedApproxDiv256` (`tabmath.h:35`).
  `adivtab.h`.

---

## 5. Tools/

All four tools are plain C99 command-line programs with **Code::Blocks** projects (`*.cbp`, gcc /
MinGW). Prebuilt Windows `.exe` files are in `Tools/*/bin/Debug/`. None of them needs a runtime
beyond the C library.

### 5.1 QuakeCConverter (`Tools/QuakeCConverter/`)

- Based on id's `qcc` (`qcc.c`, `pr_comp.c`, `pr_lex.c`), modified to emit C (`main.c` header,
  lines 1-29).
- **Input**: a hard-coded directory `v101qc\` holding `progs.src` and the `.qc` files
  (`main.c: sourcedir = "v101qc\\"`). These are a modified v1.01 QuakeC source: spawn takes the class
  name, and functions must be declared before use (`main.c` header). It takes no arguments.
- **Process**: concatenates all files into `bigprog.qc`, compiles once, then emits C.
- **Outputs** (in the current directory): `progs.c` + `progs.h` → merged into **`quakeProgs.c`**,
  plus **`quakeProgs.h`** (symbol/string index defines) and `newprogsdef.h` (`main.c`, end of `main()`).
  The generated `progs_t` global is `progsvars_t`. The output is wrapped in `#if !USE_PROGSDAT`.
  Copies of these outputs are checked in at `Q/Quake/quakeProgs.[ch]`.
- **FunctionArrayCreator** (`Tools/FunctionArrayCreator/main.c`) is the second step. It reads
  `quakeProgs.h` and the hand-tuned `fieldDefs.h`, then generates `field_getters_setters.[ch]`: one
  struct per edict class, holding only the fields that class uses, with typed getter and setter
  functions (`main.c:1119-1395`). Its own comment says the configuration was tuned by playtesting,
  and that editing the QuakeC means rerunning both tools and hand-fixing the headers.
- **QTablesGenerator** (`Tools/QTablesGenerator/main.c`) prints the constant tables (turbulence sin
  tables, `FINE_ANGLES 8192` trig, vertex normals) to paste into the engine.

### 5.2 MCUPackConverter (`Tools/MCUPackConverter/`)

- **Input**: `pak0.pak` and optionally `pak1.pak` (retail). Usage:
  `MCUPackConverter pak0 [pak1] output`, defaulting to `pak0.pak`/`pak1.pak` → `pak0conv.pak`
  (`main.c:236-265`).
- **Output**: one standard-layout PAK (`"PACK"` header, 64-byte directory entries with 56-char names,
  `main.c` types `pak_header_t`/`pak_file_t`) holding converted contents. Files in pak1 override
  pak0 (`joinPaks`). The user copies it to an SD card as `PAK0.PAK`. The firmware's installer
  (`checkForDataFileUpdate`, `Q/src/main.c:162+`, FatFs in `Q/src/ff/`) writes it raw into external
  flash at `EXT_FLASH_BASE+4` (inferred: the first 4 bytes are a header or cookie). About 100 s erase
  plus about 2 s/MB.
- **Conversions** (`modifyFilesInPak`, `main.c:~320-400`):
  - `.mdl` → `Convert_AliasModel` (`model.c:1696`): adds triangle data and per-triangle skin
    offset tables so that skins can be sampled with single-byte reads (`addSkinTriangle`,
    `calculateTriangleOffsets`, `model.c:506,663`). v2.0 adds a 0xFFFFFFFF slot for skin caching.
  - `sound/*.wav` → resampled to 11,025 Hz, 8-bit (`wavconverter.c:34-75`), stored as length/loop
    ints followed by the samples.
  - `*.bsp` → `Convert_BrushModel` (`model.c:1525`): lumps reordered, nodes and leafs merged and
    restructured (`Mod_LoadNodes/Mod_LoadLeafs`), textures modified (`Modify_Textures`), skies
    extracted.
  - Each unique sky → prerendered `skyNN` (128×128×128 B), plus `skylist.dat`.
  - A NUL byte is appended to every file (`addNullTerminatorToFilesInPack`).
  - All strings found in the PAK (entity lumps) are printed so they can be pasted into C as
    `pakStrings.h` (`pakStringGenerator.c`). The engine refers to strings by index.
- **Format consequence**: the data is still in "file in a PAK" form, and the engine still parses BSP
  lumps at load time (into internal flash). It is not a ready-to-use memory image.

---

## 6. Platform abstraction

Board selection: `#define BOARD ...` in `Q/src/boards.h:33`. Per-board pins and peripherals are at
`boards.h:41-720`.

### 6.1 MG24/board-specific (to replace)

| Area | Files | Functions and responsibilities |
|---|---|---|
| Boot/clock/timers | `Q/src/main.c`, `main.h`, `Q/autogen/*`, `Q/config/*` | `main`, `sysInit`, `overclock` (DPLL), `Sys_Error`, `Sys_FloatTime` (`TIME_TIMER->CNT`, `main.c:421`), `I_GetTimeMicrosecs`, `delay.h` |
| Display | `Q/src/display.c/.h`, `ST77xx_registers.h`, `graphics.c/.h` | `displayInit`, `initDisplaySpi`, `displaySetNumberOfUpdateLines`, `startDisplayRefresh`, LDMA ISR, `waitForDisplayDMA`, `initGraphics`, `displayPrintf/Println` (boot text) |
| VID glue | `Q/Quake/vid_sdl.c` (`#else` = MCU half, lines 422-760) | `VID_Init`, `VID_SetPalette` (builds `g_palette` RGB565), `VID_ShiftPalette`, `VID_Update`, `D_Begin/EndDirectRect`, `Sys_SendKeyEvents` (button → Quake key mapping, `vid_sdl.c:566+`), `IN_Init/IN_Move/IN_Commands` (analog sticks) |
| Input | `Q/src/keyboard.c/.h` | `initKeyboard`, `getKeys` (2× 74HC165 shift registers), `getAnalogInput` (IADC joysticks) |
| Sound | `Q/src/audio.c/.h`, `Q/Quake/snd_sdl.c:151` | `initAudio` (TIMER + LDMA into PWM or VDAC), `SNDDMA_Init`, `SNDDMA_GetDMAPos` (`audio.c:254`), `SNDDMA_Shutdown` |
| External flash | `Q/src/extMemory.c/.h`, `interleavedSpiFlash.c/.h`, `sharedUsart.c/.h` | the `extMem*` API (section 3.2), `extMemInit/Write/Erase`, `interleavedSpiFlash*` |
| Internal-flash cache | `Q/Quake/internalFlash.c/.h` (MSC) | `storeToInternalFlash`, `storeToInternalFlashAtPointer`, `reserveInternalFlashSize`, `getCurrentInternalFlashPtr`, `internalFlashInit/SetCommonZone/ResetToCommonZoneEnd`, `eraseInternalFlash` |
| SD card (PAK install) | `Q/src/ff/*` (FatFs + `mmc_spi.c`) | used only by `checkForDataFileUpdate` |
| Misc | `Q/src/printf.c`, `dynMemWrapper.c`, `memory_defs.h` | printf to UART. malloc wrappers. Short-pointer helpers that hard-code `RAM_PTR_BASE 0x20000000` (`getLongPtr/getShortPtr`, `memory_defs.h:48-62`). The RP2040 also has SRAM at 0x20000000, so this keeps working as long as all short-pointed objects are in the 256 KB below 0x20040000 (inferred). |

Also MCU-specific inside "portable" files: the Cortex-M33 inline asm in `d_scan.c` (11 blocks),
`snd_dma.c` (7), `d_polyse.c`, `r_edge.c`, `draw.c`, `d_surf.c` (one or two each), and direct use of
`displayData`/`LDMA` in `r_edge.c:2137-2142` and `draw.c`.

### 6.2 Portable engine

The rest of `Q/Quake/*.c`: client, server, renderer, zone, QuakeC-as-C, console, menu. These
include `quakedef.h`, which picks `extMemoryWin32.h` or `extMemory.h` (`quakedef.h:88-93`).

---

## 7. PC / Windows build

- **There is no Windows project or build script in the repo.** The only build files are the two
  Simplicity Studio-generated makefiles (`Q/SparkDoom/makefile`,
  `Q/TheGamepad (Arduino Nano Matter)/makefile`). They need `arm-none-eabi-gcc` and the Gecko SDK 4.4.3,
  which is deliberately not shipped (`Q/.gitignore`: "remove gecko_sdk ... This requires project to be
  generated!"), plus Silicon Labs project files (`QuakeMG24.slcp`, `.cproject`).
  Flags: `-mcpu=cortex-m33 -mfpu=fpv5-sp-d16 -mfloat-abi=hard --specs=nano.specs`, malloc wrapping
  (`makefile:55`), `-Ofast` on hot files, `-fsingle-precision-constant`.
- A **WIN32 code path exists throughout the source** (`#if WIN32`): SDL2 video, input and audio in
  `Q/Quake/vid_sdl.c` and `Q/Quake/snd_sdl.c`. External flash is emulated with a RAM array
  `ext_memory[EXT_MEMORY_SIZE]` plus memcpy (`Q/Quake/extMemoryWin32.c`). Internal flash is a 15 MB
  array (`internalFlash.c:46-53`). `FIXME` pauses with `system("pause")` (`quakedef.h:62`).
  The readme mentions "Windows build" (changelog v2.0).
- **What's missing to build it**: a project/makefile, a `main()`/`Sys_Init` for WIN32 (`main.c` calls
  `Sys_Init()` under `#if WIN32`, `src/main.c:376`, but no `sys_*.c` defines it), the `Sys_File*`
  implementations, the code that loads the converted PAK into `ext_memory`, and SDL2 headers and libs.
  (inferred) Upstream builds it from a private SDLQuake tree; the makefile references
  `C:\Users\niwrachi\Documents\GitHub\SDLQuake\SDLQuake\QuakeMG24\linkerfile.ld`.
  To build on a PC we would need to write a small SDL2 host layer (main loop, file loading,
  `Sys_*`) and a CMake file, with `WIN32=1`. Note that much code is guarded by `#if WIN32`, not
  `_WIN32`, and assumes 32-bit pointers (16-bit short pointers, `STATIC_ASSERT`s on struct sizes),
  so a **32-bit (`-m32`) build** is required (inferred).

---

## 8. Will it fit in 232 KB (237,568 B)?

### 8.1 Model

Split the measured 282,560 B into fixed and resolution-dependent parts:

- **Fixed (does not depend on resolution)**: zone 48,412 + textureCacheBuffer 20,480 + `_g` 4,828
  + other `.bss` 4,864 + `.data` 296 + SEQRAM objects 20,456 = **99,336 B**.
- **Stack** = lsurfs 16,800 + spans (5 × MAXSPANS + 1) + about 1,213 B other.
  MAXSPANS scaled with 3D height (3,650 at 152 rows).
- **Frame buffer** = W × H3 + 1,024 (DMA line buffers) + 16. H3 = 3D view height; the status bar
  rows go in the z-buffer tail.
- **Z-buffer** = W × H3 × 2. During the edge phase it must hold: status bar W × (H − H3) +
  fixed edge scratch 64,312 B (= cached-edge overflow 15,232 + edges 38,520 + new/remove 1,216 +
  owners 3,200 + leafKey 6,144). That scratch is geometry-bound, not resolution-bound. Any excess
  that doesn't fit must be allocated separately ("overflow").

Assumption (inferred, design choice): the status bar scales with the screen (48 → 36 → 24 rows).
The real sbar art is 320 px wide, so 240 and 160 need a scaled or redesigned HUD.

### 8.2 Arithmetic

| | 320×200 (H3=152) | 240×150 (H3=114) | 160×100 (H3=76) |
|---|---|---|---|
| Fixed | 99,336 | 99,336 | 99,336 |
| Stack | 36,264 | 16,800 + 13,691 + 1,213 = 31,704 | 16,800 + 9,126 + 1,213 = 27,139 |
| Frame buffer | 48,640 + 1,040 = 49,680 | 27,360 + 1,040 = 28,400 | 12,160 + 1,040 = 13,200 |
| Z-buffer | 97,280 | 54,720 | 24,320 |
| Needed in Z during edge phase | 15,360 + 64,312 = 79,672 (fits, 17.6 KB slack) | 8,640 + 64,312 = 72,952 | 3,840 + 64,312 = 68,152 |
| Edge-scratch overflow | 0 | 72,952 − 54,720 = 18,232 | 68,152 − 24,320 = 43,832 |
| **Total** | **282,560 (275.9 KiB)** | **232,392 (226.9 KiB)** | **207,827 (203.0 KiB)** |
| vs 237,568 | **−44,992 (does not fit)** | +5,176 (fits, barely) | +29,741 (fits) |

Notes:
- At 320×200 even the RP2040's entire 264 KB (270,336 B) is too small, by 12 KB.
- At 240×150 the margin (about 5 KB) is smaller than normal porting overhead (Pico SDK statics,
  core-1 stack, USB/stdio, PIO/DMA descriptors), so treat it as "fits only with some of the levers
  below."
- At 160×100 the z-buffer is too small to host the edges, so most of the MG24 sharing benefit is
  lost. It still fits comfortably.

### 8.3 Biggest levers to cut RAM (approximate savings)

1. **3D view height or resolution** (dominant): every row costs W × 3 B (1 B frame + 2 B z).
   At 320 wide that is 960 B/row. A 320×120 3D view saves 32 × 960 = 30,720 B.
2. **Use XIP for read-only data instead of RAM or caches** (RP2040 advantage, inferred):
   - Textures could be read in place (or DMA'd in smaller chunks) to shrink `textureCacheBuffer`
     (20,480). The cached-edge-offset table (34,048 B for retail `MAX_MAP_EDGES`; 29,600 for
     shareware) would still need a home, part of which fits in the z-buffer slack.
   - Colormap (16 KB), lightmaps, skins, sounds and all BSP structures are already outside RAM on
     MG24 and stay so. On the RP2040 they are directly addressable, which removes the internal-flash
     copy step and the 264-byte SPI rx buffer.
3. **Edge/surface/span limits**: NUMSTACKEDGES 1600 → 1200 saves 10,400 B (edges + owners).
   NUMSTACKSURFACES 600 → 400 saves 5,600 B (lsurfs) + 400 B (`surfIdx`). MAXSPANS scales with
   view height. These matter most where the z-buffer can't host the edges (240×150 and below).
4. **Zone size** (48,412): sized for retail E1-E4. Shareware-only builds already use 39,292
   (−9,120 B, but MG24 gives that back to `textureCacheBuffer`). Shrinking it further means fewer
   simultaneous edicts or skipping the colormap copy (the colormap could stay in XIP flash).
5. **Small SEQRAM-class items**: particles 512 → 256 (−4,096). Channels 88 → 32 (−1,120).
   `audioBuffer` 2,048 could shrink with a faster refill. `cl_entities` is already 8 B per entity.
6. **Display path**: keep the palette-converting DMA approach (2 × 256 px lines = 1 KB). On the RP2040
   this can be PIO + DMA. A second frame buffer is unaffordable at any of these sizes except perhaps
   160×100.
7. **Second core** (inferred): no RAM saving by itself. The core-1 stack (Pico SDK default 2 KB in
   SCRATCH_X) must come out of the budget.

Bottom line: 240×150 is the largest of the three that fits, and only with a few KB of trimming
(levers 3 and 5). 160×100 fits comfortably. 320×200 needs about 45 KB cut: a much shorter 3D view
(lever 1), plus a texture or edge-offset rework (lever 2), plus limit cuts (levers 3-5).
