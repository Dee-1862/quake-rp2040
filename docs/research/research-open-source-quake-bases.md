# Open-source Quake codebases as a starting point for an RP2040 port

Research date, 2026-11-22. Target, a single RP2040 (2x Cortex-M0+ at 200 MHz, **no FPU**, no 32x32->64 multiply instruction, ~232 KB usable of 264 KB SRAM, 16 MB QSPI flash via XIP at 50 MHz with a 16 KB XIP cache, no PSRAM, 320x240 SPI LCD).

Verification legend.
- **[V]**, URL fetched or returned HTTP 200 during this research, and the details come from the page or the local clone.
- **[P]**, the page exists, but some details (licence, RAM, fixed-point use) were not stated on it and are inferred.
- **[U]**, unverified. Either the source was not found, the site blocked fetching, or the information comes from third-party write-ups only.

---

## 1. Key finding up front

No published Quake port targets a Cortex-M0/M0+ or any other FPU-less microcontroller **without external RAM**. The nearest prior art is the MG24Quake lineage.

| Project | MCU | FPU | SRAM | External RAM | Status |
|---|---|---|---|---|---|
| next-hack/MG24Quake | EFR32MG24 (Cortex-M33) | yes (SP) | 276 KB used | none (ext. flash only) | released v2.0, runs on HW at ~29 fps demo3 |
| zenodante/RP2350-Quake (fork of MG24Quake) | RP2350B (Cortex-M33) | yes (SP) | 520 KB available | none (16 MB flash only) | active, 102 commits, HW playthrough not yet verified |
| TokyoDanInJapan/github-universe-26-quake | RP2350 (badge) | yes | 520 KB | none | small patch set on RP2350-Quake |
| recrof/Quake-ESP32S3 (MG24Quake 2.0 based) | ESP32-S3 | yes (SP) | 512 KB | 8 MB PSRAM used for PAK cache | ~42 fps timedemo |

So the RP2040 port has two hard problems that nobody has solved in public yet.
1. **RAM deficit.** MG24Quake already needs ~276 KB, so the RP2040's ~232 KB budget is at least ~44 KB short (~16%) before counting core-1 stacks and LCD DMA line buffers.
2. **No FPU.** Every MCU port so far relies on a hardware single-precision FPU. The only real-world precedents for FPU-less Quake are 2000s handheld ports, PocketQuake (Pocket PC/StrongARM), gpQuake (GP32), the Nintendo DS ports, nQuake (TI-Nspire), and quakepsx (PS1). All of them had megabytes of RAM.

---

## 2. Upstream, id Software GPL release

### id-Software/Quake [V]
- URL. https://github.com/id-Software/Quake
- Licence, the GitHub label is "GPL-2.0". The **file headers say "either version 2 of the License, or (at your option) any later version"**, so it is effectively **GPL-2.0-or-later**. This was confirmed in headers copied verbatim into quakegeneric and MG24Quake. Game data (`pak0.pak`) is *not* GPL. Users must supply their own shareware or retail copy.
- Layout. `WinQuake/`, `QW/` (QuakeWorld client/server), `qw-qc/` (QuakeWorld QuakeC), `gnu.txt`, `readme.txt`. GLQuake shares the `WinQuake/` tree. Its `gl_*.c` files are built by a separate project.
- **Relevant subtree. `WinQuake/` software renderer only** (`r_*.c` front end, `d_*.c` rasteriser, `sv_*`, `pr_*`, `cl_*`, `host*`, `model.c`, `snd_*`, `z_zone.c`, plus `*_null.c` stubs). Ignore.
  - GLQuake (`gl_*.c`), needs OpenGL.
  - QuakeWorld (`QW/`), network-oriented and missing the single-player server.
  - All `.s`/`.asm` x86 assembly. The readme says the C-only build loses "almost half" its speed on x86. That speed is irrelevant on ARM, but it means the C paths of `d_scan.c`, `d_polyse.c`, `r_edge.c` and others are the ones that will run.
- Activity, frozen historical dump (2 commits).
- Relevance to RP2040, it is the canonical source and the licence root. As a direct base it is poor, because it assumes 8 MB+ of heap (DOS minimum spec was 8 MB RAM), x86 asm and float everywhere. Every embedded port below starts from here or from SDLQuake.

---

