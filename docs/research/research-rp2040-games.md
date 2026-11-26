# Research: how big games were fitted onto the RP2040, mapped onto our Quake port

Date: 2026-11-22. Primary case study: kilograham/rp2040-doom (Graham Sanderson, Raspberry Pi). It is cloned at `third_party/rp2040-doom` (depth 1, upstream HEAD). All `file:line` citations below are relative to `third_party/rp2040-doom/`.

Tags used:
- **[V]** I read it in the cited source.
- **[E]** My own arithmetic from verified inputs.
- **[U]** Unverified, or the source is secondary.

Companion documents, not repeated here:
- `research-embedded-techniques.md`: XIP, interpolator and divider facts, plus the earlier blog-level rp2040-doom notes.
- `phase1-mg24quake-notes.md`: the MG24Quake RAM layout.

---

## 1. rp2040-doom deep dive

### 1.1 Starting point and goals

- **Chocolate Doom baseline.** It needs "300K of static mutable data and a minimum of about another 700K" of zone memory [RD-RAM] [V]. It was squeezed into 256 KB main RAM plus 2 × 4 KB scratch banks plus 3 KB stolen from USB DPRAM. The 16 KB XIP cache was kept as a cache, not used as RAM [RD-RAM] [V].
- **Rules the author set himself.** The full `DOOM1.WAD`, no cut splash screens, no altered levels, no downsampled textures. Demos, saves, cheats, 4-player network and OPL2 music all kept. 320x200 at 30-35+ fps on a 270 MHz overclock (`README.md:46-74`) [V].
- **Linker map.** RAM is `0x20000000` for 256 KB, with SCRATCH_X and SCRATCH_Y at 4 KB each (`src/pico/memmap_doom.ld:26-29`). Core 0's stack sits at the top of SCRATCH_Y and core 1's at the top of SCRATCH_X (`memmap_doom.ld:222-241`). Core 1's stack is only `0x4f8` (1,272 B) (`src/CMakeLists.txt:569`) [V].
- **Overall split.** "Almost 180K" is display-related. The heap is 58 KB. Zone use is "up to about 45K depending on the level" [RD-RAM] [V].

### 1.2 RAM budget: what lives in SRAM and how big it is

The author published no per-symbol size report; the blog has only a RAM pie chart, `ram.png`. The figures below are therefore computed from the source declarations.

| Item | Size | Source |
| --- | --- | --- |
| Two 8-bit frame buffers, 320x168 each. The 32-line status bar is not stored | 2 × 53,760 = **107,520 B** [E] | `src/pico/i_video.c:131`, `src/i_video.h:31` [V] |
| Render list buffer: 3,600 `pd_column` × 12 B + one 64x64 flat. Also the flat cache, the span list, and the save/load and ENDOOM work area | **47,296 B** [E] | `src/pd_render.cpp:319-330`, `228-250`, `3031-3034` [V] |
| `visplane_bit`: a 1-bit-per-pixel plane marker. Reused for patch decoding on core 1 | 320/8 × 168 = **6,720 B** [E] | `pd_render.cpp:171` [V] |
| `column_heads` (normal + fuzzy) | 640 × 2 = 1,280 B | `pd_render.cpp:266` [V] |
| Patch decoder: circular buffer, decoder tables, hash | 3,584 + 1,024 + 8 + 256 ≈ **4.9 KB** [E] | `pd_render.cpp:103-115` [V] |
| Flat decoder buffers | 1,024 + 512 = 1.5 KB [E] | `pd_render.cpp:103-104`, `whddata.h:782` [V] |
| Visplane heads (128) and frame-drawable heads (128) | ≈ 0.6 KB | `pd_render.cpp:120-122`, `r_state.h:229`, `r_data.h:48` [V] |
| Overlay display lists (`vpatchlists_t`), double-buffered | < 3 KB (asserted < 0xc00) | `v_video.h:73`, `pd_render.cpp:831` [V] |
| Scanline buffers: 4 × 164 words | 2,624 B [E] | `CMakeLists.txt` render_newhope `PICO_SCANVIDEO_SCANLINE_BUFFER_COUNT=4`, `MAX_SCANLINE_BUFFER_WORDS=164` [V] |
| Palette, plus a status-bar palette in scratch X used as a **stack-overflow canary** | 512 B + small | `i_video.c:132-133` [V], [RD-RAM] [V] |
| Audio: 2 × 1,024-sample producer buffers, plus 8 channels each with a 249-sample ADPCM decode block | ≈ 8 KB + 8 × ~260 B [E, U on the exact format] | `src/pico/i_picosound.c:40,65,112,424` [V] |
| OPL2 emulator code and tables copied into RAM | "about 4K" | [RD-RAM] [V] |
| Zone + malloc heap (one region): sectors, thinkers, mobjs, bitmaps | 58 KB total, of which zone ≤ ~45 KB per level | [RD-RAM] [V] |
| Stacks | core 0 ≤ 4 KB (scratch Y); core 1 1,272 B (scratch X) | `memmap_doom.ld:222-241`, `CMakeLists.txt:569` [V] |

Rendering and display therefore cost about 107.5 + 47.3 + 6.7 + 1.3 + 4.9 + 1.5 + 3 + 2.6 ≈ **175 KB** [E]. That matches the blog's "almost 180K".

What the game itself keeps in RAM fits in about 45 KB of zone:
- sectors
- thinkers and mobjs
- three line bitmaps
- the switched-sides list
- small per-level arrays

### 1.3 Game-object shrinking (mobj_t, thinkers, allocator)

