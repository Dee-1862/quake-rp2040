# Research: embedded techniques for fitting and speeding up Quake on one RP2040

Date: 2026-11-22. Scope: published, documented techniques that apply to the remaining RAM items (map structures 781 KB, edicts 513 KB, static tables 478 KB, particles 88 KB, net buffers 81 KB, gfx.wad 110 KB) and to frame time (target 240x150, 10-15 fps, so 66-100 ms per frame).

Already done, so not proposed again: BSP, textures, alias models, sounds and progs read in place from flash; no surface cache (per-pixel texture x lightmap bilinear lookup); QuakeC compiled to C. MG24Quake's own techniques are already written up in `docs/research/phase1-mg24quake-notes.md`. This document only repeats them where a new detail or a different RP2040 angle adds something.

How to read the tables:
- **Benefit** is either RAM saved or ms/frame saved at 200 MHz on 240x150 = 36,000 pixels. The rule of thumb is 1 ms = 200,000 cycles, so 1 cycle/pixel = 0.18 ms/frame.
- **Verified** means I read the claim in the cited source. **Estimate** means it is my own arithmetic from verified inputs. **Unverified** means the source is secondary or I could not confirm it.

---

## 0. Hardware facts that drive everything below (all verified, RP2040 datasheet)

| Fact | Value | Source |
| --- | --- | --- |
| XIP cache | 16 KB, **2-way set-associative, 8-byte lines**, 1-cycle hit. The flush takes about 1024 cycles, which is "16 kB / 8 bytes per line / 2 ways" | [DS] 2.6.3.1-2 |
| XIP aliases | 0x10 cached and allocating; **0x11 cached but non-allocating** (check for a hit, don't fill on a miss); 0x12 non-cached but allocating; 0x13 bypasses the cache | [DS] 2.6.3.1 |
| Cache as SRAM | With the cache disabled, 0x15000000 becomes 16 KB of extra SRAM. Flash code then runs uncached, which the datasheet calls a "significant impact" | [DS] 2.6.2.1, 2.6.3.1 |
| USB DPRAM | 4 KB more SRAM at 0x50100000 if USB is unused | [DS] 2.6.2.1 |
| XIP perf counters | `CTR_HIT` / `CTR_ACC` registers count cache hits and accesses (see the pico-examples `flash_cache_perfctr`) | [DS] 2.6.3.2 |
| Flash streaming | `STREAM_ADDR`/`STREAM_CTR` run a background linear read into a FIFO that feeds DMA (DREQ). It has lower priority than XIP misses, with a 7-cycle cooldown after a miss. The FIFO can be read via the **XIP_AUX** slave on FASTPERI, so the DMA never stalls | [DS] 2.6.3.4 |
| SSI clock | Maximum SPI clock = sys_clk/2. `SCKDV` must be **even**. At 200 MHz the only choices are 100 MHz (÷2) or 50 MHz (÷4) | [DS] 2.6.3.3, 4.10 (BAUDR) |
| Continuous read | After setup, boot2 runs EBh quad I/O with `INST_L=0`, `ADDR_L=32`, `XIP_CMD=0xA0`. Each access sends 24 address bits plus the 0xA0 continuation code and no command byte | [DS] 4.10 |
| RX sample delay | SSI has a programmable RXD sample delay. It is needed when raising the SPI clock | [DS] 4.10.9.1.1 |
| Divider | 8-cycle signed/unsigned divide and modulo per core. Results are readable on the 9th cycle, and the CPU may do other work meanwhile | [DS] 2.3.1.5 |
| Interpolators | Two per core. Register read/write takes 1 cycle and results are ready the next cycle. `POPx` advances the state. INTERP0 has blend mode (8-bit alpha lerp between BASE0/BASE1). INTERP1 has clamp mode | [DS] 2.3.1.6-9 |
| Affine texture mapping | Official example: interp0 with `ADD_RAW`, lane shift/mask set to texture width/height bits, and `base[2]`=texture. `*(uint8_t*)interp0->pop[2]` gives one texel per iteration, "a single cycle iteration". **It needs power-of-two width and height** (mask fields) | [DS] 2.3.1.11 |
| "MODE7" note | "INTERP0 can provide a stream of tile lookups … and INTERP1 can provide offsets into the tiles" | [DS] 2.3.1.6 |
| Bootrom float (cycles, average) | fadd 71, fsub 74, fmul 58 (V2), **fdiv 71**, fsqrt 63, float2int 40, float2fix 39, int2float 55, fix2float 53, fsin/fcos about 580 | [DS] 2.8.3.2 Table 170 |
| Bootrom bit ops | `_clz32`, `_ctz32`, `_popcount32`, `_memcpy44`, `_memset4`. **The M0+ has no CLZ instruction** | [DS] 2.8.3.1 |
| 200 MHz | Certified since pico-sdk 2.1.1 with a regulator of at least 1.15 V (`SYS_CLK_MHZ=200`) | [SDK211] |

Consequences used throughout:
- An XIP miss fetches only **8 bytes**. A miss at 50 MHz SPI costs about 6 address clocks + 2 mode + 4 dummy + 16 data, so about 28 SPI clocks, or **about 112 system cycles (0.56 us)**. At 100 MHz it costs about 56 cycles. **Estimate.** The dummy-cycle count depends on the flash part. Measure it with `CTR_HIT`/`CTR_ACC` and a cycle counter.
- Soft float is not catastrophic per operation, at about 60-75 cycles. It is fatal inside per-pixel or per-segment loops. **fdiv costs the same as fmul** because the ROM uses the hardware divider.

---

## 1. Memory

### 1.1 Hot/cold split of BSP structures: read-only part in flash, a tiny mutable part in RAM

- **What.** Split each map struct (`mnode_t`, `mleaf_t`, `msurface_t`, `medge_t`, `mtexinfo_t`, `mplane_t`) into:
  - a **const, pointer-free** record built by the converter and stored in flash, with indices instead of pointers;
  - **per-frame or per-level mutable fields** in small RAM arrays indexed by the same number.

  This is the classic "structure splitting" of Chilimbi, Davidson and Larus (PLDI 1999). They split by access frequency (hot/cold) and moved cold fields behind an indirection, cutting cache misses by 10-27% (**verified, abstract**).

  Here the split is by mutability, not frequency. Both rp2040-doom and MG24Quake did exactly this:
  - rp2040-doom used a `should_be_const` typedef that becomes `const` on RP2040. "Level data is not instantiated in RAM", and texture metadata is resolved at render time (**verified** [RD-RAM]).
  - MG24Quake: "dynamic lighting and visited flags for nodes, surfaces and leaves" are the only parts that must stay in RAM (**verified** [MG24-habr]).
- **Mutable fields in id's structs** (WinQuake source):
  - `visframe` (node, leaf, surface)
  - `dlightframe` and `dlightbits` (surface)
  - `cachededgeoffset` (medge)
  - `efrags` (leaf)
  - `key` (leaf, used for the BSP sort key)
  - `cachespots` (surface cache, already gone)
  - `parent` is constant and can live in flash.
- **Layout inside flash.** An XIP miss fetches 8 bytes, so put the culling-hot fields of a node into one or two 8-byte lines:
  - `int16 minmaxs[6]` = 12 B
  - `int16 children[2]`
  - `uint16 plane`

  The cold fields (`firstsurface`, `numsurfaces`) go in a parallel array. Use the same idea for leaves (`contents`, `minmaxs`, `firstmarksurface`, `nummarksurfaces`, `visofs`).
- **Benefit.** RAM drops from 781 KB to a few KB plus the mutable arrays (see 1.2-1.3), about **-760 KB** (estimate). Speed cost: a few thousand XIP misses per frame during the BSP walk, about 0.5-2 ms (estimate). It can be measured with `CTR_ACC`/`CTR_HIT`.
- **Effort.** High: every `node->`, `surf->` and `leaf->` access site changes, and a converter is needed. The work is mechanical, and accessor macros keep the PC build identical.
- **Risk.** Low to medium. Correctness can be checked by hashing frames on PC against id, as was done for previous steps.
- **Attacks:** map structures 781 KB.

### 1.2 Per-frame marks as bitsets or small arrays instead of `int visframe`

- **What.** Replace `node/leaf/surface->visframe == r_visframecount` with one bit per item in a RAM bitset that is cleared at the start of the frame. The memset of `numleafs/8` bytes is negligible.
  - rp2040-doom replaced the per-object "valid count" with "an explicitly cleared bit-set". It also replaced rarely changing mutable fields with "a 'cleared' bit plus the original value in flash" (**verified** [RD-RAM]).
  - The generation-counter scheme (increment a frame counter instead of clearing) is how id does it (**verified** [FS-render]). It costs 4 B per item, while the bitset costs 1 bit.
  - Alternative that keeps id's semantics: a `uint8_t` generation per item, cleared on wrap every 256 frames. That uses 1 B per item and needs no per-frame clear.
- **Dynamic lights.** `dlightframe` and `dlightbits` on every surface (8 B each) can become a per-frame list of (surface index, bits) entries. Only surfaces touched by `R_MarkLights` appear in it, typically tens or hundreds.
- **`cachededgeoffset`.** It is only meaningful within the current frame. Store it as a `uint16` index into the frame's edge table plus a "fully clipped" flag bit, giving 2 B per `medge_t`. MG24Quake stores these offsets in temporary buffers: the first 10K in the texture cache buffer and the rest in the z-buffer (**verified** [MG24-gh]). This works because the array is dead outside `R_RenderView`.
- **Benefit.**
  - Bitsets: about 4 B → 1 bit per node, leaf and surface.
  - dlight fields: 8 B per surface → a small list.
  - edge cache: 4 B → 2 B per edge, or 0 B permanent if it lives in a reused buffer.

  The exact figure depends on map counts (unverified for E1M1; run the converter to count). Typically this is 10-40 KB of what would otherwise remain after 1.1 (estimate).
- **Effort.** Low to medium. **Risk:** low.
- **Attacks:** map structures.

### 1.3 Index compression: 16-bit indices, relative pointers, narrower types

- **What.**
  - rp2040-doom uses **16-bit pointers into main RAM**. This works because dynamic objects are 32-bit aligned, so 16 bits × 4 covers 256 KB, with 0 kept as NULL.
  - Flash arrays are referenced by 16-bit indices or byte offsets.
  - Some sets larger than 256 items were renumbered so that common references fit in 8 bits.
  - Precision was reduced, for example 14:2 fixed point for floor and ceiling heights.
  - Fields were reordered to pack well, but **without** `packed` structs, which are slow on M0+.
  - "Runtime flags that never change" became `#define`s.

  All of this is **verified** [RD-RAM]. MG24Quake uses the same scheme in its zone (16-bit block pointers, see the phase-1 notes).
- **Quake-specific cases:**
  - edge_t, surf_t and espan_t (see 4.6)
  - entity links (`area` links, `efrag` lists)
  - `cl_entities`, where MG24Quake packed `entity_t` to 8 bytes
  - `mtexinfo_t` vectors as fixed-point `int16`
- **Benefit.** It is spread across the static tables (478 KB) and edicts. On pointer-heavy structs it typically halves size (estimate).
- **Effort.** Medium. **Risk:** low with accessor macros.
- **Attacks:** static tables, edicts, map structures.

### 1.4 Edict compaction beyond per-class structs

- **What, verified:**
  - **MG24Quake** gives each QuakeC class its own C struct, allocated at its exact size in the zone. When the zone is full it recycles the oldest gibs or backpacks. It generates field getters and setters (phase-1 notes; [MG24-habr] "generator for getters/setters"). With QuakeC already compiled to C, the code generator can emit these accessors directly.
  - **rp2040-doom** split `mobj_t` into a **"static" variant and a "full" variant**. Simple decorations "may store position in flash" (**verified** [RD-RAM]). It also:
    - changed doubly linked lists to singly linked ones, without reordering, to stay demo-safe;
    - replaced function pointers with enums;
    - packed object pools 8 per block, using a spare byte for the allocation flags.
- **Quake mapping.**
  - A large share of edicts at load time never think and never move: `info_*`, path corners, `func_wall`, and lights removed or never touched by QuakeC.
  - Give them a "static edict" form: model index, origin and angles in **flash** (from the map's entity lump, pre-parsed by the converter), plus a 1-bit "removed/changed" flag in RAM. Promote one to a full edict on the first write.
  - Use 16-bit edict references (`EDICT_TO_PROG` as an index, not a byte offset).
  - Make string fields indices into a const string table. MG24Quake does this for model names in flash (**verified** [MG24-gh]).
- **Benefit.** 513 KB → about 30-50 KB of live edict memory on E1M1 (estimate; MG24Quake fits all edicts plus other data in a 48 KB zone, phase-1 notes).
- **Effort.** High, because generated accessors touch all game code. **Risk:** medium. Use the existing state-hash comparison against id's interpreter.
- **Attacks:** edicts 513 KB.

### 1.5 Bake per-map tables offline into flash (the "WHD" idea applied to Quake)

- **What rp2040-doom actually did (verified [RD-FLASH]):**
  - A `whd_gen` tool converts the WAD into a read-only `.WHD`/`.WXD` image: "Where's Half the Data". DOOM1.WAD shrinks from 4098K to 1758K. Everything is lossless except the sound effects, which use ADPCM.
  - The key constraint is **random access**: there is no RAM to decompress even one level. So it avoids LZ77, which needs a long decoded history, and uses the following instead:
    - narrower types and bit-packed fields;
    - per-instance flag bits that select a reduced encoding;
    - **varints** (7 bits + continuation) with zigzag for signed values;
    - context-dependent bit widths;
    - **per-entity Huffman** coding of sequences "tens of bytes" long.
  - Level lumps:
    - LineDefs save 62.6%, using vertex indices as deltas from a linear prediction.
    - SideDefs save 90%, using a 4-bit pattern selector.
    - Nodes save 50%, using child indices predicted as deltas and child bounding boxes on a 4-bit grid within the parent box.
    - Blockmap saves 68%, using a per-row base plus a presence bitmap.
    - Vertexes and Reject are not compressed.
  - Graphics save 56%:
    - Patches use one Huffman set per patch, plus 7 delta symbols (-3..+3) that exploit the palette's shade ordering, saving about 130K.
    - Identical columns are stored once.
    - **The 33 tinted palettes are dropped and recomputed at runtime** (97% saved).
  - Saved games are diffs against the level-load state, typically 1-2 KB instead of 10-60 KB.
- **Quake transfer.** Use the converter to bake, per map:
  - the `mod_known` model table (97 KB of static RAM; MG24Quake builds it in the z-buffer and freezes it into internal flash, phase-1 notes);
  - pre-parsed entity lump fields (see 1.4);
  - precache lists;
  - node bounding boxes quantized relative to the parent (the rp2040-doom node trick works directly on Quake's `int16 mins/maxs`).

  Use the delta-from-snapshot save game format, since a flash save slot is small.
- **Benefit.** `mod_known` -97 KB. Flash savings on node and leaf data are about 50% (ratio by analogy to Doom's nodes, unverified for Quake).
- **Effort.** Medium (converter work). **Risk:** low.
- **Attacks:** static tables, flash budget.

### 1.6 Other static-table items with documented fixes

| Item | Fix | Source | Benefit | Effort / risk |
| --- | --- | --- | --- | --- |
| Sky buffers (`newsky` 32 KB, `bottomsky`/`bottommask` 16 KB, about 65 KB total) | Sample both sky layers per pixel directly from flash in the span loop, with no composite buffer. Or pre-render one composite layer in flash with no scroll. MG24Quake still spends 48 KB on sky (2×128² + output) | [MG24-habr] (verified 48 KB) | -48 to -65 KB | low / low (visual change) |
| Mixer volume table (32 KB) | Use the single-cycle `MULS`: `sample * vol >> 8` per sample instead of the table. rp2040-doom keeps only small tables (OPL about 4 KB) in RAM | [RD-RAM] (pattern) | -32 KB, about +1 cycle per sample | low / low |
| Particles (88 KB) | 2048 → 256-512 particles with `int16` fixed-point fields (about 16 B each) | engine constant; estimate | -80 KB | low / low |
| Net sockets and buffers (81 KB) | Single-player loopback only. MG24Quake dropped the radio and network (+20 KB) | [MG24-habr] | -75 KB or more | low / low |
| gfx.wad (110 KB) | Read in place from flash, like the other lumps | already a pattern | -110 KB | low / low |
| Colormap (16 KB) | Keep in **SRAM**. Random per-pixel reads would thrash the 16 KB XIP cache. MG24Quake caches it in RAM only when zone space allows (phase-1 notes). Optionally use 32 light rows (8 KB) | estimate | 0 or -8 KB | low / medium (banding) |

### 1.7 Buffer sharing and "data overlays" (no MMU, so overlays mean union-by-phase)

- **What.**
  - Code overlays buy nothing for RAM, because code already runs from XIP. The useful form is **phase-exclusive data**: level-load scratch, the runtime edge and span tables, and the save/load buffers all share one region.
  - rp2040-doom: "buffers are reused across phases when their lifetimes don't overlap". It merged zone and malloc into one heap and cut allocation overhead from 20 B to 8 B per block (**verified** [RD-RAM]).
  - MG24Quake reuses the z-buffer for the edge tables, the PVS bit arrays, the warp copy, the status bar and level-load temps (phase-1 notes).
  - Extra RAM to harvest on RP2040:
    - 4 KB of USB DPRAM if USB is unused (**verified** [DS] 2.6.2.1);
    - the two 4 KB scratch banks X/Y, ideal for the core stacks or span-loop code, since they avoid bank contention (**verified** [DS] 2.6.2).

    The 16 KB XIP cache as SRAM is **not** worth it, because everything executes from flash (rp2040-doom rejected it, **verified** [RD-RAM]).
- **Benefit.** +4 KB, and arbitrarily large savings from phase unions. **Effort:** low. **Risk:** low.
- **Attacks:** all RAM items.

---

## 2. Flash / XIP

### 2.1 Raise the QSPI clock from 50 MHz to 100 MHz (÷2), if the flash part allows it

- **What.** At 200 MHz the SSI divider must be even, so the choices are 50 or 100 MHz (**verified** [DS]). The default boot2 divider is 4 (**verified** [DS] note on `PICO_FLASH_SPI_CLKDIV`). rp2040-doom runs its flash at **135 MHz** (270 MHz ÷2), and its README says the flash chip must be capable of that (**verified** [RD-intro]).
- **Benefit.** Every XIP miss and every streaming read gets about 2x faster. Miss cost falls from about 112 to about 56 cycles (estimate). That probably buys several ms/frame, because texture, lightmap and BSP reads are miss-heavy.
- **Effort.** Low: board config plus `PICO_FLASH_SPI_CLKDIV=2`, possibly with RX sample delay tuning.
- **Risk.** **Medium.** It depends on the flash chip rating (many W25Q parts are rated about 104-133 MHz for EBh; the exact part on this board is unverified), board trace length and the vendor's support position. It is not a CPU overclock, but it is still outside the vendor's validated configuration. Test with a flash read-back CRC over all 16 MB across temperature.
- **Attacks:** flash read time in every step.

### 2.2 Use the XIP aliases deliberately: non-allocating reads for streaming data

- **What.** The 0x11 alias checks the cache but doesn't allocate on a miss. The 0x13 alias bypasses it (**verified** [DS]). Read one-shot data (sound samples being mixed, pre-baked sky, demo streams, alias frame vertices for a single draw) through **0x11/0x13**. That way it doesn't evict the code, BSP and colormap-adjacent hot lines from the 16 KB cache.
- **Caveat.** rp2040-doom tried to control what gets cached and found it "hard and often counterproductive", so it left the cache alone (**verified** [RD-RAM]). So treat this as a measured experiment, not a default.
- **Benefit.** Unknown until measured; probably 0-10% fewer code misses (unverified). **Effort:** low: a pointer OR with 0x01000000. **Risk:** low.
- **Attacks:** ms/frame.

### 2.3 Stream the next texture mip into an SRAM tile buffer with XIP_STREAM + DMA

- **What.**
  - MG24Quake sorts surfaces by texture. It DMA-loads the **next** texture while the CPU renders the current surface with the previous one, and its authors say this gained more in Quake than in their Doom port (**verified** [MG24-habr]). Its texture cache buffer is 20 KB (**verified** [MG24-gh]).
  - On RP2040 the correct mechanism is **`XIP_STREAM` + DMA from the XIP_AUX FIFO**. It runs in the background, yields to CPU XIP misses, and never stalls the DMA (**verified** [DS] 2.6.3.4).
  - With the texture in SRAM, interp0's affine-texture mode (3.3) runs at full speed, and the inner loop has no XIP misses at all.
  - Quake's `D_DrawSurfaces` already walks surfaces in an order that can be grouped by texinfo/texture within a frame (rp2040-doom does the analogous "draw visplanes sharing a flat together" ordering, **verified** [RD-RENDER]).
- **Details.**
  - Only stream the selected mip level. At 240x150 most surfaces use mip 1-3; mip1 of a 128×128 texture is 4 KB.
  - Textures whose mip0 exceeds the buffer (sky 256×128, some 128×128 at mip0) fall back to the XIP path.
  - Double-buffer two 8-10 KB slots.
- **Bandwidth.** XIP_STREAM performs repeated 32-bit XIP reads. Each pays the address and continuation overhead, roughly 20 SPI clocks per 4 bytes, so about 10 MB/s at 50 MHz and about 20 MB/s at 100 MHz (**estimate, unverified**). MG24Quake measured 17 MB/s on external SPI (**verified** [MG24-habr]). If too slow, drive the SSI directly with a long EBh burst via DMA, as rp2040-doom does for its 5 KB status bar ("streams from flash by DMA", **verified** [RD-RENDER]). That requires pausing XIP, so it is a bigger change.
- **Benefit.** Removes the XIP misses on texel fetches. Per-pixel texture cost goes from "1 cycle hit / about 112-cycle miss" to a steady 2-cycle SRAM load. Plausibly 3-8 ms/frame on texture-heavy views (estimate; measure the miss rate first).
- **Effort.** Medium. **Risk:** low to medium (stalls when one surface's texture is huge).
- **Attacks:** ms/frame (span loop).

### 2.4 Tiled (blocked) texture layout in flash for the XIP fallback path

- **What.** Hakura & Gupta (ISCA 1997) showed that "a blocked representation is required to fully exploit spatial locality and to avoid any dependency on the orientation of textures". Large images must be padded or blocked at a coarser level to avoid conflicts (**verified**, abstract and summary).
  - With an 8-byte XIP line, a row-major texture walked vertically misses on every pixel.
  - A 4×2 texel block per 8-byte line (or 8×8 = 8 lines) makes hits independent of orientation.
- **Trade-off.** Blocked addressing is not a single interp0 `POP2`. The pop sums lane0 + lane1 + base2 and cannot interleave bit fields. A 4×2 block layout needs about 3-4 extra ALU instructions per pixel. Prefer 2.3 (stream to SRAM row-major) for the common case, and use blocked layout only for textures too big to stream.
- **Alternative.** Store the **mip used for distant surfaces** (2-3) row-major. These are small (e.g. 32×32 = 1 KB), so they become cache-resident by themselves.
- **Benefit.** Fewer misses on the fallback path (unquantified). **Effort:** medium (converter plus a second span loop). **Risk:** medium.
- **Attacks:** ms/frame.

### 2.5 Lightmap reads: sample less often, not faster

- **What.** Lightmaps are in flash and are touched per pixel by the current bilinear loop. This is 4 reads per pixel, mostly cache hits but scattered. See 4.1: sampling at segment endpoints cuts lightmap reads by 8-16x. Quake's surfaces carry at most 18×18 light samples (surface extents ≤ 256 texels / 16 + 1), so a per-surface **copy of the lightmap into a small SRAM buffer** (≤ 324 B × styles) at surface start is also cheap.
- **Benefit.** See 4.1. **Effort:** low. **Risk:** low.

---

## 3. Fixed point / no FPU

### 3.1 Price list for soft float on this chip (verified inputs, estimated totals)

- ROM averages: fadd 71, fmul 58, fdiv 71, float2int 40 cycles ([DS] Table 170). The SDK wrappers add a call or veneer.
- id's `D_DrawSpans8` (WinQuake `d_scan.c`) does the following per 8-pixel segment:

  ```
  sdivz += sdivz8stepu; tdivz += …; zi += …;   // 3 fadd
  z = (float)0x10000 / zi;                      // fdiv
  snext = (int)(sdivz * z) + sadjust;           // fmul + f2i + add
  tnext = (int)(tdivz * z) + tadjust;           // fmul + f2i + add
  ```

  That is about 3×71 + 71 + 2×58 + 2×40 ≈ **480-620 cycles per 8 pixels, so 60-78 cycles/pixel**. At 36,000 px that is **about 11-14 ms/frame of pure soft float** in the world span loop (estimate).
- Per-surface setup (`D_CalcGradients`, about 30 float operations) × about 300 surfaces is about 0.6 M cycles, or 3 ms (estimate).
- Per-node culling (`R_RecursiveWorldNode`: a plane dot product plus the bbox-vs-frustum test with 4 planes) × 1000-3000 nodes is about 1-4 ms (estimate).
- Edge setup (`R_ClipEdge`, `R_EmitEdge`, about 20-40 float operations per edge) × 1000-2000 edges is about 2-5 ms (estimate).

### 3.2 Fixed-point perspective in the span loop, using the hardware divider

- **What.**
  - Quake's own design: compute exact s,t at every 16th pixel (8 in the C version) from screen-linear 1/z, s/z and t/z, and interpolate linearly in between. On Pentium the FDIV overlapped with drawing 16 pixels (**verified**, Abrash Black Book ch. 70 [BB70]).
  - Chris Hecker's Game Developer series (Parts I-V, 1995-96) is the standard derivation of the math and the approximations (subdivision, affine error) ([Hecker]; Part IV, "approximations", not read in full, so its content is unverified).
  - On RP2040: keep `zi`, `sdivz` and `tdivz` as 32-bit fixed point (they step linearly). At each segment endpoint compute **s = sdivz / zi and t = tdivz / zi with two hardware divides** (8 cycles each, and the CPU can set up the next one meanwhile, **verified** [DS]).
- **Why divide twice instead of reciprocal + multiply.** The M0+ `MULS` keeps only the low 32 bits, and there is no `SMULL`. A 16.16 × 16.16 product needs a multi-instruction 64-bit sequence. rp2040-doom wrote a custom 16:16 multiply that "skips discarded cross-terms" for this reason (**verified** [RD-RAM]).
  - Two 32/32 divides with a pre-scaled dividend avoid 64-bit products entirely.
  - Scale per surface: the converter or `D_CalcGradients` picks shifts so that `sdivz << k` fits 31 bits and the quotient comes out in about 12.4 texel fixed point.
- **Why not table + Newton-Raphson.** Without a CLZ instruction, normalisation needs `_clz32` (a ROM call) or a branch tree. NR then needs 2 multiplies with the same 64-bit problem. The 8-cycle divider wins (estimate; the standard NR step is described in [NR-patent]).
- **Segment length.** At 240 px width, 16-pixel segments halve the divide count again.
- **Benefit.** About 60-78 → about 3-5 cycles/pixel for the perspective part, so **about 10-13 ms/frame** (estimate). This is likely the single biggest speed item.
- **Effort.** Medium (the span loop plus `D_CalcGradients` in fixed point; keep the float version on PC for A/B).
- **Risk.** Medium: precision. Quake already clamps s,t (`sadjust`, `bbextents`) against overflow, and the same clamps are needed. Divider state must be saved if IRQ handlers also divide; the SDK provides save/restore (`hw_divider_save_state`) (unverified whether the default IRQ path handles it).
- **Attacks:** ms/frame.

### 3.3 Interpolators for texel address generation (and for the colormap row)

- **What.** The official affine example produces a texel address per `POP2` from u/v accumulators with masks for power-of-two sizes (**verified** [DS] 2.3.1.11, `hello_interp.c`). Between two perspective-correct endpoints a Quake span segment is exactly affine, so:
  - at each segment start, write `accum0=s`, `base0=sstep`, `accum1=t`, `base1=tstep`;
  - each pixel is then `texel = *(u8*)interp0->pop[2]`.
- **Lighting.** Use **interp1** as a second accumulator for light. With `base2 = colormap`, a shift/mask of light into the row bits and the light step as `base0`, `interp1->pop[2]` yields `&colormap[row]`, and the pixel is `row_ptr[texel]`.
- **Loop cost.** The inner loop becomes about: 2 IOPORT loads (1 cycle each) + ldrb texel + ldrb colormap + strb, so roughly 7-9 cycles/pixel including the loop overhead (estimate; unrolled ×8).
- **Limits.**
  - Masks need **power-of-two** texture dimensions. Quake textures are multiples of 16, and most are power-of-two (unverified that all are). The converter can pad or resample the exceptions, or they can use a C fallback.
  - The interpolators are per core, so both cores can run span loops in parallel (see 5). Save and restore them if used in IRQs (SDK `interp_save`/`interp_restore`).
  - The datasheet explicitly suggests MODE7-style use: interp0 for tile lookups, interp1 for in-tile offsets.
- **Benefit.** Versus a C loop with shifts and masks for s,t wrap and light, probably 4-8 cycles/pixel saved, so **about 0.7-1.5 ms/frame** (estimate). It also frees registers, which rp2040-doom notes ("reduce register pressure", **verified** [RD-RAM]).
- **Effort.** Low to medium. **Risk:** low.
- **Attacks:** ms/frame.

### 3.4 Fixed-point culling and edge setup

- **What.** Planes and vertices can be pre-converted by the converter: integer world units × 1.14 normals fit 32-bit products for Quake's ±4096 coordinates.
  - Axial planes (`type < 3`) need no multiply at all, and id already special-cases them.
  - Convert `R_RecursiveWorldNode` bbox culling, `R_ClipEdge` and the projection to fixed point. The perspective projection is one divide per vertex, using the hardware divider.
  - Precision headroom: use conservative culling epsilons.
- **Benefit.** About 3-8 ms/frame across culling, edges and surface gradients (estimate from 3.1). It also removes float from the converter's flash structures, so planes take 8 B instead of 20 B.
- **Effort.** High: many files, and edge-sort precision matters. Quake's edge `u` is already 16.16 fixed. **Risk:** medium: precision cracks or sort errors. Use frame-hash A/B on PC.
- **Attacks:** ms/frame, flash size.

---

## 4. Rendering cuts

### 4.1 Light per segment endpoint instead of per pixel

- **What.** Quake's lightmap is a 16-texel grid, and "lighting values are linearly interpolated between the light map's 16-texel grid points" (**verified** [BB68], [BB70]). The surface cache did this in texture space at 2.25 cycles per texel (**verified** [BB68]).
  - Without a cache, the equivalent cheap approximation is to compute the bilinear light **once per segment endpoint** (every 8/16 px, where s,t are exact anyway). Then step it linearly in screen space across the segment, using interp1 as in 3.3.
  - The error is the same order as the affine-texture error between endpoints, and Quake already accepts that for texels.
  - MG24Quake computes lighting per pixel with an assembly bilinear interpolation (**verified** [MG24-habr]), the current approach here. It runs at 136 MHz with a single-cycle MAC and an FPU, which this chip lacks.
- **Variants.**
  - Per-surface flat light (one value from the lightmap mean) for surfaces at mip ≥ 2 (distant). Light varies at most a few steps across a small on-screen polygon.
  - Dynamic lights: add dlight contributions only at segment endpoints too.
- **Benefit.** Removes about 4 lightmap reads and 3 lerps per pixel, leaving a 1-cycle add. That is about **15-25 cycles/pixel, or 3-4.5 ms/frame** (estimate).
- **Effort.** Low. **Risk:** low (small visual difference; measure against id frames as before).
- **Attacks:** ms/frame.

### 4.2 Bias mip selection toward smaller levels

- **What.** Quake chooses the mip so that the texel:pixel ratio is about 1:1-1:2 (**verified** [BB68]). WinQuake has the `d_mipscale` and `d_mipcap` cvars to bias or cap it (engine source).
  - A bias of +0.5 to +1 level at 240x150 gives fewer distinct texels touched, so fewer XIP misses and smaller DMA tiles (2.3), at a slight blur.
  - Capping at mip ≥ 1 for all but the nearest surfaces also lets the converter **drop mip0 for some textures** (6.2).
- **Benefit.** Fewer misses and smaller streams; 1-3 ms on texture-bound views (estimate). **Effort:** trivial. **Risk:** low (visual).
- **Attacks:** ms/frame, flash.

### 4.3 Longer affine segments for distant or flat-on surfaces; affine distant models

- **What.** When `|d_zistepu|·N` is small relative to `zi` (distant or screen-parallel surface), perspective error over N px is sub-texel. Use 32-pixel or whole-span affine segments, chosen per surface in `D_CalcGradients`.
  - For alias models Quake already switches distant models to the recursive-subdivision affine rasterizer, which Abrash measured as "about 3 times faster" (**verified** [BB70]; cvar `r_aliastransition` in the engine).
  - Lower the transition distance at 240x150.
- **Benefit.** Fewer divides on far surfaces, about 0.5-2 ms after 3.2 (estimate). Models: 1-3 ms in monster-heavy scenes (estimate).
- **Effort.** Low. **Risk:** low (texture swim on large near-flat floors if the threshold is too loose).
- **Attacks:** ms/frame.

### 4.4 Lazy z-buffer fill (only where entities can appear)

- **What.** After world spans are drawn, Quake writes 1/z for every world pixel (write-only, no compare) so entities can z-test. Abrash puts this at about 10% of cost (**verified** [BB70]).
  - Project each visible entity's (alias model, sprite, particle cluster) bbox before world drawing. Write z only for span parts that intersect the union of those screen rectangles.
  - In corridors with no monsters, z-fill drops to almost nothing.
- **RAM angle.** The z-buffer at 240x150 is 72,000 B. Its unused regions can host the edge tables and temps, as MG24Quake already does (phase-1 notes).
- **Benefit.** Up to about 0.5-1 ms (estimate). **Effort:** low to medium. **Risk:** low (the particle bbox must be conservative).
- **Attacks:** ms/frame.

### 4.5 Keep PVS + frustum + edge sort, and tune the limits to 240x150

- id's edge sorter keeps the worst:best drawing time ratio at about 4:1, versus 10:1 or more for plain polygon drawing, and removes overdraw (**verified** [BB70]). That predictability is valuable at 10-15 fps.
- "PVS-only" culling (dropping the frustum test) would raise the polygon count 2-3x, because the PVS is already loose (**verified** [BB70]: PVS lets "two or three times as many polygons" through). Not recommended.
- Abrash notes about 500 potentially drawable polygons per Quake level view (**verified** [BB70]). At 240x150 the span counts drop proportionally to height × fragmentation.
- **Action.** Size `NUMSTACKSURFACES`, `NUMSTACKEDGES` and `MAXSPANS` from measured E1-E4 peaks at 240x150, not id's 800/2400/3000. Instrument the high-water marks on the PC build.

### 4.6 Smaller edge/span tables (id vs MG24 vs RP2040 at 240 px)

Sizes with 32-bit pointers, from WinQuake `r_shared.h`:

| Table | id (per entry × count) | id total | MG24Quake (phase-1 notes) | Possible at 240x150 |
| --- | --- | --- | --- | --- |
| `edge_t` | 32 B × 2400 (`NUMSTACKEDGES`) | 76.8 KB | in z-buffer | 16-20 B: `u`/`u_step` 16.16 kept; prev/next/nextremove/owner as `uint16`; `surfs[2]` u16; `nearzi` as u16 or dropped |
| `surf_t` | 64 B × 800 | 51.2 KB | 28 B × 600 = 16.8 KB | 24-28 B × measured peak |
| `espan_t` | 16 B × 3000 | 48 KB | 4 B bitfield (`next:13, u:9, count:9`) + 1 B v × 3650 = 18.3 KB | 4 B (`u:8, count:8, next:16`) since width ≤ 255; v as a separate byte |

- **Benefit.** On top of MG24's numbers, `u`/`count` fit 8 bits at 240 px. This frees one bitfield and removes masking cost.
- **Effort.** Medium (r_edge.c is intricate). **Risk:** medium.
- **Attacks:** stack/temp RAM.

---

## 5. Dual core

### 5.1 First check whether scanout actually needs core 1's CPU

- **What.** A 320x200 RGB565 frame is 128,000 B. At a 62.5 MHz SPI LCD that is about 16.4 ms of wire time, so the 22.4 ms is probably mostly SPI-bound (estimate; the LCD SPI clock is unverified).
  - If core 1 busy-waits on SPI, restructure it: core 1 expands and scales one line (8-bit palette → RGB565, 240 → 320 horizontal, 150 → 200 vertical by line repeat) into a ping-pong line buffer, and **DMA** sends the previous line to the SPI/PIO.
  - Palette expansion at 2-3 cycles/pixel × 64,000 px is about 1 ms of real CPU per frame.
  - rp2040-doom does exactly this split: core 1 handles video via IRQs (a per-line DMA setup IRQ plus a scanline-buffer refill IRQ feeding a small circular queue) and is otherwise free for work (**verified** [RD-RAM]).
- **Benefit.** Turns about 20 ms per frame of core-1 time into available compute for 5.2, without changing the panel timing. Potentially the largest dual-core win (estimate).
- **Effort.** Low to medium. **Risk:** low.
- **Attacks:** ms/frame (wall clock).

### 5.2 Core 1 as a synchronised render worker (rp2040-doom model)

- **What.**
  - rp2040-doom: core 1 is "synchronized with core 0 by semaphores" and **sits idle during game logic, so no shared state needs mutexes**. During rendering it generates audio and then pulls work from a shared queue. Core 0 can take over audio if it finishes first (**verified** [RD-RAM]).
  - In its renderer, columns are sorted into per-x lists with no overlap, so they can be drawn in any order and split between cores. Both cores pull sprite and wall patches from one shared queue (**verified** [RD-RENDER]).
- **Quake mapping.**
  - **Core 0:** BSP walk, edge sort, `R_ScanEdges` (span generation). It is sequential by scanline.
  - **Core 1:** `D_DrawSurfaces`, the span fill: texture, light, z-fill. Each core has its own interpolators and divider (**verified** [DS]), so the span loop runs at full speed on core 1.
  - id already flushes spans in batches (`D_DrawSurfaces` is called when the span pool fills), so a **double-buffered span pool** gives a natural producer/consumer pipeline. Core 0 fills pool B while core 1 draws pool A.
  - Then alias models: core 0 transforms and projects, core 1 rasterises, or split the screen in halves for `D_PolysetDraw`.
  - Sound mixing goes to whichever core is idle (rp2040-doom pattern).
- **Contention.** Both cores share one QSPI. Simultaneous XIP misses serialise, which is another reason for 2.3. Use the 4 KB scratch banks X/Y for each core's stack and hot loop to avoid SRAM bank conflicts (**verified** [DS] 2.6.2).
- **Benefit.** If span drawing is about 40-60% of render time, overlapping it gives about **25-40% shorter frames** (estimate).
- **Effort.** Medium to high. **Risk:** medium (sync bugs; mitigated by the "core 1 idle during game logic" rule).
- **Attacks:** ms/frame.

### 5.3 Single framebuffer, with game logic overlapped with scanout

- **What.** A second 240x150 buffer costs 36 KB. Instead, order the frame like this:
  1. render;
  2. start scanout (core 1 plus DMA);
  3. while scanout runs, core 0 does server physics, the compiled QuakeC think functions, client parsing and sound. **None of these write the framebuffer.**
  4. Then render the next frame.

  rp2040-doom ran out of room for full double buffers. It uses 320x168 buffers and single-buffers full-screen pages across two buffers (**verified** [RD-RENDER]).
- **Benefit.** -36 KB versus double buffering, with no tearing. **Effort:** low. **Risk:** low, as long as game logic takes about 22 ms or more. Otherwise race-the-beam rendering top to bottom is the fallback.
- **Attacks:** RAM.

---

## 6. Data trimming for flash (16 MB)

| Idea | Source / evidence | Benefit | Effort / risk |
| --- | --- | --- | --- |
| **Deduplicate textures across BSPs** into one shared texture pool. Each Quake .bsp embeds its own copies | standard BSP format; the saving size is unverified | likely MBs across E1-E4 (unverified) | medium / low |
| **Drop mip0** for textures whose mip0 is never chosen at 240x150 with 4.2 bias. mip0 is about 75% of a miptex (64/85) | arithmetic | up to about 75% of texture bytes for those textures | low / low to medium (blur up close) |
| **ADPCM sounds** (4-bit, as rp2040-doom does with ADPCM-XQ; 49% saved, lossy) | [RD-FLASH] verified | about -50% of sound bytes | low / low |
| **Keep 11 kHz 8-bit**, and convert the few 22 kHz sounds offline (MG24Quake resampled to 11,025 Hz 8-bit) | [MG24-habr] verified | small | trivial |
| **Recompute palette tints** (damage, pickup, underwater blends) at runtime, at most once per frame, instead of storing tables (rp2040-doom drops 33 palettes, 97%) | [RD-FLASH] verified | small (KB) | low |
| **4-bit lightmaps** (Quake uses 64 light rows; 4 bits gives 16 levels) | estimate | -50% of lightmap bytes | low / medium (banding) |
| **Delta-coded saves** from the level-load state | [RD-FLASH] verified | saves fit in one flash sector | medium / low |
| **Random-access compression** of node, leaf, edge and vertex lumps (varint, delta from prediction, 4-bit child boxes) | [RD-FLASH] verified for Doom | about 50% of those lumps (by analogy) | medium / medium (decode cost per access) |
| **Per-map asset manifest** from the compiled QuakeC precache calls, so the converter can lay out each map's data contiguously and skip unused assets | design | layout and less flash | low / low |

---

## 7. Ranked top 10 next steps

Ranked by benefit per effort for the stated goal (fit in about 245 KB, then reach 10-15 fps), with the RAM-critical items first because nothing runs until it fits.

| # | Step | Attacks | Benefit (estimate) | Effort | Risk |
| --- | --- | --- | --- | --- | --- |
| 1 | **Hot/cold split of BSP structs**: const, pointer-free records in flash built by the converter, plus mutable RAM arrays (1.1) | map 781 KB | about -760 KB | high | low-med |
| 2 | **Per-frame marks as bitsets or u8 generations; dlight list; `cachededgeoffset` as u16 in a reused buffer** (1.2). Do it together with #1 | map remainder | -10 to -40 KB | low-med | low |
| 3 | **Edicts: per-class structs plus a "static edict" form in flash plus 16-bit refs** (1.4, MG24 + rp2040-doom static mobj) | edicts 513 KB | about -470 KB | high | med |
| 4 | **Quick static wins**: net and sockets out, particles to 256 × 16 B, gfx.wad in flash, mixer volume table → MULS, sky drawn from flash, `mod_known` baked per map (1.5, 1.6) | static 478 + 88 + 81 + 110 KB | about -450 KB | low | low |
| 5 | **Fixed-point span perspective with two hardware divides per 16-px segment** (3.2) | span loop | -10 to 13 ms | med | med |
| 6 | **Light at segment endpoints, stepped linearly (interp1), per-surface lightmap copy to SRAM** (4.1, 2.5) | span loop | -3 to 4.5 ms | low | low |
| 7 | **Interp0 texel addressing plus interp1 colormap row, unrolled span loop in SRAM (scratch bank)** (3.3, 1.7) | span loop | -0.7 to 1.5 ms plus register relief | low-med | low |
| 8 | **Free core 1 from scanout busy time (DMA the line out), then make it a semaphore-synced span-fill worker with a double-buffered span pool** (5.1, 5.2) | wall clock | -25 to 40% frame time | med-high | med |
| 9 | **Texture mip streaming via XIP_STREAM + DMA into double SRAM slots, surfaces grouped by texture, mip bias +0.5 to 1** (2.3, 4.2) | texel misses | -3 to 8 ms (measure the miss rate first) | med | low-med |
| 10 | **Fixed-point culling, edge setup and gradients** (3.4), then **measure QSPI ÷2 (100 MHz)** (2.1) with the vendor's consent and a full-flash CRC test | setup cost, all flash reads | -3 to 8 ms; ÷2 halves miss cost | high / low | med / med |

Measurement first, for every speed item: build an on-target profiler from the SysTick or timer counter plus the XIP `CTR_HIT`/`CTR_ACC` counters ([DS] 2.6.3.2). The ms estimates above are arithmetic from verified cycle counts, not measurements on this code.

---

## Sources

- [DS] Raspberry Pi Ltd, *RP2040 Datasheet*. Sections 2.3.1.5 (divider), 2.3.1.6-2.3.1.11 (interpolator, blend/clamp, affine texture mapping example), 2.6.2-2.6.3 (SRAM banks, XIP cache, aliases, flush, SSI, flash streaming), 2.8.3.1-2.8.3.2 (bootrom bit ops, fast float table 170), 4.10 (SSI: XIP_CMD 0xA0 continuation, SCKDV even, RXD sample delay). https://datasheets.raspberrypi.com/rp2040/rp2040-datasheet.pdf (resolves to https://pip-assets.raspberrypi.com/categories/814-rp2040/documents/RP-008371-DS-2-rp2040-datasheet.pdf)
- [SDK211] pico-sdk 2.1.1 release notes (200 MHz certification, `SYS_CLK_MHZ=200`, regulator ≥ 1.15 V). https://github.com/raspberrypi/pico-sdk/releases/tag/2.1.1 ; coverage: https://www.tomshardware.com/raspberry-pi/the-raspberry-pi-picos-rp2040-has-been-certified-for-200-mhz-clock-speeds-up-from-133-mhz
- pico-sdk hardware API docs (interp, divider, xip_cache). https://www.raspberrypi.com/documentation/pico-sdk/hardware.html ; runtime docs: https://www.raspberrypi.com/documentation/pico-sdk/runtime.html
- [RD-intro] G. Sanderson (kilograham), *RP2040 Doom*, introduction. https://kilograham.github.io/rp2040-doom/
- [RD-FLASH] *RP2040 Doom: Making It All Fit In Flash* (WHD/WXD format). https://kilograham.github.io/rp2040-doom/flash.html
- [RD-RAM] *RP2040 Doom: Making It Run Fast And Fit in RAM*. https://kilograham.github.io/rp2040-doom/speed_and_ram.html
- [RD-RENDER] *RP2040 Doom: Rendering And Display Composition*. https://kilograham.github.io/rp2040-doom/rendering.html
- rp2040-doom source. https://github.com/kilograham/rp2040-doom
- [MG24-gh] next-hack, MG24Quake repository and README. https://github.com/next-hack/MG24Quake
- [MG24-habr] next-hack, Quake port to Arduino Nano Matter / SparkFun Thing Plus Matter (English article on Habr). https://habr.com/en/articles/848396 . The original is at https://next-hack.com/index.php/2024/09/22/quake-port-to-sparkfun-and-arduino-nano-matter-boards-using-only-276-kb-ram/ (returned HTTP 500 when fetched). Follow-up: https://next-hack.com/index.php/2024/11/02/now-even-the-full-retail-quake-version-runs-on-the-arduino-nano-matter-board-and-it-is-faster-as-well/
- Hackaday, *Quake In 276 KB Of RAM*. https://hackaday.com/2024/10/05/quake-in-276-kb-of-ram
- [BB68] M. Abrash, *Graphics Programming Black Book*, ch. 68 (Quake's lighting model, surface caching, 16-texel lightmap grid, 600K surface cache, 2.25 cycles/texel). https://github.com/jagregory/abrash-black-book/blob/master/src/chapter-68.md
- [BB70] M. Abrash, *Graphics Programming Black Book*, ch. 70 (Quake post-mortem: 16-pixel perspective subdivision with FDIV overlap, edge list 4:1, PVS looseness, z-buffer fill about 10%, distant models about 3x faster). https://github.com/jagregory/abrash-black-book/blob/master/src/chapter-70.md
- Abrash, *Quake's Hidden-Surface Removal*. https://archive.gamedev.net/archive/reference/articles/article656.html
- [FS-render] F. Sanglard, *Quake Engine code review: Rendition* (R_MarkLeaves, visframe counter, PVS compression). https://fabiensanglard.net/quakeSource/quakeSourceRendition.php
- id Software, Quake source (WinQuake `r_shared.h`, `d_scan.c`, `d_local.h`, `r_main.c`). https://github.com/id-Software/Quake
- [Hecker] C. Hecker, *Perspective Texture Mapping*, Parts I-V, Game Developer 1995-96. https://chrishecker.com/Miscellaneous_Technical_Articles (Part IV: https://www.chrishecker.com/images/3/37/Gdmtex4.pdf)
- T. Chilimbi, B. Davidson, J. Larus, *Cache-Conscious Structure Definition*, PLDI 1999. https://ftp1.cs.wisc.edu/wwt/pldi99_cache_def.pdf
- Z. Hakura, A. Gupta, *The Design and Analysis of a Cache Architecture for Texture Mapping*, ISCA 1997. https://graphics.stanford.edu/papers/texture_cache/
- [NR-patent] Newton-Raphson reciprocal refinement (background for 3.2). https://image-ppubs.uspto.gov/dirsearch-public/print/downloadPdf/10146504
- element14 forum, *Probing the RP2040 QSPI flash interface* (EBh continuous mode in boot2; anecdotal performance). https://community.element14.com/products/raspberry-pi/f/forum/52035/probing-the-rp2040-qspi-flash-interface

## Addendum: how RP2350-Quake fits (from its own docs, 2026-11-22)

It does not fit 264 KB. It allocates **439,248 bytes of SRAM** on the RP2350, which has 520 KB: 32 + 4 KB of stacks, a 128 KB game zone (43 KB peak), a 32 KB metadata pool (14 KB peak), and about 91 KB of libc heap headroom. Its main savings come from the MG24Quake engine: per-class edict structs (monster 212 B, player 312 B, trigger 116 B, against stock Quake's uniform ~876 B), immutable BSP and model data baked into XIP at fixed addresses, deduplicated textures, 400 edicts, 512 particles, 1,500 edges, 4-bit ADPCM sound with 8 channels, and no saves or multiplayer. For our 245 KB, per-class edicts are the largest remaining lever. They are generated from QuakeC, so they wait for registered data.