## 3. Minimal-platform-layer forks (the "doomgeneric" equivalents)

### erysdren/quakegeneric [V]
- URL. https://github.com/erysdren/quakegeneric
- Licence, GPL-2.0-or-later (header verified).
- Base, GPL WinQuake, software renderer, 32-bit-only build. Backends. `quakegeneric_null.c`, `_dos.c`, `_sdl2.c`, `_w32.c`.
- **Platform API** (7 functions to implement, 2 provided).
  `QG_Init`, `QG_Quit`, `QG_DrawFrame(void *pixels)`, `QG_SetPalette(unsigned char[768])`, `QG_GetKey(int *down, int *key)`, `QG_GetMouseMove(int*,int*)`, `QG_GetJoyAxes(float*)`. Provided. `QG_Create(argc, argv)` and `QG_Tick(double dt)`.
- Activity, 62 commits, ~43 stars, 19 forks.
- Relevance, an excellent **platform-layer shape** to copy, and it is the base of espressif/esp32-quake. However it keeps stock WinQuake memory use (multi-MB hunk), so it does nothing for RAM.

### ozkl/quake2generic [V]
- URL. https://github.com/ozkl/quake2generic. Quake **II** equivalent, a "few functions in a single file" port layer. Only useful as a second example of the pattern. It is also the base of the RP2350 Quake II ports.

### SDLQuake (mckayemu/sdlquake mirror) [V]
- URL. https://github.com/mckayemu/sdlquake (mirror of Sam Lantinga's libsdl.org SDLQuake). GPL.
- Software renderer only, with a tiny SDL video/sound/input layer.
- Relevance, **MG24Quake was derived from WinQuake/SDLQuake**, so this is the "pristine" tree to diff against to see what next-hack changed.

### voidstar-dev/softquake-testbed [V]
- URL. https://github.com/voidstar-dev/softquake-testbed. A modern software-renderer-faithful port (SoftQuake). Desktop only, and not a candidate. Mildly useful as a portable C reference for the software renderer.

---

## 4. Microcontroller ports

### next-hack/MG24Quake [V] (local clone in `third_party/MG24Quake`)
- URL. https://github.com/next-hack/MG24Quake
- Licence, repo `LICENSE` is **GPL-3.0**. Individual engine files keep the id header "**GPL v2 or (at your option) any later version**" with a 2023-2024 Nicola Wrachien copyright added (verified in `Quake/d_scan.c` and ~98 `.c` files). The distributed combination is therefore GPL-3.0.
- Hardware, EFR32MG24 / MGM240P (Cortex-M33 **with single-precision FPU**). It uses 256 KB + 20 KB embedded RAM, with **no external RAM**. PAK data sits on external SPI flash (8 MB shareware, 16 MB SparkDoom, 2x32 MB retail).
- Renderer, full WinQuake software renderer with static and dynamic lights, Gouraud-shaded alias models, animated sky, turbulent surfaces, particles, and 8-bit stereo 11025 Hz sound. Frame buffer is 320x200 (8-bit). The 3D view is limited to `MAXHEIGHT 152` lines. Missing, music/CD audio, multiplayer, variable screen size.
- Performance, demo3 timedemo averages ~29.2 fps (18-48 fps instantaneous).
- Memory techniques visible in the source and changelog.
  - 16-bit compressed pointers for edicts and links (`EDICTS_USE_SHORT_PTR`, `LINKS_USE_SHORT_PTR`), plus getter/setter accessors (`entity_getters_setters.h`, `field_*_getters_setters.c`).
  - QuakeC `progs.dat` translated to C (`quakeProgs.c`), so no bytecode or global tables are held in RAM.
  - PAK data used directly from flash (XIP-like) instead of loading into the hunk. Some model skins are cached into internal flash (`internalFlash.c`).
  - Buffer aliasing, cached edge offsets split between `textureCacheBuffer` and the Z-buffer, an aux buffer carved from the Z-buffer, and a 20 KB texture cache.
  - Unions for alias/brush model pointers, string-table indices instead of strings, `surfaceNodeIndex` folded into `surface_t`, and unused MP code/cvars/commands removed.
  - Small fixed-point helpers already present (`tabmath.c/h`, e.g. `fixedReciprocal256[]`, `fixedApproxDiv256` using `int64_t`).