| Technique | Detail | Saved | Source |
| --- | --- | --- | --- |
| **Static vs full mobj** | Objects flagged `MF_DECORATION` or `MF_SPECIAL` (decorations and pickups) are allocated as the 32-byte `mobj_t` "core" only. Movers get `mobjfull_t`, 84 B: the core plus movedir, reaction time, health, momentum, floorz/ceilingz, radius/height and target/tracer/player. For static objects, accessors fall back to the mapthing in flash (angle), `mobjinfo` (radius, height), or the sector (floorz/ceilingz): `mobj_radius()`, `mobj_floorz()`, `mobj_angle()` | Vanilla 32-bit `mobj_t` ≈ 150+ B [U] → 32 B (static) or 84 B (full) | `src/doom/p_mobj.h:193-194, 227-286, 290-358, 374-410, 418-424` (`static_assert(sizeof(mobj_t)==0x20)`, `sizeof(mobjfull_t)==0x54`) [V] |
| **Derive, don't store** | `SHRINK_MOBJ` drops `sprite`, `frame` and `info` (derived from `state_num` → `states[]` and `type` → `mobjinfo[]` in flash). The subsector pointer becomes a 16-bit `sector_num`. `spawnpoint` becomes a 16-bit index into the mapthings in flash | ~20 B per mobj [E] | `p_mobj.h:236-265, 196-221` [V] |
| **Singly linked lists** | `sprev` and `bprev` removed (sector and blockmap lists) | 2 shortptrs per mobj | `p_mobj.h:274-284` [V] |
| **Narrow fields** | `tics` int8. `reactiontime` uint8. `threshold` and `lastlook` int8. `health` int16 | ~12 B per full mobj | `p_mobj.h:236-324` [V] |
| **Thinker = 4 B** | `{shortptr next; uint8 function enum; uint8 pool_info}`. Function pointers became an enum dispatched by a `switch` in `P_RunThinkers` | 12 B → 4 B per thinker | `src/doom/d_think.h:70-88`, `p_tick.c` `P_RunThinkers` [V] |
| **Pooled allocation** | mobj and mobjfull are allocated 8 per zone block. The free-slot bitset lives in the spare byte of the block header (`Z_ObjectExtra`), and a 3-bit slot index lives in `thinker.pool_info` | Removes the 8 B header per object | `p_tick.c:210-260` (`Z_ThinkMallocImpl`), `z_zone.c:69-80` [V] |
| **8-byte zone header** | `{shortptr next, prev; uint16 size/4; uint8 tag; uint8 extra}`. No user pointer, no ZONEID | 20 B → 8 B per allocation | `src/z_zone.c:53-93` (`static_assert(sizeof(memblock_t)==8)`) [V] |
| **Zone and malloc merged** | One heap, to avoid fragmentation across two arenas | (fragmentation) | [RD-RAM] [V] |

### 1.4 16-bit pointers ("shortptr") and narrow types

- **`shortptr_t`** is a `uint16_t`. It decodes as `SHORTPTR_BASE + s*4`, with 0 meaning NULL. Every pointed-to object must be 4-aligned and inside the 256 KB main RAM: 65,536 × 4 = 256 KB. The encoder traps (`bkpt`) on any pointer that breaks the rule. On the RP2040 the base is `0x20000000`, and encoding is `(v<<14)>>16` (`src/doomtype.h:196-218`). Host builds map `shortptr_t` to `void*`, so the same code still runs as a PC build for verification (`doomtype.h:219-223`) [V].
- **Where shortptrs are used:**
  - zone links (`z_zone.c:55-56`)
  - thinker next (`d_think.h:78`)
  - mobj snext, bnext, target, tracer, player (`p_mobj.h:274-356`)
  - sector `soundtarget`, `thinglist`, `specialdata` (`r_defs.h:118-136`) [V]
- **Pointers into flash become 16-bit indices or byte offsets.** Examples are `lumpindex_t` short, `texnum_t` short, `flatnum_t` uint8 (`doomtype.h:150-187`), and `USE_INDEX_LINEBUFFER` (`r_defs.h:138-142`). Menu and status-bar graphics are renumbered to fit 8 bits [RD-RAM] [V].
- **`isb_int8_t` / `isb_int16_t`.** Typedefs that are `int` on the PC and narrow on the RP2040 (`doomtype.h:125-133`), so the PC build keeps vanilla behaviour [V].
- **Reduced-precision heights.** Sector heights are stored as 16-bit integers in "14:2" form, `rawfloorheight << 14` (`r_defs.h:86-92, 176-199`). An assert catches any height with low bits set [V].
- **Bitfields.** For example `soundtraversed:2` and `special:5` (`r_defs.h:106-112`). Fields are sorted smallest-first rather than `__packed`, to keep alignment [RD-RAM] [V].

### 1.5 `should_be_const` and level data in flash

- **`should_be_const`** expands to `const` when `DOOM_CONST=1` (`doomtype.h:164-168`, set at `CMakeLists.txt:325`). It is threaded through all the level-data accessors, so the compiler flags any write to flash-resident data. `USE_ROWAD` / `rowad_const` marks read-only WAD data, and `hack_rowad_p()` is the explicit escape hatch (`doomtype.h:240-251`) [V].
- **`W_CacheLumpNum` returns a pointer straight into memory-mapped flash.** The WHD image sits at the fixed address `TINY_WAD_ADDR`, `0x10040000`, `0x10042000` or `0x10048000` (`w_wad.c:166-178`, `w_file_memory.c:37-40`, `CMakeLists.txt:653-680`) [V].
- **Level lumps are used raw from flash** (`USE_RAW_MAPNODE/VERTEX/SEG/LINEDEF/THING`, `CMakeLists.txt:427-433`):
  - `vertex_t` is `const mapvertex_t` (`r_defs.h:60-68`).
  - In super-tiny WHX, `line_t`, `side_t` and `seg_t` are `const uint8_t` streams of variable-length compressed records (`r_defs.h:226-233, 290-300, 340-345`).
  - `subsector_t` is a `const uint16_t` holding only `firstseg`; the count is `next.firstseg - firstseg` (`r_defs.h:326`).
  - `node_t` is a 14-byte `whdnode_t` (`whddata.h:747-762`) [V].
- **Sectors are the only level structure instantiated in RAM** ("one minimal case"). Each is 36 B (`static_assert(sizeof(sector_t)==0x24)`, `r_defs.h:202`), allocated from `whdsector_t` at load (`p_setup.c:387-395`). The fields marked `(CAN BE CONST)` are still there; the author noted a further possible saving [V].

**How the mutable fields were separated:**

