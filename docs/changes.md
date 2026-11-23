# Changes log

Newest first. Each entry says what changed, why, and whether it ran on hardware.

## 2026-11-22: Engine-only RAM cuts (PC, measured, all verified hidden and silent)

| Step | Change | Saved (E1M1) |
| --- | --- | ---: |
| A | Single player: one client slot, two loopback sockets (`QOG_SINGLE_PLAYER`) | 107 KB hunk |
| B | Edict array sized per map from its entity lump, minus what QuakeC frees at spawn, plus a margin of 128 (`QOG_EDICT_MARGIN`): 247–480 instead of 600 | 242 KB hunk |
| C | Client entity table sized the same way, on the hunk | 100 KB static |
| D | Mixer volume table replaced by an equivalent multiply | 32 KB static |
| E | `gfx.wad` read in place; only its 5 KB directory is copied | 105 KB hunk |
| F | Model table 192 instead of 256 (`QOG_MAX_MOD_KNOWN`); the most one level references is 160 | 27 KB static |

E1M1 now: hunk low 1,705 KB, total dynamic 2,019 KB, static 320 KB (interpreter, since QuakeC translation waits for registered data). State hashes are unchanged throughout. Demo frames are identical to the reference build, status bar included. All nine levels and every menu run. Sound was verified by level only (peak 19,193, RMS 6,395), never played aloud.

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