- Float use, **heavy**. There are ~5,700 lines containing `float` across the engine, because the MG24 has an FPU. Some hand-written Thumb-2/M33 inline asm exists (`d_polyse.c`, `mathlib.h`, `pr_exec.c` and others), and none of it will assemble for M0+.
- Activity, 4 commits on GitHub (code dumps), releases v1.0/v1.1/v2.0, 37 stars.
- **Relevance, highest of any upstream project.** It is the only hardware-proven Quake that fits in under 300 KB SRAM with no PSRAM, and its storage model (read-only data in memory-mapped flash) maps directly onto RP2040 XIP. The problems are the 44 KB+ RAM deficit, pervasive float, and M33-only asm.

### zenodante/RP2350-Quake [V]
- URL. https://github.com/zenodante/RP2350-Quake (fork of MG24Quake)
- Licence, **GPL-3.0** (LICENSE fetched). Retains id/next-hack notices.
- Hardware, Pimoroni Explorer RP2350B (Cortex-M33 **with FPU**, 520 KB SRAM), 16 MB flash, **no external RAM**, ST7789 parallel LCD, QwSTPad over I2C, PWM audio.
- Notable engineering, much of it directly reusable on RP2040.
  - **Offline host-side resource compiler**, PAK -> conversion/dedup -> QXIP3 -> ARM-native "QRN1" blob -> separate resource UF2 at `0x100C0000` (~15.25 MB capacity). No flash erase/program at runtime. Engine headers plus the ARM compiler decide the native struct layout, so structs are memory-mapped straight out of XIP.
  - **Dual-core split**, core 0 runs game, server/client, collision, QuakeC-as-C and the renderer. Core 1 runs LCD output, input, ADPCM decode and mixing. A 64,000-byte 8-bit framebuffer is converted to RGB565 through two 320-pixel scanline buffers.
  - **`QMAC_RENDER_C=1`**, C replacements for the ARM-asm render paths. This is essential for M0+.
  - **macOS SDL host test app** (engine on a worker thread).
  - Firmware size ~614 KB (fits 768 KB reservation).
- Caveats, it **disables MG24's 16-bit pointer compression** (it uses native 32-bit pointers), so its SRAM footprint is likely *larger* than MG24Quake's. Total SRAM use is not published. Hardware validation is ongoing, a full playthrough is not verified, and fps is not measured (screenshots come from an emulator). There are no saves, no MP, no CD music.
- Activity, 102 commits, very recent (build dated 2026-09-29), 7 stars.
- **Relevance, very high.** It uses the same vendor SDK (pico-sdk), the same XIP flash model and the same LCD-over-core-1 architecture as an RP2040 port. Its main gap versus an RP2040 is RAM headroom (520 KB vs ~232 KB) and FPU.

### TokyoDanInJapan/github-universe-26-quake [V]
- URL. https://github.com/TokyoDanInJapan/github-universe-26-quake
- Licence, GPL-3.0 for its patches and scripts. It states the id engine source is GPL-2.0-or-later.
- RP2350 conference badge (Pimoroni Tufty 2350 derivative, 16 MB flash). It is a 3-commit patch-and-build wrapper around RP2350-Quake, tilt and button controls, no sound. It shows that the RP2350-Quake path is reproducible, but adds no engine work.

### ChristianIvarsson/MCU-Quake [V]
- URL. https://github.com/ChristianIvarsson/MCU-Quake
- Licence, GPL-2.0. Target, RP2350 **with PSRAM**. Described as a "quick and dirty POC", no input, limited SPI LCDs. Last push 2024-08. Base not stated, but it has a `quake/` tree plus `targets/` and a `regions.h`.
- Relevance, low. It depends on PSRAM. Its SPI-LCD target code might be glanced at.

### FantomJAC/quakembd (QuakEMBD) [V]
- URL. https://github.com/FantomJAC/quakembd (fork with CMake fixes. https://github.com/facchinm/quakembd [V])
- Licence, GPL-2.0 (`gpl-2.0.txt`). Base, WinQuake. Layout. `winquake/` (engine), `port/` (board layer), `include/`, `lib/`. Uses CMake + arm-none-eabi.
- Hardware, STM32H747I-DISCO (Cortex-M7 + FPU, 1 MB SRAM plus **32 MB SDRAM**, of which the author reported ~8 MB plus a framebuffer used. Reddit, 2020). Touch and joystick input, no sound.
- Activity, 19 commits, 44 stars, dormant.
- Relevance, useful as a **clean, minimal Cortex-M `port/` layer + CMake skeleton**. Its memory model (multi-MB hunk in SDRAM) is irrelevant.