| Mutable thing in vanilla | rp2040-doom replacement | Source |
| --- | --- | --- |
| `line->validcount`, `sector->validcount` (32-bit each) | One shared bitset, `line_sector_check_bitmap`, explicitly cleared (`line_check_reset` / `sector_check_reset`) | `r_main.c:1250, 1317-1382`; `p_setup.c:577` [V] |
| `line->flags & ML_MAPPED` (automap "seen") | `line_mapped_bitmap` | `r_main.c:1334-1356` [V] |
| `line->special` cleared after use | Original value stays in flash, plus a per-line "cleared" bit in `line_special_cleared_bitmap` | `r_main.c:1253-1312` [V] |
| Switch textures (side top/mid/bottom changed by switches) | Switch textures are renumbered to be first (`<= LAST_SWITCH_TEXTURE`), so on/off is `t ^ 1`. A 32-entry list, `switched_sides[]`, records which sides are currently flipped | `r_main.c:1385-1468`; `r_state.h:258-278` [V] |
| Scrolling-wall `textureoffset` (special 48) | One global, `linespecialoffset++`, shared by all scrolling lines, since they all scroll at the same rate | `p_spec.c:1141-1147, 1386-1387`, `p_saveg.c:2142,2250` [V] |
| Animated texture/flat translation | `whd_gen` renumbers animated textures to low numbers, keeping the translation arrays short | [RD-RAM] [V], `whddata.h:553-556` [V] |
| Texture composite metadata (vanilla `texture_t`, column lumps and offsets) | None in RAM. Composite columns are decoded from flash at render time | [RD-RAM] [V] |

### 1.6 The WHD/WHX compressed WAD

`DOOM1.WAD` shrinks from 4,098 KB to 1,758 KB (57%) [RD-FLASH] [V]. The checked-in `doom1.whx` is 1,800,344 B [V]. Only sound is lossy. The encoder is `src/whd_gen/whd_gen.cpp` (5,331 lines); the decoders are `src/tiny_huff.c`, `src/image_decoder.c`, `src/musx_decoder.c` and `pd_render.cpp`.

**Primitives** [RD-FLASH] [V]:
- LEB128-style varints, with zigzag for signed values.
- Context-dependent bit widths.
- Canonical Huffman with only code lengths stored. Decoder tables are built into small RAM buffers (`tiny_huff.c:17-150`). Huffman was chosen over LZ because records of tens of bytes must decode independently, for random access.
- Per-record flag bits that select one of several encodings.

| Data | Encoding | Ratio | Decoded on demand or read raw? |
| --- | --- | --- | --- |
| Patches/sprites | One shared Huffman table per patch. Optional ±3 palette-delta symbols ("another 130K or so"). Identical columns are deduplicated (`0xffnn`). Pixel data is decoded forward and metadata backward from the column offsets, so no lengths are stored | 1,880 K → 851 K | Decoded per column into a temp buffer on every draw (`patch_decoder_*`, `pd_render.cpp:105-115`) |
| V-patches (menu, HUD) | Fixed 4/6/8-bit pixels, three transparency modes, row-strided | 286 K → 117 K | Decoded per scanline into the overlay, ahead of the beam |
| Textures | Single-patch flag. Local palette of ≤ 16 patches. Run-length of "one-patch" vs "composite" columns. Composite = drawing commands (1-bit end, 7-bit length, 4-bit patch, 4 flags: explicit_y, memcpy, memcpy_source, backwards) (`whddata.h:773-779`) | 16% | Composite columns decoded on core 0 only, visible range only |
| Flats | Huffman | (in the graphics 56%) | Decoded once per frame per flat into the top of `list_buffer`, which acts as the flat cache (`pd_render.cpp:325-330`) |
| Palettes | 33 tinted palettes dropped, recomputed with the original formula at most once per frame | 97% | Computed |
| Linedefs | Vertex index as 8-bit delta from a linear prediction. Optional special/tag. 1-bit back side. Sidedef delta ≤ 7 in the flags (`whd_gen.cpp:2690-2692`) | 62.6% | **Raw from XIP**, parsed per access |
| Sidedefs | 4-bit "which textures present" pattern selector + needed texture numbers | 90% | Raw from XIP |
| Segs | Variable-size, custom bit widths | 57% | Raw from XIP |
| Subsectors | `numsegs` dropped (implied by the next record) | 50% | Raw |
| Nodes | Children coded in 16 bits, exploiting the post-order layout: one child is always `n-1`, leaf deltas are 7-bit (`whd_gen.cpp:2920-2990`, decoder `r_state.h:148-175`). Child bounding boxes become 4 × 4-bit on a 16×16 grid of the parent box ("over-coverage pretty miniscule"). 28 B → 14 B | 50% | Raw from XIP, decoded per traversal |
| Blockmap | Per-row base + bitmap of non-empty blocks. One-line blocks inline. Varint line numbers | 68% | Raw |
| Sectors | Lump names → indices | 46% | Copied to RAM, the only level struct that is |
| Things, vertexes, reject | Uncompressed, to keep random access | 0% | Raw |
| Sound effects | 4-bit ADPCM (ADPCM-XQ) | 49%, lossy | Decoded per 249-sample block per channel (`i_picosound.c:40,65`) |
| Music (MUS) | Re-coded into 8 event types, delta/Huffman | 240 K → 91 K | Streamed |
| Demos | Delta + 4-bit "what changed" symbol + Huffman | 43 K → 10 K | Streamed |
| Saved games | Diff against the level-start state, with flag bits per changed field. Bit-packed (`th_bit_output`) | vanilla 10-60 K+ → typically 1-2 K, worst DOOM1 level 5-6 K | Written to flash slots (`g_game.c:1746-1900`, `p_saveg.c` `SAVE_COMPRESSED`) |

Cost: the author gives no timings. He says the decode cost is "probably quite a lot" offset by reading fewer bytes from slow flash [RD-FLASH] [V].

Note that the WHX limits are strict. Ultimate Doom and Doom II levels fail the super-tiny constraints, for example the node delta range (`whd_gen.cpp:2944-2946`), and must use the larger WHD format (`README.md:186-196`) [V].

### 1.7 Rendering memory tricks, core 1, interpolators, DMA

