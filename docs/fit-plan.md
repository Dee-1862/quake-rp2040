# Plan: Quake in 245 KB of RP2040 RAM

How the precedents fit, where we are, and the remaining steps in order. Sources: `phase1-mg24quake-notes.md` (MG24Quake measured), RP2350-Quake's own results (`third_party/rp2350-quake/docs/RP2350_FULL_FIRMWARE_RESULTS.md`), `research-rp2040-games.md` (rp2040-doom, computed from its source), and our RAM meter.

## How the others fit

| | RAM | Key techniques |
| --- | ---: | --- |
| **MG24Quake** (EFR32MG24, Cortex-M33 + FPU) | 276 KB | Everything immutable in flash; one 48 KB zone replaces hunk and cache; per-class edicts generated from QuakeC; QuakeC compiled to C; `entity_t` packed to 8 B, particles to 16 B; 320×152 view; z-buffer reused as a temporary buffer for edges and messages; edge/span tables on a 36 KB stack; 1 KB console |
| **RP2350-Quake** (RP2350, 520 KB SRAM) | 439 KB | MG24Quake engine; immutable BSP and model data baked into XIP at fixed addresses; deduplicated textures; 128 KB zone (43 KB peak), 32 KB metadata pool; 32 KB stack; ADPCM sound |
| **rp2040-doom** (RP2040, 264 KB) | ~240 KB | All level data read in place, `const`; only sectors in RAM; 16-bit RAM pointers; 32 B static vs 84 B full mobjs; per-line bitsets instead of counters; status bar streamed per scanline; compressed WHD format (DOOM1.WAD 4.1 MB → 1.8 MB); saves as bit-packed diffs |

The common rule: **RAM holds only what changes during play; everything else is read from flash in a compact layout; buffers are shared between jobs that never run at the same time.**

## Where we are (E1M1, PC build, 32-bit layout)

| | KB |
| --- | ---: |
| Unmodified id engine with sound | ≈ 7,270 |
| **Now: dynamic 1,583 + static 265** | **≈ 1,848** |
| Target | 245 |

Hunk low (1,269 KB) is mostly the BSP structures (≈ 780 KB, already read-only during play), edicts (≈ 278 KB), then smaller items. Video is 189 KB (320×200 frame + z-buffer).

## Remaining steps, biggest first

| # | Step | Saves (est.) | Needs registered data? | Precedent |
| --- | --- | ---: | --- | --- |
| 1 | Bake the read-only BSP structures into flash (pointers resolved for the fixed flash address) | ~640 KB | **Yes** (writes a converted copy of the data) | RP2350-Quake, rp2040-doom |
| 2 | Edicts: hot/cold split now, per-class layouts with compiled QuakeC later | 150–230 KB | Hot/cold no, per-class yes | MG24Quake, rp2040-doom static mobjs |
| 3 | Render at 240×150, HUD drawn as an overlay, z-buffer reused for edges/spans/messages | ~85 KB | No | MG24Quake, rp2040-doom |
| 4 | Model and sound headers and the remaining cache in flash or tiny tables | ~100 KB | Partly | RP2350-Quake |
| 5 | Packed `entity_t` (client), particles to 16 B, server message buffers removed, static tables shrunk | ~80 KB | No | MG24Quake |
| 6 | Constant QuakeC globals left in flash, only mutable ones in RAM | 15–30 KB | Best via compiled QuakeC (yes) | rp2040-doom `should_be_const` |
| 7 | 16-bit indices instead of pointers in RAM structures | 10–30 KB | No | rp2040-doom, MG24Quake |

Target budget from the research (≈ 244 KB): frame buffer 35, z-buffer 70, render tables 20, stacks 8, edicts 40, QuakeC globals 12, zone 16, other 38, margin 5. Fallbacks if over: 224×140 view (−14 KB), 8-bit depth buffer (−35 KB, visual risk).

## Flash budget

The whole registered game is about 50 MB; the board's game-data region is 12–15 MB. One episode per flash image (episode 1 is 16.6 MB before texture deduplication, ≈ 14.9 MB after). Switching episodes means reflashing the data region.

## Hardware options (from `research-freewili-hardware.md`)

- **The OG stays the target.** Its main RP2040 has to run anyway: it carries the display firmware and must kick the watchdog. The honest claim is "all game code runs on one RP2040; the other chip boots it".
- **Two-chip fallback:** Quake's own client/server split maps onto the two chips (the server and entities on the main CPU, rendering and sound on the display CPU). The 12.5 Mbaud UART is about 1.25 MB/s, and a frame's server message is about 1 KB. Keep a swappable network-driver seam so this stays cheap if 245 KB fails. Benchmark the link with the BSP's `smoketest` app first; the rate has never been measured on a board.
- **Levels on demand:** the OG has no Wi-Fi or Bluetooth, so the path is USB to a PC companion app. It converts a level from the user's own pak and streams it into the flash data region (estimated 3–15 s per level; benchmark it). That makes all four episodes playable on a 16 MB chip. Saves go back to a PC folder, which the user may sync to their own cloud. A public server would be redistribution and is not allowed.
- **Controller:** no USB host or Bluetooth on the OG. First the PC companion relays a gamepad over USB. For untethered play, an ESP32 running Bluepad32 wired to the main CPU's UART1 pins.
- **FREE-WILi 2** (two RP2350B, 520 KB SRAM and 8 MB PSRAM each, Wi-Fi and Bluetooth via an ESP32-C5, USB host): today's build would fit as is, so it's an easy later port, not the headline.