### espressif/esp32-quake [V]
- URL. https://github.com/espressif/esp32-quake. **No licence file stated on the page [P]**. Its base, quakegeneric, is GPL, so it is GPL by derivation.
- ESP32-P4 (dual RISC-V with FPU, 32 MB PSRAM), 512x300 upscaled to 1024x600 MIPI, ~20-25 fps, LAN multiplayer. Uses esp-bsp for hardware abstraction.
- Forks, GOB52/M5StackTab5-quake [V], androot2/ESP32-P4-Quake-fork, Wirelesstag-ESP/esp32-quake-wirelesstag (GitHub API listing only).
- Relevance, low (PSRAM-hungry stock engine). It is only interesting as a quakegeneric-on-MCU example.

### recrof/Quake-ESP32S3 [V]
- URL. https://github.com/recrof/Quake-ESP32S3
- Licence, the sidebar says GPL-3.0 while the README says "GPL v2". It inherits MG24Quake, so treat it as GPL-3.0.
- Base, **MG24Quake 2.0**. Targets, Seeed Wio Tracker L2, LilyGO T-Deck, XIAO ESP32S3 + ILI9341 **320x240 SPI**, and generic ESP32-S3 boards with SPI LCD + SD. The PAK cache lives in **8 MB PSRAM**. ~42 fps timedemo at 240 MHz. The README also mentions an **nRF54 port using 511 KB RAM, no PSRAM**.
- Activity, 13 commits, created 2026.
- Relevance, medium. It has a 320x240 SPI-LCD MG24Quake variant, so its display and DMA code is worth reading. It relies on PSRAM and FPU, though.

### johnmacdowall/esp32-quake [U]
- Listed via the GitHub API as "A QUAKE port for the EC Buying ESP32-S3 5.0 Inch IPS LCD Development Board". Not inspected.

### Quake II on RP2350 (rh1tech/frank-quake2, PicoPlus-devel/pico-quake2, 13Z0/pico-quake2) [V]
- GPL-2.0 or GPL-3.0. Quake II via quake2generic, RP2350 at up to 384 MHz, **8 MB PSRAM required**, 16 MB flash, 320x240 doubled to HDMI, ~20-25 fps.
- Relevance, none for the engine (wrong game, and they need PSRAM). The FRANK/Murmulator family (rh1tech, e.g. murmduke3d [V]) has **no Quake 1 port**. Their RP2350 3D ports all require PSRAM, and they explicitly say they "will not run on Pico 1 (RP2040)".

### rp2040-doom (kilograham) [V], not Quake but essential reading
- URL. https://github.com/kilograham/rp2040-doom. GPL-2.0.
- It is the best existing study of fitting an id engine into RP2040 SRAM, WAD preprocessing/compression into flash, struct packing, 16-bit pointers, and dual-core rendering/scanout. Its "behind the scenes" blog series is the closest thing to a design doc for our problem space.

### Kendryte K210 Quake (Elektor / Mathias Claussen) [U]
- RISC-V with FPU, 8 MB SRAM. Not relevant.

### Teensy 4.x, STM32 (other than QuakEMBD), Playdate
- **No Quake 1 ports found [U]**. Searches only returned Doom, Keen and emulators for Teensy, and only Quake-styled original games for Playdate (e.g. freds72 "Power Off").

---

## 5. FPU-less / fixed-point handheld ports (2000s)

These are the only real precedents for running Quake on cores without an FPU. All of them had several MB of RAM.