- **No visplanes and no drawsegs** (`NO_DRAWSEGS`, `NO_VISPLANE_GUTS`, `NO_VISSPRITES`, `CMakeLists.txt:357-361`). Together these were "about 70K" [RD-RENDER] [V].
- **Column lists replace them.** Each wall, sprite or plane column is one 12-byte `pd_column`: 16-bit next, yl, yh, 24-bit scale, 5-bit colormap, 8-bit column index, 19-bit texturemid (`pd_render.cpp:228-250`). It sits in one of 320 per-x linked lists sorted by y, with a separate fuzzy list (`pd_render.cpp:266`). Every pixel is covered exactly once, so there is no screen clear [RD-RENDER] [V].
- **Visplanes become a 9th framebuffer bit.** Floor and ceiling columns write their visplane index as the "color", and `visplane_bit` (1 bpp) marks those pixels (`pd_render.cpp:171`). Core 1 then scans the framebuffer for runs and builds 6-byte `flat_run` spans. Two spans pack into one freed `pd_column` slot (`pd_render.cpp:259-264`) [V].
- **Flat ordering and cache.** Visplanes sharing a flat are drawn together, so each flat is decoded once per frame. Cached flats grow down from the top of the same 42 KB list buffer [RD-RENDER] [V].
- **Double-buffered 320x168 frame buffers.** There was no room for the extra 10 KB per buffer that the status-bar rows would cost. The status bar and menus are **overlays composited per scanline** from display lists. The status bar background (320x16x4-bit, 5 KB) is **streamed from flash by low-priority DMA every frame** [RD-RENDER] [V].
- **Band buffers rejected.** A two 320x8 band-buffer prototype was abandoned for speed and complexity [RD-RENDER] [V].
- **Wipe without a third buffer.** The wipe uses about 1.68 frames' worth of buffer: the scanline code picks pixels from either screen. Text mode borrows the render work area (`i_video.c:1201-1212`) [V].
- **Phase reuse of the work area.** Save/load and ENDOOM text mode use `pd_get_work_area()`, which is the column-list buffer, between frames (`g_game.c:1750,1820,1877`; `pd_render.cpp:3031`) [V].
- **Core 1.**
  - It is blocked by semaphores during game logic, so no locking is needed on game state.
  - During rendering it generates audio (1.5-4.5 ms), builds spans, and pulls patch columns from a shared queue (`pd_render.cpp:819-839`).
  - It handles the video IRQs: 60,000/s for scanline DMA setup and 12,000/s for scanline fills (`i_video.c:983` `fill_scanlines` in scratch X).
  - Core 0 steals audio work if it finishes first.

  [RD-RAM] [RD-RENDER] [V]
- **Interpolators.**
  - Span texturing uses `interp1` with `add_raw`, shift 4 and mask 6-11, then a cross-input lane, so `pop[2]` gives the flat texel address (`pd_render.cpp:796-814, 1325-1420`).
  - Columns use `col_interp` with `accum`/`base` for frac/fracstep and `base[2]` as the source (`pd_render.cpp:1552-1590`).
  - When core 1's audio IRQ uses the interpolators, its state is saved and restored (`interp_in_use`, `pd_render.cpp:268-279`).

  [V]
- **16.16 FixedMul inline asm.** Four `muls`, dropping the AL×BL low half and skipping 64-bit arithmetic (`m_fixed.h:39-60`) [V].
- **Hot code in RAM.** Rendering inner loops, IRQ handlers, OPL and palette are kept in RAM (`__not_in_flash_func`, `__scratch_x`). The XIP cache is otherwise left alone, because manual control was "hard and often counterproductive" [RD-RAM] [V].
- **Flash clocked at 135 MHz** (270 MHz ÷ 2) [V].

### 1.8 Saves, demos, what was dropped

- **Saves.** Diff-from-level-start, bit-packed, written to flash. 34 KB of a 2 MB Pico holds up to 6 saves [RD-FLASH] [V].
- **Demos.**
  - Original demo playback is required to be bit-exact.
  - Reflecting the sine table's quarter cycles (error ≤ 1/65536) desynced the demos, so that saving was abandoned [RD-intro] [V].
  - Demo recording is dropped (`NO_DEMO_RECORDING`, `CMakeLists.txt:451`) [V].
- **Dropped or disabled** (`CMakeLists.txt:318-455, 495-560`) [V]:
  - wipe in some builds (`NO_USE_WIPE`)
  - DeHackEd, joystick, music packs
  - config saving, command-line args
  - float (`NO_USE_FLOAT`; `pico_set_float_implementation none`)
  - screenshots
  - checksums, WAD reload
  - runtime screen-size options (tables moved to flash)
  - Heretic/Hexen/Strife runtime flags (`DOOM_ONLY`)
  - the zlight table (`NO_USE_ZLIGHT`)
  - intercepts-overrun emulation
  - sound channels limited to 8
  - Doom II cast and bunny finales in the 2 MB build
- **Known overflow.** "Some later-game levels can run out of rendering-structure space", which leaves black areas (the column cap is 3,600; see `not_fully_covered_cols`, `pd_render.cpp:116-119`) [RD-RAM] [V].

### 1.9 Summary table: technique → what it saved → source

