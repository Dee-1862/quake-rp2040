# Current plan

The full plan with its reasoning is ../QuakeOG Port Plan.html (kept locally, not in the repository) (version 2, Nov 22 2026). This page tracks where we are against it and records any change to it. When the plan changes, update this page and log the change in [changes.md](../changes.md).

## Goal

Quake running on one RP2040, the display CPU of the FreeWili OG, with 264 KB RAM and 16 MB flash. The main CPU runs only the minimal companion (boot the display, kick the watchdog). The fallbacks are Duke 3D (Build engine), then a Battlezone-style game.

Done means.

- **Tier 1.** the first map is playable at 10 fps or better.
- **Tier 2.** the shareware episode plays, saves work, 15 fps or better.
- **Tier 3.** a release with a converter, a flashing script, a README, a video and a write-up.

## Status by phase

| Phase | State | What's left |
| --- | --- | --- |
| 0 · Set up, learn the facts | **Mostly done** | LVGL example (optional now), data-region UF2 write. Ask FREE-WILi the GPL question. |
| 1 · Study the reference, set the RAM budget | **Base chosen, id's clean source** | Engine imported, builds on the PC, RAM meter running (435 KB static to start). Still to do, run with `pak0.pak`, record the dynamic RAM, and write the shrink plan. MG24Quake and RP2350-Quake are references. |
| 2 · Platform layer | **Running on hardware** | 32 fps at 320×200, 22.4 ms scanout. Still to do, data UF2 test, the 240×150 and 160×100 runs, a red-hold check. |
| 3 · Renderer spike (GATE) | Not started | |
| 4 · Game logic and saves | Not started | |
| 5 · Sound, controls, polish | Not started | |
| 6 · Package and publish | Not started | |

## Next actions, in order

**Blocked on game data.** the next big RAM step (baking the now read-only map structures into flash, about 640 KB) and fitting data to the board both produce derived data. The shareware licence forbids that, so they wait for registered or retail data (Steam or GOG) in `data/id1/`. Engine-only work continues meanwhile, entity records, static tables, gfx.wad, particles, net buffers, and the RP2040 port layer.

0. **User.** put `pak0.pak` in `data\id1\` (`game-data.md`). Run the PC build, check that E1M1 plays, and try board mode (F12).
0b. Measure the dynamic RAM for E1M1 with `-heapsize` stepping down (`ram-meter.md`). Then shrink the biggest static items one commit at a time, starting with `cl_entities` and `mod_known`.

1. ~~Flash `qog` and measure all three resolutions~~ Done (changes.md). **Proposed, develop at 240×150, fall back to 160×100.**
2. **On the board.** write `build/qogdata_test.uf2` to the display CPU, press green at the bootloader screen, and check that the still image shows and the console reports `data=ok`. Hold green during boot to time a full CRC.
3. Settle the red-button conflict (see [lessons.md](../lessons.md), L6) before the controls are final.
4. Finish Phase 1, choose the engine base and the resolution, and replace the plan's RAM estimates with measured ones.
5. Start Phase 3 with a **PC build first** of the trimmed engine (see [structure.md](../structure.md)).

## Open questions

- Is the GPL compatible with the BSP's FreeWili Hardware License? Ask Dave/FREE-WILi before publishing anything.
- Does copying a data-only UF2 to the display CPU's BOOTSEL drive behave as expected on hardware? This is reasoned from the BSP code, not yet tested.
- How much heap do the BSP and USB use at runtime? Static RAM is measured at about 8.5 KB.