### PocketQuake (Dan East, Pocket PC / StrongARM, no FPU) [P]
- Source download, HPC:Factor "Pocket Quake 0.62" listing, https://www.hpcfactor.com/scl/1057/PocketMatrix/Pocket_Quake/version_0.62 [V, page reachable. The archive was not downloaded]. The listing states it is bound by id's GPL. No canonical Git repo was found. (github.com/thinkelastic/PocketQuake is an **unrelated** 2026 Analogue Pocket FPGA port, VexiiRiscv soft core with FPU, MIT firmware plus GPL-2.0 engine.)
- Fixed point, forum reports (InsideQC "Fixed point Quake" thread, which blocked fetching [U]) say PocketQuake contains "many fixed-point conversions", including a **fixed-point `D_DrawSpans`** credited to Jacco Bikker, because "floats kill performance" on Pocket PC CPUs.
- Relevance, **the best available donor of fixed-point rasteriser code**. It must be obtained from the HPC:Factor archive and audited.

### gpQuake (Anders Granlund, GP32, ARM920T, no FPU) [V]
- URL. https://sourceforge.net/p/gpquake. GPL-2.0. Based on PocketQuake. Last update 2013-03-07. Download `gpquake-0.5.zip`.
- The author reported a very low framerate due to no FPU and C instead of asm. "Total rewrite to only fixed point" was on its wish list, so it is **not fully fixed-point**.
- Relevance, low-medium. It is a second PocketQuake-derived tree, and a warning that partial fixed-point conversion is not enough.

### HandheldQuake (SourceForge) [P]
- URL. https://handheldquake.sourceforge.net/ [V]. The project page https://sourceforge.net/projects/handheldquake/ returned 403 to the checker but appears in search results. GPLv2 per search snippets.
- PocketPC, Qt Palmtop/Zaurus and iPAQ X11, built on PocketQuake's modifications. Described as "optimized for CPUs without FPUs". Its CVS-era code status is unknown.

### Nintendo DS (ARM946E-S, no FPU, 4 MB RAM)
- **QuakeDS** (Simon Hall, 2007), uses the DS 3D hardware. GameBrew lists a "Mixed" licence and **no source link found [U]**.
- **cQuake** (elhobbs). https://github.com/elhobbs/cnquake [V], GPL. One commit, with arm7/arm9 split. Fixed-point usage not documented [P].
- **Quake 1 DS** (Izhido, devkitARM), source not located [U]. **quake2dsi** (elhobbs) [V] is Quake II.
- Relevance, low. They rely on DS hardware 3D (fixed-point GPU) instead of the software rasteriser, though the DS fixed-point math utilities could be reused.