| # | Technique | What it saved | Source |
| --- | --- | --- | --- |
| D1 | Level data used in place from XIP (raw or compressed records); only sectors (36 B each) copied to RAM | Vanilla instantiates line_t, side_t, seg_t, subsector_t, node_t and vertex_t in zone: "700K" zone → ≤ 45 K | `r_defs.h:60-345`, `p_setup.c:556-582`, [RD-RAM] |
| D2 | `should_be_const` / `rowad_const` threaded through accessors | Compile-time proof that flash data is never written | `doomtype.h:164-168, 240-251` |
| D3 | validcount, mapped and special-cleared as bitsets | 3 × 32-bit per line → 3 bits | `r_main.c:1250-1382` |
| D4 | "Cleared" bit + original value in flash; switch flip `t^1` + 32-entry list; one global scroll offset | Per-side and per-line mutable copies eliminated | `r_main.c:1253-1468`, `p_spec.c:1141-1147` |
| D5 | Static (32 B) vs full (84 B) mobj; fields derived from tables | ~150+ B → 32/84 B per object; "thousands" of objects per level | `p_mobj.h:227-424` |
| D6 | 16-bit shortptr for all RAM-to-RAM links; 16-bit indices into flash | Half the size of every link | `doomtype.h:196-223` |
| D7 | Thinker = 4 B, function enum, pooled 8-per-block allocation | 12 B thinker + 20 B header → 4 B + ~0 | `d_think.h:70-88`, `p_tick.c:210-260` |
| D8 | 8 B zone header; zone and malloc merged | 20 → 8 B per allocation; no cross-heap fragmentation | `z_zone.c:53-93` |
| D9 | Narrow types (`isb_*`, 14:2 heights, uint8 pics, bitfields), sorted fields | Sector ~110 B (vanilla, [U]) → 36 B | `r_defs.h:86-202` |
| D10 | Column lists instead of visplanes/drawsegs; 9th-bit plane markers; span records reuse column slots | ~70 K of structures → 47 K buffer that is also flat cache and save work area | `pd_render.cpp:171, 228-330` |
| D11 | Frame buffer only for the 3D view; HUD and menus as per-scanline overlays from flash; status bar DMA-streamed | 2 × 10 KB of buffer rows; no HUD buffer | [RD-RENDER] |
| D12 | Phase-based buffer reuse (render list ↔ save/load ↔ text mode; `visplane_bit` ↔ patch decode) | No dedicated save buffer (vanilla save is 10-60 K) | `pd_render.cpp:171, 3031`, `g_game.c:1750-1880` |
| D13 | WHD compression: Huffman, varints, deltas, column dedup, texture command lists, ADPCM, node post-order coding | 4,098 K → 1,758 K of flash. Also fewer XIP bytes per frame | [RD-FLASH], `whd_gen.cpp` |
| D14 | Recompute tinted palettes at runtime | 97% of PLAYPAL | [RD-FLASH] |
| D15 | Diff-from-level-start bit-packed saves in flash | 10-60 K → 1-6 K | [RD-FLASH], `p_saveg.c` |
| D16 | Core 1 does audio, span building and half the column drawing, gated by semaphores | Speed: 30-35 fps | [RD-RENDER], `pd_render.cpp:819-839` |
| D17 | Interpolators for span/column texel addressing; inline 16.16 multiply; hot code in RAM; 135 MHz flash | Speed | `pd_render.cpp:796-814`, `m_fixed.h:39-60` |
| D18 | Stack-overflow canary (status-bar palette); tiny core-1 stack (1,272 B) | Lets stacks be "finely tuned to the largest they can be" | `CMakeLists.txt:569`, [RD-RAM] |

---

## 2. Other RP2040-class ports of big games (brief)

| Port | Hardware / RAM | Techniques beyond rp2040-doom | Source |
| --- | --- | --- | --- |
| **MG24Quake** (next-hack) | EFR32MG24 Cortex-M33 + FPU, 256 + 20 KB, no PSRAM. Quake at 320x152 view, 276 KB used | Z-buffer as a general temp buffer (edges, PVS bits, net messages, `mod_known` build, warp); status bar stored in the z-buffer tail; 8 B packed client entities with 1/8-unit short coordinates; per-class edicts in a 48 KB zone; savegame = raw zone dump (works because of 16-bit relative pointers); `mod_known` frozen to flash after load | `phase1-mg24quake-notes.md` §1.2-1.6 [V]; [MG24-habr] |
| **RP2350-Quake** (zenodante) | RP2350, 520 KB, no PSRAM | Same engine. Immutable BSP and models baked into XIP at fixed addresses by an offline resource compiler; deduplicated textures. Uses 439,248 B (128 KB zone, 43 KB peak), so it shows what happens when the MG24 discipline is relaxed | `research-embedded-techniques.md` addendum [V] |
| **Doom on nRF52840** (next-hack) | 256 KB, QSPI flash for the WAD, 240x240 16-bpp LCD | Derived from doomhack's GBA Doom, which runs in "about 110 kB" (GBA EWRAM). Short pointers, an optimized allocator, reduced structs for bonuses and decorations (the same idea as D5), and a WAD tool that pre-converts to fixed point and adds column lengths so structures need not be copied to RAM. QSPI random reads are only about 250 kB/s because of a 4 µs latency | [NRF-DOOM] [V for the listed items; implementation details U] |
| **Wolfenstein 3D in 48 KB** (S. Germino, RETRO-CIAA) | 48 KB RAM, 1 MB flash | Fixed-width types sized to value ranges; no heap; bit-field booleans; struct reordering; runtime-computed tables made `const`; textures pre-converted to the display format offline; palette fades applied in the video output path, not in stored pixels; 256x144 view; iterate on the `-Map` file | [WOLF48] [V] |
| **Wolfenstein 3D on RP2040** | none found | No dedicated RP2040 port found in search [U, absence] | search 2026-11-22 |
| **Duke Nukem 3D / Build** | RP2350 only (rh1tech murmduke3d / frank-duke3d, pico-duke3D) | All need 8 MB PSRAM and state they "will not run on Pico 1 (RP2040)". No Build-engine port without PSRAM found | [MURM] [V], `research-open-source-quake-bases.md:136` |
| **Heretic / Hexen** | rp2040-doom can build them, but they are "almost certainly broken" (`README.md:12-14`). MurmHeretic targets the RP2350 with PSRAM | nothing new | [V] |
| **Quake on ESP32 without PSRAM** | none found | ESP32 Doom/Quake ports all use PSRAM. MG24Quake is the only no-external-RAM Quake 1 port found [U, absence] | search 2026-11-22 |
| **QuakEMBD** (FantomJAC) | STM32 Cortex-M, with external SDRAM [U] | Thin WinQuake platform layer; not a RAM-reduction reference | `research-open-source-quake-bases.md:111` |
| **Game Boy (RP2040-GB / Peanut-GB)** | RP2040 | Code runs from RAM for speed, while the ROM array is pinned in flash with `__in_flash("rom")` so it is not copied: the inverse of the usual layout. The bank-switched ROM is read directly through XIP | [RP2040-GB] [V] |
| **NES (pico-infonesPlus)** | RP2040/RP2350 | Without PSRAM the selected ROM is **programmed into flash** at a fixed XIP address, then the device reboots and runs from XIP. The image already in flash is tagged `[READY]` to avoid rewriting it (flash-wear warning). Older PicoSystem builds load the ROM at `0x10110000` with picotool | [INFONES] [V] |
| **GBA emulation on RP2040** | none found | [U, absence] | |

**Additional techniques that are not in rp2040-doom:**
- **Programming derived data into flash on the device** (infones): a way to bake BSP data without shipping converted id data. See M1 below.
- **Z-buffer as an omnibus temp buffer** (MG24).
- **Code in RAM, data in XIP** (RP2040-GB): the right split for hot render loops.
- **Format conversion in the output path** (Wolf48): palette effects applied at display time.

---

## 3. Mapping rp2040-doom techniques onto our remaining items

Our state at the time of writing: E1M1 needs about 1.95 MB dynamic + 271 KB static.

"Licensed data?":
- **yes** means the technique requires converting or deriving id game data files offline, such as a baked image built from `pak0.pak` or tables generated from `progs.dat`.
- **no** means it is pure engine code that works on whatever data the user supplies at run time.

All KB figures are [E].

| # | rp2040-doom technique | Quake equivalent | Our item | Est. KB saved | Effort | Licensed data? |
| --- | --- | --- | --- | --- | --- | --- |
| M1 | D1 + D2: level data in place in XIP, `should_be_const` | Put `msurface_t`, `mnode_t`, `mleaf_t`, `mplane_t`, `mtexinfo_t`, `medge_t`/`mvertex_t`, marksurfaces, surfedges and hull0/1/2 clipnodes in flash, typed `const`. **Option A (no):** stream-convert each lump at map load and program it into a reserved flash region (64 KB block erase), with pointers computed as final flash addresses, because the counts of every lump are known before any conversion. This is the infones pattern applied per map, and it can be cached by map name or hash so each map is baked once. **Option B (yes):** an offline baker emits the image, as RP2350-Quake does. Our mutable fields are already in side arrays, so the structs are read-only | Hunk low, ~1.64 MB | **~1,600** (everything except side arrays and the load scratch) | Med-high. A: about 10 s on the first load of each map [E: 1.6 MB of 64 KB block erases at ~150 ms + page programs] | **A: no**. B: yes |
| M2 | D3: validcount as explicit bitsets | `visframe` on nodes, leaves and surfaces → one bit each, cleared per frame (or 2-frame ping-pong); `dlightframe`/`dlightbits` → one bitset per active dlight (≤ 8) | Our side arrays (size not given; 4-12 B × ~5.4k surfaces + nodes + leaves) | ~20-60 → ~2-6 | Low | no |
| M3 | D4: "cleared bit + flash original"; switch `t^1`; global scroll offset | Light styles are already global. Moving brush models (doors, plats) change only the entity origin, not the BSP, so the BSP stays const. `texture->anim_next` animation is a global frame counter. Quake has no writes to BSP data at all once the side arrays exist; this confirms M1 is safe | none (confirmation) | 0 | — | no |
| M4 | D5: static vs full mobj | Two-tier edicts. **Engine-only version:** keep a hot core in RAM: `edict_t` header, links, origin/angles/velocity, think/nextthink, flags, model, frame, solid, movetype, absmin/absmax. The remaining ~80 `entvars_t` floats go in a cold record allocated only when a non-zero value is written (default 0 is returned while absent, which is the "cleared bit" idea). Non-thinking items and triggers rarely need the cold record. **progs-derived version:** per-class layouts (MG24: monster 212 B, player 312 B, trigger 116 B, against our 876 B) | Edicts ~278 KB (E1M1) | Engine-only: **~150-180** [E: 876 → ~200 B hot + occasional ~350 B cold]. Per-class: **~200-230** | High | Engine-only: no. Per-class layouts from progs: yes |
| M5 | D6: 16-bit shortptr | `edict_t.area` links (`link_t` 2 × 4 → 2 × 2), efrag lists, `cl_entities` pointers, zone headers, `leafnums`, `mnode_t` children and parent if a RAM copy ever exists. Keep host builds `void*` as rp2040-doom does (`doomtype.h:219`) | Edicts, `cl_efrags`, zone | ~10-15 | Med | no |
| M6 | D7 + D8: 8 B zone header, merged zone and malloc, pooled fixed-size objects | Quake has hunk + zone + cache + malloc. Collapse them into one arena with an 8 B header (shortptr links, size/4, tag). Pool efrags, particles and temp entities 8 per block | Zone 48 KB, cache ~150 KB, static `cl_efrags` and `cl_temp_entities` | ~10-20 (headers and fragmentation) | Med | no |
| M7 | D1 for assets: `W_CacheLumpNum` returns a flash pointer | Make `Mod_Extradata` / `S_LoadSound` / `Cache_Check` return XIP pointers for `aliashdr_t`, frames, skins and `sfxcache_t`. The PCM is already in flash; extend that to the headers, building any header that needs fix-up into flash at map load as in M1 A | Model/sound cache ~150 KB | **~130-140** | Med | A: no. Offline: yes |
| M8 | D2 for progs | Read `dfunction_t`, `dstatement_t`, `ddef_t` and strings in place from XIP (they are never written) | progs bytecode 360 KB | **~360** | Low (if statements are read in place); a moderate interpreter speed cost through XIP | no |
| M9 | D4 + D9 for progs globals: separate immutable from mutable | QuakeC immediates live in the globals area and are never written. At load, scan the statements for every global that is a store target, a parm/return slot, or a function local; only those (plus system globals) need RAM. Constants stay in flash behind a remap table or bitmap. With the **AOT compiler**, constants become C literals and only the mutable globals remain as C variables | progs globals 45 KB | Scan: **~15-25** [U on the constant share]. AOT: ~25-30 | Scan: med. AOT: low (already written) | Scan: no. AOT output: yes |
| M10 | D9: narrow types, 14:2 heights | Client entity state as int16 at 1/8 unit (MG24's 8 B `entity_t`); static entities as `{model u8, frame u8, skin u8, origin s16×3, angles u8×3}` ≈ 12 B (rp2040-doom keeps static mobj positions in flash, but ours come from the server message, so they stay in RAM) | `cl_static_entities` 23 KB; `cl_entities` | ~20 + ~10 | Low-med | no |
| M11 | D10: column lists instead of visplanes | Quake equivalent: shrink `surf_t`, `edge_t` and `espan_t` (MG24 packs `espan_t` to 4 B with v stored separately; `edge_t` 24 B; `surf_t` 28 B) and size the arrays to 240x150 | Render tables (stack) | ~10-20 against stock sizes | Med | no |
| M12 | D11: frame buffer only for the 3D view, HUD as overlay | Render the 3D view to a 240x150 buffer. Draw the status bar, console and centerprint as overlays (flash-resident `gfx.wad` pics) composited into the LCD DMA line stream, or pushed to the LCD's GRAM only when they change | Video 64 KB FB + 125 KB z | FB 64 → 35; z 125 → 70 (from 240x150 itself) = **~84** | Med | no |
| M13 | D12: phase-based reuse (MG24 extends it) | The z-buffer hosts edges, PVS/visleaf bits, edge-offset cache, net sizebufs and `mod_known` during load (MG24 §1.5). Save/load work area = the z-buffer | Message buffers, `cachededgeoffset`, save | ~20-30 that would otherwise need dedicated RAM | Med | no |
| M14 | D13: WHD-style compression | Pack BSP records (`msurface_t` 56 B → ~28-32 B with 16-bit indices, node children coded relative to n+1 if qbsp emits nodes in pre-order [U, needs a check on real maps], 4-bit child bboxes relative to the parent). This saves **flash and XIP bandwidth, not RAM**. ADPCM sound halves the flash used by sound | flash / speed | RAM: 0 | High | yes if offline; no if done in the M1 A on-device bake |
| M15 | D14: recompute palette tints | `cshift` / `V_UpdatePalette` already computes this; keep the 16 KB colormap in flash (read via XIP) | colormap | 0-16 | Low | no |
| M16 | D15: diff-from-spawn saves | Save = edicts that differ from the map's spawn state + globals, bit-packed into flash, built in the z-buffer work area | Save path (no resident cost today) | Avoids a 20-60 KB save buffer | Med | no |
| M17 | D16: core 1 gated by semaphores | Core 1: LCD DMA feed, sound mix, then half of `D_DrawSpans` / alias spans for the frame; idle during the server and QuakeC frame, so no locks | Speed | 0 (costs a 1-2 KB stack) | Med | no |
| M18 | D17: interpolators, inline `FixedMul`, hot loops in RAM | Span texel and lightmap addressing via `interp0`/`interp1` (power-of-two texture sizes); turbulence and sky via lane masks; keep `D_DrawSpans`, z-spans and alias span loops in SRAM (about 4-8 KB) | Speed (costs RAM) | −4 to −8 | Med | no |
| M19 | D18: stack canary, tight stacks | Stack paint and a canary for both cores; size from measurement | margin | 2-6 | Low | no |
| M20 | Narrow and remove static tables (rp2040-doom D9 plus MG24) | `con_text` 16 KB → ~1-2 KB; key bindings `char*` → u8 command index; `mod_known` frozen to flash after load; `cl_temp_entities` / beams to small pools | keys/console, `mod_known`, temp ents | ~25-35 | Low | no |

**Reading the table.** Everything needed to get under 245 KB except the per-class edict layouts (M4, progs-derived) and the AOT constant folding (M9 AOT) can be done in pure engine code, as long as M1 and M7 use the on-device bake-to-flash route. That route is the pico-infonesPlus "program the image into flash, then run it from XIP" pattern, applied per map. It keeps the repository free of converted id data, at the cost of a slow first load per map and some flash wear (one rewrite per map change, far below the 100k-cycle rating [U on the exact part]).

---

## 4. Proposed 245 KB SRAM budget for Quake on the RP2040

**Precedents:**
- **MG24Quake** fits Quake in 276 KB at a 320x152 view on a Cortex-M33 with FPU:
  - z-buffer 97 KB, including 15 KB of status bar
  - frame buffer 49.7 KB
  - zone 48 KB, which holds the edicts
  - stack 36 KB, which holds the edge, surf and span tables
  - texture cache 20 KB
  - SEQRAM extras 20 KB
- **Scaling MG24 to 240x150:**
  - The frame buffer and z-buffer fall from 146.7 KB to 35.2 + 70.3 = 105.5 KB, saving 41 KB.
  - Removing its texture cache saves another 20 KB, since we read texels per pixel from XIP.
  - Its 276 KB becomes about **215 KB** [E], before adding RP2040 platform costs (core-1 stack, LCD DMA, hot code in RAM, about 10-15 KB).
  - That lands at about 225-230 KB, so 245 KB is plausible with about 15 KB to spare.
- **rp2040-doom** shows the same proportions: about 175 KB for display and render, about 58 KB for heap, and stacks in the scratch banks.
- **RP2350-Quake** at 439 KB shows what happens without that discipline: a 128 KB zone and 91 KB of heap headroom.

| Category | KB | Contents | Precedent / justification |
| --- | ---: | --- | --- |
| 3D frame buffer, 240x150 8-bit | 35.2 | view only; HUD as overlay (M12) | rp2040-doom D11; MG24 FB covers only the 3D rows |
| Z-buffer, 240x150 16-bit | 70.3 | also hosts edges, PVS bits, edge-offset cache, sizebufs, `mod_known` build, save work area (M13) | MG24 §1.5 (17.6 KB slack after edges at 320x152) |
| LCD DMA line buffers + RGB565 palette | 1.5 | 2 × 320 × 2 B + 512 B | MG24 `displayData` 1 KB lines; rp2040-doom 4 scanline buffers |
| Render tables: `surf_t`, `espan_t` (core 0) | 20 | ~400 surfs × 28 B + ~2,700 spans × ~4-5 B | MG24 35 KB at 48.6k px, scaled to 36k px |
| Stacks: core 0 call stack + core 1 | 8 | 6 + 2 KB, can sit in the scratch X/Y banks | rp2040-doom 4 KB + 1.25 KB in scratch |
| Edicts | 40 | ~260 live × ~150 B average, hot/cold or per-class (M4, M5) | MG24 holds edicts inside a 48 KB zone; RP2350-Quake 43 KB zone peak |
| Progs mutable globals | 12 | mutable subset only (M9) | constants never written |
| Unified zone/heap | 16 | alias and sound headers not in flash, efrags (pooled), strings, small per-map arrays, load scratch | rp2040-doom ≤ 45 KB zone for all game state; MG24 48 KB including edicts |
| BSP mutable side state | 6 | visframe bitsets, dlight bitsets, leaf efrag heads (shortptr) (M2) | rp2040-doom D3 |
| Client entities | 8 | `cl_entities` 8-16 B packed, static entities ~12 B × 128, temp ents, beams, dlights, lightstyles (M10, M20) | MG24 8 B `entity_t` |
| Particles | 4 | 256 × 16 B. 512 × 16 B = 8 KB if margin allows | MG24 512 × 16 B in SEQRAM |
| Sound | 5 | 8 channels, mix ring, ADPCM state | rp2040-doom 8 channels + 2 × 1,024-sample pool; MG24 3.8 KB + 8 KB paint buffer on stack |
| Lighting scratch | 1.5 | `blocklights` 18 × 18 × 4 | MG24 `tmpLightMap` 1.3 KB |
| Console, keys, cmd buffer, server/client state, misc statics | 5 | `con_text` ~1 KB, u8 key bindings, trimmed `sv`/`cl` | MG24 `con_text` 913 B, `sv` 836 B |
| Pico SDK runtime, vectors, driver state | 3 | | rp2040-doom builds with alarm pools and shared IRQs disabled (`CMakeLists.txt:500-506`) |
| Hot code in SRAM | 4 | span, z-span and alias inner loops | rp2040-doom renderer, IRQs and OPL in RAM |
| Margin | 4.5 | | |
| **Total** | **≈ 244** | | |

**If it does not fit**, these levers apply, in order of cost:

| Lever | Saving |
| --- | --- |
| Particles 256 → 128 | −2 KB |
| Render view 224x140 (FB 30.6 + z 61.3) | −13.6 KB |
| Edge, surf and span caps tuned to measured E1M1-E4M8 peaks (MG24 bails a frame on overflow; rp2040-doom leaves black gaps) | varies |
| 8-bit log-depth z for model-vs-world tests only. Risk: model/wall z-fighting [U] | −35 KB |

**What it needs first, in order** (KB moved out of RAM):

| Step | Change | KB moved |
| --- | --- | --- |
| 1 | M8: progs bytecode in place | 360 |
| 2 | M1: BSP to flash, on-device bake | ~1,600 |
| 3 | M7: model and sound headers to flash | ~140 |
| 4 | M12 + M13: 240x150 view, HUD overlay, z-buffer temp aliasing | ~84 + dedicated buffers |
| 5 | M4 + M5: edicts | ~240 |
| 6 | M20 + M10: static tables | ~50 |
| 7 | M9: globals | ~30 |
| 8 | M6: allocator | ~15 |

Only M4's per-class variant and M9's AOT variant need licensed data; the engine-only variants listed above do not.

---

## Sources

- [RD-intro] G. Sanderson, *RP2040 Doom* (blog index). https://kilograham.github.io/rp2040-doom/
- [RD-RAM] *RP2040 Doom: Making It Run Fast And Fit in RAM*. https://kilograham.github.io/rp2040-doom/speed_and_ram.html
- [RD-FLASH] *RP2040 Doom: Making It All Fit In Flash*. https://kilograham.github.io/rp2040-doom/flash.html
- [RD-RENDER] *RP2040 Doom: Rendering And Display Composition*. https://kilograham.github.io/rp2040-doom/rendering.html
- rp2040-doom source: https://github.com/kilograham/rp2040-doom (local clone `third_party/rp2040-doom`). Files cited:
  - `src/doomtype.h`, `src/z_zone.c`, `src/pd_render.cpp`, `src/picodoom.h`, `src/whddata.h`, `src/tiny_huff.c`, `src/m_fixed.h`, `src/w_wad.c`, `src/w_file_memory.c`, `src/CMakeLists.txt`
  - `src/pico/i_video.c`, `src/pico/i_picosound.c`, `src/pico/memmap_doom.ld`
  - `src/doom/p_mobj.h`, `src/doom/d_think.h`, `src/doom/p_tick.c`, `src/doom/r_defs.h`, `src/doom/r_state.h`, `src/doom/r_main.c`, `src/doom/p_setup.c`, `src/doom/p_spec.c`, `src/doom/p_saveg.c`, `src/doom/g_game.c`
  - `src/whd_gen/whd_gen.cpp`
- [MG24-habr] next-hack, Quake port to Arduino Nano Matter (276 KB). https://habr.com/en/articles/848396 ; Hackaday, *Quake In 276 KB Of RAM*. https://hackaday.com/2024/10/05/quake-in-276-kb-of-ram ; local notes in `docs/research/phase1-mg24quake-notes.md`
- RP2350-Quake (zenodante). https://github.com/zenodante/RP2350-Quake ; local notes in `docs/research/research-embedded-techniques.md` (addendum) and `docs/research/research-open-source-quake-bases.md`
- [NRF-DOOM] next-hack, *Doom on an nRF52840-based USB Bluetooth dongle*. https://hackaday.io/project/181224-doom-on-an-nrf52840-based-usb-bluetooth-dongle/details
- [WOLF48] S. Germino, *Wolfenstein 3D in 48 KB of RAM*. https://hackaday.io/project/181407-wolfenstein-3d-in-48-kb-of-ram/details
- [MURM] rh1tech murmduke3d / frank-duke3d (RP2350 + PSRAM). https://github.com/rh1tech/murmduke3d ; Hackaday *Does It Run… Duke Nukem 3D?* https://hackaday.com/?p=1240415
- [RP2040-GB] deltabeard, RP2040-GB (Peanut-GB). https://github.com/deltabeard/RP2040-GB ; fork https://github.com/YouMakeTech/Pico-GB
- [INFONES] fhoedemakers, pico-infonesPlus. https://github.com/fhoedemakers/pico-infonesPlus (README: https://raw.githubusercontent.com/fhoedemakers/pico-infonesPlus/main/README.md) ; PicoSystem_InfoNes https://www.github.com/fhoedemakers/PicoSystem_InfoNes
- MurmHeretic (RP2350). https://github.com/rh1tech/murmheretic
- QuakEMBD. https://github.com/FantomJAC/quakembd