### TI-Nspire nQuake (ARM926EJ-S, no FPU, 32-64 MB RAM) [U]
- ticalc.org (2014) and Omnimaga threads. **No repo found**. The NZ:P "Vril" engine (https://github.com/nzp-team/vril-engine [V]) has a TI-Nspire target "based on dQuakePlus, ctrQuake and nQuake". Its licence and fixed-point status were not checked.

### quakepsx (fgsfdsfgs, PlayStation, R3000A, no FPU, 2 MB RAM) [V]
- URL. https://github.com/fgsfdsfgs/quakepsx. GPL-2.0. 245 commits, WIP.
- Keeps original game code but **replaces resource formats** with an offline "cook" step (`tools/scripts/cook_id1.sh`). Many E2 maps don't fit in memory. The renderer is slow and lacks proper clipping and dynamic lights. It presumably uses the PS1 GTE (fixed-point geometry coprocessor) [P].
- Relevance, medium as a **design reference** for offline map cooking plus integer-only gameplay code on a 2 MB, FPU-less machine. Its renderer is not reusable (it targets the PS1 GPU).

### GBA Quake (Randy Linden, 2002 prototype) [V, news only]
- Proprietary prototype. **No source released**, so it cannot be used.

### Dreamcast / PSP / Wii / 3DS ports
- These have FPUs (SH-4, Allegrex VFPU, Broadway, ARM11 VFP), so they are irrelevant to the no-FPU problem.

---

## 6. Heavy desktop source ports (ruled out)

| Port | URL | Licence | Why ruled out |
|---|---|---|---|
| TyrQuake | https://github.com/libretro/tyrquake [V] | GPL (packagers disagree, 2.0-only / 2.0+ / 3.0+) | Conservative, but still desktop-sized heap and float-heavy. Libretro core licence ambiguity |
| QuakeSpasm | https://github.com/sezero/quakespasm [V] | GPL-2.0+ | OpenGL renderer, SDL, raised limits = more RAM |
| vkQuake / Ironwail | github.com/Novum/vkQuake, github.com/andrei-drexler/ironwail | GPL-2.0+ | Vulkan/GL, MB-to-GB heaps |
| SoftQuake | https://github.com/voidstar-dev/softquake-testbed [V] | GPL | Desktop. At most a C reference for the software renderer |

---

## 7. RP2040-specific considerations that drive the choice

- **No FPU, no `UMULL`/`SMULL`.** Cortex-M0+ has only a 32x32->32 `MULS`. A 16.16 x 16.16 fixed-point multiply needs a 64-bit product, which means a multi-instruction sequence or `__aeabi_lmul`. Naive fixed-point is therefore *not* free on M0+. Mitigations.
  - pico-sdk's `pico_float` uses the RP2040 **boot-ROM optimised soft-float** routines, which cost tens of cycles per op instead of hundreds for libgcc. Profile with these before converting anything.
  - Use narrower formats (e.g. 8.8 x 16.16 -> fits 32 bits, or 2.14 for trig/normals) and table lookups.
  - Use the RP2040 **SIO hardware divider** (8-cycle 32/32) for perspective divides.
  - Use the **SIO interpolators** for texture-coordinate stepping in span loops.
- **Where the floats are.** The WinQuake rasteriser inner loops (`d_scan.c` `D_DrawSpans8`, `d_polyse.c` affine spans) are already 16.16 fixed-point, with one float perspective divide per 8/16 pixels. Float-heavy code is in setup and game code. `r_bsp.c`/`r_edge.c`/`r_draw.c` (clipping, projection, edge emission), `r_alias.c`/`r_aclip.c` (model transform), `r_light.c`, `sv_phys.c`/`world.c` (physics/collision), `pr_cmds.c`/QuakeC. Convert the per-pixel/per-span divide first (PocketQuake's approach), then per-edge setup, and leave game-logic floats on ROM soft-float.
- **RAM.** MG24Quake's 276 KB includes the 320x200 8-bit framebuffer (64 KB) and a 320x152 16-bit Z-buffer (~48 KB). Possible RP2040 savings.
  - Render the 3D view in horizontal bands or a smaller view (e.g. 320x120 or 240x160), and send rows straight to the SPI LCD instead of holding a full framebuffer.
  - Re-enable/extend short pointers.
  - Shrink surface/edge caches.
  - Move more constant tables into XIP (const + aligned).
  - Shrink the sound mix buffer (core 1, 11 kHz mono).
- **XIP bandwidth.** 50 MHz QSPI with a 16 KB cache is much slower than SRAM for random texture fetches. Mitigations, keep the surface cache in SRAM, pre-swizzle and dedup textures offline (RP2350-Quake's resource compiler already does this), and pin hot render code with `__not_in_flash_func`.
- **M33 asm.** Everything Thumb-2-only must go. RP2350-Quake's `QMAC_RENDER_C=1` C render paths are the starting point.

---

## 8. Ranked recommendation

**1. Primary base. `next-hack/MG24Quake` engine tree (already cloned), ported onto `zenodante/RP2350-Quake`'s pico-sdk platform layer and resource pipeline.**
- Practical way to do it, **fork RP2350-Quake** (same SDK, XIP model, core-1 LCD/audio split, C render fallbacks, host SDL app, offline PAK->native compiler), then **re-import MG24Quake's RAM-saving choices that RP2350-Quake relaxed**, 16-bit `EDICTS_/LINKS_USE_SHORT_PTR` and the tight buffer aliasing.
- Before committing, run a `git diff` of RP2350-Quake against MG24Quake v2.0 and measure RP2350-Quake's `.bss`/`.data`/heap on the macOS/host build compiled 32-bit. If it is already ≤ ~260 KB, fork it directly. If it has grown well beyond 276 KB, start from MG24Quake and port only RP2350-Quake's `platform/`, resource compiler and core-1 services.
- Why this route.
  - It is the only lineage proven to fit in roughly the RP2040's memory class without PSRAM.
  - Its flash-resident data model matches XIP.
  - RP2350-Quake has already done the RP-family bring-up and toolchain work.
- What is left for us, cut ~45-60 KB of SRAM, strip M33 asm/FPU assumptions, and add fixed-point or ROM-float.

**2. Secondary reference (not a base). `erysdren/quakegeneric` for the platform-API shape.** Adopt a doomgeneric-style `QG_*`-like boundary (init, draw rows, palette, key events, ticks) between the trimmed engine and an RP2040 board layer plus a desktop SDL harness. This keeps the engine testable on the PC for RAM/accuracy regression (cf. our `platform/` dir).

**What to borrow from others**
| Need | Borrow from | Notes |
|---|---|---|
| RAM tricks, short pointers, QuakeC->C, buffer aliasing, string tables, flash-resident models | MG24Quake | Already in tree |
| RP-family platform, XIP resource compiler (PAK->native blob UF2), dual-core LCD/audio, C render paths, SDL host app | RP2350-Quake | GPL-3.0 |
| Fixed-point span drawer, other float->fixed conversions | PocketQuake 0.62 source (HPC:Factor), gpQuake 0.5 | GPL-2.0. Download and audit. Not fully fixed-point. |
| Offline map/resource "cooking" for a tiny-RAM, FPU-less target | quakepsx | Ideas only. Renderer is PS1-specific |
| Flash/WAD packing, RP2040 dual-core scanout, SRAM budgeting methodology | kilograham/rp2040-doom (+ blog series) | GPL-2.0 |
| 320x240 SPI-LCD DMA path on an MG24Quake derivative | recrof/Quake-ESP32S3 | ESP-IDF specific, ideas only |
| Minimal Cortex-M CMake port skeleton | FantomJAC/quakembd | GPL-2.0 |
| Float maths on M0+ | pico-sdk `pico_float` (boot-ROM soft-float), SIO divider/interpolators | BSD-3 |

---

## 9. Licence compatibility notes

- **id Quake engine source, GPL-2.0-or-later** (file headers). It can be combined with GPL-3.0 code, and the combination must be distributed as GPL-3.0.
- **MG24Quake and RP2350-Quake, GPL-3.0** at repo level, with per-file "v2 or later" headers retained. **Our port will therefore be GPL-3.0** (or GPL-3.0-or-later). This is fine for a hobby/open project and requires full source for any binary we distribute.
- **GPL-2.0-only code cannot be merged into a GPL-3.0 project.** Check each borrowed file's header, not just the repo badge.
  - quakembd, quakepsx, gpQuake, cQuake, PocketQuake, MCU-Quake and pico-quake2 are labelled "GPL-2.0". Most likely keep id's "or later" wording, but **verify per file**.
  - rp2040-doom inherits Chocolate Doom's **GPL-2.0-or-later** (verify), which is compatible.
  - TyrQuake (libretro) is listed by some packagers as GPL-2.0-only, so avoid copying from it unless headers say "or later".
- **pico-sdk (BSD-3-Clause), TinyUSB (MIT), FatFs (BSD-1-like), tiny printf (MIT)** are all GPL-3.0-compatible permissive licences. Keep their notices.
- **Game data** (`pak0.pak`, shareware or retail) is id/Bethesda-copyrighted and not GPL. Don't commit it or ship it in UF2s. Users supply it, as RP2350-Quake does. The shareware episode is freely redistributable only in its original unmodified form. A converted resource UF2 is a derivative, so keep it a local build artefact.
- Keep all existing copyright notices (id 1996-97, Wrachien 2023-24, zenodante, etc.) and mark modified files with change notices (GPL-3.0 §5a).

---

## 10. Open questions / follow-ups

1. Download PocketQuake 0.62 and gpQuake 0.5 sources, then grep for the fixed-point replacements (`D_DrawSpans`, `R_TransformVector`, `R_EmitEdge`, `D_PolysetDraw`).
2. Measure RP2350-Quake's static SRAM footprint and diff it against MG24Quake v2.0.
3. Count float ops per frame on the host build (e.g. `-msoft-float` + instrumented `__aeabi_f*` counters) to quantify ROM-float cost before converting anything to fixed point.
4. Inspect the nRF54 MG24Quake variant mentioned by recrof (511 KB, no PSRAM) for any further memory changes.
5. Confirm the licence of espressif/esp32-quake and the Vril engine's Nspire target if anything is borrowed from them.

---

## Sources

- id-Software/Quake. https://github.com/id-Software/Quake
- erysdren/quakegeneric. https://github.com/erysdren/quakegeneric (header. https://raw.githubusercontent.com/erysdren/quakegeneric/main/source/quakegeneric.h)
- ozkl/quake2generic. https://github.com/ozkl/quake2generic
- SDLQuake mirror. https://github.com/mckayemu/sdlquake
- SoftQuake. https://github.com/voidstar-dev/softquake-testbed
- next-hack/MG24Quake. https://github.com/next-hack/MG24Quake (local clone `third_party/MG24Quake`)
- Hackaday, "Quake In 276 KB Of RAM". https://hackaday.com/2024/10/05/quake-in-276-kb-of-ram
- CNX Software, Arduino Nano Matter gamepad runs Quake. https://www.cnx-software.com/2024/10/07/arduino-nano-matter-powered-gamepad-runs-quake-at-27-fps/
- zenodante/RP2350-Quake. https://github.com/zenodante/RP2350-Quake (LICENSE. https://raw.githubusercontent.com/zenodante/RP2350-Quake/main/LICENSE)
- TokyoDanInJapan/github-universe-26-quake. https://github.com/TokyoDanInJapan/github-universe-26-quake
- ChristianIvarsson/MCU-Quake. https://github.com/ChristianIvarsson/MCU-Quake
- FantomJAC/quakembd. https://github.com/FantomJAC/quakembd ; fork https://github.com/facchinm/quakembd
- r/embedded thread on QuakEMBD memory. https://redlib.hackliberty.org/r/embedded/comments/gdrmlv/i_ported_quake_for_stm32h747_need_more
- espressif/esp32-quake. https://github.com/espressif/esp32-quake ; M5Stack Tab5 fork https://github.com/GOB52/M5StackTab5-quake
- recrof/Quake-ESP32S3. https://github.com/recrof/Quake-ESP32S3
- 13Z0/pico-quake2. https://github.com/13Z0/pico-quake2 ; PicoPlus-devel/pico-quake2. https://github.com/PicoPlus-devel/pico-quake2 ; rh1tech/frank-quake2 (GitHub API listing)
- rh1tech/murmduke3d. https://github.com/rh1tech/murmduke3d
- kilograham/rp2040-doom. https://github.com/kilograham/rp2040-doom
- Elektor, Kendryte K210 running Quake 1. https://www.elektormagazine.com/labs/kendryte-k210-running-quake-1
- Pocket Quake 0.62 (source download). https://www.hpcfactor.com/scl/1057/PocketMatrix/Pocket_Quake/version_0.62
- InsideQC "Fixed point Quake" thread (blocked by Anubis. Via search snippets). https://forums.insideqc.com/viewtopic.php?p=36394
- thinkelastic/PocketQuake (Analogue Pocket, unrelated). https://github.com/thinkelastic/PocketQuake
- gpQuake. https://sourceforge.net/p/gpquake
- HandheldQuake. https://handheldquake.sourceforge.net/ ; https://sourceforge.net/projects/handheldquake/
- QuakeDS (GameBrew). https://www.gamebrew.org/wiki/QuakeDS ; Quake 1 DS. https://www.gamebrew.org/wiki/Q1DS ; cQuake. https://www.gamebrew.org/wiki/CQuake
- elhobbs/cnquake. https://github.com/elhobbs/cnquake ; elhobbs/quake2dsi. https://github.com/elhobbs/quake2dsi
- nQuake news (ticalc.org). https://ticalc.org/archives/news/articles/14/148/148639.html ; Omnimaga. https://omnimaga.org/news/after-ndoom-comes-nquake!
- NZ:P Vril engine. https://github.com/nzp-team/vril-engine
- fgsfdsfgs/quakepsx. https://github.com/fgsfdsfgs/quakepsx
- GBA Quake prototype. https://www.xda-developers.com/how-quake-ported-game-boy-advance/ ; https://new.tcrf.net/Quake_(Game_Boy_Advance)
- libretro/tyrquake. https://github.com/libretro/tyrquake ; openSUSE spec. https://build.opensuse.org/package/show/games/tyrquake
- sezero/quakespasm. https://github.com/sezero/quakespasm
- Novum/vkQuake. https://github.com/Novum/vkQuake ; andrei-drexler/ironwail. https://github.com/andrei-drexler/ironwail
- libfixgl (fixed-point GL for FPU-less handhelds). https://libfixgl.sourceforge.net/
- GitHub repository search API (queries "quake rp2350", "quake esp32", "quake microcontroller"), run 2026-11-22
